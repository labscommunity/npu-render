"""Replay converted frame batches on one isolated NPU worker and verify target pixels.

Batches come from nr_frame_batch_replay_convert (batches.jsonl + batch-*.bin).
Each reply's dirty rectangles are applied to the batch's uploaded background and
the resulting target image is hashed against the recorded per-draw response.
Timings include pipe transport and are diagnostic only.
"""

import argparse
import hashlib
import json
import os
import secrets
import struct
import subprocess
import time
import traceback
from datetime import UTC, datetime
from pathlib import Path

HEADER = struct.Struct("<3Q14I")  # 80 bytes
BATCH_HEADER = struct.Struct("<8I2Q")  # 48
SHADER_RECORD = struct.Struct("<2I32s")  # 40
RESOURCE_RECORD = struct.Struct("<2Q4I")  # 32
UPLOAD_RECORD = struct.Struct("<6I")  # 24
RESPONSE_HEADER = struct.Struct("<4I2Q4Q4I")  # 80
TARGET_RECORD = struct.Struct("<2Q4I")  # 32


def rows(path):
    return [
        json.loads(x)
        for x in path.read_text(encoding="utf-8-sig").splitlines()
        if x.startswith("{") and x.endswith("}")
    ]


def background_of(packet):
    """The full upload of resource 0 (the target) inside a batch packet."""
    at = HEADER.size
    _, _, shaders, resources, uploads, _, _, _, _, _ = BATCH_HEADER.unpack_from(packet, at)
    at += BATCH_HEADER.size
    for _ in range(shaders):
        _, words, _ = SHADER_RECORD.unpack_from(packet, at)
        at += SHADER_RECORD.size + words * 4
    sizes = []
    for _ in range(resources):
        _, _, width, height, _, _ = RESOURCE_RECORD.unpack_from(packet, at)
        sizes.append((width, height))
        at += RESOURCE_RECORD.size
    for _ in range(uploads):
        resource, x, y, width, height, _ = UPLOAD_RECORD.unpack_from(packet, at)
        at += UPLOAD_RECORD.size
        pixels = packet[at : at + width * height * 4]
        at += width * height * 4
        if resource == 0:
            assert (x, y) == (0, 0) and (width, height) == sizes[0]
            return bytearray(pixels), width, height
    raise AssertionError("batch without a target upload")


def apply_reply(reply, image, width, height):
    """Returns the response header fields and applies target rectangles to `image`."""
    at = HEADER.size
    fields = RESPONSE_HEADER.unpack_from(reply, at)
    at += RESPONSE_HEADER.size
    response = dict(
        target_count=fields[2],
        missing_count=fields[3],
        generation=fields[4],
        batch_sequence=fields[5],
        provider_requests=fields[6],
        covered_samples=fields[7],
        kernel_ms=fields[8] / 1000,
        host_ms=fields[9] / 1000,
        commands_executed=fields[10],
        commands_fallback=fields[11],
        escapes=fields[12],
    )
    if response["missing_count"]:
        return response
    for _ in range(response["target_count"]):
        key, _version, x, y, w, h = TARGET_RECORD.unpack_from(reply, at)
        at += TARGET_RECORD.size
        assert key == 1 and x + w <= width and y + h <= height
        for row in range(h):
            src = at + row * w * 4
            dst = ((y + row) * width + x) * 4
            image[dst : dst + w * 4] = reply[src : src + w * 4]
        at += w * h * 4
    assert at == len(reply), "reply has trailing bytes"
    return response


def replay_reference(args, env, pipe, worker_pid, entry, out):
    """Replays the recorded per-draw packets of a batch; returns the final target pixels."""
    pixels = None
    elapsed = 0.0
    for ticket in entry["tickets"]:
        path = args.capture / f"request-{ticket:08d}.bin"
        began = time.perf_counter()
        result = subprocess.run(
            [
                str(args.client),
                pipe,
                str(worker_pid),
                str(path),
                str(out / "reference.bin"),
                str(args.timeout_ms),
                "success",
            ],
            env=env,
            capture_output=True,
            timeout=args.timeout_ms / 1000 + 30,
        )
        elapsed += (time.perf_counter() - began) * 1000
        if result.returncode:
            raise AssertionError(
                f"reference replay failed at ticket {ticket}: "
                + result.stderr.decode(errors="replace")[-300:]
            )
        pixels = (out / "reference.bin").read_bytes()[HEADER.size + 48 :]
        (out / "reference.bin").unlink()
    return pixels, elapsed


def compare(ours, reference):
    """Per-channel difference statistics between two RGBA images of equal size."""
    import numpy as np

    a = np.frombuffer(ours, np.uint8).astype(np.int16)
    b = np.frombuffer(reference, np.uint8).astype(np.int16)
    delta = np.abs(a - b)
    pixel_delta = delta.reshape(-1, 4).max(axis=1)
    return dict(
        max_diff=int(delta.max()) if delta.size else 0,
        differing_pixels=int((pixel_delta > 0).sum()),
        differing_fraction=float((pixel_delta > 0).mean()) if pixel_delta.size else 0.0,
        pixels_over_two=int((pixel_delta > 2).sum()),
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("worker", "client", "runtime", "cache", "batches", "capture", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--env", action="append", default=[], help="KEY=VALUE for the worker")
    parser.add_argument(
        "--reference",
        action="store_true",
        help="also replay each batch's recorded per-draw packets and compare pixels with tolerance",
    )
    parser.add_argument(
        "--max-diff", type=int, default=2, help="largest tolerated channel difference"
    )
    parser.add_argument(
        "--max-fraction",
        type=float,
        default=0.01,
        help="largest tolerated differing pixel fraction",
    )
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--timeout-ms", type=int, default=90000)
    args = parser.parse_args()
    out = args.output
    out.mkdir(exist_ok=False)
    batches = rows(args.batches / "batches.jsonl")
    if args.limit:
        batches = batches[: args.limit]
    env = os.environ.copy()
    env["PATH"] = str(args.runtime) + os.pathsep + env.get("PATH", "")
    for key in (
        "NR_RENDER_CAPTURE_DIRECTORY",
        "NR_RENDER_AFFINE_BATCH",
        "NR_RENDER_INTERACTIVE_PIPE",
    ):
        env.pop(key, None)
    env.update(
        NR_RENDER_NPU_CACHE=str(args.cache),
        NR_RENDER_PIPELINE_CACHE="expanded",
        NR_RENDER_SHARED_CORE="1",
        NR_RENDER_SHARED_QUEUE="1",
        NR_RENDER_MEMORY_CACHE="1",
        NR_RENDER_MEMORY_CACHE_CAPACITY="64",
        NR_RENDER_COMPACT_COVERAGE="0",
        NR_RENDER_OPAQUE_CROP="1",
        NR_RENDER_PROVIDER_RECOVERY="0",
        NR_RENDER_FAILURE_PACKET=str(out / "failed-request.bin"),
    )
    for item in args.env:
        key, value = item.split("=", 1)
        env[key] = value
    report = dict(
        started=datetime.now(UTC).isoformat(),
        completed=False,
        passed=False,
        batches=len(batches),
        legacy_replayed=0,
        matched=0,
        within_tolerance=0,
        beyond_tolerance=[],
        escapes=0,
        reference_ms=0.0,
        mismatched=[],
        missing=[],
        commands=0,
        commands_fallback=0,
        kernel_requests=0,
        kernel_ms=0.0,
        host_ms=0.0,
        client_ms=0.0,
        fallback_routes={},
        desktop_changed=False,
        driver_installed=False,
    )

    def save():
        (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")

    save()
    worker = None
    try:
        pipe = r"\\.\pipe\npu-render-indexed-" + secrets.token_hex(16)
        with (
            (out / "worker.stdout.jsonl").open("xb") as stdout,
            (out / "worker.stderr.log").open("xb") as stderr,
        ):
            worker = subprocess.Popen(
                [
                    str(args.worker),
                    pipe,
                    str(
                        len(batches)
                        + (
                            sum(len(b["tickets"]) for b in batches if not b.get("legacy"))
                            if args.reference
                            else 0
                        )
                    ),
                    "1800",
                ],
                env=env,
                stdout=stdout,
                stderr=stderr,
                creationflags=subprocess.CREATE_NO_WINDOW,
            )
            report["worker_pid"] = worker.pid
            save()
            deadline = time.perf_counter() + 300
            while not any(x.get("local_worker_ready") for x in rows(out / "worker.stdout.jsonl")):
                assert worker.poll() is None and time.perf_counter() < deadline, "worker not ready"
                time.sleep(0.05)
            with (out / "replays.jsonl").open("a") as log:
                for n, entry in enumerate(batches):
                    legacy = bool(entry.get("legacy"))
                    packet_path = (args.capture if legacy else args.batches) / entry["file"]
                    packet = packet_path.read_bytes()
                    if legacy:
                        image, width, height = None, 0, 0
                    else:
                        image, width, height = background_of(packet)
                    began = time.perf_counter()
                    result = subprocess.run(
                        [
                            str(args.client),
                            pipe,
                            str(worker.pid),
                            str(packet_path),
                            str(out / "reply.bin"),
                            str(args.timeout_ms),
                            "success",
                        ],
                        env=env,
                        capture_output=True,
                        timeout=args.timeout_ms / 1000 + 30,
                    )
                    elapsed = (time.perf_counter() - began) * 1000
                    record = dict(
                        batch=entry.get("batch", -1),
                        legacy=legacy,
                        tickets=entry["tickets"],
                        client_exit=result.returncode,
                        client_ms=elapsed,
                    )
                    if result.returncode:
                        record["stderr"] = result.stderr.decode(errors="replace")[-400:]
                        log.write(json.dumps(record) + "\n")
                        raise AssertionError(
                            f"client failed at batch {entry.get('batch')} tickets {entry['tickets']}: "
                            + record["stderr"]
                        )
                    reply = (out / "reply.bin").read_bytes()
                    if legacy:
                        pixels = reply[HEADER.size + 48 :]
                        record["exact"] = (
                            hashlib.sha256(pixels).hexdigest() == entry["expected_sha256"]
                            and len(pixels) == entry["expected_bytes"]
                        )
                        report["legacy_replayed"] += 1
                        if record["exact"]:
                            report["matched"] += 1
                        else:
                            report["mismatched"].append(entry["tickets"])
                        report["client_ms"] += elapsed
                        log.write(json.dumps(record) + "\n")
                        (out / "reply.bin").unlink()
                        continue
                    response = apply_reply(reply, image, width, height)
                    record.update(response)
                    report["client_ms"] += elapsed
                    report["kernel_ms"] += response["kernel_ms"]
                    report["host_ms"] += response["host_ms"]
                    report["kernel_requests"] += response["provider_requests"]
                    report["commands"] += response["commands_executed"]
                    report["commands_fallback"] += response["commands_fallback"]
                    if response["missing_count"]:
                        report["missing"].append(entry["batch"])
                        record["exact"] = False
                    else:
                        digest = hashlib.sha256(bytes(image)).hexdigest()
                        record["exact"] = (
                            digest == entry["expected_sha256"]
                            and len(image) == entry["expected_bytes"]
                        )
                        record["sha256"] = digest
                        if record["exact"]:
                            report["matched"] += 1
                        else:
                            report["mismatched"].append(entry["batch"])
                    report["escapes"] += response["escapes"]
                    (out / "reply.bin").unlink()
                    if args.reference and not response["missing_count"]:
                        reference, reference_ms = replay_reference(
                            args, env, pipe, worker.pid, entry, out
                        )
                        report["reference_ms"] += reference_ms
                        record["reference_ms"] = reference_ms
                        if reference is not None and len(reference) == len(image):
                            diff = compare(bytes(image), reference)
                            record.update(diff)
                            ok = (
                                diff["max_diff"] <= args.max_diff
                                and diff["differing_fraction"] <= args.max_fraction
                            )
                            record["within_tolerance"] = ok
                            if ok:
                                report["within_tolerance"] += 1
                            else:
                                report["beyond_tolerance"].append(entry["batch"])
                                (out / f"batch-{entry['batch']:06d}-ours.rgba").write_bytes(
                                    bytes(image)
                                )
                                (out / f"batch-{entry['batch']:06d}-reference.rgba").write_bytes(
                                    reference
                                )
                    log.write(json.dumps(record) + "\n")
                    if n % 50 == 0:
                        save()
                        print(
                            json.dumps(dict(replayed=n + 1, matched=report["matched"])), flush=True
                        )
            worker.wait(timeout=30)
            report["worker_exit"] = worker.returncode
        batch_count = sum(1 for b in batches if not b.get("legacy"))
        report["passed"] = (
            report["matched"] == len(batches)
            and not report["missing"]
            and report["worker_exit"] == 0
        )
        if args.reference:
            report["passed"] = (
                report["within_tolerance"] == batch_count
                and not report["missing"]
                and report["worker_exit"] == 0
                and report["legacy_replayed"] + batch_count == len(batches)
            )
    except Exception as error:  # noqa: BLE001
        report.update(error=repr(error), traceback=traceback.format_exc())
    finally:
        try:
            for line in rows(out / "worker.stdout.jsonl"):
                if line.get("frame_batch_fallback_begin"):
                    route = line.get("route", "?")
                    report["fallback_routes"][route] = report["fallback_routes"].get(route, 0) + 1
        except OSError:
            pass
        if worker is not None and worker.poll() is None:
            worker.kill()
            worker.wait(timeout=5)
            report["owned_worker_terminated"] = True
        report.update(completed=True, finished=datetime.now(UTC).isoformat())
        save()
    print(json.dumps({k: v for k, v in report.items() if k != "traceback"}))
    raise SystemExit(0 if report["passed"] else 1)


if __name__ == "__main__":
    main()
