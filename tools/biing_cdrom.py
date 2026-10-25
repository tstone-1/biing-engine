"""Read the Biing! GOG CD image (game.gog + cue sheet game.ins): ISO 9660 data track and CD audio tracks.

Usage:
  python3 -I tools/biing_cdrom.py info    <game.ins>
  python3 -I tools/biing_cdrom.py list    <game.ins>
  python3 -I tools/biing_cdrom.py extract <game.ins> <out-dir> [--edc all|sample|none]
      writes <out-dir>/iso/<tree> and <out-dir>/audio/track02.wav ...
  python3 -I tools/biing_cdrom.py compare <dir-a> <dir-b>
      sha256 comparison of two file trees (names matched case-insensitively)
  uv run --no-project --with pillow python -I tools/biing_cdrom.py vpx <VSCENxnn.VPX> <out-dir> [--every N]
      decodes a VPX1 video: every Nth frame as PNG, the whole soundtrack as WAV
  uv run --no-project --with pypdf --with fonttools python -I tools/biing_cdrom.py manual <manual.pdf> <out.tsv>
      the manual as printed page / line / text, counted the way the password prompt counts (aid, see docs)

Format description: docs/cdrom.md. Raw sectors are 2,352 bytes: MODE1 = 12 sync + 4 header (BCD minute,
second, frame of the absolute address, mode byte) + 2,048 user data + 4 EDC + 8 zero + 276 ECC.
Audio sectors are 2,352 bytes of 16-bit little-endian stereo PCM at 44,100 Hz (588 sample frames).
Runs on macOS's own python3 (3.9): annotations are not evaluated (`from __future__ import annotations`).
A missing or unreadable cue sheet or image ends with one `[FAIL]` line, not a traceback.
"""
from __future__ import annotations

import argparse
import hashlib
import pathlib
import re
import struct
import sys
from dataclasses import dataclass

RAW = 2352
USER = 2048
SYNC = b"\x00" + b"\xff" * 10 + b"\x00"
FRAMES_PER_SEC = 75
LEAD_IN = 150  # absolute address of LBA 0 is 00:02:00


@dataclass
class Track:
    number: int
    mode: str  # "MODE1/2352" or "AUDIO"
    start: int  # LBA of INDEX 01
    end: int = 0  # exclusive LBA, filled in by parse_cue


def msf_to_lba(msf: str) -> int:
    m, s, f = (int(x) for x in msf.split(":"))
    if s >= 60 or f >= FRAMES_PER_SEC:
        raise ValueError(f"bad MSF {msf}")
    return (m * 60 + s) * FRAMES_PER_SEC + f


def parse_cue(text: str, total_sectors: int) -> tuple[str, list[Track]]:
    """One FILE, TRACK lines in either 'TRACK 01' or 'TRACK 1' form, INDEX 01 per track (INDEX 00 ignored)."""
    fname, tracks, cur = None, [], None
    for line in text.splitlines():
        words = line.split()
        if not words:
            continue
        kw = words[0].upper()
        if kw == "FILE":
            if fname is not None:
                raise ValueError("more than one FILE entry is not supported")
            m = re.match(r'\s*FILE\s+"([^"]+)"', line, re.I)
            fname = m.group(1) if m else words[1]
        elif kw == "TRACK":
            cur = Track(int(words[1]), words[2].upper(), -1)
            tracks.append(cur)
        elif kw == "INDEX" and cur is not None and int(words[1]) == 1:
            cur.start = msf_to_lba(words[2])
    if fname is None or not tracks or any(t.start < 0 for t in tracks):
        raise ValueError("cue sheet lacks FILE, TRACK or INDEX 01")
    for a, b in zip(tracks, tracks[1:]):
        if b.start <= a.start:
            raise ValueError(f"track {b.number} does not start after track {a.number}")
        a.end = b.start
    tracks[-1].end = total_sectors
    if tracks[-1].end <= tracks[-1].start:
        raise ValueError("last track starts past the end of the image")
    return fname, tracks


# --- MODE1 sector checks -------------------------------------------------------------------------------

def _edc_table() -> list[int]:
    table = []
    for i in range(256):
        e = i
        for _ in range(8):
            e = (e >> 1) ^ (0xD8018001 if e & 1 else 0)
        table.append(e)
    return table


EDC_TABLE = _edc_table()


def edc(data: bytes) -> int:
    """CD-ROM EDC: reflected CRC-32 with polynomial 0x8001801B (table form 0xD8018001), init 0, no xor-out."""
    e = 0
    t = EDC_TABLE
    for b in data:
        e = (e >> 8) ^ t[(e ^ b) & 0xFF]
    return e


def bcd(v: int) -> int:
    return (v // 10) * 16 + v % 10


def expected_header(lba: int, mode: int = 1) -> bytes:
    a = lba + LEAD_IN
    return bytes((bcd(a // (60 * 75)), bcd(a // 75 % 60), bcd(a % 75), mode))


def check_mode1(raw: bytes, lba: int, check_edc: bool) -> str | None:
    """None if the raw sector is a valid MODE1 sector for this LBA, else a reason."""
    if raw[:12] != SYNC:
        return "sync pattern"
    if raw[12:16] != expected_header(lba):
        return f"header {raw[12:16].hex()} != {expected_header(lba).hex()}"
    if check_edc and edc(raw[:2064]) != struct.unpack_from("<I", raw, 2064)[0]:
        return "EDC"
    return None


class Image:
    def __init__(self, cue_path: pathlib.Path):
        self.cue_path = cue_path
        text = cue_path.read_text(encoding="latin-1")
        m = re.search(r'FILE\s+"([^"]+)"', text, re.I)
        if not m:
            raise ValueError("no FILE in cue sheet")
        self.bin_path = cue_path.parent / m.group(1)
        size = self.bin_path.stat().st_size
        if size % RAW:
            raise ValueError(f"image size {size} is not a multiple of {RAW}")
        self.total = size // RAW
        _, self.tracks = parse_cue(text, self.total)
        self.f = self.bin_path.open("rb")
        data = self.tracks[0]
        if data.mode != "MODE1/2352" or data.start != 0:
            raise ValueError("track 1 is not MODE1/2352 at LBA 0")
        self.data_track = data

    def raw(self, lba: int, count: int = 1) -> bytes:
        self.f.seek(lba * RAW)
        return self.f.read(count * RAW)

    def user(self, lba: int, count: int = 1) -> bytes:
        """2,048-byte user data of `count` MODE1 sectors; checks sync, header and mode of every sector."""
        if lba + count > self.data_track.end:
            raise ValueError(f"sectors {lba}+{count} run past the data track")
        raw = self.raw(lba, count)
        out = bytearray()
        for i in range(count):
            s = raw[i * RAW : (i + 1) * RAW]
            why = check_mode1(s, lba + i, False)
            if why:
                raise ValueError(f"LBA {lba + i}: bad {why}")
            out += s[16 : 16 + USER]
        return bytes(out)


# --- ISO 9660 -----------------------------------------------------------------------------------------

@dataclass
class Entry:
    path: str  # '/' separated, version suffix ';1' removed
    lba: int
    size: int
    is_dir: bool


def both_endian(b: bytes, off: int, width: int) -> int:
    """ISO 9660 both-byte-order field: little-endian then big-endian copy; they must agree."""
    fmt = {4: ("<I", ">I"), 2: ("<H", ">H")}[width]
    le = struct.unpack_from(fmt[0], b, off)[0]
    be = struct.unpack_from(fmt[1], b, off + width)[0]
    if le != be:
        raise ValueError(f"both-endian field at {off}: {le} != {be}")
    return le


def parse_pvd(sector: bytes) -> dict:
    if sector[0] != 1 or sector[1:6] != b"CD001" or sector[6] != 1:
        raise ValueError("sector 16 is not an ISO 9660 primary volume descriptor")
    return {
        "system_id": sector[8:40].decode("ascii", "replace").rstrip(),
        "volume_id": sector[40:72].decode("ascii", "replace").rstrip(),
        "volume_space": both_endian(sector, 80, 4),
        "block_size": both_endian(sector, 128, 2),
        "path_table_size": both_endian(sector, 132, 4),
        "path_table_l": struct.unpack_from("<I", sector, 140)[0],
        "path_table_m": struct.unpack_from(">I", sector, 148)[0],
        "root": sector[156:190],
        "volume_set_id": sector[190:318].decode("ascii", "replace").rstrip(),
        "publisher": sector[318:446].decode("ascii", "replace").rstrip(),
        "preparer": sector[446:574].decode("ascii", "replace").rstrip(),
        "application": sector[574:702].decode("ascii", "replace").rstrip(),
        "created": sector[813:829].decode("ascii", "replace"),
        "modified": sector[830:846].decode("ascii", "replace"),
    }


def parse_dir_records(data: bytes) -> list[tuple[str, int, int, int]]:
    """(name, lba, size, flags) of every record; records never cross a 2,048-byte sector boundary."""
    out, pos = [], 0
    while pos < len(data):
        n = data[pos]
        if n == 0:  # rest of this sector is padding
            pos = (pos // USER + 1) * USER
            continue
        rec = data[pos : pos + n]
        if n < 34 or len(rec) < n:
            raise ValueError(f"truncated directory record at {pos}")
        lba = both_endian(rec, 2, 4)
        size = both_endian(rec, 10, 4)
        flags = rec[25]
        if rec[26] or rec[27]:
            raise ValueError("interleaved files are not supported")
        name_len = rec[32]
        raw_name = rec[33 : 33 + name_len]
        name = {b"\x00": ".", b"\x01": ".."}.get(raw_name) or raw_name.decode("latin-1")
        out.append((name, lba, size, flags))
        pos += n
    return out


def parse_path_table(data: bytes, big_endian: bool) -> list[tuple[int, int, str]]:
    """(directory LBA, parent number, name) per path table record; record 1 is the root."""
    order = ">" if big_endian else "<"
    out, pos = [], 0
    while pos < len(data):
        name_len = data[pos]
        if name_len == 0:
            raise ValueError(f"zero-length path table name at {pos}")
        lba = struct.unpack_from(order + "I", data, pos + 2)[0]
        parent = struct.unpack_from(order + "H", data, pos + 6)[0]
        name = data[pos + 8 : pos + 8 + name_len].decode("latin-1")
        out.append((lba, parent, "" if name == "\x00" else name))
        pos += 8 + name_len + (name_len & 1)
    return out


class Iso:
    def __init__(self, read_user):
        """read_user(lba, count) -> bytes of 2,048-byte sectors."""
        self.read = read_user
        self.pvd = parse_pvd(read_user(16, 1))
        if self.pvd["block_size"] != USER:
            raise ValueError("logical block size is not 2048")

    def walk(self) -> list[Entry]:
        root = parse_dir_records(self.pvd["root"])[0]
        entries: list[Entry] = []
        seen: set[int] = set()

        def visit(lba: int, size: int, path: str):
            if lba in seen:
                raise ValueError(f"directory loop at LBA {lba}")
            seen.add(lba)
            records = parse_dir_records(self.read(lba, (size + USER - 1) // USER))
            if records[0][0] != "." or records[0][1] != lba:
                raise ValueError(f"{path or '/'}: first record is not '.' pointing to itself")
            for name, rlba, rsize, flags in records[2:]:
                is_dir = bool(flags & 2)
                if flags & 0x80:
                    raise ValueError(f"{name}: multi-extent files are not supported")
                clean = name.split(";")[0]
                if not is_dir and clean.endswith("."):
                    clean = clean[:-1]
                # ISO entries are single path components on every host platform.
                if clean in {"", ".", ".."} or any(c in clean for c in "/\\:\0"):
                    raise ValueError(f"unsafe ISO name: {name!r}")
                full = f"{path}/{clean}"
                entries.append(Entry(full.lstrip("/"), rlba, rsize, is_dir))
                if is_dir:
                    visit(rlba, rsize, full)

        visit(root[1], root[2], "")
        return entries

    def check_path_tables(self, entries: list[Entry]) -> int:
        """Both path tables must list exactly the directories the tree walk found. Returns the count."""
        size = self.pvd["path_table_size"]
        n = (size + USER - 1) // USER
        tables = [
            parse_path_table(self.read(self.pvd["path_table_l"], n)[:size], False),
            parse_path_table(self.read(self.pvd["path_table_m"], n)[:size], True),
        ]
        if tables[0] != tables[1]:
            raise ValueError("L and M path tables differ")
        table = tables[0]
        # rebuild full paths from parent numbers
        paths = []
        for lba, parent, name in table:
            paths.append(name if parent == 1 or not paths else f"{paths[parent - 1]}/{name}")
        paths[0] = ""
        from_table = {(p.lstrip("/"), lba) for p, (lba, _, _) in zip(paths, table)}
        root_lba = parse_dir_records(self.pvd["root"])[0][1]
        from_walk = {(e.path, e.lba) for e in entries if e.is_dir} | {("", root_lba)}
        if from_table != from_walk:
            raise ValueError(f"path table and directory tree disagree: {sorted(from_table ^ from_walk)[:5]}")
        return len(table)


# --- audio --------------------------------------------------------------------------------------------

def wav_header(n_bytes: int) -> bytes:
    """RIFF/WAVE, PCM, 2 channels, 44,100 Hz, 16 bit; CD audio is already little-endian, as WAV wants."""
    return (b"RIFF" + struct.pack("<I", 36 + n_bytes) + b"WAVEfmt "
            + struct.pack("<IHHIIHH", 16, 1, 2, 44100, 44100 * 4, 4, 16)
            + b"data" + struct.pack("<I", n_bytes))


def write_wav(img: Image, track: Track, dst: pathlib.Path) -> int:
    n = track.end - track.start
    with dst.open("wb") as out:
        out.write(wav_header(n * RAW))
        img.f.seek(track.start * RAW)
        left = n * RAW
        while left:
            chunk = img.f.read(min(left, RAW * 1024))
            if not chunk:
                raise ValueError(f"track {track.number}: image ends early")
            out.write(chunk)
            left -= len(chunk)
    if dst.stat().st_size != 44 + n * RAW:
        raise ValueError(f"{dst}: size mismatch")
    return n


def fmt_len(sectors: int) -> str:
    sec = sectors / FRAMES_PER_SEC
    return f"{int(sec // 60)}:{sec % 60:05.2f}"


# --- VPX1 video (VPX/VSCEN[DQ]nn.VPX) ------------------------------------------------------------------

VPX_MAGIC = b"VPX1"
VPX_BANNER = 0x78  # text banner, ends in "HEAD88----"
BLOCK = 8
CODE_BYTES = 600  # 40 x 30 blocks, one nibble each
VPX_COST = {0: 0, 1: 16, 2: 32, 3: 64, 4: 1}  # data bytes per block, by block code
VPX_START_COLOUR = 2  # the player fills the video buffer with colour 2 at each scene start (docs/intro.md)
# Checkerboard phases (row, column) of the four 16-byte groups, by frame parity (docs/cdrom.md, docs/intro.md).
VPX_PHASES = {0: ((0, 0), (1, 1), (0, 1), (1, 0)), 1: ((0, 1), (1, 0), (0, 0), (1, 1))}


@dataclass
class Vpx:
    width: int
    height: int
    fps: int
    rate: int
    palette: bytes  # 768 B, 8-bit RGB
    chunks: list  # (tag, payload) in file order: b"SOUN", b"CODE", b"VIDE"


def parse_vpx(d: bytes) -> Vpx:
    """Banner, 4 LE u16 (width, height, fps, sample rate), 'COLS' + 768 B palette, then chunks:
    'SOUN' u32 size + unsigned 8-bit mono PCM; 'CODE' + 600 B block codes (no size); 'VIDE' u32 size + data."""
    if d[:4] != VPX_MAGIC:
        raise ValueError("not a VPX1 file")
    w, h, fps, rate = struct.unpack_from("<4H", d, VPX_BANNER)
    p = VPX_BANNER + 8
    if d[p : p + 4] != b"COLS":
        raise ValueError("no COLS palette after the header")
    pal = d[p + 4 : p + 772]
    p += 772
    if (w // BLOCK) * (h // BLOCK) != CODE_BYTES * 2:
        raise ValueError(f"{w}x{h} does not match a {CODE_BYTES}-byte block code map")
    chunks = []
    while p < len(d):
        tag = d[p : p + 4]
        if tag == b"CODE":
            if p + 4 + CODE_BYTES > len(d):
                raise ValueError("truncated CODE chunk")
            chunks.append((tag, d[p + 4 : p + 4 + CODE_BYTES]))
            p += 4 + CODE_BYTES
        elif tag in (b"VIDE", b"SOUN"):
            (n,) = struct.unpack_from("<I", d, p + 4)
            if p + 8 + n > len(d):
                raise ValueError(f"truncated {tag.decode()} chunk at {p:#x}")
            chunks.append((tag, d[p + 8 : p + 8 + n]))
            p += 8 + n
        else:
            raise ValueError(f"unknown chunk {tag!r} at {p:#x}")
    return Vpx(w, h, fps, rate, pal, chunks)


def apply_frame(fb: bytearray, width: int, codes: bytes, data: bytes, parity: int = 0) -> None:
    """Update the frame buffer in place. One nibble per 8x8 block, high nibble first, blocks row-major.
    A group is 16 bytes, 4 rows of 4, set on every other row and column of the block at one phase
    (VPX_PHASES by frame parity). Code 0 keep; 1 one group on both phases of the frame's pair (16 B);
    2 two groups on that pair (32 B); 3 four groups on all four phases (64 B); 4 one colour on the pair (1 B)."""
    cols = width // BLOCK
    phases = VPX_PHASES[parity & 1]
    p = 0
    for i in range(len(codes) * 2):
        b = codes[i >> 1]
        c = b >> 4 if i % 2 == 0 else b & 15
        if c == 0:
            continue
        if c not in VPX_COST:
            raise ValueError(f"unknown block code {c}")
        n = VPX_COST[c]
        src = data[p : p + n]
        if len(src) != n:
            raise ValueError("VIDE data ends inside a block")
        p += n
        if c == 4:
            groups = [src * 16] * 2
        else:
            groups = [src[k * 16 : (k + 1) * 16] for k in range(n // 16)]
            if c == 1:
                groups = groups * 2
        x0, y0 = (i % cols) * BLOCK, (i // cols) * BLOCK
        for g, (pr, pc) in zip(groups, phases):
            for yy in range(4):
                o = (y0 + 2 * yy + pr) * width + x0 + pc
                fb[o : o + 8 : 2] = g[yy * 4 : yy * 4 + 4]
    if p != len(data):
        raise ValueError(f"{len(data) - p} VIDE bytes left after the last block")


def vpx_frames(v: Vpx):
    """Yield the frame buffer after each CODE+VIDE pair."""
    fb = bytearray([VPX_START_COLOUR]) * (v.width * v.height)
    codes = None
    parity = 0
    for tag, payload in v.chunks:
        if tag == b"CODE":
            codes = payload
        elif tag == b"VIDE":
            if codes is None:
                raise ValueError("VIDE before CODE")
            apply_frame(fb, v.width, codes, payload, parity)
            parity ^= 1
            codes = None
            yield fb


def wav_u8_mono(pcm: bytes, rate: int) -> bytes:
    return (b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt "
            + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate, 1, 8) + b"data" + struct.pack("<I", len(pcm)) + pcm)


def cmd_vpx(src: pathlib.Path, out: pathlib.Path, every: int) -> int:
    from PIL import Image

    v = parse_vpx(src.read_bytes())
    out.mkdir(parents=True, exist_ok=True)
    pcm = b"".join(p for t, p in v.chunks if t == b"SOUN")
    (out / f"{src.stem}.wav").write_bytes(wav_u8_mono(pcm, v.rate))
    n = 0
    for n, fb in enumerate(vpx_frames(v), 1):
        if every and (n - 1) % every == 0:
            im = Image.frombytes("P", (v.width, v.height), bytes(fb))
            im.putpalette(v.palette)
            im.save(out / f"{src.stem}_{n:05d}.png")
    print(f"[OK] {src.name}: {v.width}x{v.height}, {v.fps} fps, {n} frames ({n / v.fps:.1f} s), "
          f"audio {len(pcm)} B u8 mono at {v.rate} Hz ({len(pcm) / v.rate:.1f} s) -> {out}")
    return 0


# --- manual (password lookup aid) ---------------------------------------------------------------------

SHADOW = 5  # the PDF draws headings and page footers five times (shadow effect); text extraction repeats them


def unshadow(line: str, k: int = SHADOW) -> str | None:
    """'PPPPPasswortabfrageasswortabfrage...' -> 'Passwortabfrage': the line split into segments that each occur k
    times in a row. None if the line does not decompose that way (an ordinary text line)."""
    out, i = [], 0
    while i < len(line):
        for n in range(1, (len(line) - i) // k + 1):
            seg = line[i : i + n]
            if line.startswith(seg * k, i):
                out.append(seg)
                i += n * k
                break
        else:
            return None
    return "".join(out)


FOOTER = re.compile(r"^- (\d+) -$")


def manual_lines(page_texts: list[str]) -> list[tuple[int, int, str]]:
    """(printed page, line number from 1, text) using the manual's counting rule: headings and text lines count
    one each, empty lines do not. Text inside pictures cannot be told apart here and is counted."""
    out = []
    for text in page_texts:
        lines, printed = [], None
        for raw in text.split("\n"):
            s = raw.strip()
            if not s:
                continue
            u = unshadow(s)
            if u is not None:
                s = u
            m = FOOTER.match(s)
            if m:
                printed = int(m.group(1))
                continue
            lines.append(s)
        if printed is not None:
            out += [(printed, n, s) for n, s in enumerate(lines, 1)]
    return out


def cmd_manual(pdf: pathlib.Path, dst: pathlib.Path) -> int:
    import pypdf

    pages = [p.extract_text() or "" for p in pypdf.PdfReader(str(pdf)).pages]
    rows = manual_lines(pages)
    dst.write_text("page\tline\ttext\n" + "".join(f"{p}\t{n}\t{s}\n" for p, n, s in rows), encoding="utf-8")
    print(f"[OK] {len(rows)} lines on {len({r[0] for r in rows})} numbered pages -> {dst}")
    return 0


# --- commands -----------------------------------------------------------------------------------------

def scan_data_track(img: Image, edc_mode: str) -> tuple[int, int, list[str]]:
    """Check every sector of the data track (sync, header, EDC per mode); returns checked, edc-checked, errors."""
    errors, edc_done = [], 0
    t = img.data_track
    step = 256
    for base in range(t.start, t.end, step):
        n = min(step, t.end - base)
        raw = img.raw(base, n)
        for i in range(n):
            lba = base + i
            do_edc = edc_mode == "all" or (edc_mode == "sample" and lba % 97 == 0)
            s = raw[i * RAW : (i + 1) * RAW]
            if s[:12] != SYNC and not any(s):  # silent postgap sectors written as zeros
                errors.append(f"LBA {lba}: all-zero sector")
                continue
            why = check_mode1(s, lba, do_edc)
            edc_done += do_edc
            if why:
                errors.append(f"LBA {lba}: {why}")
    return t.end - t.start, edc_done, errors


def cmd_info(img: Image, iso: Iso) -> None:
    p = iso.pvd
    print(f"image {img.bin_path.name}: {img.total} sectors of {RAW} B")
    for k in ("system_id", "volume_id", "volume_set_id", "publisher", "preparer", "application",
              "created", "modified", "volume_space", "path_table_size"):
        print(f"  {k}: {p[k]}")
    for t in img.tracks:
        print(f"  track {t.number:02d} {t.mode:11s} LBA {t.start:6d}-{t.end - 1:6d}  {t.end - t.start:6d} sectors"
              f"  {fmt_len(t.end - t.start)}")


def extraction_path(root: pathlib.Path, relative: str) -> pathlib.Path:
    """Keep writes beneath the selected root, including pre-existing symlinks."""
    dst = root / relative
    try:
        dst.resolve().relative_to(root)
    except ValueError:
        raise ValueError(f"extraction path escapes output: {relative!r}") from None
    return dst


def cmd_extract(img: Image, iso: Iso, out: pathlib.Path, edc_mode: str) -> int:
    out = out.resolve()
    entries = iso.walk()
    dirs = iso.check_path_tables(entries)
    vs = iso.pvd["volume_space"]
    if vs > img.data_track.end:
        print(f"[FAIL] volume space {vs} exceeds data track ({img.data_track.end})")
        return 1
    checked, edc_n, errors = scan_data_track(img, edc_mode)
    zero_tail = [e for e in errors if e.endswith("all-zero sector")]
    hard = [e for e in errors if not e.endswith("all-zero sector")]
    for e in hard[:20]:
        print(f"[FAIL] {e}")
    if zero_tail:
        lbas = [int(e.split()[1].rstrip(":")) for e in zero_tail]
        print(f"[INFO] {len(lbas)} all-zero sectors in the data track, LBA {min(lbas)}-{max(lbas)}"
              f" (volume space ends at {vs})")
        if min(lbas) < vs:
            print("[FAIL] all-zero sectors inside the ISO volume")
            return 1
    print(f"[OK] {checked} data sectors checked (sync, header), {edc_n} EDC checks, {len(hard)} errors")
    print(f"[OK] path tables (L and M) match the {dirs} directories of the tree")
    iso_dir = extraction_path(out, "iso")
    files = 0
    for e in entries:
        dst = extraction_path(out, "iso/" + e.path)
        if e.is_dir:
            dst.mkdir(parents=True, exist_ok=True)
            continue
        if e.lba + (e.size + USER - 1) // USER > vs:
            print(f"[FAIL] {e.path} extends past the volume")
            return 1
        dst.parent.mkdir(parents=True, exist_ok=True)
        data = img.user(e.lba, (e.size + USER - 1) // USER)[: e.size] if e.size else b""
        dst.write_bytes(data)
        files += 1
    print(f"[OK] {files} files, {len(entries) - files} directories -> {iso_dir}")
    audio_dir = extraction_path(out, "audio")
    audio_dir.mkdir(parents=True, exist_ok=True)
    for t in img.tracks[1:]:
        if t.mode != "AUDIO":
            print(f"[FAIL] track {t.number} is {t.mode}")
            return 1
        n = write_wav(img, t, extraction_path(out, f"audio/track{t.number:02d}.wav"))
        print(f"[OK] track {t.number:02d}: {n} sectors = {fmt_len(n)} -> track{t.number:02d}.wav")
    return 1 if hard else 0


def tree_hashes(root: pathlib.Path) -> dict[str, tuple[str, str, int]]:
    """upper-case relative path -> (real relative path, sha256, size)."""
    out = {}
    for p in sorted(root.rglob("*")):
        if p.is_file():
            rel = p.relative_to(root).as_posix()
            data = p.read_bytes()
            out[rel.upper()] = (rel, hashlib.sha256(data).hexdigest(), len(data))
    return out


def cmd_compare(a: pathlib.Path, b: pathlib.Path) -> int:
    ha, hb = tree_hashes(a), tree_hashes(b)
    same = [k for k in ha if k in hb and ha[k][1] == hb[k][1]]
    diff = [k for k in ha if k in hb and ha[k][1] != hb[k][1]]
    only_a = [k for k in ha if k not in hb]
    only_b = [k for k in hb if k not in ha]
    print(f"A {a}: {len(ha)} files; B {b}: {len(hb)} files")
    print(f"identical {len(same)}, differ {len(diff)}, only in A {len(only_a)}, only in B {len(only_b)}")
    for label, keys, src in (("DIFF", diff, None), ("ONLY-A", only_a, ha), ("ONLY-B", only_b, hb)):
        for k in keys:
            if src is None:
                print(f"{label}\t{ha[k][0]}\t{ha[k][2]}\t{hb[k][2]}")
            else:
                print(f"{label}\t{src[k][0]}\t{src[k][2]}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("info", "list", "extract"):
        sp = sub.add_parser(name)
        sp.add_argument("cue", type=pathlib.Path)
        if name == "extract":
            sp.add_argument("out", type=pathlib.Path)
            sp.add_argument("--edc", choices=("all", "sample", "none"), default="sample")
    sp = sub.add_parser("compare")
    sp.add_argument("a", type=pathlib.Path)
    sp.add_argument("b", type=pathlib.Path)
    sp = sub.add_parser("vpx")
    sp.add_argument("src", type=pathlib.Path)
    sp.add_argument("out", type=pathlib.Path)
    sp.add_argument("--every", type=int, default=15, help="write every Nth frame as PNG (0: none)")
    sp = sub.add_parser("manual")
    sp.add_argument("pdf", type=pathlib.Path)
    sp.add_argument("out", type=pathlib.Path)
    args = ap.parse_args()
    try:
        return run(args)
    except FileNotFoundError as e:
        print(f"[FAIL] cannot open {e.filename}: no such file or folder")
    except (ValueError, OSError) as e:
        where = next((getattr(args, k) for k in ("cue", "src", "pdf", "a") if getattr(args, k, None)), "")
        print(f"[FAIL] {where}: {e}")
    return 1


def run(args: argparse.Namespace) -> int:
    if args.cmd == "manual":
        return cmd_manual(args.pdf, args.out)
    if args.cmd == "compare":
        return cmd_compare(args.a, args.b)
    if args.cmd == "vpx":
        return cmd_vpx(args.src, args.out, args.every)
    img = Image(args.cue)
    iso = Iso(img.user)
    if args.cmd == "info":
        cmd_info(img, iso)
        return 0
    if args.cmd == "list":
        for e in iso.walk():
            print(f"{'D' if e.is_dir else 'F'}\t{e.lba}\t{e.size}\t{e.path}")
        return 0
    return cmd_extract(img, iso, args.out, args.edc)


if __name__ == "__main__":
    sys.exit(main())
