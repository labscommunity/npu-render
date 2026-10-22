"""Create the ScatterElementsUpdate twelve-row carrier whose code window the
frame kernel replaces.

The op has a larger SHAVE code window than Softmax (about 24 KiB) with the same
twelve 258 KiB rows on six engines. The original op is checked against numpy so
the original/custom/original phases can still validate the carrier. Nothing is
patched here and the desktop is untouched.
"""

import argparse
import hashlib
import json
import struct
import sys
import time
import traceback
from datetime import UTC, datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ptl_elf import Elf  # noqa: E402

ROWS, WIDTH, UPDATES = 12, 132096, 16


def describe_twelve_rows(elf, tensors):
    """Two sequential tasks per engine: 80-byte range records and 192-byte invocation records."""
    params = elf.by_name["shave.params"]
    relocations = elf.relocations(params)
    invocations = []
    for s in elf.sections:
        if not s["name"].startswith("task.shave.invocation."):
            continue
        assert s["size"] % 96 == 0, "invocation record size"
        refs = elf.relocations(s)
        for row in range(s["size"] // 96):
            base = refs[row * 96 + 8]["addend"]
            tile = struct.unpack_from("<I", s["data"], row * 96 + 84)[0]
            record = dict(name=s["name"], row=row, tile=tile, parameter_offset=base, tensors=[])
            for n in range(tensors):
                at = base + n * 40
                address, static, rank, dp, sp, dtype, order, location = struct.unpack_from(
                    "<QIIIIIQI", params["data"], at
                )
                dims = []
                for field, fmt in ((16, "<4i"), (20, "<4q")):
                    ref = relocations.get(at + field)
                    dims.append(
                        list(struct.unpack_from(fmt, params["data"], ref["addend"]))
                        if ref
                        else None
                    )
                record["tensors"].append(
                    dict(
                        address=address,
                        rank=rank,
                        dtype=dtype,
                        order=order,
                        location=location,
                        dims=dims[0],
                        strides=dims[1],
                    )
                )
            invocations.append(record)
    return dict(blob_sha256=hashlib.sha256(elf.data).hexdigest(), invocations=invocations)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    report = dict(
        started=datetime.now(UTC).isoformat(),
        completed=False,
        passed=False,
        custom_kernel_executed=False,
    )
    try:
        import numpy as np
        import openvino as ov
        from openvino import opset13 as op

        core = ov.Core()
        data = op.parameter([ROWS, WIDTH], ov.Type.f16, name="data")
        updates = op.parameter([ROWS, UPDATES], ov.Type.f16, name="updates")
        indices = op.constant(np.tile(np.arange(UPDATES, dtype=np.int32) * 97, (ROWS, 1)))
        result = op.scatter_elements_update(
            data, indices, updates, op.constant(np.array(1, np.int64))
        )
        config = dict(PERFORMANCE_HINT="LATENCY", NPU_COMPILATION_MODE="DefaultHW", NPU_TILES=3)
        began = time.perf_counter_ns()
        model = core.compile_model(ov.Model([result], [data, updates]), "NPU", config)
        report["compile_ms"] = (time.perf_counter_ns() - began) / 1e6
        devices = model.get_property("EXECUTION_DEVICES")
        devices = [devices] if isinstance(devices, str) else list(devices)
        assert devices in (["NPU"], ["NPU.0"]), devices
        blob = model.export_model().getvalue()
        (args.output / "carrier.blob").write_bytes(blob)
        report.update(
            blob_sha256=hashlib.sha256(blob).hexdigest(),
            blob_bytes=len(blob),
            execution_devices=devices,
        )
        elf = Elf(blob)
        layout = describe_twelve_rows(elf, tensors=4)
        report["shave_text_bytes"] = elf.by_name["shave.text"]["size"]
        report["shave_params_bytes"] = elf.by_name["shave.params"]["size"]
        report["shave_data_bytes"] = elf.by_name["shave.data"]["size"]
        ranges = [s for s in elf.sections if s["name"].startswith("task.shave.range")]
        extents = []
        for s in ranges:
            for at in range(0, s["size"], 40):
                kind, entry, base, size, dep, count, pad = struct.unpack_from(
                    "<B7xQQIIII", s["data"], at
                )
                extents.append(
                    dict(section=s["name"], kind=kind, entry=hex(entry), code_bytes=size)
                )
        report["range_extents"] = extents
        (args.output / "shave-layout.json").write_text(json.dumps(layout, indent=2) + "\n")
        request = model.create_infer_request()
        rng = np.random.default_rng(4242)
        checks = []
        for i in range(6):
            values = rng.uniform(-8, 8, (ROWS, WIDTH)).astype(np.float16)
            upd = rng.uniform(-8, 8, (ROWS, UPDATES)).astype(np.float16)
            expected = values.copy()
            for row in range(ROWS):
                expected[row, np.arange(UPDATES) * 97] = upd[row]
            began = time.perf_counter_ns()
            request.start_async({0: values, 1: upd})
            assert request.wait_for(10000)
            actual = request.get_output_tensor().data.copy()
            elapsed = (time.perf_counter_ns() - began) / 1e6
            assert np.array_equal(actual.view(np.uint16), expected.view(np.uint16)), (
                "scatter oracle"
            )
            checks.append(
                dict(iteration=i, request_ms=elapsed, exact_components=int(expected.size))
            )
        report["checks"] = checks
        report["passed"] = True
    except Exception as error:
        report.update(error=repr(error), traceback=traceback.format_exc())
    finally:
        report.update(completed=True, finished=datetime.now(UTC).isoformat())
        (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k not in ("checks", "range_extents")}))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
