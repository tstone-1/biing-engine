"""Distribution checks use synthetic files, never original game data."""
import hashlib
import json
from pathlib import Path
import sys
import zipfile

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import package_notices as notices


def test_source_bundle_matches_every_build_input_and_rejects_stale_content(tmp_path, monkeypatch):
    monkeypatch.setattr(notices, 'RUNTIME_SOURCES', ('launcher.py',))
    monkeypatch.setattr(notices, 'edition_tables', lambda text: text)
    files = ['LICENSE', 'COPYING.txt', '.python-version', 'packaging/version.txt',
             'packaging/requirements.txt', 'engine/src/example.c', 'tools/launcher.py',
             'tools/inspector/app.js', 'packaging/licenses/notice.txt', 'docs/editions.md']
    for name in files:
        p = tmp_path / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('synthetic ' + name)
    archive = tmp_path / 'source.zip'
    with zipfile.ZipFile(archive, 'w') as bundle:
        for name in files:
            bundle.write(tmp_path / name, 'biing-engine/' + name)
    assert notices.project_sources(archive, tmp_path) == len(files) - 1
    (tmp_path / 'engine/src/example.c').write_text('changed implementation')
    with pytest.raises(ValueError, match='differs from build input'):
        notices.project_sources(archive, tmp_path)
    (tmp_path / 'engine/new.c').write_text('new implementation')
    with pytest.raises((KeyError, ValueError)):
        notices.project_sources(archive, tmp_path)


@pytest.mark.parametrize('entry', ['../outside', 'biing-engine/.git/config', 'biing-engine/original/BIPRO.EXE'])
def test_source_bundle_rejects_unsafe_or_private_entries(tmp_path, entry):
    archive = tmp_path / 'source.zip'
    with zipfile.ZipFile(archive, 'w') as bundle:
        bundle.writestr('biing-engine/LICENSE', 'synthetic')
        bundle.writestr(entry, 'synthetic')
    with pytest.raises(ValueError, match='Unsafe|Private'):
        notices.project_sources(archive, tmp_path)


def test_runtime_inventory_rejects_added_changed_and_missing_native_code(tmp_path):
    root = tmp_path / 'source'
    app = tmp_path / 'app'
    app.mkdir()
    library = app / 'synthetic.dll'
    library.write_bytes(b'MZsynthetic fixture')
    config = root / 'packaging/licenses/runtime-inventory.json'
    config.parent.mkdir(parents=True)
    config.write_text(json.dumps({'Windows': {'synthetic.dll': {'component': 'synthetic', 'sha256': [notices.digest(library)]}}}))
    assert notices.native_inventory(app, 'Windows', root)['synthetic.dll']['component'] == 'synthetic'
    library.write_bytes(b'MZchanged fixture')
    with pytest.raises(ValueError, match='Unreviewed'):
        notices.native_inventory(app, 'Windows', root)
    library.write_bytes(b'MZsynthetic fixture')
    extra = app / 'additional.dll'
    extra.write_bytes(b'MZunexpected fixture')
    with pytest.raises(ValueError, match='Unreviewed'):
        notices.native_inventory(app, 'Windows', root)
    extra.unlink()
    library.unlink()
    with pytest.raises(ValueError, match='differs'):
        notices.native_inventory(app, 'Windows', root)


def test_recorded_notice_texts_match_their_provenance():
    root = Path(__file__).resolve().parents[1] / 'packaging/licenses'
    entries = json.loads((root / 'provenance.json').read_text())
    assert len(entries) >= 20
    assert len({e['file'] for e in entries}) == len(entries)
    for entry in entries:
        assert hashlib.sha256((root / entry['file']).read_bytes()).hexdigest() == entry['sha256']


def test_only_windows_os_fallbacks_are_omitted_from_new_package(tmp_path):
    runtime = tmp_path / '_internal'
    runtime.mkdir()
    removed = {'api-ms-win-core-console-l1-1-0.dll', 'API-MS-WIN-CRT-RUNTIME-L1-1-0.DLL', 'ucrtbase.dll'}
    retained = {'VCRUNTIME140.dll', 'python315.dll', 'api-ms-win-other-l1-1-0.dll', 'ucrtbase.dll.txt'}
    for name in removed | retained:
        (runtime / name).write_bytes(b'MZ synthetic')
    sources = runtime / 'sources'
    sources.mkdir()
    (sources / 'ucrtbase.dll').write_bytes(b'synthetic source fixture')
    assert set(notices.omit_windows_os_runtime(tmp_path)) == {'_internal/' + name for name in removed}
    assert {p.name for p in runtime.iterdir() if p.is_file()} == retained
    assert (sources / 'ucrtbase.dll').read_bytes() == b'synthetic source fixture'
    assert notices.omit_windows_os_runtime(tmp_path) == []


def test_runtime_inventory_reports_every_unreviewed_file(tmp_path):
    config = tmp_path / 'packaging/licenses/runtime-inventory.json'
    config.parent.mkdir(parents=True)
    config.write_text(json.dumps({'Windows': {}}))
    app = tmp_path / 'app'
    app.mkdir()
    for name in ('first.dll', 'second.dll'):
        (app / name).write_bytes(b'MZ synthetic ' + name.encode())
    with pytest.raises(ValueError, match='Unreviewed') as error:
        notices.native_inventory(app, 'Windows', tmp_path)
    assert all(name in str(error.value) and notices.digest(app / name) in str(error.value)
               for name in ('first.dll', 'second.dll'))


@pytest.mark.parametrize('creator_os', [0, 3])
def test_iconv_source_archive_has_identical_bytes_across_hosts(tmp_path, monkeypatch, creator_os):
    recipe = tmp_path / 'packaging/libiconv'
    recipe.mkdir(parents=True)
    (recipe / 'PKGBUILD').write_bytes(b'synthetic recipe\n')
    tar = tmp_path / 'upstream.tar.gz'
    tar.write_bytes(b'synthetic upstream bytes')
    expected = tmp_path / 'expected.zip'
    with zipfile.ZipFile(expected, 'w', zipfile.ZIP_STORED) as archive:
        for name, data in [('PKGBUILD', b'synthetic recipe\n'), ('libiconv-1.19.tar.gz', tar.read_bytes())]:
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.create_system = 3
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)
    config = tmp_path / 'packaging/licenses/source-archives.json'
    config.parent.mkdir()
    config.write_text(json.dumps({'libiconv': {'upstream_sha256': notices.digest(tar), 'sha256': notices.digest(expected)}}))
    original_zip_info = zipfile.ZipInfo

    class HostZipInfo(original_zip_info):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            self.create_system = creator_os

    monkeypatch.setattr(notices.zipfile, 'ZipInfo', HostZipInfo)
    actual = tmp_path / 'actual.zip'
    notices.prepare_iconv_source(actual, root=tmp_path, cached_tar=tar)
    assert actual.read_bytes() == expected.read_bytes()
