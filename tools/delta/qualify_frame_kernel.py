"""Execute frame-kernel fixtures on the NPU and compare with the host reference.

Fixtures are produced on any machine by nr_frame_kernel_test --fixtures. Each
record holds one input span and its expected output span. The kernel is patched
into the pinned twelve-row carrier; original, custom and original phases run.
Timings include host tensor copies and are diagnostic only.
"""

import argparse
import hashlib
import json
import os
import struct
import sys
import time
import traceback
from datetime import UTC, datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ptl_frame_carrier  # noqa: E402
import ptl_twelve_row_carrier  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
from npu_render.ptl_carrier import read_kernel_text  # noqa: E402

RUNTIME = "2026.4.1-22982-e213a147257-releases/2026/4"
ROWS = 12
WIDTH = 132096
HEADER = struct.Struct("<8sIIQ")  # magic, version, record count, span bytes


def write(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def load_fixtures(path):
    data = path.read_bytes()
    magic, version, count, span = HEADER.unpack_from(data)
    assert magic == b"NRFFIX01" and version == 1 and span == WIDTH * 2 and 0 < count <= 4096
    records = []
    at = HEADER.size
    for _ in range(count):
        (name_bytes,) = struct.unpack_from("<I", data, at)
        at += 4
        name = data[at : at + name_bytes].decode()
        at += name_bytes
        at = (at + 3) & ~3
        inputs = data[at : at + span]
        at += span
        expected = data[at : at + span]
        at += span
        records.append((name, inputs, expected))
    assert at == len(data)
    return records


def compare(name, expected, actual):
    tw, th = struct.unpack_from("<II", expected, 16)
    live = 64 + tw * th * 4
    if expected[:live] == actual[:live]:
        return None
    first = next(i for i in range(live) if expected[i] != actual[i])
    return dict(
        fixture=name,
        first_difference=first,
        expected=list(expected[first : first + 16]),
        actual=list(actual[first : first + 16]),
        expected_status=struct.unpack_from("<I", expected, 4)[0],
        actual_status=struct.unpack_from("<I", actual, 4)[0],
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--carrier", type=Path, required=True)
    parser.add_argument("--kernel-elf", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeat", type=int, default=1, help="timing repeats of the whole set")
    parser.add_argument(
        "--only", action="append", default=[], help="fixture name prefix filter (repeatable)"
    )
    parser.add_argument("--deadline-ms", type=int, default=10000)
    parser.add_argument(
        "--skip-original", action="store_true", help="omit the original Softmax phases"
    )
    parser.add_argument("--timing-only", action="store_true", help="skip output comparison")
    parser.add_argument("--carrier-kind", choices=["softmax", "scatter"], default="softmax")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    report = dict(
        started=datetime.now(UTC).isoformat(),
        completed=False,
        passed=False,
        desktop_changed=False,
        tahoma_tested=False,
        phases=[],
        mismatches=[],
    )
    try:
        import numpy as np
        import openvino as ov

        if ov.get_version() != RUNTIME:
            raise RuntimeError("This carrier requires its pinned OpenVINO runtime")
        carrier_module = (
            ptl_frame_carrier if args.carrier_kind == "scatter" else ptl_twelve_row_carrier
        )
        SPAN = carrier_module.SPAN
        original = args.carrier.read_bytes()
        assert hashlib.sha256(original).hexdigest() == carrier_module.ORIGINAL_SHA, "carrier pin"
        elf = args.kernel_elf.read_bytes()
        text = read_kernel_text(elf)
        patched, proof = carrier_module.patch(original, text)
        (args.output / "patched.blob").write_bytes(patched)
        write(args.output / "contract.json", proof)
        report.update(
            kernel_elf_sha256=hashlib.sha256(elf).hexdigest(),
            kernel_text_bytes=len(text),
            patched_sha256=hashlib.sha256(patched).hexdigest(),
        )
        records = load_fixtures(args.fixtures)
        if args.only:
            records = [r for r in records if any(r[0].startswith(prefix) for prefix in args.only)]
        report["fixtures"] = len(records)
        report["fixture_names"] = [r[0] for r in records]
        if not records:
            raise RuntimeError("no fixtures selected")
        report["fixtures_sha256"] = hashlib.sha256(args.fixtures.read_bytes()).hexdigest()
        core = ov.Core()
        if "NPU" not in core.available_devices:
            raise RuntimeError("NPU is unavailable")
        phases = (
            ("original-before", original),
            ("custom-frame", patched),
            ("original-after", original),
        )
        if args.skip_original:
            phases = (("custom-frame", patched),)
        for phase, blob in phases:
            model = core.import_model(blob, "NPU", {"NPU_TURBO": True})
            assert model.get_property("NPU_TURBO") is True
            raw = model.get_property("EXECUTION_DEVICES")
            devices = [raw] if isinstance(raw, str) else list(raw)
            assert devices in (["NPU"], ["NPU.0"]), devices
            request = model.create_infer_request()
            timings = []
            if phase != "custom-frame" and args.carrier_kind == "scatter":
                rng = np.random.default_rng(4242)
                for _ in range(4):
                    values = rng.uniform(-8, 8, (ROWS, WIDTH)).astype(np.float16)
                    upd = rng.uniform(-8, 8, (ROWS, 16)).astype(np.float16)
                    expected = values.copy()
                    for row in range(ROWS):
                        expected[row, np.arange(16) * 97] = upd[row]
                    request.start_async({0: values, 1: upd})
                    assert request.wait_for(10000)
                    actual = request.get_output_tensor().data.copy()
                    if not np.array_equal(actual.view(np.uint16), expected.view(np.uint16)):
                        raise AssertionError("original scatter oracle failed in " + phase)
                report["phases"].append(dict(phase=phase, passed=True, checks=4))
                del request, model
                continue
            if phase != "custom-frame":
                rng = np.random.default_rng(321)
                for i in range(4):
                    data = np.full((ROWS, SPAN // 2), -np.inf, np.float16)
                    target = np.zeros_like(data)
                    active = 1024 * (2 ** (i % 4))
                    for b in range(ROWS):
                        at = rng.choice(SPAN // 2, active, replace=False)
                        data[b, at] = np.float16(i % 4)
                        target[b, at] = np.float16(1 / active)
                    request.start_async({0: data})
                    assert request.wait_for(10000)
                    actual = request.get_output_tensor().data.copy()
                    if not np.array_equal(actual.view(np.uint16), target.view(np.uint16)):
                        raise AssertionError("original Softmax oracle failed in " + phase)
                report["phases"].append(dict(phase=phase, passed=True, checks=4))
                del request, model
                continue
            compared = 0
            for repeat in range(args.repeat):
                for first in range(0, len(records), ROWS):
                    group = records[first : first + ROWS]
                    data = np.zeros((ROWS, SPAN), np.uint8)
                    for row, (_, inputs, _) in enumerate(group):
                        data[row] = np.frombuffer(inputs, np.uint8)
                    tensor = data.view(np.float16)
                    began = time.perf_counter_ns()
                    if args.carrier_kind == "scatter":
                        request.start_async({0: tensor, 1: np.zeros((ROWS, 16), np.float16)})
                    else:
                        request.start_async({0: tensor})
                    if not request.wait_for(args.deadline_ms):
                        report["deadline_group"] = dict(first=first, names=[g[0] for g in group])
                        raise AssertionError("NPU inference deadline")
                    output = request.get_output_tensor().data.copy().view(np.uint8)
                    elapsed = (time.perf_counter_ns() - began) / 1e6
                    timings.append(
                        dict(repeat=repeat, first=first, rows=len(group), request_ms=elapsed)
                    )
                    if repeat:
                        continue
                    for row, (name, _, expected) in enumerate(group):
                        if args.timing_only:
                            continue
                        if args.timing_only:
                            continue
                        actual = output[row].tobytes()
                        difference = compare(name, expected, actual)
                        compared += 1
                        if difference:
                            report["mismatches"].append(difference)
                            if len(report["mismatches"]) >= 32:
                                raise AssertionError("too many mismatches")
            report["phases"].append(
                dict(
                    phase=phase, passed=not report["mismatches"], compared=compared, timings=timings
                )
            )
            del request, model
        report["passed"] = all(p["passed"] for p in report["phases"]) and len(
            report["phases"]
        ) == len(phases)
    except Exception as error:
        report.update(error=repr(error), traceback=traceback.format_exc())
    finally:
        report.update(completed=True, finished=datetime.now(UTC).isoformat())
        write(args.output / "report.json", report)
    summary = {k: v for k, v in report.items() if k != "phases"}
    summary["phase_summary"] = [
        {k: v for k, v in p.items() if k != "timings"}
        | (
            {
                "median_request_ms": sorted(t["request_ms"] for t in p["timings"])[
                    len(p["timings"]) // 2
                ]
            }
            if p.get("timings")
            else {}
        )
        for p in report["phases"]
    ]
    print(json.dumps(summary))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    os.environ.setdefault("PYTHONIOENCODING", "utf-8")
    raise SystemExit(main())
