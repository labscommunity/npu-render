"""Validate the bounded NRFRAME1 readback format; no rendering or device claims."""

import json
import os
import struct
import time
from dataclasses import dataclass
from pathlib import Path

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


def confirmed_sole_display(path: Path, now: float | None = None) -> bool:
    """Fail closed if the collector's layout confirmation is absent or stale."""
    try:
        with path.open("rb") as source:
            published = os.fstat(source.fileno()).st_mtime
            age = (time.time() if now is None else now) - published
            data = source.read(4097)
        if not 0 <= age <= 15 or len(data) > 4096:
            return False
        state = json.loads(data)
        return (
            isinstance(state, dict)
            and isinstance(state.get("topology_name"), str)
            and state["topology_name"].startswith("idd-topology-")
            and state.get("sole_indirect") is True
        )
    except (OSError, ValueError):
        return False
