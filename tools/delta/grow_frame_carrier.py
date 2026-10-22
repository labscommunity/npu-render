"""Derive the wide-window scatter carrier (v2) from the pinned v1 carrier.

The frame kernel runs inside a ScatterElementsUpdate blob whose SHAVE text
section is the code window the kernel is patched into. The compiler emitted a
24,496-byte window (v1); this tool grows that section to a 48,048-byte window
(v2) by rewriting the ELF in place: every later section shifts by the growth,
both rows of each of the six range descriptors advertise the new code size, and
the OpenVINO NPU metadata trailer (which records the ELF region's size) is
re-attached after the re-padded ELF. Nothing else in the blob changes. Both
carriers are pinned by sha256 in ptl_frame_carrier.py; qualification and the
worker accept either.
"""

import argparse
import hashlib
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ptl_frame_carrier  # noqa: E402
from ptl_elf import check  # noqa: E402

TRAILER_MAGIC = b"OVNPU"
SHDR = "<IIQQQQIIQQ"  # name type flags addr offset size link info addralign entsize
NOBITS = 8
RANGE_ROW_BYTES = 40
RANGE_SIZE_OFFSET = 24


def split_trailer(blob):
    """The ELF region (padded to 4 KiB), the plugin metadata, and the trailer layout."""
    check(blob.endswith(TRAILER_MAGIC) and len(blob) > 13, "OpenVINO NPU trailer")
    (size,) = struct.unpack_from("<Q", blob, len(blob) - 13)
    check(0 < size <= len(blob) - 13 and size % 4096 == 0, "trailer data size")
    return blob[:size], blob[size : len(blob) - 13]


def grow_text(elf, region_bytes):
    """Grow shave.text to `region_bytes` (a 1 KiB multiple); return the new ELF image."""
    check(elf[:6] == b"\x7fELF\x02\x01", "ELF64 little-endian")
    (shoff,) = struct.unpack_from("<Q", elf, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", elf, 0x3A)
    check(shentsize == 64 and shoff == 0x40 and 0 < shnum < 8192, "section header table")
    headers = [list(struct.unpack_from(SHDR, elf, shoff + i * 64)) for i in range(shnum)]
    strings = headers[shstrndx]

    def name(h):
        at = strings[4] + h[0]
        return elf[at : elf.index(b"\0", at)].decode("ascii")

    names = [name(h) for h in headers]
    text = names.index("shave.text")
    offset, size, align = headers[text][4], headers[text][5], headers[text][8]
    check(align == 1024 and region_bytes % align == 0, "text alignment")
    later = sorted(h[4] for h in headers if h[1] != NOBITS and h[4] > offset)
    region = later[0] - offset
    check(region_bytes > region and size <= region, "growing text")
    delta = region_bytes - region
    for h in headers:
        if h[1] != NOBITS and h[4] > offset:
            h[4] += delta
    headers[text][5] = size + delta
    out = bytearray(elf[: offset + size] + bytes(region_bytes - size) + elf[later[0] :])
    for i, h in enumerate(headers):
        struct.pack_into(SHDR, out, shoff + i * 64, *h)
    old_code = None
    for i, h in enumerate(headers):
        if not names[i].startswith("task.shave.range."):
            continue
        check(h[5] == 2 * RANGE_ROW_BYTES, "range rows")
        for row in range(2):
            at = h[4] + row * RANGE_ROW_BYTES + RANGE_SIZE_OFFSET
            (code,) = struct.unpack_from("<I", out, at)
            check(old_code in (None, code) and 0 < code <= size, "range code size")
            old_code = code
            struct.pack_into("<I", out, at, code + delta)
    check(old_code is not None, "range descriptors")
    return bytes(out), old_code + delta


def grow(blob, region_bytes):
    elf, metadata = split_trailer(blob)
    grown, code_bytes = grow_text(elf, region_bytes)
    body = grown.rstrip(b"\0")
    padded = body + bytes((len(body) + 4095) // 4096 * 4096 - len(body))
    return padded + metadata + struct.pack("<Q", len(padded)) + TRAILER_MAGIC, code_bytes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="the pinned v1 carrier.blob")
    parser.add_argument("--output", type=Path, required=True, help="where to write carrier v2")
    args = parser.parse_args()
    source = args.source.read_bytes()
    check(hashlib.sha256(source).hexdigest() == ptl_frame_carrier.V1_SHA, "v1 carrier pin")
    grown, code_bytes = grow(source, ptl_frame_carrier.V2_TEXT_REGION)
    check(code_bytes == ptl_frame_carrier.CARRIERS[ptl_frame_carrier.V2_SHA]["code"], "v2 code")
    check(hashlib.sha256(grown).hexdigest() == ptl_frame_carrier.V2_SHA, "v2 carrier pin")
    proof = ptl_frame_carrier.inspect(grown)
    check(proof["code_bytes"] == code_bytes, "v2 inspection")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(grown)
    print(
        f"wrote {args.output} ({len(grown)} bytes, code window {code_bytes} bytes, "
        f"sha256 {ptl_frame_carrier.V2_SHA[:16]})"
    )


if __name__ == "__main__":
    main()
