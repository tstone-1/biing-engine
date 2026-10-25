"""Prepare and sign a copy of a verified Windows package, without publishing it.

uv run --no-project python tools/sign_windows.py prepare --app <dist/Biing> --manifest <manifest.json> --output <new-folder> --signer <certificate-common-name>
uv run --no-project python tools/sign_windows.py sign --output <same-folder> --ssign <ssign.exe> --ssign-sha256 <reviewed-client-sha256>

Windows requires PowerShell 7 and Windows SDK signtool (optionally --signtool).
Build the client outside this checkout, from the reviewed pin used by the other
signing projects: cargo install --locked --git https://github.com/Le-Syl21/ssign
--rev 5fd4daf22155b19b645aef3dd467f3c4c9e440b3 --root <client-root> ssign
Record its SHA-256 after building. Credentials are environment-only: CERTUM_EMAIL
and exactly one of CERTUM_OTP (full otpauth URI) or CERTUM_TOKEN (current code).
Serialize signing across projects; repository-local CI concurrency is insufficient.
One invocation signs the entire batch; its private temporary session is removed on
success or failure. Existing valid, timestamped vendor signatures are preserved.
No secret or raw signing-service response is printed or written to the release.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

OWN_CODE = {'Biing.exe', '_internal/engine/biing.exe', '_internal/engine/biing-check.exe'}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(app):
    result = {}
    for path in sorted(app.rglob('*')):
        if path.is_symlink() or path.is_junction():
            raise ValueError('Package links are not supported')
        if path.is_file():
            result[path.relative_to(app).as_posix()] = digest(path)
    return result


def native_files(app, files):
    names = []
    for name in files:
        with (app / name).open('rb') as stream:
            if stream.read(2) == b'MZ':
                names.append(name)
    if not OWN_CODE <= set(names):
        raise ValueError('Missing launcher or engine executables')
    return names


def verify_input(app, manifest):
    if manifest.get('platform') != 'Windows' or manifest.get('dirty') is not False:
        raise ValueError('Require a clean Windows package manifest')
    files = inventory(app)
    if files != manifest.get('files'):
        raise ValueError('Package differs from its manifest')
    source = manifest.get('source_distribution', {}).get('project_source_sha256')
    if not source or files.get('Sources/project-source.zip') != source:
        raise ValueError('Require the matching source distribution')
    return files


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def signatures(app, names, signtool=None):
    # Child verification tools have no reason to inherit the cloud login.
    env = {key: value for key, value in os.environ.items() if not key.startswith('CERTUM_')}
    with tempfile.TemporaryDirectory(prefix='biing-signatures-') as directory:
        paths = Path(directory) / 'paths.json'
        write_json(paths, [str(app / name) for name in names])
        args = ['pwsh', '-NoProfile', '-File', str(Path(__file__).with_name('windows_signatures.ps1')),
                '-PathsFile', str(paths)]
        if signtool:
            args += ['-SignTool', str(signtool)]
        result = subprocess.run(args, capture_output=True, encoding='utf-8', env=env, timeout=180)
        if result.returncode:
            raise RuntimeError('Native signature inspection failed; verify PowerShell 7 and the Windows SDK')
        rows = json.loads(result.stdout.lstrip('\ufeff'))
    if not isinstance(rows, list) or len(rows) != len(names):
        raise ValueError('Incomplete signature inspection')
    indexed = {Path(row['path']).relative_to(app).as_posix(): row for row in rows}
    if set(indexed) != set(names):
        raise ValueError('Signature inspection returned different files')
    for row in indexed.values():
        row.pop('path')
    return indexed


def signing_plan(rows, signer):
    pending = []
    for name, row in rows.items():
        if row['status'] == 'NotSigned':
            pending.append(name)
        elif not (row['status'] == 'Valid' and row['verified'] and row['timestamp']):
            raise ValueError('Invalid or untimestamped existing signature: ' + name)
        elif name in OWN_CODE and row['signer'] != signer:
            raise ValueError('Unexpected project signer: ' + name)
    return sorted(pending)


def prepare(app, manifest_path, output, signer, signtool=None):
    if output.resolve().is_relative_to(app.resolve()):
        raise ValueError('Output must be outside the input package')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    files = verify_input(app, manifest)
    names = native_files(app, files)
    rows = signatures(app, names, signtool)
    pending = signing_plan(rows, signer)
    output.mkdir(parents=True, exist_ok=False)
    target = output / 'Biing'
    shutil.copytree(app, target)
    if inventory(target) != files:
        raise ValueError('Copied package differs from verified input')
    write_json(output / 'release.json', {'input_manifest': manifest, 'files': files,
               'native_files': names, 'signatures_before': rows, 'to_sign': pending,
               'signer': signer, 'status': 'prepared', 'publication_approved': False})
    print(f'[OK] Prepared {len(names)} native files: {len(pending)} need signing; no login performed')


def verify_payload(before, after, changed):
    if set(before) != set(after):
        raise ValueError('Signing added or removed package files')
    if any(after[name] != sha for name, sha in before.items() if name not in changed):
        raise ValueError('Signing changed a preserved package file')


def sign(output, client, client_sha, signtool=None):
    record = json.loads((output / 'release.json').read_text(encoding='utf-8'))
    if record['status'] != 'prepared':
        raise ValueError('Require a fresh prepared package')
    target = output / 'Biing'
    before = verify_input(target, record['input_manifest'])
    if before != record['files']:
        raise ValueError('Prepared package changed')
    names = native_files(target, before)
    rows = signatures(target, names, signtool)
    pending = signing_plan(rows, record['signer'])
    if pending != record['to_sign'] or rows != record['signatures_before']:
        raise ValueError('Signature state changed since preparation')
    if not re.fullmatch(r'[0-9a-fA-F]{64}', client_sha) or digest(client) != client_sha.lower():
        raise ValueError('Signing client differs from its reviewed SHA-256')
    if pending:
        if not os.environ.get('CERTUM_EMAIL') or bool(os.environ.get('CERTUM_OTP')) == bool(os.environ.get('CERTUM_TOKEN')):
            raise ValueError('Set CERTUM_EMAIL and exactly one of CERTUM_OTP or CERTUM_TOKEN')
        # XDG_RUNTIME_DIR is the pinned client's first-choice cache location.
        # Python 3.15 creates this directory with an owner-only Windows ACL.
        with tempfile.TemporaryDirectory(prefix='biing-signing-session-') as session:
            env = {**os.environ, 'XDG_RUNTIME_DIR': session}
            result = subprocess.run([str(client), *[str(target / name) for name in pending]],
                                    env=env, capture_output=True, timeout=600)
            if result.returncode:
                raise RuntimeError('Signing failed; no automatic login retry. Prepare a fresh copy before retrying')
    after = inventory(target)
    verify_payload(before, after, pending)
    final = signatures(target, names, signtool)
    if signing_plan(final, record['signer']):
        raise ValueError('Unsigned files remain after signing')
    if any(final[name]['signer'] != record['signer'] for name in pending):
        raise ValueError('New signature has an unexpected signer')
    # Copying/signing never changes notices or source ZIPs. Validate the final
    # archive against the exact verified on-disk inventory before reporting it.
    version = record['input_manifest']['version']
    if not re.fullmatch(r'[0-9A-Za-z.+-]+', version):
        raise ValueError('Unsafe package version')
    archive = output / f'Biing-{version}-windows-x86_64-signed.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
        for name in after:
            bundle.write(target / name, 'Biing/' + name)
    with zipfile.ZipFile(archive) as bundle:
        actual = {name.removeprefix('Biing/'): hashlib.sha256(bundle.read(name)).hexdigest() for name in bundle.namelist()}
        if actual != after:
            raise ValueError('Signed archive differs from verified files')
    record.update(status='signed', files=after, signatures_after=final, client_sha256=client_sha.lower(),
                  archive=archive.name, archive_sha256=digest(archive))
    write_json(output / 'release.json', record)
    (output / 'SHA256SUMS.txt').write_text(digest(archive) + '  ' + archive.name + '\n', encoding='utf-8', newline='\n')
    print(f'[OK] All {len(names)} native signatures verified; package ready for runtime acceptance, not published')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    p = commands.add_parser('prepare')
    p.add_argument('--app', type=Path, required=True)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--signer', required=True)
    s = commands.add_parser('sign')
    s.add_argument('--ssign', type=Path, required=True)
    s.add_argument('--ssign-sha256', required=True)
    for command in (p, s):
        command.add_argument('--output', type=Path, required=True)
        command.add_argument('--signtool', type=Path)
    args = parser.parse_args()
    if sys.platform != 'win32':
        parser.error('Signature verification requires Windows')
    if args.command == 'prepare':
        prepare(args.app.resolve(), args.manifest, args.output.resolve(), args.signer, args.signtool)
    else:
        sign(args.output.resolve(), args.ssign.resolve(), args.ssign_sha256, args.signtool)


if __name__ == '__main__':
    main()
