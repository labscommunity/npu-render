import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--exe", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
v = p.parse_args()
v.output.mkdir(exist_ok=False)
b = v.exe.parent.parent / "build-report.json"
raw = b.read_bytes()
proof = json.loads(raw.decode("utf-16") if raw.startswith(b"\xff\xfe") else raw.decode("utf-8-sig"))
assert (
    proof["clean_build"]
    and hashlib.sha256(v.exe.read_bytes()).hexdigest() == proof["binary_sha256"].lower()
)
h = [0x31454D415246524E, 1, 64, 7, 4, 2, 87, 16, 32, 1000, 123, 0]


def packet(name, changes={}, truncate=0):
    values = h.copy()
    for i, x in changes.items():
        values[i] = x
    data = struct.pack("<QIIQIIIIQQII", *values) + bytes(range(32))
    data = data[:-truncate] if truncate else data
    path = v.output / (name + ".bin")
    path.write_bytes(data)
    return path


first = packet("first")
cases = []
for name, changes, truncated, expected in [
    ("same", {}, 0, True),
    ("new", {3: 8, 9: 1100}, 0, True),
    ("sequence-backwards", {3: 6, 9: 1100}, 0, False),
    ("time-backwards", {3: 8, 9: 999}, 0, False),
    ("same-time", {3: 8}, 0, False),
    ("producer-change", {3: 8, 9: 1100, 10: 124}, 0, False),
    ("duplicate-mutation", {9: 1001}, 0, False),
    ("truncated", {}, 1, False),
    ("bad-stride", {7: 12}, 0, False),
    ("bad-format", {6: 1}, 0, False),
    ("reserved", {11: 1}, 0, False),
]:
    f = packet(name, changes, truncated)
    r = subprocess.run(
        [str(v.exe), "--read-pair", str(first), str(f)], capture_output=True, timeout=5
    )
    assert (r.returncode == 0) == expected, (name, r.returncode, r.stderr)
    entry = dict(
        name=name,
        accepted=r.returncode == 0,
        expected_accepted=expected,
        stderr=r.stderr.decode().strip(),
    )
    if expected:
        result = json.loads(r.stdout)
        assert result["frame_pair_valid"] and result["new_pixel_bytes_read"] == (
            0 if name == "same" else 32
        )
        entry["receipt"] = result
    cases.append(entry)
report = dict(
    passed=True,
    tests=cases,
    binary_sha256=proof["binary_sha256"],
    physical_presentation_tested=False,
    desktop_changed=False,
)
(v.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report))
