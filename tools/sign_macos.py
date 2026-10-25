"""Sign a verified package copy, then notarize it without rebuilding gameplay code.

uv run --no-project python tools/sign_macos.py prepare --app Biing.app --manifest manifest.json --output <new-folder>
uv run --no-project python tools/sign_macos.py notarize --output <same-folder>

prepare uses APPLE_SIGNING_IDENTITY. notarize uses APPLE_API_KEY_PATH,
APPLE_API_KEY and APPLE_API_ISSUER, or --keychain-profile. Credentials stay outside
the repository. Notarization uploads the signed app to Apple, not a public release.
The original app is never changed. A pending notarization resumes by submission ID.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

MAGICS = {b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(app):
    """Reject escaping links before following any package file."""
    result = {}
    root = app.resolve()
    for path in sorted(app.rglob('*')):
        if not path.resolve().is_relative_to(root):
            raise ValueError('Package link escapes app: ' + path.relative_to(app).as_posix())
        if path.is_file():
            result[path.relative_to(app).as_posix()] = digest(path)
    return result


def verify_input(app, manifest):
    if manifest.get('platform') != 'Darwin' or manifest.get('dirty') is not False:
        raise ValueError('Require a clean macOS package manifest')
    if not manifest.get('source_distribution', {}).get('project_source_sha256'):
        raise ValueError('Require matching source distribution')
    actual = inventory(app)
    if actual != manifest.get('files'):
        raise ValueError('App content differs from the verified package manifest')
    return actual


def run(args):
    # Do not echo command lines: notarization authentication stays out of logs.
    return subprocess.check_output(list(map(str, args)), stderr=subprocess.STDOUT, text=True)


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def verify_payload(before, after, signed_files):
    allowed = set(signed_files)
    for name, sha in before.items():
        if name not in allowed and not name.startswith('Contents/_CodeSignature/'):
            if after.get(name) != sha:
                raise ValueError('Signing changed non-code payload: ' + name)
    unexpected = set(after) - set(before)
    if any(not name.startswith('Contents/_CodeSignature/') for name in unexpected):
        raise ValueError('Unexpected files added while signing')
    if set(before) - set(after):
        raise ValueError('Signing removed package files')


def prepare(app, manifest_path, output, identity):
    if output.resolve().is_relative_to(app.resolve()):
        raise ValueError('Output must be outside the input package')
    if not identity.startswith('Developer ID Application:'):
        raise ValueError('Require a Developer ID Application identity')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    before = verify_input(app, manifest)
    output.mkdir(parents=True, exist_ok=False)
    target = output / 'Biing.app'
    run(['ditto', app, target])
    if inventory(target) != before:
        raise ValueError('Copied app differs from verified input')
    code, seen = [], set()
    for path in sorted(target.rglob('*')):
        if not path.is_file() or path.resolve() in seen:
            continue
        seen.add(path.resolve())
        with path.open('rb') as stream:
            if stream.read(4) not in MAGICS:
                continue
        code.append(path.resolve())
    if not code:
        raise ValueError('No native code found')
    # Sign every nested executable/library first, then seal the outer bundle.
    # No runtime exceptions are granted: all bundled libraries share this identity.
    for path in code:
        run(['codesign', '--force', '--timestamp', '--options', 'runtime', '--sign', identity, path])
    run(['codesign', '--force', '--timestamp', '--options', 'runtime', '--sign', identity, target])
    run(['codesign', '--verify', '--deep', '--strict', '--verbose=2', target])
    signatures = {}
    for path in [*code, target]:
        value = run(['codesign', '-d', '--verbose=4', path])
        if 'Authority=' + identity not in value or '(runtime)' not in value or 'Timestamp=' not in value:
            raise ValueError('Missing Developer ID, hardened runtime or timestamp')
        signatures[path.relative_to(target).as_posix()] = value
    after = inventory(target)
    signed_names = {p.relative_to(target).as_posix() for p in code}
    # Inventory includes symlink aliases; their targets were signed exactly once.
    signed_names.update(p.relative_to(target).as_posix() for p in target.rglob('*') if p.is_file() and p.resolve() in code)
    verify_payload(before, after, signed_names)
    archive = output / 'Biing-signed-submission.zip'
    run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', target, archive])
    write_json(output / 'signatures.json', signatures)
    write_json(output / 'release.json', {'input_manifest': manifest, 'signed_files': sorted(signed_names),
               'files': after, 'submission_sha256': digest(archive), 'status': 'signed',
               'publication_approved': False})
    print('[OK] Signed package prepared; notarization has not been submitted')


def notarize(output, profile):
    record_path = output / 'release.json'
    record = json.loads(record_path.read_text(encoding='utf-8'))
    if record['status'] == 'notarized':
        raise ValueError('Package is already notarized')
    target = output / 'Biing.app'
    archive = output / 'Biing-signed-submission.zip'
    if inventory(target) != record['files'] or digest(archive) != record['submission_sha256']:
        raise ValueError('Signed package changed before notarization')
    run(['codesign', '--verify', '--deep', '--strict', target])
    if profile:
        auth = ['--keychain-profile', profile]
    else:
        auth = ['--key', os.environ['APPLE_API_KEY_PATH'], '--key-id', os.environ['APPLE_API_KEY'],
                '--issuer', os.environ['APPLE_API_ISSUER']]
    if not record.get('submission_id'):
        submitted = json.loads(run(['xcrun', 'notarytool', 'submit', archive, *auth, '--output-format', 'json']))
        record['submission_id'] = submitted['id']
        record['status'] = 'submitted'
        write_json(record_path, record)
    submission = record['submission_id']
    status = json.loads(run(['xcrun', 'notarytool', 'wait', submission, *auth, '--timeout', '10m', '--output-format', 'json']))
    write_json(output / 'notary-status.json', status)
    log = json.loads(run(['xcrun', 'notarytool', 'log', submission, *auth]))
    write_json(output / 'notary-log.json', log)
    if status.get('status') != 'Accepted' or log.get('issues'):
        raise ValueError('Notarization needs review; inspect the retained status/log')
    run(['xcrun', 'stapler', 'staple', target])
    run(['xcrun', 'stapler', 'validate', target])
    run(['codesign', '--verify', '--deep', '--strict', target])
    assessment = run(['spctl', '--assess', '--type', 'execute', '--verbose=4', target])
    if 'source=Notarized Developer ID' not in assessment:
        raise ValueError('Gatekeeper did not establish notarized Developer ID')
    (output / 'gatekeeper.txt').write_text(assessment, encoding='utf-8')
    version = record['input_manifest']['version']
    final = output / f'Biing-{version}-macos-arm64-notarized.zip'
    run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', target, final])
    record.update(status='notarized', files=inventory(target), archive=final.name, archive_sha256=digest(final))
    write_json(record_path, record)
    (output / 'SHA256SUMS.txt').write_text(digest(final) + '  ' + final.name + '\n', encoding='utf-8')
    print('[OK] Notarized, stapled and accepted by Gatekeeper; publication remains separate')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    p = commands.add_parser('prepare')
    p.add_argument('--app', type=Path, required=True)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--identity', default=os.environ.get('APPLE_SIGNING_IDENTITY', ''))
    p.add_argument('--output', type=Path, required=True)
    n = commands.add_parser('notarize')
    n.add_argument('--output', type=Path, required=True)
    n.add_argument('--keychain-profile')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('Signing requires macOS')
    if args.command == 'prepare':
        prepare(args.app.resolve(), args.manifest, args.output.resolve(), args.identity)
    else:
        notarize(args.output.resolve(), args.keychain_profile)


if __name__ == '__main__':
    main()
