"""Build a self-contained internal test package on its target OS.

uv run --no-project --with-requirements packaging/requirements.txt python tools/package_app.py --source-archive <reviewed-source.zip> --output extracted/releases/<run>
Requires the platform's C compiler, CMake, Ninja and SDL3 only on the build machine.
No original data is read or included. Output must be a new directory. The application
is ad-hoc signed on macOS; notarization/production signing remain separate release gates.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import sys
from package_notices import assemble, native_inventory, omit_windows_os_runtime

REPO = Path(__file__).resolve().parents[1]


def run(command, **kwargs):
    subprocess.run(list(map(str, command)), check=True, **kwargs)


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def main():
    if sys.version_info[:3] != (3, 15, 1):
        raise RuntimeError("Package builds require Python 3.15.1; run through uv from the repository root")
    import PyInstaller
    if PyInstaller.__version__ != '6.22.3':
        raise RuntimeError('Review the bootloader notices before changing PyInstaller')
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--output", required=True, type=Path)
    ap.add_argument("--source-archive", required=True, type=Path, help="reviewed public source ZIP matching this build")
    ap.add_argument("--libiconv-source", type=Path, help="offline reviewed libiconv source ZIP; otherwise Windows builds download pinned source")
    args = ap.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    build = REPO / "build-package"
    version = (REPO / "packaging/version.txt").read_text().strip()
    flags = f"-ffile-prefix-map={REPO.as_posix()}=. -fdebug-prefix-map={REPO.as_posix()}=."
    run(["cmake", "-S", REPO / "engine", "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_FLAGS=" + flags])
    run(["cmake", "--build", build, "--target", "biing", "biing-check", "--parallel", "4"])
    suffix = ".exe" if os.name == "nt" else ""
    command = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onedir", "--windowed", "--noupx",
               "--name", "Biing", "--distpath", out / "dist", "--workpath", out / "work", "--specpath", out,
               "--add-data", str(REPO / "packaging/version.txt") + os.pathsep + "packaging",
               "--add-data", str(REPO / "tools/inspector") + os.pathsep + "inspector"]
    if os.name == "nt":
        numbers = tuple(map(int, version.split("-", 1)[0].split("."))) + (0,)
        resource = out / "version-resource.txt"
        resource.write_text(
            "VSVersionInfo(ffi=FixedFileInfo(filevers=" + repr(numbers) + ", prodvers=" + repr(numbers) + ", mask=0x3f, flags=0, OS=0x40004, fileType=1, subtype=0, date=(0,0)), "
            "kids=[StringFileInfo([StringTable('040904B0', [StringStruct('FileDescription','Biing launcher'), StringStruct('ProductName','Biing'), "
            "StringStruct('FileVersion'," + repr(version) + "), StringStruct('ProductVersion'," + repr(version) + ")])]), VarFileInfo([VarStruct('Translation',[1033,1200])])])",
            encoding="utf-8")
        command.extend(["--version-file", resource])
    for binary in [build / ("biing" + suffix), build / ("biing-check" + suffix), *sorted(build.glob("*.dll"))]:
        command.extend(["--add-binary", str(binary) + os.pathsep + "engine"])
    if sys.platform == "darwin":
        command.extend(["--osx-bundle-identifier", "local.biing.launcher"])
    command.append(REPO / "tools/launcher.py")
    run(command, cwd=REPO)
    app = out / "dist" / ("Biing.app" if sys.platform == "darwin" else "Biing")
    os_runtime = omit_windows_os_runtime(app) if os.name == 'nt' else []
    notice_dir = app / ("Contents/Resources/Notices" if sys.platform == "darwin" else "Notices")
    source_info = assemble(notice_dir, args.source_archive, args.libiconv_source, system=platform.system())
    # Preserve the exact interpreter license in addition to the complete source notices.
    licenses = list(Path(sys.base_prefix).glob("lib/python*/LICENSE.txt")) + list(Path(sys.base_prefix).glob("LICENSE.txt"))
    if not licenses:
        raise RuntimeError("Python runtime license not found")
    shutil.copy2(licenses[0], notice_dir / "Python-LICENSE.txt")
    notice_dir.joinpath("BUILD-NOTICE.txt").write_text(
        "Internal test build. No original game data is included.\n"
        "Project code: GPL-3.0-only. See NOTICES.txt and the included Sources directory.\n"
        "Native acceptance and production signing remain separate public-download gates.\n",
        encoding="utf-8")
    quickstart = ("Biing " + version + " - internal test build\n\n"
        "ENGLISH\n1. Extract the complete package. On macOS open Biing.app; on Windows open Biing.exe inside the Biing folder.\n"
        "2. Click Add game files and choose your own disk-version zip (macOS also accepts a GOG .pkg or .app).\n"
        "   Use Add game folder for an installed game. The original files are only read.\n"
        "3. Select the imported edition and click Play. Next time, just open the app and click Play.\n"
        "Show saved games opens the save folder. Updating/replacing the app keeps imported games and saves.\n"
        "Close the game before closing the launcher. The optional bug fixes are off by default.\n\n"
        "TRAINER: Select New trainer session or a dated session, then Start trainer. Pause to edit.\n"
        "Save checkpoint keeps progress; Restore checkpoint first backs up the current state.\n"
        "Stop session saves an exit checkpoint. Save first if closing the game window directly.\n"
        "Trainer sessions opens the separate copies. They are not copied back to the normal game.\n\n"
        "DEUTSCH\n1. Entpacke das gesamte Paket. Öffne auf macOS Biing.app, auf Windows Biing.exe im Ordner Biing.\n"
        "2. Wähle unter Spieldateien hinzufügen dein eigenes ZIP der Diskversion (auf macOS auch eine GOG-.pkg oder -.app).\n"
        "   Für ein installiertes Spiel nutze Spielordner hinzufügen. Die Originaldateien werden nur gelesen.\n"
        "3. Wähle die importierte Ausgabe und klicke auf Spielen. Beim nächsten Start genügt Spielen.\n"
        "Spielstände anzeigen öffnet den Speicherordner. Ein App-Update behält Spielkopien und Spielstände.\n"
        "Schließe das Spiel vor dem Starter. Die optionalen Fehlerkorrekturen sind zunächst ausgeschaltet.\n\n"
        "TRAINER: Neue oder datierte Trainersitzung wählen, dann Trainer starten. Zum Bearbeiten pausieren.\n"
        "Spielstand sichern behält den Fortschritt; Wiederherstellen sichert vorher den aktuellen Stand.\n"
        "Sitzung beenden sichert abschließend. Vor direktem Schließen des Spielfensters selbst sichern.\n"
        "Trainersitzungen zeigt die separaten Kopien. Keine automatische Übertragung ins normale Spiel.\n\n"
        "No game data is included. This test package is not approved for public distribution.\n")
    notice_dir.parent.joinpath("START-HERE.txt").write_text(quickstart, encoding="utf-8")
    dependencies = {}
    minimum_os = None
    if sys.platform == "darwin":
        seen = set()
        minima = []
        for p in sorted(app.rglob("*")):
            if not p.is_file() or p.resolve() in seen:
                continue
            seen.add(p.resolve())
            with p.open("rb") as stream:
                magic = stream.read(4)
            if magic not in (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe"):
                continue
            lines = subprocess.check_output(["otool", "-L", str(p)], text=True).splitlines()[1:]
            names = [line.strip().split(" (", 1)[0] for line in lines if line.strip()]
            if any(not name.startswith(("@", "/usr/lib/", "/System/Library/")) for name in names):
                raise RuntimeError("Nonportable library dependency in " + str(p))
            dependencies[p.relative_to(app).as_posix()] = names
            loads = subprocess.check_output(["otool", "-l", str(p)], text=True)
            for block in loads.split("Load command"):
                if "cmd LC_BUILD_VERSION\n" in block or "cmd LC_VERSION_MIN_MACOSX\n" in block:
                    match = re.search(r"\b(?:minos|version) ([0-9.]+)", block)
                    if match:
                        minima.append(match.group(1))
        if not minima:
            raise RuntimeError("Cannot determine the package's macOS deployment target")
        minimum_os = max(minima, key=lambda value: tuple(map(int, value.split("."))))
        info = app / "Contents/Info.plist"
        value = plistlib.loads(info.read_bytes())
        value.update(CFBundleShortVersionString=version.split("-", 1)[0], CFBundleVersion="1", LSMinimumSystemVersion=minimum_os)
        info.write_bytes(plistlib.dumps(value))
        run(["codesign", "--force", "--deep", "--sign", "-", app])
        run(["codesign", "--verify", "--deep", "--strict", app])
    runtime = native_inventory(app, platform.system())
    files = {p.relative_to(app).as_posix(): digest(p) for p in sorted(app.rglob("*")) if p.is_file()}
    forbidden = [p for p in files if any(part.upper() in {"BIPRO.EXE", "BITEXPZ", "BITEXPE", "BIDATP", "BIDATPE", "GAME.GOG", "ORIGINAL", "EXTRACTED"} for part in Path(p).parts)]
    if forbidden:
        raise RuntimeError("Game content in package: " + ", ".join(forbidden))
    manifest = {"version": version, "platform": platform.system(), "architecture": platform.machine(),
                "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip(),
                "dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=REPO, text=True).strip()),
                "python": platform.python_version(), "pyinstaller": PyInstaller.__version__, "files": files, "dependencies": dependencies,
                "minimum_os": minimum_os, "publication_approved": False,
                "runtime_inventory": runtime, "os_provided_runtime": os_runtime, "source_distribution": source_info}
    sources = [REPO / ".python-version", REPO / "LICENSE", REPO / "COPYING.txt", *sorted((REPO / "engine").rglob("*")), *sorted((REPO / "packaging").rglob("*")),
               *sorted((REPO / "tools/inspector").glob("*")),
               *[REPO / "tools" / n for n in ("launcher.py", "setup_game.py", "biing_cdrom.py", "biing_inspector.py", "biing_control.py", "package_app.py", "package_notices.py", "check_package.py")]]
    manifest["source_files"] = {p.relative_to(REPO).as_posix(): digest(p) for p in sources if p.is_file()}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    system = "macos" if sys.platform == "darwin" else platform.system().lower()
    architecture = {"AMD64": "x86_64", "aarch64": "arm64"}.get(platform.machine(), platform.machine())
    archive = out / f"Biing-{version}-{system}-{architecture}.zip"
    if sys.platform == "darwin":
        run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", app, archive])
    else:
        shutil.make_archive(str(archive.with_suffix("")), "zip", app.parent, app.name)
    (out / "SHA256SUMS.txt").write_text(digest(archive) + "  " + archive.name + "\n", encoding="utf-8", newline="\n")
    print("[OK] " + str(archive))
    return 0


if __name__ == "__main__":
    sys.exit(main())
