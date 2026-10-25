"""Tests for tools/biing_cdrom.py on synthetic CD images, videos and manual text (no game bytes)."""
import pathlib
import struct
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import biing_cdrom as cd  # noqa: E402


# --- synthetic image builders -------------------------------------------------------------------------

def both(v: int, width: int) -> bytes:
    f = {4: ("<I", ">I"), 2: ("<H", ">H")}[width]
    return struct.pack(f[0], v) + struct.pack(f[1], v)


def dir_record(name: bytes, lba: int, size: int, is_dir: bool) -> bytes:
    body = (both(lba, 4) + both(size, 4) + bytes(7) + bytes([2 if is_dir else 0, 0, 0]) + both(1, 2)
            + bytes([len(name)]) + name)
    rec = b"\0\0" + body  # length byte (set below), extended attribute length 0
    if len(rec) % 2:
        rec += b"\0"
    return bytes([len(rec)]) + rec[1:]


def path_record(name: bytes, lba: int, parent: int, big: bool) -> bytes:
    o = ">" if big else "<"
    r = bytes([len(name), 0]) + struct.pack(o + "I", lba) + struct.pack(o + "H", parent) + name
    return r + (b"\0" if len(name) % 2 else b"")


def build_iso(file_data: bytes, file_name: bytes = b"HELLO.;1") -> list[bytes]:
    """Sectors (2,048 B each): 16 empty, PVD, terminator, L table, M table, root dir, SUB dir, file."""
    ROOT, SUB, FILE, LT, MT = 20, 21, 22, 18, 19
    nfile = (len(file_data) + 2047) // 2048
    total = FILE + nfile
    root = (dir_record(b"\0", ROOT, 2048, True) + dir_record(b"\1", ROOT, 2048, True)
            + dir_record(b"SUB", SUB, 2048, True))
    sub = (dir_record(b"\0", SUB, 2048, True) + dir_record(b"\1", ROOT, 2048, True)
           + dir_record(file_name, FILE, len(file_data), False))
    lt = path_record(b"\0", ROOT, 1, False) + path_record(b"SUB", SUB, 1, False)
    mt = path_record(b"\0", ROOT, 1, True) + path_record(b"SUB", SUB, 1, True)
    pvd = bytearray(2048)
    pvd[0:7] = b"\x01CD001\x01"
    pvd[8:40] = b"TEST".ljust(32)
    pvd[40:72] = b"SYNTH_VOL".ljust(32)
    pvd[80:88] = both(total, 4)
    pvd[128:132] = both(2048, 2)
    pvd[132:140] = both(len(lt), 4)
    pvd[140:144] = struct.pack("<I", LT)
    pvd[148:152] = struct.pack(">I", MT)
    pvd[156:190] = dir_record(b"\0", ROOT, 2048, True)
    term = b"\xffCD001\x01".ljust(2048, b"\0")
    sectors = [bytes(2048)] * 16 + [bytes(pvd), term, lt.ljust(2048, b"\0"), mt.ljust(2048, b"\0"),
                                    root.ljust(2048, b"\0"), sub.ljust(2048, b"\0")]
    sectors += [file_data[i * 2048 : (i + 1) * 2048].ljust(2048, b"\0") for i in range(nfile)]
    return sectors


def mode1(lba: int, user: bytes) -> bytes:
    head = cd.SYNC + cd.expected_header(lba) + user
    return head + struct.pack("<I", cd.edc(head)) + bytes(8 + 276)


def write_image(tmp: pathlib.Path, file_data: bytes, audio: bytes, short_form: bool,
                file_name: bytes = b"HELLO.;1") -> pathlib.Path:
    iso = build_iso(file_data, file_name)
    data = b"".join(mode1(i, s) for i, s in enumerate(iso))
    t2 = len(iso)
    (tmp / "img.bin").write_bytes(data + audio)
    m, s, f = t2 // 4500, t2 // 75 % 60, t2 % 75
    if short_form:
        cue = f'FILE "img.bin" BINARY\n TRACK 1 MODE1/2352\n INDEX 1 00:00:00\n TRACK 2 AUDIO\n INDEX 1 {m:02d}:{s:02d}:{f:02d}\n'
    else:
        cue = f'FILE "img.bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n  TRACK 02 AUDIO\n    INDEX 01 {m:02d}:{s:02d}:{f:02d}\n'
    (tmp / "img.cue").write_text(cue)
    return tmp / "img.cue"


# --- tests --------------------------------------------------------------------------------------------

def test_msf_and_header():
    assert cd.msf_to_lba("52:54:70") == 238120
    assert cd.expected_header(0) == bytes([0x00, 0x02, 0x00, 1])  # LBA 0 is 00:02:00
    with pytest.raises(ValueError):
        cd.msf_to_lba("00:60:00")


def test_edc_detects_a_flipped_byte():
    s = mode1(5, bytes(range(256)) * 8)
    assert cd.check_mode1(s, 5, True) is None
    bad = bytearray(s)
    bad[100] ^= 1
    assert cd.check_mode1(bytes(bad), 5, True) == "EDC"
    assert cd.check_mode1(s, 6, True).startswith("header")


@pytest.mark.parametrize("short_form", [False, True])
def test_extract_synthetic_image(tmp_path, short_form):
    payload = bytes((i * 7) & 0xFF for i in range(5000))  # spans three sectors
    audio = struct.pack("<4h", 1, -1, 300, -300) * (588 // 2) * 3  # 3 sectors of 16-bit stereo
    cue = write_image(tmp_path, payload, audio, short_form)
    img = cd.Image(cue)
    assert [(t.number, t.mode, t.end - t.start) for t in img.tracks] == [(1, "MODE1/2352", 25), (2, "AUDIO", 3)]
    out = tmp_path / "out"
    assert cd.cmd_extract(img, cd.Iso(img.user), out, "all") == 0
    assert (out / "iso" / "SUB" / "HELLO").read_bytes() == payload
    wav = (out / "audio" / "track02.wav").read_bytes()
    assert wav[:4] == b"RIFF" and wav[22:24] == b"\x02\x00" and struct.unpack_from("<I", wav, 24)[0] == 44100
    assert wav[44:] == audio  # CD audio is copied unchanged: little-endian samples


def test_path_table_mismatch_is_reported(tmp_path):
    cue = write_image(tmp_path, b"x", bytes(2352), False)
    img = cd.Image(cue)
    iso = cd.Iso(img.user)
    entries = iso.walk()
    entries[0].lba += 1  # the SUB directory now disagrees with the path table
    with pytest.raises(ValueError, match="path table"):
        iso.check_path_tables(entries)


@pytest.mark.parametrize("name", [b"../../../victim;1", b"..\\..\\victim;1", b"/victim;1",
                                  b"C:VICTIM;1", b".;1", b"..;1", b";1", b"BAD\0NAME;1"])
def test_extract_rejects_unsafe_iso_names(tmp_path, name):
    victim = tmp_path / "victim"
    victim.write_bytes(b"synthetic outside sentinel")
    cue = write_image(tmp_path, b"replacement", bytes(2352), False, name)
    img = cd.Image(cue)
    with pytest.raises(ValueError, match="unsafe ISO name"):
        cd.cmd_extract(img, cd.Iso(img.user), tmp_path / "out", "all")
    assert victim.read_bytes() == b"synthetic outside sentinel"


@pytest.mark.parametrize("relative", ["iso", "iso/SUB", "iso/SUB/HELLO", "audio", "audio/track02.wav"])
def test_extract_rejects_output_symlink_escape(tmp_path, relative):
    outside = tmp_path / "outside"
    outside.mkdir()
    victim = outside / "victim"
    victim.write_bytes(b"synthetic outside sentinel")
    out = tmp_path / "out"
    link = out / relative
    link.parent.mkdir(parents=True)
    directory = relative in {"iso", "iso/SUB", "audio"}
    try:
        link.symlink_to(outside if directory else victim, target_is_directory=directory)
    except OSError:
        pytest.skip("host does not permit test symlinks")
    cue = write_image(tmp_path, b"replacement", bytes(2352), False)
    img = cd.Image(cue)
    with pytest.raises(ValueError, match="escapes output"):
        cd.cmd_extract(img, cd.Iso(img.user), out, "all")
    assert {p.name: p.read_bytes() for p in outside.iterdir()} == {"victim": b"synthetic outside sentinel"}


def test_corrupt_sync_is_found(tmp_path):
    cue = write_image(tmp_path, b"x", bytes(2352), False)
    raw = bytearray((tmp_path / "img.bin").read_bytes())
    raw[2352 * 3 + 5] = 0x12
    (tmp_path / "img.bin").write_bytes(bytes(raw))
    _, _, errors = cd.scan_data_track(cd.Image(cue), "none")
    assert errors == ["LBA 3: sync pattern"]


# --- VPX ---------------------------------------------------------------------------------------------

def vpx_file(frames: list[tuple[bytes, bytes]], pcm: bytes) -> bytes:
    banner = b"VPX1 synthetic".ljust(cd.VPX_BANNER, b"-")
    d = banner + struct.pack("<4H", 320, 240, 15, 22222) + b"COLS" + bytes(range(256)) * 3
    d += b"SOUN" + struct.pack("<I", len(pcm)) + pcm
    for codes, data in frames:
        d += b"CODE" + codes + b"VIDE" + struct.pack("<I", len(data)) + data
    return d


def test_vpx_block_codes():
    codes = bytearray(600)
    codes[0] = 0x41  # block 0: one colour on the pair, block 1: one group on both phases of the pair
    codes[1] = 0x23  # block 2: two groups, block 3: four groups (all phases)
    group = bytes(range(16))
    two = bytes(range(100, 132))
    four = bytes(range(150, 214))
    data = bytes([9]) + group + two + four
    v = cd.parse_vpx(vpx_file([(bytes(codes), data), (bytes(600), b"")], b"\x80" * 10))
    frames = [bytes(f) for f in cd.vpx_frames(v)]
    fb = frames[0]
    px = lambda x, y: fb[y * 320 + x]  # noqa: E731
    # even frame: the pair is (row 0, col 0) and (row 1, col 1); the other checkerboard half keeps colour 2
    assert [px(x, 0) for x in range(8)] == [9, 2, 9, 2, 9, 2, 9, 2]
    assert [px(x, 1) for x in range(8)] == [2, 9, 2, 9, 2, 9, 2, 9]
    assert [px(8 + x, 0) for x in range(8)] == [0, 2, 1, 2, 2, 2, 3, 2]
    assert [px(8 + x, 3) for x in range(8)] == [2, 4, 2, 5, 2, 6, 2, 7]
    assert [px(16 + x, 2) for x in range(8)] == [104, 2, 105, 2, 106, 2, 107, 2]
    assert [px(16 + x, 1) for x in range(8)] == [2, 116, 2, 117, 2, 118, 2, 119]
    assert [px(24 + x, 0) for x in range(8)] == [150, 182, 151, 183, 152, 184, 153, 185]
    assert [px(24 + x, 1) for x in range(8)] == [198, 166, 199, 167, 200, 168, 201, 169]
    assert frames[1] == fb  # an all-keep frame changes nothing


def test_vpx_odd_frame_uses_the_other_pair():
    codes = bytearray(600)
    codes[0] = 0x40
    v = cd.parse_vpx(vpx_file([(bytes(600), b""), (bytes(codes), bytes([7]))], b""))
    fb = [bytes(f) for f in cd.vpx_frames(v)][1]
    assert [fb[x] for x in range(8)] == [2, 7, 2, 7, 2, 7, 2, 7]
    assert [fb[320 + x] for x in range(8)] == [7, 2, 7, 2, 7, 2, 7, 2]


def test_vpx_rejects_leftover_data():
    codes = bytearray(600)
    codes[0] = 0x40
    v = cd.parse_vpx(vpx_file([(bytes(codes), b"\x01\x02")], b""))
    with pytest.raises(ValueError, match="left"):
        list(cd.vpx_frames(v))


# --- manual -------------------------------------------------------------------------------------------

def test_unshadow_and_line_count():
    assert cd.unshadow("PPPPP" + "asswortabfrage" * 5) == "Passwortabfrage"
    assert cd.unshadow("Ein ganz normaler Satz.") is None
    page = "Kopf" * 5 + "\nerste Zeile\n\nzweite Zeile\n" + "- 7 -" * 5
    assert cd.manual_lines([page]) == [(7, 1, "Kopf"), (7, 2, "erste Zeile"), (7, 3, "zweite Zeile")]


def test_edc_matches_bitwise_crc():
    """Independent bit-by-bit form of the CD-ROM EDC (polynomial x^32+x^31+x^16+x^15+x^4+x^3+x+1, reflected)."""
    def bitwise(data: bytes) -> int:
        e = 0
        for b in data:
            e ^= b
            for _ in range(8):
                e = (e >> 1) ^ (0xD8018001 if e & 1 else 0)
        return e

    sample = bytes((i * 31 + 7) & 0xFF for i in range(2064))
    assert cd.edc(sample) == bitwise(sample)
    assert cd.edc(sample + struct.pack("<I", cd.edc(sample))) == 0  # CRC residue property
