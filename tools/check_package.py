"""Verify a packaged launcher without Python/build tools on the child's PATH.

uv run --no-project python tools/check_package.py --app <Biing.app or Biing folder>
  --originals original --output extracted/validation/<new-folder>
No native input/listening coverage is claimed. Each edition gets a fresh data folder.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--originals", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    app = args.app.resolve()
    notices = app / ('Notices' if os.name == 'nt' else 'Contents/Resources/Notices')
    sources = notices.parent / 'Sources'
    for path in (notices / 'NOTICES.txt', notices / 'runtime-inventory.json', sources / 'project-source.zip'):
        if not path.is_file() or not path.stat().st_size:
            raise RuntimeError('Missing distribution notice or source: ' + str(path))
    if os.name == 'nt' and not (sources / 'libiconv-source.zip').is_file():
        raise RuntimeError('Missing bundled libiconv corresponding source')
    binary = app / ("Biing.exe" if os.name == "nt" else "Contents/MacOS/Biing")
    env = {k: v for k, v in os.environ.items() if not k.startswith(("PYTHON", "DYLD_", "LD_", "BIING_"))}
    env["PATH"] = str(Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32") if os.name == "nt" else "/usr/bin:/bin:/usr/sbin:/sbin"
    sources = {"disk": "zip/Biing!_Diskversion.zip"}
    if sys.platform == "darwin":
        sources.update({"gog-de": "pkg/biing___sex__intrigue_and_scalpels_deDE_1_0_35750.pkg",
                        "gog-en": "pkg/biing___sex__intrigue_and_scalpels_enUS_1_0_35750.pkg"})
    for edition, source in sources.items():
        report = out / (edition + ".json")
        with (out / (edition + ".log")).open("w", encoding="utf-8") as log:
            result = subprocess.run([str(binary), "--verify-package", str((args.originals / source).resolve()),
                                     "--data-root", str(out / edition), "--report", str(report)],
                                    cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=300)
        if result.returncode or not report.is_file():
            raise RuntimeError(f"{edition} package verification failed; see {out}")
        value = json.loads(report.read_text(encoding="utf-8"))
        if value.get("python") != [3, 15, 1]:
            raise RuntimeError(f"{edition} package does not run Python 3.15.1: {value.get('python')}")
        if value.get("edition") != edition or any(value.get(k) is not True for k in ("frozen", "import", "reopen", "saved_file_kept", "pictures_differ")):
            raise RuntimeError(f"{edition} incomplete package verification: {value}")
        if edition == "disk" and value.get("trainer_restore_reopen_assets") is not True:
            raise RuntimeError("Packaged trainer restore/reopen/assets verification is missing")
        print("[PASS] packaged " + edition + ": Python 3.15.1, import, reopen, saved file, title/office; restricted PATH", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
