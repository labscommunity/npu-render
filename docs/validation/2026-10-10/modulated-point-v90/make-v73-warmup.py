import hashlib
import json
import struct
from pathlib import Path

root = Path(__file__).resolve().parent
b = (root / "v72-dwm-draw-7464-complete.bin").read_bytes()
assert b[:8] == b"NPDRAW01"
at = 8
r = {}
while at < len(b):
    key = b[at : at + 32].split(b"\0")[0].decode()
    n = struct.unpack_from("<Q", b, at + 32)[0]
    at += 40
    r[key] = b[at : at + n]
    at += n
assert at == len(b) and r["complete"] == b""


def words(name):
    return struct.unpack("<" + "I" * (len(r[name]) // 4), r[name])


args = words("arguments")
count, first, base = args[:3]
assert (count, first, base) == (6, 66, 0)
ib = words("indices-description")
vb = words("vertices-0-description")
layout = words("layout")
assert ib[3] in (57, 42) and vb[1] == 32
indices = struct.unpack_from(
    "<" + ("H" if ib[3] == 57 else "I") * count,
    r["indices"],
    ib[2] + first * (2 if ib[3] == 57 else 4),
)
inputs = bytearray()
for index in indices:
    regs = bytearray(256)
    for offset in range(0, len(layout), 6):
        slot, field, fmt, classification, step, reg = layout[offset : offset + 6]
        assert slot == 0 and classification == step == 0 and reg < 16
        begin = vb[2] + index * vb[1] + field
        if fmt == 28:
            values = [v / 255 for v in r["vertices-0"][begin : begin + 4]]
        else:
            lanes = {6: 3, 16: 2}[fmt]
            values = (
                list(struct.unpack_from("<" + "f" * lanes, r["vertices-0"], begin))
                + [0] * (3 - lanes)
                + [1]
            )
        regs[reg * 16 : reg * 16 + 16] = struct.pack("<4f", *values)
    inputs += regs
assert len(r["vertex-shader"]) == 360 and len(r["pixel-shader"]) == 148
s = words("source-description")
t = words("target-description")
assert t[3:6] == (1024, 768, 87) and s[3:6] == (1280, 48, 87)
body = (
    r["viewport"]
    + r["vertex-shader"]
    + r["pixel-shader"]
    + inputs
    + r["constants-0-0"][:16]
    + r["target"]
    + struct.pack("<4I", 1280, 48, 87, 0)
    + r["source"]
)
header = struct.pack(
    "<QQQ14I",
    0x4E525F4452415731,
    0x12345678,
    0xABCDEF,
    5,
    1024,
    768,
    6,
    87,
    0xF13,
    90,
    37,
    1,
    len(body),
    42,
    1,
    0,
    0,
)
packet = header + body
path = root / "v73-modulated-point-warm-packet.bin"
path.write_bytes(packet)
report = {
    "packet": path.name,
    "bytes": len(packet),
    "sha256": hashlib.sha256(packet).hexdigest(),
    "expected_output_sha256": hashlib.sha256(r["target"]).hexdigest(),
    "expected_basis": "source is all zero including alpha; source-over preserves destination",
}
assert not any(r["source"])
(root / "v73-warm-packet.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report))
