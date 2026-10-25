"""Library boundaries use synthetic data; packaged acceptance uses owned inputs."""
import json
from pathlib import Path
from types import SimpleNamespace
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import launcher


@pytest.fixture
def library(tmp_path, monkeypatch):
    lib = launcher.Library(tmp_path / "user", tmp_path / "binaries")
    def setup(source, dest):
        dest.mkdir(parents=True)
        (dest / "GAMES").mkdir()
        (dest / "BIPRO.EXE").write_bytes(b"synthetic")
    monkeypatch.setattr(launcher, "setup", setup)
    monkeypatch.setattr(launcher, "identify", lambda path: SimpleNamespace(name="Synthetic edition", key="disk"))
    monkeypatch.setattr(lib, "run", lambda *args, **kwargs: None)
    return lib


def test_import_and_reopen_keep_multiple_games_and_saves(library):
    first = library.import_game(Path("synthetic.zip"))
    save = library.game_dir(first["id"]) / "GAMES/BGAME1"
    save.write_bytes(b"saved progress")
    second = library.import_game(Path("another.zip"))
    assert first["id"] != second["id"]
    reopened = launcher.Library(library.root, Path("new-app-version"))
    assert len(reopened.state["games"]) == 2
    assert reopened.state["selected"] == second["id"]
    assert save.read_bytes() == b"saved progress"
    with pytest.raises(launcher.SetupError, match="Choose"):
        reopened.game_dir("../../outside")


def test_failed_validation_does_not_publish_game_or_replace_previous(library, monkeypatch):
    library.import_game(Path("first"))
    before = library.file.read_bytes()
    def fail(*args, **kwargs):
        raise launcher.SetupError("synthetic checker rejection")
    monkeypatch.setattr(library, "run", fail)
    with pytest.raises(launcher.SetupError, match="checker rejection"):
        library.import_game(Path("bad"))
    assert library.file.read_bytes() == before
    assert len(library.state["games"]) == 1


@pytest.mark.parametrize("state", [{}, {"version": 99}, {"version": 1, "language": "en", "selected": "", "games": [{"id": "../../escape", "name": "bad"}]}])
def test_invalid_library_is_preserved(tmp_path, state):
    path = tmp_path / "library.json"
    path.write_text(json.dumps(state))
    before = path.read_bytes()
    with pytest.raises(launcher.SetupError, match="Cannot read"):
        launcher.Library(tmp_path, tmp_path / "app")
    assert path.read_bytes() == before


def test_single_instance_lock_is_released(tmp_path):
    with launcher.library_lock(tmp_path):
        with pytest.raises(launcher.SetupError, match="already open"):
            with launcher.library_lock(tmp_path):
                pytest.fail("second instance acquired lock")
    with launcher.library_lock(tmp_path):
        pass


def test_play_validates_first_and_forwards_only_selected_options(library, monkeypatch):
    game = library.import_game(Path("source"))
    calls = []
    monkeypatch.setattr(library, "run", lambda name, args, **kwargs: calls.append((name, args)))
    library.play(game["id"], True, True)
    assert calls == [("biing-check", [library.game_dir(game["id"])]),
                     ("biing", [library.game_dir(game["id"]), "--no-intro", "--fixes"])]
    def fail(name, args, **kwargs):
        assert name == "biing-check"
        raise launcher.SetupError("bad assets")
    monkeypatch.setattr(library, "run", fail)
    with pytest.raises(launcher.SetupError, match="bad assets"):
        library.play(game["id"])


def test_languages_cover_the_same_controls():
    assert launcher.TEXT["de"].keys() == launcher.TEXT["en"].keys()
    assert all(launcher.TEXT[lang].values() for lang in launcher.TEXT)


def test_trainer_uses_selected_game_and_rejects_unrelated_session(library, monkeypatch):
    entry = library.import_game(Path("synthetic.zip"))
    calls = []
    monkeypatch.setattr(library, "binary", lambda name: Path("test-engine"))
    monkeypatch.setattr(launcher, "InspectorSession", lambda *args, **kwargs: calls.append((args, kwargs)) or "session")
    monkeypatch.setattr(launcher, "serve", lambda session, port, ready: ready("http://127.0.0.1:1/#synthetic"))
    monkeypatch.setattr(launcher.webbrowser, "open", lambda url: calls.append(url))
    notified = []
    library.trainer(entry["id"], fixes=True, ready=notified.append)
    args, options = calls[0]
    assert args[:2] == (Path("test-engine"), library.game_dir(entry["id"]))
    assert args[2].parent == library.root / "trainer-sessions"
    assert options == {"previous": None, "fixes": True}
    assert notified == ["http://127.0.0.1:1/#synthetic"]
    with pytest.raises(launcher.SetupError, match="for this game"):
        library.trainer(entry["id"], previous="/not-a-session")
    assert len(calls) == 2


def test_close_keeps_active_game_and_import_alive():
    window = object.__new__(launcher.Window)
    calls = []
    window.root = SimpleNamespace(destroy=lambda: calls.append("destroy"))
    window.message = SimpleNamespace(showinfo=lambda *args, **kwargs: calls.append("busy message"))
    window.library = SimpleNamespace(state={"language": "de"})
    window.busy = True
    window.close()
    assert calls == ["busy message"]
    window.busy = False
    window.close()
    assert calls == ["busy message", "destroy"]
