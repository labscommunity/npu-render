"""Verify finite physical-NPU worker output and malformed-input rejection."""

import argparse
import hashlib
import json
import os
import secrets
import struct
import subprocess
from datetime import UTC, datetime
from pathlib import Path

HEADER = struct.Struct("<3Q8IQ")
MAGIC = 0x4E525F4558434831


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", required=True, type=Path)
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if os.name != "nt":
        raise RuntimeError("Real Windows NPU required")
    args.output.mkdir(parents=True, exist_ok=False)
    started = datetime.now(UTC).isoformat()
    width, height = 320, 192
    frame_bytes = width * height * 4
    background, foreground, expected = bytearray(), bytearray(), bytearray()
    for y in range(height):
        for x in range(width):
            alpha = (x * 13 + y * 17) % 256
            for channel in range(3):
                bg = (x * 23 + y * 7 + channel * 47) % 256
                fg = (x * 7 + y * 11 + channel * 29) % (alpha + 1)
                background.append(bg)
                foreground.append(fg)
                expected.append(fg + (bg * (255 - alpha) + 127) // 255)
            background.append(255)
            foreground.append(alpha)
            expected.append(255)
    fields = [
        MAGIC,
        secrets.randbits(64),
        secrets.randbits(64),
        1,
        width,
        height,
        1,
        frame_bytes,
        1,
        0,
        0,
        0,
    ]
    valid = HEADER.pack(*fields) + background + foreground
    wrong_device = fields.copy()
    wrong_device[9] = 1
    oversized = fields.copy()
    oversized[4:6] = [8192, 8192]
    oversized[7] = 8192 * 8192 * 4
    cases = {
        "valid": valid,
        "truncated": valid[:-1],
        "trailing": valid + b"x",
        "cpu-device": HEADER.pack(*wrong_device) + background + foreground,
        "oversized": HEADER.pack(*oversized),
    }
    environment = os.environ.copy()
    environment["PATH"] = str(args.runtime) + os.pathsep + environment["PATH"]
    results = []
    for name, contents in cases.items():
        request, reply = args.output / f"{name}.request.bin", args.output / f"{name}.reply.bin"
        request.write_bytes(contents)
        run = subprocess.run(
            [str(args.worker), str(request), str(reply)],
            env=environment,
            capture_output=True,
            timeout=90,
            check=False,
        )
        (args.output / f"{name}.stdout.jsonl").write_bytes(run.stdout)
        (args.output / f"{name}.stderr.log").write_bytes(run.stderr)
        result = {"case": name, "exit_code": run.returncode, "reply_exists": reply.exists()}
        if name == "valid":
            if run.returncode != 0:
                raise RuntimeError(f"NPU worker failed: {run.stderr!r}")
            telemetry = json.loads(run.stdout)
            device = telemetry["execution_device"]
            if device != "NPU" and not device.startswith("NPU."):
                raise RuntimeError("Wrong execution device")
            returned = reply.read_bytes()
            response_fields = fields.copy()
            response_fields[8:10] = [2, 3]
            if len(returned) != HEADER.size + frame_bytes or returned[: HEADER.size] != HEADER.pack(
                *response_fields
            ):
                raise RuntimeError("Wrong response identity or extent")
            error = max(abs(a - b) for a, b in zip(returned[HEADER.size :], expected, strict=True))
            if error > 1:
                raise RuntimeError(f"NPU pixels mismatch: {error}")
            result.update(verified_bytes=frame_bytes, max_error=error, execution_device=device)
        elif run.returncode == 0 or reply.exists() or reply.with_suffix(".bin.tmp").exists():
            raise RuntimeError(f"Malformed {name} request was accepted")
        results.append(result)
    report = {
        "started": started,
        "finished": datetime.now(UTC).isoformat(),
        "passed": True,
        "worker_sha256": hashlib.sha256(args.worker.read_bytes()).hexdigest(),
        "cases": results,
        "npu_execution_tested": True,
        "registered_adapter_tested": False,
        "desktop_offload_tested": False,
    }
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
