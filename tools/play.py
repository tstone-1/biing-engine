"""Build, prepare a private game copy and play, using Python 3.9+ and the standard library.

From a clone: python3 tools/play.py
The first run asks for the disk zip, GOG .pkg/app or installed game folder. Later runs
reuse the prepared game, including saves. Build tools must already be installed.
--source PATH avoids the prompt; --game PATH chooses another destination/edition.
--prepare-only performs the same build/setup/validation without opening a window.
No dependencies are installed, originals overwritten, or files downloaded by this tool.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from setup_game import SetupError, child, identify, setup


def source_path(text: str) -> Path:
    """Accept a plain path or a file dragged into a shell, without evaluating it."""
    text = text.strip()
    if not text:
        raise SetupError("no game source given: enter the path to your game archive or installed game folder")
    direct = Path(text).expanduser()
    if direct.exists():
        return direct
    if os.name == "nt":
        # Preserve Windows backslashes; shell-style escapes are a POSIX convention.
        if len(text) > 1 and text[0] == text[-1] and text[0] in "\"'":
            text = text[1:-1]
    else:
        try:
            words = shlex.split(text)
        except ValueError:
            words = []
        if len(words) == 1:
            text = words[0]
    return Path(text).expanduser()


def prepare(repo: Path, game: Path, source: Path | None = None) -> Path:
    """Keep source and destination separate; never replace an existing prepared game."""
    if source is not None and game.exists() and (not game.is_dir() or any(game.iterdir())):
        raise SetupError(f"{game} already contains files: omit --source to keep playing, or choose another --game folder")
    existing = game.exists() and (not game.is_dir() or any(game.iterdir()))
    if existing:
        exe = child(game, "BIPRO.EXE")
        if exe is None:
            raise SetupError(f"{game} is not a prepared game: choose another destination with --game")
        edition = identify(exe)
        if edition.fix:
            raise SetupError(edition.fix)
        print(f"[INFO] Keeping your existing game and saves: {game}", flush=True)
    else:
        if source is None:
            print("Choose your own game files: a disk-version zip, GOG installer (.pkg), app or installed folder.", flush=True)
            try:
                source = source_path(input("Paste or drag its path here, then press Enter: "))
            except EOFError:
                raise SetupError("no input available: run in a terminal, or supply --source with your game path") from None
        if not source.exists():
            raise SetupError(f"{source} does not exist: check the game path")

    missing = [name for name in ("cmake", "ninja") if shutil.which(name) is None]
    if missing:
        raise SetupError("missing build tools: " + ", ".join(missing) + "; install the prerequisites in README.md")
    build = repo / "build"
    build.mkdir(exist_ok=True)
    log_path = build / "startup.log"
    suffix = ".exe" if os.name == "nt" else ""
    engine, checker = build / ("biing" + suffix), build / ("biing-check" + suffix)
    print("[INFO] Building the engine (or checking that it is up to date)...", flush=True)
    with log_path.open("w", encoding="utf-8") as log:
        for command in (["cmake", "-S", str(repo / "engine"), "-B", str(build), "-G", "Ninja"],
                        ["cmake", "--build", str(build), "--target", "biing", "biing-check", "--parallel", "4"]):
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
            if result.returncode:
                raise SetupError(f"build failed: see {log_path}; check the compiler and SDL3 prerequisites in README.md")
        if not existing:
            print("[INFO] Preparing a separate game copy; your original files stay unchanged...", flush=True)
            print(setup(source, game), flush=True)
        print("[INFO] Checking the game files...", flush=True)
        result = subprocess.run([str(checker), str(game)], stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise SetupError(f"game-file check failed: see {log_path}; the existing game and saves have been kept")
    print(f"[OK] Ready: {game}", flush=True)
    return engine


def main(argv=None) -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", type=Path, help="your game archive, app or installed folder (first run only)")
    parser.add_argument("--game", type=Path, default=repo.parent / "game", help="prepared game folder; default: game beside the repository")
    parser.add_argument("--prepare-only", action="store_true", help="build and check without opening a window")
    parser.add_argument("--no-intro", action="store_true", help="skip the opening sequence")
    parser.add_argument("--fixes", action="store_true", help="enable the optional original-game bug fixes")
    args = parser.parse_args(argv)
    try:
        game = args.game.expanduser().resolve()
        source = args.source.expanduser().resolve() if args.source is not None else None
        engine = prepare(repo, game, source)
        if args.prepare_only:
            return 0
        command = [str(engine), str(game)]
        if args.no_intro:
            command.append("--no-intro")
        if args.fixes:
            command.append("--fixes")
        return subprocess.call(command)
    except (SetupError, OSError) as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\n[INFO] Cancelled.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
