"""Validate the bounded NRFRAME1 readback format; no rendering or device claims."""

import struct
from dataclasses import dataclass

HEADER = struct.Struct("<QIIQIIIIQQII")
MAX_PIXELS = 4 * 1024 * 1024
MAX_BYTES = HEADER.size + MAX_PIXELS * 4


@dataclass(frozen=True)
class IndirectFrame:
    sequence: int
    width: int
    height: int
    format: int
    qpc: int
    producer_pid: int
    pixels: bytes

    @property
    def identity(self):
        return self.producer_pid, self.sequence, self.qpc


def parse_frame(data: bytes) -> IndirectFrame:
    if not HEADER.size <= len(data) <= MAX_BYTES:
        raise ValueError("frame size out of bounds")
    magic, version, size, seq, w, h, fmt, pitch, count, qpc, pid, reserved = HEADER.unpack_from(
        data
    )
    if magic != 0x31454D415246524E or version != 1 or size != HEADER.size or reserved:
        raise ValueError("unsupported frame header")
    if not seq or not pid or not qpc:
        raise ValueError("missing frame identity")
    if not (0 < w <= 8192 and 0 < h <= 8192 and w * h <= MAX_PIXELS):
        raise ValueError("frame dimensions out of bounds")
    if fmt not in (28, 87) or pitch != w * 4 or count != pitch * h:
        raise ValueError("unsupported packed frame layout")
    if len(data) != HEADER.size + count:
        raise ValueError("truncated frame or trailing bytes")
    return IndirectFrame(seq, w, h, fmt, qpc, pid, data[HEADER.size :])
