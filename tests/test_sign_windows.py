"""Synthetic package tests never contact Certum or use a real credential source."""
import json
from pathlib import Path
import sys
from types import SimpleNamespace

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import sign_windows as signing

SIGNER = 'Synthetic Project Signer'


@pytest.fixture
def package(tmp_path, monkeypatch):
    for key in ('CERTUM_EMAIL', 'CERTUM_OTP', 'CERTUM_TOKEN'):
        monkeypatch.delenv(key, raising=False)
    app = tmp_path / 'input'
    for name in signing.OWN_CODE | {'_internal/vendor.dll'}:
        path = app / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'MZ synthetic native fixture')
    (app / 'Sources').mkdir()
    source = app / 'Sources/project-source.zip'
    source.write_bytes(b'synthetic source')
    manifest = {'platform': 'Windows', 'dirty': False, 'version': '0.0.0-test',
                'files': signing.inventory(app),
                'source_distribution': {'project_source_sha256': signing.digest(source)}}
    manifest_path = tmp_path / 'manifest.json'
    signing.write_json(manifest_path, manifest)
    unsigned = {'status': 'NotSigned', 'verified': False, 'timestamp': False, 'signer': '', 'thumbprint': ''}
    rows = {name: dict(unsigned) for name in signing.OWN_CODE}
    rows['_internal/vendor.dll'] = {'status': 'Valid', 'verified': True, 'timestamp': True,
                                    'signer': 'Synthetic Vendor', 'thumbprint': 'vendor'}
    monkeypatch.setattr(signing, 'signatures', lambda *args: {name: dict(row) for name, row in rows.items()})
    output = tmp_path / 'output'
    signing.prepare(app, manifest_path, output, SIGNER)
    client = tmp_path / 'client.exe'
    client.write_bytes(b'synthetic client')
    return app, manifest, output, client, rows


def test_prepare_preserves_vendor_and_source_and_does_not_sign(package):
    app, manifest, output, _, _ = package
    record = json.loads((output / 'release.json').read_text())
    assert set(record['to_sign']) == signing.OWN_CODE
    assert signing.inventory(output / 'Biing') == manifest['files'] == signing.inventory(app)
    assert record['status'] == 'prepared' and record['publication_approved'] is False


def test_nested_output_is_rejected_without_touching_input(package):
    app, manifest, output, _, _ = package
    with pytest.raises(ValueError, match='outside the input'):
        signing.prepare(app, output / 'missing-manifest', app / 'nested', SIGNER)
    assert signing.inventory(app) == manifest['files']
    assert not (app / 'nested').exists()


@pytest.mark.parametrize('change', ['changed', 'added', 'removed', 'source-mismatch', 'dirty'])
def test_bad_input_fails_before_native_tool_or_login(package, monkeypatch, change):
    app, manifest, output, client, _ = package
    target = output / 'Biing'
    if change == 'changed':
        (target / 'Biing.exe').write_bytes(b'MZ changed')
    elif change == 'added':
        (target / 'extra.py').write_text('extra')
    elif change == 'removed':
        (target / 'Biing.exe').unlink()
    else:
        record = json.loads((output / 'release.json').read_text())
        if change == 'dirty':
            record['input_manifest']['dirty'] = True
        else:
            record['input_manifest']['source_distribution']['project_source_sha256'] = 'wrong'
        signing.write_json(output / 'release.json', record)
    monkeypatch.setattr(signing, 'signatures', lambda *args: pytest.fail('Bad package reached native verification'))
    monkeypatch.setattr(signing.subprocess, 'run', lambda *args, **kwargs: pytest.fail('Bad package reached signing'))
    with pytest.raises(ValueError):
        signing.sign(output, client, signing.digest(client))


@pytest.mark.parametrize('field,value', [('status', 'HashMismatch'), ('verified', False), ('timestamp', False)])
def test_existing_invalid_vendor_signature_is_not_overwritten(package, field, value):
    *_, rows = package
    rows['_internal/vendor.dll'][field] = value
    with pytest.raises(ValueError, match='Invalid or untimestamped'):
        signing.signing_plan(rows, SIGNER)


def test_wrong_client_rejected_before_login(package, monkeypatch):
    _, _, output, client, _ = package
    monkeypatch.setattr(signing.subprocess, 'run', lambda *args, **kwargs: pytest.fail('Wrong client executed'))
    with pytest.raises(ValueError, match='client differs'):
        signing.sign(output, client, '0' * 64)


@pytest.mark.parametrize('fault', ['none', 'service', 'notice', 'wrong-signer', 'unsigned'])
def test_signing_batch_verification_and_session_cleanup(package, monkeypatch, fault):
    app, manifest, output, client, rows = package
    monkeypatch.setenv('CERTUM_EMAIL', 'synthetic@example.invalid')
    monkeypatch.setenv('CERTUM_TOKEN', '000000')
    sessions, batches = [], []

    def fake_client(args, **kwargs):
        batches.append(args)
        session = Path(kwargs['env']['XDG_RUNTIME_DIR'])
        sessions.append(session)
        (session / 'session.json').write_text('synthetic token')
        if fault == 'service':
            return SimpleNamespace(returncode=1)
        for path in map(Path, args[1:]):
            name = path.relative_to(output / 'Biing').as_posix()
            path.write_bytes(path.read_bytes() + b' synthetic signature')
            rows[name].update(status='Valid', verified=True, timestamp=True, signer=SIGNER, thumbprint='synthetic')
        if fault == 'notice':
            (output / 'Biing/Sources/project-source.zip').write_bytes(b'changed source')
        if fault == 'wrong-signer':
            rows['Biing.exe']['signer'] = 'Wrong signer'
        if fault == 'unsigned':
            rows['Biing.exe']['status'] = 'NotSigned'
        return SimpleNamespace(returncode=0)

    monkeypatch.setattr(signing.subprocess, 'run', fake_client)
    if fault == 'none':
        signing.sign(output, client, signing.digest(client))
        record = json.loads((output / 'release.json').read_text())
        assert record['status'] == 'signed' and record['publication_approved'] is False
        assert signing.digest(output / record['archive']) == record['archive_sha256']
        assert (output / 'SHA256SUMS.txt').read_bytes() == (record['archive_sha256'] + '  ' + record['archive'] + '\n').encode()
    else:
        with pytest.raises((ValueError, RuntimeError)):
            signing.sign(output, client, signing.digest(client))
        assert not list(output.glob('*-signed.zip'))
    assert len(batches) == 1
    assert {Path(p).relative_to(output / 'Biing').as_posix() for p in batches[0][1:]} == signing.OWN_CODE
    assert all(not session.exists() for session in sessions)
    assert signing.inventory(app) == manifest['files']
