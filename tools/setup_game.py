"""Prepare the game folder the engine plays from, out of the user's own copy of Biing!, in one command.

Usage:
  python3 tools/setup_game.py <source> <dest>

<source> is any of:
  - the disk version's zip (`Biing!_Diskversion.zip`), or the folder it was unpacked to;
  - a GOG installer for macOS (`biing___sex__intrigue_and_scalpels_<deDE|enUS>_1_0_35750.pkg`), unpacked with
    `pkgutil --expand-full` into a temporary folder (runs none of the installer's scripts);
  - the installed GOG app (`Biing! Sex, Intrigue and Scalpels.app`) or its `Contents/Resources/game` folder;
  - a game folder that is already unpacked (an installed floppy version's `BIING!`, a folder prepared earlier).
<dest> must not exist or be an empty folder; it becomes the game folder (`BIPRO.EXE`, the texts, `DATA/`, `GAMES/`).

What it does: finds the game inside the source (a GOG `game.ins`, else the one folder holding `BIPRO.EXE` and
`DATA/`), copies the CD image's files out with tools/biing_cdrom.py and merges GOG's `BIING!` folder (configs,
`GAMES/`) over them, puts the CD's audio tracks (the music the GOG game plays with `MCARD CD`) into `CDAUDIO/` as
`TRACK02.WAV` ... `TRACK09.WAV` (16-bit stereo 44,100 Hz, the image's audio sectors unchanged, about 88 MB; the
engine plays its songs from them, docs/cdrom.md "CD audio music in BIPRO.EXE"), identifies the edition by the size and SHA-256 of `BIPRO.EXE` (EDITIONS below), puts
version 1.4 back when the folder holds version 1.0 and the disk zip's `VERSION14/` is in the source (what
`PlayV14.bat` does), makes everything user-writable, creates `GAMES/`, checks the folder (program, texts
`BITEXPZ`/`BITEXPE`, records `BIDATP`/`BIDATPE`, `DATA/`, a write into `GAMES/`) and only then moves it to <dest>.
Output: one `[OK] <edition> ...` line, or one `[FAIL] <what>: <fix>` line (exit 1). The source is only read;
temporary files go to a hidden folder next to <dest> and are deleted in every case.

Runs on macOS's own /usr/bin/python3 (3.9), standard library only. Tests: tests/test_setup_game.py (synthetic zip,
folder trees and CD images; the edition table is injected, so no game bytes are needed). The user-facing steps and
every [FAIL] message the manual quotes: MANUAL.md section 4, checked by tools/test_manual.py. Mutation check
(2026-10-20): 28 mutants of this file (each refusal, check, merge, clean-up and message choice removed or weakened)
each made tests/test_setup_game.py fail.

The EDITIONS checksums were measured 2026-10-20 on the owner's originals (docs/editions.md section 1): disk
`BIING/BIING!/BIPRO.EXE` (= `VERSION14/`), the German and English CD images' `BIPRO.EXE`, and the zip's
`VERSION10/BIPRO.EXE` (only identified, to name the fix; nothing of version 1.0 is examined).
"""
from __future__ import annotations

import argparse
import contextlib
import hashlib
import io
import os
import pathlib
import shutil
import stat
import subprocess
import sys
import secrets
import time
import zipfile
from dataclasses import dataclass

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))  # python -I omits the script dir
import biing_cdrom  # noqa: E402


@dataclass(frozen=True)
class Edition:
    key: str
    name: str  # as in MANUAL.md's table of editions
    size: int
    sha256: str
    texts: str = "BITEXPZ"
    records: str = "BIDATP"
    fix: str = ""  # set: not supported, and this is what to do


EDITIONS = (
    Edition("disk", "German disk version 1.4", 476874,
            "f51934e75f9a903a32b41d8aad9a181e85f4ee97b142723b8ccdd69584d53718"),
    Edition("gog-de", "GOG, German", 479018,
            "53584c169d9071c7b70e2d5a226f526426a5d08a76c34b5d518a4b003f6a024e"),
    Edition("gog-en", "GOG, English", 476027,
            "1c8606a0d16280c8f6c70a519f7897837d1030dabb50ae48b48f92560ad644e9", "BITEXPE", "BIDATPE"),
    Edition("disk-1.0", "German disk version 1.0", 476020,
            "369858c2c788cc576a75b7cc8e060132ce5f8f9b742c095562dcaafe3cd098b7",
            fix="copy the files of the zip's VERSION14 folder over it (what PlayV14.bat does), or give the zip itself"),
)
SUPPORTED = "German disk version 1.4, GOG German, GOG English"
NO_GAME = ("holds no Biing! game: give the disk version's zip, the GOG installer (.pkg), the GOG app or a folder "
           "with BIPRO.EXE and DATA")
SKIP_DIRS = {"DATA", "VPX", "GAMES", "CLASSICS", "__MACOSX", ".GIT"}
CD_AUDIO = "CDAUDIO"  # the game folder's CD audio tracks (TRACKnn.WAV), read by the engine (engine/src/cdaudio.h)
MAX_DEPTH = 8
MAX_DIRS = 5000  # the disk game has 37 folders, the unpacked GOG installer about 60; a home folder has thousands
MB = 1 << 20


class SetupError(Exception):
    """One line for the user: what is wrong, then the fix."""


# ---------------------------------------------------------------- small helpers

def entries(folder: pathlib.Path) -> dict[str, pathlib.Path]:
    """Upper-case name -> path of the folder's entries (DOS names: compare without case)."""
    try:
        return {p.name.upper(): p for p in folder.iterdir()}
    except OSError:
        return {}


def child(folder: pathlib.Path, name: str) -> pathlib.Path | None:
    return entries(folder).get(name.upper())


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def tree_size(folder: pathlib.Path) -> int:
    total = 0
    for root, _, files in os.walk(folder):
        for n in files:
            with contextlib.suppress(OSError):
                total += os.lstat(os.path.join(root, n)).st_size
    return total


def identify(exe: pathlib.Path, editions=EDITIONS, shown: str = "") -> Edition:
    """The edition of a BIPRO.EXE; `shown` names it in messages (where it is in the source, not in the temp folder)."""
    shown = shown or str(exe)
    size = exe.stat().st_size
    digest = sha256(exe)
    for e in editions:
        if e.size == size and e.sha256 == digest:
            return e
    same = [e for e in editions if e.size == size]
    if same:
        raise SetupError(f"{shown} is damaged or changed: it has the size of the {same[0].name} program but other "
                         f"contents; prepare the folder again from your original files")
    raise SetupError(f"{shown} ({size:,} bytes) is not the program of a supported edition ({SUPPORTED}): prepare the "
                     f"folder from your original files")


# ---------------------------------------------------------------- finding the game in a source

@dataclass
class Found:
    gog: list  # folders holding game.ins
    games: list  # folders holding BIPRO.EXE and DATA
    v14: list  # folders named VERSION14 holding BIPRO.EXE


def find(root: pathlib.Path, max_dirs: int = MAX_DIRS) -> Found:
    found = Found([], [], [])
    base = len(root.parts)
    for count, (here, dirs, files) in enumerate(os.walk(root)):
        p = pathlib.Path(here)
        if count >= max_dirs:
            raise SetupError(f"{root} holds too many folders to search for the game: give the zip, the .pkg, the "
                             f"app or the game folder itself")
        names = {n.upper() for n in files}
        dnames = {n.upper() for n in dirs}
        if "GAME.INS" in names:
            found.gog.append(p)
        if "BIPRO.EXE" in names and "DATA" in dnames:
            found.games.append(p)
        if "BIPRO.EXE" in names and p.name.upper() == "VERSION14":
            found.v14.append(p)
        if len(p.parts) - base >= MAX_DEPTH:
            dirs[:] = []
        else:
            dirs[:] = sorted(d for d in dirs if d.upper() not in SKIP_DIRS)
    return found


def check_space(folder: pathlib.Path, need: int, free_space) -> None:
    free = free_space(folder)
    if free < need:
        raise SetupError(f"not enough free disk space next to the destination: about {need // MB:,} MB needed, "
                         f"{free // MB:,} MB free; free some space or choose a destination on another disk")


def disk_free(folder: pathlib.Path) -> int:
    return shutil.disk_usage(folder).free


# ---------------------------------------------------------------- unpacking

def unpack_zip(src: pathlib.Path, tmp: pathlib.Path, free_space) -> pathlib.Path:
    try:
        with zipfile.ZipFile(src) as z:
            check_space(tmp, sum(i.file_size for i in z.infolist()) + 16 * MB, free_space)
            out = tmp / "zip"
            z.extractall(out)  # extractall drops absolute paths and `..` from member names
    except (zipfile.BadZipFile, EOFError) as e:
        raise SetupError(f"{src} cannot be unpacked ({e}): download it again from your original source")
    return out


def unpack_pkg(src: pathlib.Path, tmp: pathlib.Path, free_space) -> pathlib.Path:
    check_space(tmp, 3 * src.stat().st_size, free_space)
    out = tmp / "pkg"
    try:
        p = subprocess.run(["pkgutil", "--expand-full", str(src), str(out)], capture_output=True, text=True,
                           errors="replace", stdin=subprocess.DEVNULL)
    except FileNotFoundError:
        raise SetupError(f"{src} cannot be unpacked (no pkgutil: an installer .pkg unpacks only on macOS): "
                         f"give the installed GOG game folder instead")
    if p.returncode != 0:
        why = " ".join((p.stderr or p.stdout).split())[:200] or f"pkgutil exit {p.returncode}"
        raise SetupError(f"{src} cannot be unpacked ({why}): download it again from your original source")
    return out


def extract_cd(gog: pathlib.Path, tmp: pathlib.Path, free_space) -> pathlib.Path:
    """The CD image's ISO tree, with GOG's BIING! folder merged over it."""
    cue = child(gog, "game.ins")
    log = io.StringIO()
    try:
        img = biing_cdrom.Image(cue)
        try:
            check_space(tmp, img.bin_path.stat().st_size + 64 * MB, free_space)
            with contextlib.redirect_stdout(log):
                rc = biing_cdrom.cmd_extract(img, biing_cdrom.Iso(img.user), tmp / "cd", "sample")
        finally:
            img.f.close()
    except (OSError, ValueError) as e:
        rc, msg = 1, str(e)
    else:
        msg = next((l[7:] for l in log.getvalue().splitlines() if l.startswith("[FAIL] ")), "")
    if rc != 0:
        raise SetupError(f"{cue}: the CD image cannot be read ({msg}): the GOG files are incomplete or damaged; "
                         f"reinstall the app or download the installer again")
    tree = tmp / "cd" / "iso"
    overlay = child(gog, "BIING!")
    if overlay is not None and overlay.is_dir():
        shutil.copytree(overlay, tree, dirs_exist_ok=True, copy_function=shutil.copyfile)
    audio = tmp / "cd" / "audio"
    tracks = sorted(audio.glob("track*.wav")) if audio.is_dir() else []
    if tracks:  # the CD audio tracks, for the CD music (moved, not copied: they were written for this run)
        (tree / CD_AUDIO).mkdir(exist_ok=True)
        for t in tracks:
            os.rename(t, tree / CD_AUDIO / t.name.upper())
    return tree


# ---------------------------------------------------------------- the folder

def make_writable(folder: pathlib.Path) -> None:
    """Folders only: copytree copies a read-only source folder's mode (an installed app, a CD copy), while files
    are always new (copyfile, zipfile and the CD extract write them with the umask's mode). A chmod of the files
    was tried and removed: no source kind can make one read-only, so deleting it reddened no test."""
    for root, dirs, _ in os.walk(folder):
        for n in dirs:
            p = os.path.join(root, n)
            if not os.path.islink(p):
                os.chmod(p, os.stat(p).st_mode | stat.S_IRWXU)
    os.chmod(folder, os.stat(folder).st_mode | stat.S_IRWXU)


def check_folder(folder: pathlib.Path, ed: Edition, src: pathlib.Path) -> None:
    have = entries(folder)
    missing = [n for n in ("BIPRO.EXE", ed.texts, ed.records) if n not in have or not have[n].is_file()]
    data = have.get("DATA")
    if data is None or not data.is_dir() or not any(p.is_dir() for p in data.iterdir()):
        missing.append("DATA")
    if missing:
        raise SetupError(f"the game in {src} is incomplete ({', '.join(missing)} missing): prepare the folder "
                         f"from your original files")
    games = have.get("GAMES")
    probe = games / ".setup_game_probe"
    try:
        probe.write_bytes(b"x")
        probe.unlink()
    except OSError as e:
        raise SetupError(f"GAMES cannot be written ({e.strerror}): choose a destination you can write to")


def prepare(src: pathlib.Path, tmp: pathlib.Path, editions, free_space) -> tuple[pathlib.Path, Edition, str]:
    """Builds the game folder inside tmp; returns it, its edition and a note for the [OK] line."""
    root, unpacked = src, False
    if src.is_file():
        name = src.name.upper()
        if name.endswith(".PKG"):
            root, unpacked = unpack_pkg(src, tmp, free_space), True
        elif name.endswith(".ZIP") or zipfile.is_zipfile(src):
            root, unpacked = unpack_zip(src, tmp, free_space), True
        elif name in ("GAME.INS", "BIPRO.EXE"):
            root = src.parent
        else:
            raise SetupError(f"{src} {NO_GAME}")
    found = find(root)
    if len(found.gog) + len(found.games) > 1:
        both = [str(p) for p in found.gog + found.games]
        raise SetupError(f"{src} holds more than one game ({'; '.join(both[:3])}): give one of these folders")
    if found.gog:
        tree = extract_cd(found.gog[0], tmp, free_space)
        shown = f"BIPRO.EXE on the CD image of {found.gog[0]}"
        if child(tree, "BIPRO.EXE") is None:
            raise SetupError(f"{found.gog[0]}: the CD image holds no BIPRO.EXE: reinstall the app or download the "
                             f"installer again")
    elif found.games:
        game = found.games[0]
        rel = game.relative_to(root) / child(game, "BIPRO.EXE").name
        # a path inside the zip or .pkg keeps its archive form (forward slashes) on Windows too
        shown = f"{src} ({rel.as_posix()})" if unpacked else str(game / rel.name)
        if unpacked:  # unpacked into tmp by this run: move instead of copying
            tree = game
        else:
            check_space(tmp, tree_size(game) + 16 * MB, free_space)
            tree = tmp / "copy"
            shutil.copytree(game, tree, symlinks=False, copy_function=shutil.copyfile)
    else:
        if src.is_dir() and child(src, "SOUND.CFG") and child(src.parent, "game.ins"):
            raise SetupError(f"{src} is only GOG's BIING! folder (no program, no CD image): give the folder above "
                             f"it ({src.parent}) or the GOG app")
        raise SetupError(f"{src} {NO_GAME}")
    make_writable(tree)
    ed = identify(child(tree, "BIPRO.EXE"), editions, shown)
    note = ""
    if ed.fix:
        v14 = [v for v in found.v14 if supported(child(v, "BIPRO.EXE"), editions)]
        if len(v14) != 1:
            raise SetupError(f"{shown} is the {ed.name}, which the engine does not support: "
                             f"{ed.fix}")
        shutil.copytree(v14[0], tree, dirs_exist_ok=True, copy_function=shutil.copyfile)
        note = f", the {ed.name} programs replaced by those of {v14[0].name}"
        ed = identify(child(tree, "BIPRO.EXE"), editions)
    games = child(tree, "GAMES")
    if games is None:
        (tree / "GAMES").mkdir()
    make_writable(tree)
    check_folder(tree, ed, src)
    return tree, ed, note


def supported(exe: pathlib.Path | None, editions) -> bool:
    """True for a program of a supported edition (the VERSION14 folder that puts 1.4 back)."""
    try:
        return exe is not None and not identify(exe, editions).fix
    except SetupError:
        return False


def inside(dest: pathlib.Path, src: pathlib.Path) -> bool:
    """True when dest is src or lies below it. Compared by file identity, not by name: Windows and macOS disks
    ignore case, so C:\\Games\\biing\\x lies inside C:\\games\\BIING although no name matches. The resolved chain
    catches a destination below a link into the source."""
    real = dest.resolve()
    for p in (dest, *dest.parents, real, *real.parents):
        with contextlib.suppress(OSError):
            if p.exists() and os.path.samefile(p, src):
                return True
    return False


def make_temp(parent: pathlib.Path) -> pathlib.Path:
    """The hidden work folder next to the destination, made with the default mode. Not tempfile.mkdtemp: its mode
    0o700 becomes, on Windows since Python 3.13, an ACL of SYSTEM, Administrators and the owner only, which every
    folder inside inherits, so the finished game folder kept it; made from an elevated terminal (owner:
    Administrators) the engine started normally was refused ("Access is denied", Windows 2026-10-21)."""
    for _ in range(100):
        p = parent / f".setup_game-{secrets.token_hex(6)}"
        try:
            p.mkdir()
            return p
        except FileExistsError:
            continue
    raise SetupError(f"no work folder can be made in {parent}: choose another destination")


def rename(old: pathlib.Path, new: pathlib.Path, tries: int = 20, pause: float = 0.25) -> None:
    """os.rename, retried on Windows for a few seconds: a virus scanner reads the freshly written files and holds
    them open, and Windows refuses to move a folder with an open file in it (WinError 5, access denied)."""
    for i in range(tries):
        try:
            os.rename(old, new)
            return
        except PermissionError:
            if os.name != "nt" or i == tries - 1:
                raise
            time.sleep(pause)


def setup(src: pathlib.Path, dest: pathlib.Path, editions=EDITIONS, free_space=disk_free) -> str:
    """Returns the [OK] line; raises SetupError with the [FAIL] text."""
    if not str(src).strip():  # "$GAME_SOURCE" in a new terminal window, where the variable is not set
        raise SetupError("no source given: set GAME_SOURCE again as shown in the manual's section 4 (a new terminal "
                         "window forgets it), then run the command again")
    src = pathlib.Path(os.path.abspath(os.path.expanduser(str(src))))
    dest = pathlib.Path(os.path.abspath(os.path.expanduser(str(dest))))
    if not src.exists():
        raise SetupError(f"{src} does not exist: check the path (you can drag the file into the terminal window "
                         f"to type it)")
    if dest.exists() or dest.is_symlink():
        if not dest.is_dir() or dest.is_symlink() or any(dest.iterdir()):
            raise SetupError(f"{dest} already exists and is not empty: give a new folder name, or delete that "
                             f"folder first if it holds nothing you want to keep (saved games are in its GAMES)")
    if inside(dest, src):
        raise SetupError(f"{dest} is inside the source {src}: choose a destination outside it")
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = make_temp(dest.parent)
    try:
        tree, ed, note = prepare(src, tmp, editions, free_space)
        if dest.exists():
            dest.rmdir()
        rename(tree, dest)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    size = (dest / child(dest, "BIPRO.EXE").name).stat().st_size
    return f"[OK] {ed.name} (BIPRO.EXE {size:,} bytes){note} -> {dest}"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("source", help="the disk zip, a GOG .pkg, the GOG app, or a game folder")
    ap.add_argument("dest", help="the game folder to make (must not exist, or be empty)")
    a = ap.parse_args(argv)
    try:
        print(setup(pathlib.Path(a.source) if a.source else "", pathlib.Path(a.dest)))
        return 0
    except SetupError as e:
        print(f"[FAIL] {e}")
    except OSError as e:
        print(f"[FAIL] {e.filename or ''}: {e.strerror or e}: check the paths and the free disk space, then run "
              f"again")
    return 1


if __name__ == "__main__":
    sys.exit(main())
