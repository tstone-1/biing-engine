"""The real window loop over pipes and the local HTTP boundary, using synthetic game data."""
import json
from pathlib import Path
import sys
import threading
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from biing_inspector import InspectorServer, InspectorSession, integer, retained_sessions, serve
from biing_control import ControlError
from biing_saves import read_save


@pytest.fixture
def session(tmp_path, monkeypatch):
    root = Path(__file__).resolve().parents[1]
    engine = root / "build" / ("test_shot.exe" if sys.platform == "win32" else "test_shot")
    if not engine.exists():
        pytest.skip("Build test_shot to exercise the native inspector protocol")
    monkeypatch.setenv("SDL_VIDEODRIVER", "dummy")
    monkeypatch.setenv("SDL_AUDIODRIVER", "dummy")
    monkeypatch.setenv("BIING_MIDI_OUT", "none")
    source = tmp_path / "source"
    source.mkdir()
    (source / "reference").write_bytes(b"synthetic unchanged input")
    with_source = InspectorSession(engine, source, tmp_path / "session", timeout=5)
    try:
        yield with_source
    finally:
        with_source.close()
        assert (source / "reference").read_bytes() == b"synthetic unchanged input"
        assert sorted(p.name for p in source.iterdir()) == ["reference"]


def test_live_pause_edit_undo_step_checkpoint_and_eof(session):
    first = session.command("pause")
    time.sleep(0.08)
    assert session.command("status")["frames"] == first["frames"]
    before = session.command("read:0:0")["values"][0]
    changed = session.change(dict(group=0, record=0, field=0, before=before, value=12345))
    assert changed["values"][0] == 12345
    assert session.command("read:0:0")["values"][0] == 12345
    with pytest.raises(ControlError, match="changed since"):
        session.change(dict(group=0, record=0, field=0, before=12344, value=8))
    assert len(session.history) == 1
    checkpoint = session.checkpoint("edited")
    decoded = read_save(session.checkpoints["edited"].read_bytes())
    assert not decoded["problems"]
    assert int.from_bytes(decoded["blocks"][26][3][0x16:0x1A], "little", signed=True) == 12345
    assert checkpoint["state"]["paused"]
    assert session.undo()["values"][0] == before
    assert session.history == []
    count = session.command("step:50")["frames"]
    deadline = time.monotonic() + 3
    while (state := session.command("status"))["steps"] and time.monotonic() < deadline:
        time.sleep(0.01)
    assert state["frames"] == count + 50 and state["steps"] == 0
    time.sleep(0.05)
    assert session.command("status")["frames"] == count + 50
    session.command("resume")
    time.sleep(0.08)
    assert session.command("status")["frames"] > count + 50
    with pytest.raises(ControlError, match="Pause before"):
        session.change(dict(group=0, record=0, field=0, before=0, value=8))
    session.close()
    assert session.process.returncode == 0  # EOF ends the normal window without a hung reader.


def test_every_schema_record_range_and_field_layout(session):
    session.command("pause")
    for group in session.schema["groups"]:
        records = session.command(f"list:{group['id']}")["records"]
        assert len(records) == group["count"] > 0
        assert [r["id"] for r in records] == list(range(group["count"]))
        last = session.command(f"read:{group['id']}:{group['count']-1}")
        assert len(last["bytes"]) == group["stride"]
        assert len(last["values"]) == len(group["fields"])
        for field in group["fields"]:
            assert field["offset"] + field["width"] <= group["stride"]
        with pytest.raises(ControlError, match="out of range"):
            session.command(f"read:{group['id']}:{group['count']}")


def test_checkpoint_restore_preserves_recovery_and_reopens_in_a_new_copy(session, tmp_path):
    session.command("pause")
    before = session.command("read:0:0")["values"][0]
    session.change(dict(group=0, record=0, field=0, before=before, value=12345))
    session.checkpoint("first")
    saved = session.checkpoints["first"].read_bytes()
    session.change(dict(group=0, record=0, field=0, before=12345, value=23456))
    restored = session.restore("first")
    assert restored["cash"] == 12345 and restored["paused"] and restored["edits"] == 0
    assert session.history == []
    backup = session.checkpoints[restored["backup"]]
    blocks = read_save(backup.read_bytes())["blocks"]
    assert int.from_bytes(blocks[26][3][0x16:0x1A], "little", signed=True) == 23456
    with pytest.raises(ValueError, match="No edit"):
        session.undo()
    assert session.metadata["latest"] == "first"  # Recovery backup is newer, but is not the resumed state.
    prior = session.output
    session.close()
    reopened = InspectorSession(session.engine, None, tmp_path / "session-reopened", previous=prior, timeout=5)
    try:
        assert reopened.command("status")["cash"] == 12345
        assert reopened.state["paused"] and reopened.history == []
        assert reopened.checkpoints["first"].read_bytes() == saved
        assert reopened.checkpoints["first"].parent == reopened.output != prior
        reopened.checkpoints["first"].write_bytes(b"damaged synthetic checkpoint")
        with pytest.raises(ControlError, match="could not be restored"):
            reopened.restore("first")
        assert reopened.command("status")["cash"] == 12345
        assert (prior / "save-first.sav").read_bytes() == saved
    finally:
        reopened.close()


def test_restore_requires_pause_and_rejects_unknown_names(session):
    session.command("pause")
    session.checkpoint("known")
    count = len(session.checkpoints)
    for name in ("../../outside", "missing", None):
        with pytest.raises(ValueError, match="Unknown checkpoint"):
            session.restore(name)
    session.command("resume")
    with pytest.raises(ControlError, match="Pause before"):
        session.restore("known")
    with pytest.raises(ControlError, match="Pause before restoring"):
        session.command(f"load:{session.checkpoints['known']}")
    assert len(session.checkpoints) == count


def test_reopen_refuses_missing_last_checkpoint_without_touching_previous(session, tmp_path):
    session.command("pause")
    session.checkpoint("missing")
    session.checkpoints["missing"].unlink()
    session.close()
    metadata = (session.output / "session.json").read_bytes()
    with pytest.raises(ValueError, match="checkpoint is missing"):
        InspectorSession(session.engine, None, tmp_path / "reopen-missing", previous=session.output, timeout=5)
    assert (session.output / "session.json").read_bytes() == metadata


def test_session_catalogue_filters_games_and_ignores_invalid_folders(session, tmp_path):
    # The session fixture predates the UUID convention; rename only after closing it.
    source = Path(session.metadata["source"])
    session.close()
    folder = tmp_path / "session-retained"
    session.output.rename(folder)
    (tmp_path / "session-invalid").mkdir()
    entries = retained_sessions(tmp_path, source)
    assert [e["path"] for e in entries] == [str(folder)]
    assert retained_sessions(tmp_path, tmp_path / "different-game") == []
    # Pre-metadata sessions are recognized through the inspector start record too.
    (folder / "session.json").unlink()
    assert retained_sessions(tmp_path, source)[0]["source"] == str(source)


def test_served_stop_saves_exit_checkpoint_and_ends_worker(session):
    urls = []
    thread = threading.Thread(target=serve, args=(session, 0, urls.append), daemon=True)
    thread.start()
    deadline = time.monotonic() + 3
    while not urls and time.monotonic() < deadline:
        time.sleep(0.01)
    assert urls
    origin, token = urls[0].split('/#')
    request = Request(origin + '/api/action', data=b'{"action":"stop"}',
                      headers={"Authorization": "Bearer " + token, "Content-Type": "application/json"})
    with urlopen(request, timeout=5) as response:
        assert json.load(response)["closed"]
    thread.join(5)
    assert not thread.is_alive() and session.closed and session.process.returncode == 0
    assert session.metadata["latest"].startswith("exit-")
    assert session.checkpoints[session.metadata["latest"]].is_file()


def test_http_access_checks_and_validated_edits(session):
    server = InspectorServer(session)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    def call(path, data=None, *, auth=True, origin=None, host=None, authorization=None):
        headers = {"Content-Type": "application/json"}
        if auth:
            headers["Authorization"] = "Bearer " + server.token
        if authorization is not None:
            headers["Authorization"] = authorization
        if origin:
            headers["Origin"] = origin
        if host:
            headers["Host"] = host
        request = Request(server.origin + path, data=None if data is None else json.dumps(data).encode(), headers=headers)
        with urlopen(request, timeout=5) as response:
            return response.read()

    try:
        assert b"Biing! Inspector" in call("/", auth=False)
        for kwargs in ({"auth": False}, {"origin": "https://example.invalid"}, {"host": "example.invalid"},
                       {"authorization": "Bearer \u00e4"}):
            with pytest.raises(HTTPError) as caught:
                call("/api/status", **kwargs)
            assert caught.value.code == 403
        call("/api/action", {"action": "pause"})
        old = json.loads(call("/api/record?group=0&record=0"))["values"][0]
        changed = json.loads(call("/api/edit", dict(group=0, record=0, field=0, before=old, value=4444)))
        assert changed["cash"] == 4444
        for data in (dict(group=0, record=0, field=0, before=4444, value=True),
                     dict(group=0, record=0, field=1, before=0, value=255)):
            with pytest.raises(HTTPError) as caught:
                call("/api/edit", data)
            assert caught.value.code == 400
        assert json.loads(call("/api/status"))["cash"] == 4444
        assert json.loads(call("/api/undo", {}))["cash"] == old
        checkpoint = json.loads(call("/api/action", {"action": "checkpoint"}))
        assert json.loads(call("/api/checkpoints"))[0]["name"] == checkpoint["name"]
        call("/api/edit", dict(group=0, record=0, field=0, before=old, value=7777))
        restored = json.loads(call("/api/action", {"action": "restore", "name": checkpoint["name"]}))
        assert restored["cash"] == old and restored["paused"] and restored["backup"]
        assert json.loads(call("/api/history")) == []
        with pytest.raises(HTTPError):
            call("/api/action", {"action": "save:/tmp/arbitrary"})
        with pytest.raises(HTTPError):
            call("/../../AGENTS.md")
        assert json.loads(call("/api/action", {"action": "stop"}))["closed"]
        thread.join(3)
        assert not thread.is_alive()
        assert session.closed and session.process.returncode == 0
    finally:
        server.shutdown()
        server.server_close()
        thread.join(3)


@pytest.mark.parametrize("value", [True, False, 1.5, "1", None, 2**63, -(2**63)-1])
def test_integer_boundary(value):
    with pytest.raises(ValueError):
        integer(value)


def test_german_catalogue_covers_fields_markup_and_placeholders():
    import html
    import re
    root = Path(__file__).resolve().parents[1]
    script = (root / 'tools/inspector/app.js').read_text()
    catalogue = json.loads(script.split('const german = ', 1)[1].split(';\nlet language', 1)[0])
    markup = (root / 'tools/inspector/index.html').read_text()
    required = {html.unescape(s) for s in re.findall(r'data-i18n(?:-placeholder|-aria)?="([^"]+)"', markup)}
    source = (root / 'engine/src/inspector.c').read_text()
    labels = set(re.findall(r'\b(?:R|E|U|G)\("([^"]+)"', source))
    assert len(labels) > 100 and len(required) > 30  # Empty extraction must not pass.
    required |= {s for s in labels if not re.fullmatch(r'Opinion of hospital \d+', s)}
    assert required <= catalogue.keys(), sorted(required - catalogue.keys())
    for english, german in catalogue.items():
        assert german.strip()
        assert set(re.findall(r'\{\w+\}', english)) == set(re.findall(r'\{\w+\}', german)), english
    assert catalogue['Height'] == 'Größe'
    assert catalogue['Undo last edit'] == 'Letzte Änderung rückgängig'


def test_trainer_language_and_name_formatters():
    import shutil
    import subprocess
    node = shutil.which('node')
    if node is None:
        pytest.skip('Node is needed for the trainer JavaScript unit check')
    root = Path(__file__).resolve().parents[1]
    # Pure formatting functions only. No browser, DOM automation or network access.
    script = (root / 'tools/inspector/app.js').read_text().split('function message(', 1)[0]
    assertions = r'''
const assert = require('node:assert/strict');
assert.equal(language, 'de');
assert.equal(t('Edit'), 'Bearbeiten');
assert.equal(t('Current: {value}. Allowed range: {min} to {max}.', {value: 3, min: 1, max: 5}), 'Aktuell: 3. Zulässiger Bereich: 1 bis 5.');
assert.equal(fmt(1234), '1.234');
hospitals = [{id: 0, name: 'Testklinik'}, {id: 1, name: 'Test Hospital'}];
schema = {groups: [{name: 'Hospitals'}, {name: 'Staff'}]};
assert.equal(hospitalName(0), 'Testklinik');
assert.equal(hospitalName(-1), 'Nicht zugeordnet');
assert.equal(fieldName({name: 'Opinion of hospital 1'}), 'Meinung über Test Hospital');
assert.equal(recordName({id: 0, name: 'Testklinik'}), 'Testklinik');
assert.equal(recordName({id: 4, name: 'Staff 4'}, 1), 'Personal 4');
language = 'en';
assert.equal(t('Edit'), 'Edit');
assert.equal(fmt(1234), '1,234');
assert.equal(hospitalName(0), 'Testklinik');
assert.equal(fieldName({name: 'Opinion of hospital 1'}), 'Opinion of Test Hospital');
assert.equal(recordName({id: 4, name: 'Staff 4'}, 1), 'Staff 4');
'''
    subprocess.run([node, '-e', "const location = {hash: ''};\n" + script + assertions], check=True, cwd=root, capture_output=True, text=True)
