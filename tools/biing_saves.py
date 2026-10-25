"""Read Biing!'s save games, high-score file and configuration files.

Usage:
  python3 -I tools/biing_saves.py save  <GAMES/BGAMEn> [<out-dir>]   list and verify the blocks;
                                                                    with out-dir, write each block plain
  python3 -I tools/biing_saves.py quack <GAMES/QUACK | BIPRO.EXE>    high-score table (from the file, or
                                                                    the built-in default in BIPRO.EXE)
  python3 -I tools/biing_saves.py cfg   <BIING!.CFG | SOUND.CFG>     settings as the game reads them
Format description: docs/saves.md. Exit code 0 = file verified, 1 = size/version/checksum mismatch.
"""
import functools
import pathlib
import struct
import sys

# ---------------------------------------------------------------- save game (GAMES\BGAME1..9)
SAVE_VERSION = 0x10
STAFF_LAST, PATIENT_LAST = 0x410, 0x3F0  # header counts: highest record index (1,041 / 1,009 records)
SAVE_KEY = 0x0C

# (name, size) in file order after the 12-byte header; sizes are those BIPRO.EXE writes. The two
# record tables take their size from the header when read back (count + 1 records).
SAVE_BLOCKS = [
    ("patients", None), ("staff", None),
    ("p5A41", 0x1900), ("p5A45", 0xAE0), ("p5A75", 0x9E0), ("p5A79", 0xB20), ("p5A7D", 0x9E0),
    ("p5A81", 0xDA0), ("p5A4D", 0x760), ("p5A5D", 0x760), ("p5A85", 0x640), ("p5A89", 0x7080),
    ("p5A95", 0x2DA0), ("openings", 0x578), ("p5AAB", 0xFA00), ("p5A49", 0x7080), ("p5A8D", 0x9600),
    ("p5A91", 0x9600), ("p5A69", 0x5100), ("p5A6D", 0x5100), ("p5A71", 0x7440), ("p5A61", 0x7080),
    ("p5A65", 0x7080), ("p5A51", 0x3E8), ("p5A55", 0x960), ("p5A59", 0x960), ("hospitals", 0x2134),
    ("s631B", 0x1F4), ("s6569", 0x1F4), ("s3DCD", 0x190), ("p5BE7", 0x1B58), ("p5C45", 0xC8),
    ("p5C6B", 0x190), ("s552A", 0x60), ("p5C71", 0x20), ("p5C75", 0x258),
]
TAIL = ("s694B", 0x5E)  # after the checksum, not covered by it; holds the game date


def save_decode(data: bytes) -> tuple[bytes, int]:
    """Running key from 0x0C: plain = stored - key, key += stored. Also returns the block's checksum
    contribution: the sum (mod 2^16) of the key after each byte."""
    key, total, out = SAVE_KEY, 0, bytearray(len(data))
    for i, c in enumerate(data):
        out[i] = (c - key) & 0xFF
        key = (key + c) & 0xFF
        total += key
    return bytes(out), total & 0xFFFF


def save_encode(plain: bytes) -> bytes:
    """Inverse of save_decode (stored = plain + key, key += stored)."""
    return _encode_saved_block(plain)[0]


def _encode_saved_block(plain: bytes) -> tuple[bytes, int]:
    # The checksum uses the updated key already computed here. Decoding the result
    # solely to recover that sum needlessly traverses every saved byte a second time.
    key, total, out = SAVE_KEY, 0, bytearray(len(plain))
    for i, p in enumerate(plain):
        out[i] = (p + key) & 0xFF
        key = (key + out[i]) & 0xFF
        total += key
    return bytes(out), total & 0xFFFF


# Consecutive simulation snapshots usually change only a few blocks. Key by the complete
# encoded bytes, never a path or timestamp; keep the cache bounded across long sweeps.
_decode_saved_block = functools.lru_cache(maxsize=128)(save_decode)


def block_sizes(staff_last: int = STAFF_LAST, patient_last: int = PATIENT_LAST) -> list[tuple[str, int]]:
    fixed = {"patients": (patient_last + 1) * 106, "staff": (staff_last + 1) * 32}
    return [(n, fixed.get(n, s)) for n, s in SAVE_BLOCKS]


def read_save(data: bytes) -> dict:
    """Split a save file. Returns header, blocks (plain), stored and computed checksum, and problems."""
    problems = []
    staff_last, patient_last, version = struct.unpack_from("<3I", data)
    if version != SAVE_VERSION:
        problems.append(f"version {version:#x}, the game loads only {SAVE_VERSION:#x}")
    pos, total, blocks = 12, 0, []
    for name, size in block_sizes(staff_last, patient_last):
        plain, part = _decode_saved_block(bytes(data[pos : pos + size]))
        blocks.append((name, pos, size, plain))
        total = (total + part) & 0xFFFF
        pos += size
    stored = struct.unpack_from("<H", data, pos)[0] if pos + 2 <= len(data) else None
    pos += 2
    tail, _ = _decode_saved_block(bytes(data[pos : pos + TAIL[1]]))
    blocks.append((TAIL[0], pos, TAIL[1], tail))
    pos += TAIL[1]
    if pos != len(data):
        problems.append(f"layout covers {pos} bytes, file has {len(data)}")
    if stored != total:
        problems.append(f"checksum stored {stored} computed {total:#06x}")
    return {"header": (staff_last, patient_last, version), "blocks": blocks, "stored": stored,
            "computed": total, "problems": problems}


def build_save(plains: list[bytes], tail: bytes, staff_last=STAFF_LAST, patient_last=PATIENT_LAST) -> bytes:
    """Write a save file from plain blocks, the way BIPRO.EXE does (used by the tests)."""
    out, total = bytearray(struct.pack("<3I", staff_last, patient_last, SAVE_VERSION)), 0
    for p in plains:
        enc, part = _encode_saved_block(p)
        total = (total + part) & 0xFFFF
        out += enc
    return bytes(out + struct.pack("<H", total) + save_encode(tail))


def save_date(tail: bytes) -> str:
    year, month, day = struct.unpack_from("<3H", tail, 6)
    return f"{day:02d}.{month:02d}.{year}"


def plant_save(data: bytes, edits: list[tuple]) -> bytes:
    """A save with planted fields: edits are (block, offset, struct format, value), block = the index in file order
    from 0 (0 patients, 1 staff, ..., 35) or "tail". Re-encoded with a valid checksum; every byte the edits do not
    name is checked unchanged (ValueError otherwise, also for a source with problems)."""
    s = read_save(data)
    if s["problems"]:
        raise ValueError(f"source save: {s['problems']}")
    plains = [bytearray(b[3]) for b in s["blocks"]]
    touched = set()
    for blk, off, fmt, value in edits:
        i = len(plains) - 1 if blk == "tail" else blk
        struct.pack_into(fmt, plains[i], off, value)
        touched |= {(i, off + k) for k in range(struct.calcsize(fmt))}
    out = build_save([bytes(p) for p in plains[:-1]], bytes(plains[-1]), *s["header"][:2])
    t = read_save(out)
    for i, (a, b) in enumerate(zip((x[3] for x in s["blocks"]), (x[3] for x in t["blocks"]))):
        stray = [k for k in range(len(a)) if a[k] != b[k] and (i, k) not in touched]
        if stray or t["problems"]:
            raise ValueError(f"block {i}: bytes {stray[:5]} changed, {t['problems']}")
    return out


def make_plants(plants: dict, saves: pathlib.Path, out: pathlib.Path) -> list[str]:
    """Write planted saves {name: (source save, edits)} from the save folder `saves` into `out` (the save folder is
    only read); returns the names whose source save is missing (tools/compare_sound.py, tools/compare_screens.py)."""
    out.mkdir(parents=True, exist_ok=True)
    missing = []
    for name, (src, edits) in plants.items():
        if (saves / src).is_file():
            (out / name).write_bytes(plant_save((saves / src).read_bytes(), edits))
        else:
            missing.append(f"{name} (needs {src})")
    return missing


# ---------------------------------------------------------------- high scores (GAMES\QUACK)
QUACK_KEY = 0x0B
QUACK_ENTRIES, QUACK_ENTRY = 20, 28
QUACK_DEFAULT = 0x5D8F + 0x11C0  # file offset of the built-in table in BIPRO.EXE


def quack_decode(data: bytes) -> bytes:
    """stored = plain + key, then key += plain, i.e. key = previous stored byte: plain = c[i] - c[i-1]."""
    prev, out = QUACK_KEY, bytearray(len(data))
    for i, c in enumerate(data):
        out[i] = (c - prev) & 0xFF
        prev = c
    return bytes(out)


def quack_encode(plain: bytes) -> bytes:
    key, out = QUACK_KEY, bytearray(len(plain))
    for i, p in enumerate(plain):
        out[i] = (p + key) & 0xFF
        key = (key + p) & 0xFF
    return bytes(out)


def quack_checksum(stored: bytes) -> int:
    return (sum(stored) & 0xFFFF) >> 1


def quack_entries(plain: bytes) -> list[tuple[str, int, int, int]]:
    rows = []
    for i in range(QUACK_ENTRIES):
        e = plain[i * QUACK_ENTRY : (i + 1) * QUACK_ENTRY]
        name = e[:20].split(b"$", 1)[0].decode("latin-1")
        score, a, b = struct.unpack_from("<IHH", e, 20)
        rows.append((name, score, a, b))
    return rows


# ---------------------------------------------------------------- BIING!.CFG / SOUND.CFG
GAME_KEYS = {"FO": "FONTS", "SO": "SOUND", "AN": "ANIMS", "IN": "INTRO", "RI": "RINGCACHE",
             "AG": "(AG..)", "FA": "(FA..)"}  # the keys BIPRO.EXE recognises (two letters; docs/saves.md)
CARDS = {"B ": 1, "BP": 2, "B1": 3, "OL": 6, "M ": 7, "D ": 8}  # 2nd/3rd value char -> card code
SOUND_KEYS = {"DC": ("DCARD", "card"), "DV": ("DV..", "dec3"), "DP": ("DPORT", "hex3"),
              "DI": ("DIRQ", "digit"), "DD": ("DDMA", "digit"), "DM": ("DMACOMP", "digit"),
              "MC": ("MCARD", "card"), "MV": ("MV..", "dec3"), "MP": ("MPORT", "hex3"),
              "MI": ("MIXER", "mixer")}


def _lines(data: bytes):
    for line in data.split(b"\n"):
        if len(line) >= 2 and b"-" in line:
            key = line[:2].decode("latin-1")
            yield key, line[line.index(b"-") + 1 :]


def parse_game_cfg(data: bytes) -> dict[str, bool]:
    """A recognised line is on unless the character after '-' is N/n."""
    return {GAME_KEYS[k]: v[:1] not in (b"N", b"n") for k, v in _lines(data) if k in GAME_KEYS}


def _hexdigit(c: int) -> int:
    return (c - 0x37 if c >= 0x41 else c) & 0xF


def parse_sound_cfg(data: bytes) -> dict:
    out = {}
    for k, v in _lines(data):
        if k not in SOUND_KEYS:
            continue
        name, kind = SOUND_KEYS[k]
        v = v + b"    "
        if kind == "card":
            out[name] = CARDS.get(v[1:3].decode("latin-1"), 0)
        elif kind == "dec3":
            out[name] = (v[0] - 48) * 100 + (v[1] - 48) * 10 + (v[2] - 48)
        elif kind == "digit":
            out[name] = v[0] - 48
        elif kind == "hex3":
            out[name] = ((v[0] & 0xF) << 8) | (((v[1] << 4) + (v[2] & 0xF)) & 0xFF)
        else:
            out[name] = ([_hexdigit(c) for c in v[:4]], v[4] != 0x20)
    return out


# ---------------------------------------------------------------- command line
def main(argv: list[str]) -> int:
    if len(argv) < 2 or argv[0] not in ("save", "quack", "cfg"):
        print(__doc__)
        return 2
    path = pathlib.Path(argv[1])
    data = path.read_bytes()
    if argv[0] == "save":
        r = read_save(data)
        print(f"header: staff_last={r['header'][0]:#x} patient_last={r['header'][1]:#x} "
              f"version={r['header'][2]:#x}")
        for name, pos, size, _ in r["blocks"]:
            print(f"  {pos:#08x} {size:#07x} {name}")
        print(f"checksum stored={r['stored']} computed={r['computed']:#06x}; date {save_date(r['blocks'][-1][3])}")
        if len(argv) > 2:
            out = pathlib.Path(argv[2])
            out.mkdir(parents=True, exist_ok=True)
            for k, (name, _, _, plain) in enumerate(r["blocks"]):
                (out / f"{k:02d}_{name}.bin").write_bytes(plain)
        for p in r["problems"]:
            print(f"[FAIL] {p}")
        if not r["problems"]:
            print(f"[OK] {len(data)} bytes, {len(r['blocks']) - 1} blocks + tail")
        return 1 if r["problems"] else 0
    if argv[0] == "quack":
        if data[:2] == b"MZ":
            plain, ok = data[QUACK_DEFAULT : QUACK_DEFAULT + QUACK_ENTRIES * QUACK_ENTRY], True
        else:
            body = data[: QUACK_ENTRIES * QUACK_ENTRY]
            ok = len(data) == len(body) + 2 and struct.unpack_from("<H", data, len(body))[0] == quack_checksum(body)
            plain = quack_decode(body)
        for k, (name, score, a, b) in enumerate(quack_entries(plain), 1):
            print(f"{k:2d}  {score:8d}  {a:5d} {b:3d}  {name}")
        if data[:2] == b"MZ":
            print("[OK] built-in default table")
        else:
            print("[OK] checksum" if ok else "[FAIL] size or checksum")
        return 0 if ok else 1
    parsed = parse_sound_cfg(data) if b"DCARD" in data or b"MCARD" in data else parse_game_cfg(data)
    for k, v in parsed.items():
        print(f"{k:10s} {v:#x}" if k.endswith("PORT") else f"{k:10s} {v}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
