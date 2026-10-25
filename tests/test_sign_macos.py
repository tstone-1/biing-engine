"""Signing boundaries use synthetic app trees and never contact a signing service."""
import json
from pathlib import Path
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import sign_macos as signing


def test_nested_output_is_rejected_without_touching_input(tmp_path):
    app = tmp_path / 'Biing.app'
    app.mkdir()
    with pytest.raises(ValueError, match='outside the input'):
        signing.prepare(app, tmp_path / 'missing-manifest', app / 'nested', 'Developer ID Application: Synthetic')
    assert list(app.iterdir()) == []


def test_signing_input_rejects_changed_added_and_missing_files(tmp_path):
    app = tmp_path / 'Biing.app'
    app.mkdir()
    code = app / 'code'
    code.write_bytes(b'synthetic executable')
    manifest = {'platform': 'Darwin', 'dirty': False,
                'source_distribution': {'project_source_sha256': 'synthetic'},
                'files': signing.inventory(app)}
    assert signing.verify_input(app, manifest) == manifest['files']
    code.write_bytes(b'changed executable')
    with pytest.raises(ValueError, match='differs'):
        signing.verify_input(app, manifest)
    code.write_bytes(b'synthetic executable')
    extra = app / 'extra'
    extra.write_bytes(b'added code')
    with pytest.raises(ValueError, match='differs'):
        signing.verify_input(app, manifest)
    extra.unlink()
    code.unlink()
    with pytest.raises(ValueError, match='differs'):
        signing.verify_input(app, manifest)


@pytest.mark.skipif(sys.platform == 'win32', reason='Windows symlink creation needs an OS privilege')
def test_signing_inventory_rejects_external_links(tmp_path):
    app = tmp_path / 'Biing.app'
    app.mkdir()
    external = tmp_path / 'outside'
    external.write_text('outside package')
    (app / 'escape').symlink_to(external)
    with pytest.raises(ValueError, match='escapes app'):
        signing.inventory(app)


def test_signing_can_only_change_declared_code_and_signature_resources():
    before = {'Contents/MacOS/Biing': 'old', 'Contents/Resources/notice.txt': 'retained'}
    after = {**before, 'Contents/MacOS/Biing': 'signed', 'Contents/_CodeSignature/CodeResources': 'seal'}
    signing.verify_payload(before, after, {'Contents/MacOS/Biing'})
    with pytest.raises(ValueError, match='non-code payload'):
        signing.verify_payload(before, {**after, 'Contents/Resources/notice.txt': 'changed'}, {'Contents/MacOS/Biing'})
    with pytest.raises(ValueError, match='Unexpected'):
        signing.verify_payload(before, {**after, 'Contents/Resources/unreviewed.py': 'new'}, {'Contents/MacOS/Biing'})
    after.pop('Contents/MacOS/Biing')
    with pytest.raises(ValueError, match='removed'):
        signing.verify_payload(before, after, {'Contents/MacOS/Biing'})


def test_modified_submission_is_rejected_before_signing_service_access(tmp_path, monkeypatch):
    app = tmp_path / 'Biing.app'
    app.mkdir()
    archive = tmp_path / 'Biing-signed-submission.zip'
    archive.write_bytes(b'approved archive')
    record = {'status': 'signed', 'files': {}, 'submission_sha256': signing.digest(archive)}
    (tmp_path / 'release.json').write_text(json.dumps(record))
    archive.write_bytes(b'changed archive')
    monkeypatch.setattr(signing, 'run', lambda args: pytest.fail('Modified content reached a signing tool'))
    with pytest.raises(ValueError, match='changed before notarization'):
        signing.notarize(tmp_path, 'synthetic-profile')
