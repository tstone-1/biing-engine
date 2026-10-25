"""Assemble notices and matching sources; refuse unreviewed runtime binaries.

Update packaging/licenses and its reviewed-runtime inventory together after reviewing
new dependency versions. License text alone does not authorize a public release.
"""
import hashlib
import json
from pathlib import Path
import re
import shutil
import zipfile
from urllib.request import urlopen
from engine_editions import parse as edition_tables

ROOT = Path(__file__).resolve().parents[1]
RUNTIME_SOURCES = ('launcher.py', 'setup_game.py', 'biing_cdrom.py', 'biing_inspector.py',
                   'biing_control.py', 'biing_saves.py', 'play.py', 'engine_editions.py',
                   'package_app.py', 'package_notices.py', 'check_package.py')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def project_sources(archive, root=ROOT):
    """Check the supplied public source ZIP against every build/runtime input."""
    required = [p for directory in ('engine', 'packaging/licenses', 'packaging/libiconv', 'tools/inspector')
                for p in (root / directory).rglob('*') if p.is_file()]
    required += [root / 'tools' / name for name in RUNTIME_SOURCES]
    required += [root / name for name in ('LICENSE', 'COPYING.txt', '.python-version',
                 'packaging/version.txt', 'packaging/requirements.txt')]
    with zipfile.ZipFile(archive) as bundle:
        entries = bundle.namelist()
        if len(entries) != len(set(entries)):
            raise ValueError('Duplicate source archive entries')
        prefix = '' if 'LICENSE' in entries else 'biing-engine/'
        for name in entries:
            path = Path(name)
            if path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name:
                raise ValueError('Unsafe source archive entry')
            rel = name.removeprefix(prefix)
            if rel.startswith(('original/', 'extracted/', '.git/')) or rel == 'CLAUDE.md':
                raise ValueError('Private content is not a distributable source archive')
        for file in required:
            name = file.relative_to(root).as_posix()
            if bundle.read(prefix + name) != file.read_bytes():
                raise ValueError('Source archive differs from build input: ' + name)
        if edition_tables(bundle.read(prefix + 'docs/editions.md').decode('utf-8')) != edition_tables((root / 'docs/editions.md').read_text(encoding='utf-8')):
            raise ValueError('Source edition measurements differ from this build')
    return len(required)


def omit_windows_os_runtime(app):
    """Windows 11 supplies UCRT and core API contracts; do not ship SDK fallbacks.

    PyInstaller collects these when they happen to be present on the build host.
    Microsoft documents that Windows 10+ always uses the system UCRT instead:
    https://learn.microsoft.com/en-us/cpp/windows/universal-crt-deployment
    Only touch the newly built package's top-level runtime directory. VC runtime
    libraries are separate redistributables and must remain in the reviewed inventory.
    """
    omitted = []
    for path in sorted((app / '_internal').iterdir()):
        if path.is_file() and (path.name.lower() == 'ucrtbase.dll' or
                              re.fullmatch(r'api-ms-win-(core|crt)-[a-z0-9-]+\.dll', path.name, re.I)):
            path.unlink()
            omitted.append(path.relative_to(app).as_posix())
    return omitted


def native_inventory(app, system, root=ROOT):
    reviewed = json.loads((root / 'packaging/licenses/runtime-inventory.json').read_text())[system]
    found, unreviewed = {}, []
    for path in sorted(app.rglob('*')):
        if not path.is_file():
            continue
        with path.open('rb') as stream:
            magic = stream.read(4)
        if magic[:2] != b'MZ' and magic not in (b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xca\xfe\xba\xbe'):
            continue
        if path.name in ('Biing', 'Biing.exe', 'biing', 'biing.exe', 'biing-check', 'biing-check.exe'):
            continue
        sha = digest(path)
        if path.name not in reviewed or sha not in reviewed[path.name]['sha256']:
            unreviewed.append(path.relative_to(app).as_posix() + ' sha256=' + sha)
            continue
        found[path.relative_to(app).as_posix()] = dict(sha256=sha, component=reviewed[path.name]['component'])
    if unreviewed:
        raise ValueError('Unreviewed runtime binaries: ' + '; '.join(unreviewed))
    if {Path(p).name for p in found} != set(reviewed):
        raise ValueError('Packaged runtime inventory differs from the reviewed platform')
    return found


def prepare_iconv_source(destination, *, root=ROOT, cached_tar=None):
    """Reproducible archive of pinned upstream source, recipe and all patches."""
    spec = json.loads((root / 'packaging/licenses/source-archives.json').read_text())['libiconv']
    if cached_tar:
        data = Path(cached_tar).read_bytes()
    else:
        with urlopen(spec['upstream'], timeout=60) as response:
            data = response.read(10_000_001)
    if len(data) > 10_000_000 or hashlib.sha256(data).hexdigest() != spec['upstream_sha256']:
        raise ValueError('libiconv upstream source checksum differs')
    entries = {p.name: p.read_bytes() for p in (root / 'packaging/libiconv').iterdir() if p.is_file()}
    entries['libiconv-1.19.tar.gz'] = data
    with zipfile.ZipFile(destination, 'w', compression=zipfile.ZIP_STORED) as bundle:
        for name, contents in sorted(entries.items()):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            # Pin the ZIP creator OS as well as time and permissions. ZipInfo
            # defaults to DOS on Windows, changing the reviewed archive hash.
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            bundle.writestr(info, contents)
    if digest(destination) != spec['sha256']:
        raise ValueError('libiconv source bundle differs from its reviewed recipe')


def assemble(notice_dir, project_archive, iconv_archive=None, *, system, root=ROOT):
    count = project_sources(project_archive, root)
    for entry in json.loads((root / 'packaging/licenses/provenance.json').read_text()):
        if digest(root / 'packaging/licenses' / entry['file']) != entry['sha256']:
            raise ValueError('License text differs from its recorded provenance: ' + entry['file'])
    notice_dir.mkdir(parents=True)
    for source in (root / 'packaging/licenses').glob('*'):
        if source.is_file():
            shutil.copy2(source, notice_dir / source.name)
    shutil.copy2(root / 'LICENSE', notice_dir / 'Project-GPL-3.0.txt')
    shutil.copy2(root / 'COPYING.txt', notice_dir / 'Project-COPYING.txt')
    sources = notice_dir.parent / 'Sources'
    sources.mkdir()
    shutil.copy2(project_archive, sources / 'project-source.zip')
    if system == 'Windows':
        expected = json.loads((root / 'packaging/licenses/source-archives.json').read_text())['libiconv']
        if iconv_archive is None:
            iconv_archive = sources / 'libiconv-source.zip'
            prepare_iconv_source(iconv_archive, root=root)
        elif digest(iconv_archive) != expected['sha256']:
            raise ValueError('Unreviewed libiconv source ZIP')
        else:
            shutil.copy2(iconv_archive, sources / 'libiconv-source.zip')
    return {'project_source_sha256': digest(project_archive), 'verified_build_inputs': count,
            'libiconv_source_sha256': digest(iconv_archive) if system == 'Windows' else None}
