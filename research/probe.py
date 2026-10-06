#!/usr/bin/env python3
"""Deterministic graphics as OpenVINO graphs; no trained model or GPU API.

CPU validates graph semantics only. NPU compilation/execution must be tested on
the target. This is a probe, not a compositor, renderer driver, or NPU benchmark
claim. See docs/perf/NPU_GRAPHICS_FIRST_PRINCIPLES.md.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import time
from pathlib import Path

import numpy as np
import openvino as ov
from openvino import opset13 as op


def parameter(name, shape, dtype):
    p = op.parameter(shape, dtype=dtype, name=name)
    p.output(0).get_tensor().set_names({name})
    return p


def composite_graph(height, width, layers, dtype, layout="NCHW"):
    """Premultiplied source-over, in the caller's specified color space."""
    shape = [1, 4, height, width] if layout == "NCHW" else [1, height, width, 4]
    channel_axis = 1 if layout == "NCHW" else 3
    params = [parameter("background", shape, dtype)]
    result = params[0]
    for i in range(layers):
        foreground = parameter(f"layer_{i}", shape, dtype)
        alpha = op.gather(foreground, op.constant([3], np.int64), op.constant(channel_axis, np.int64))
        result = op.add(foreground, op.multiply(op.subtract(op.constant(1, dtype), alpha), result))
        params.append(foreground)
    return ov.Model([result], params, "deterministic_source_over")


def composite_cases(height, width, layers, dtype):
    rng = np.random.default_rng(20261005)
    background = rng.random((1, 4, height, width)).astype(dtype)
    background[:, 3] = 1
    inputs = {"background": background}
    for i in range(layers):
        rgba = rng.random((1, 4, height, width))
        rgba[:, :3] *= rgba[:, 3:4]
        inputs[f"layer_{i}"] = rgba.astype(dtype)
    random_case = {k: v.copy() for k, v in inputs.items()}
    transparent = {k: v.copy() if k == "background" else np.zeros_like(v) for k, v in inputs.items()}
    opaque = {k: v.copy() for k, v in inputs.items()}
    opaque[f"layer_{layers - 1}"][:, 3] = 1
    return [("fractional_alpha", random_case), ("all_transparent", transparent), ("opaque_front", opaque)]


def composite_reference(inputs, layers):
    # Closed-form visibility weights, independently of the graph's recurrence.
    transmittance = np.ones_like(inputs["background"][:, 3:4], dtype=np.float64)
    expected = np.zeros_like(inputs["background"], dtype=np.float64)
    for i in reversed(range(layers)):
        rgba = inputs[f"layer_{i}"].astype(np.float64)
        expected += rgba * transmittance
        transmittance *= 1 - rgba[:, 3:4]
    return expected + inputs["background"].astype(np.float64) * transmittance


def triangle_graph(height, width, dtype):
    """Three opaque, flat-color, constant-depth triangles; inclusive edges.

    Does not implement the GPU top-left fill convention, textures, perspective,
    arbitrary shaders, or tiled binning. Distinct triangle depths are required.
    """
    count = 3
    y, x = np.mgrid[:height, :width]
    xy1 = np.stack(((x.ravel() + 0.5) / width, (y.ravel() + 0.5) / height, np.ones(height * width)), axis=1)
    edges = parameter("edges", [3, 3 * count], dtype)
    depths = parameter("depths", [1, count], dtype)
    colors = parameter("colors", [count, 3], dtype)
    background = parameter("background", [1, 3], dtype)
    distances = op.reshape(op.matmul(op.constant(xy1.astype(dtype)), edges, False, False),
                           op.constant([height * width, count, 3], np.int64), False)
    covered = op.greater_equal(op.reduce_min(distances, op.constant([2], np.int64), False), op.constant(0, dtype))
    candidate_depths = op.select(covered, depths, op.constant(2, dtype))
    nearest = op.reduce_min(candidate_depths, op.constant([1], np.int64), True)
    winner = op.logical_and(covered, op.equal(candidate_depths, nearest))
    rgb = op.matmul(op.convert(winner, ov.Type.f16 if dtype == np.float16 else ov.Type.f32), colors, False, False)
    rgb = op.add(rgb, op.multiply(op.convert(op.equal(nearest, op.constant(2, dtype)),
                                           ov.Type.f16 if dtype == np.float16 else ov.Type.f32), background))
    rgb = op.reshape(rgb, op.constant([1, height, width, 3], np.int64), False)
    rgb = op.transpose(rgb, op.constant([0, 3, 1, 2], np.int64))
    return ov.Model([rgb], [edges, depths, colors, background], "deterministic_triangles")


def triangle_inputs(vertices, depths, dtype):
    coefficients = []
    for v in vertices:
        area = np.linalg.det(np.stack((v[1] - v[0], v[2] - v[0])))
        assert area != 0
        sign = 1 if area > 0 else -1
        for a, b in zip(v, np.roll(v, -1, axis=0)):
            coefficients.append(sign * np.array([a[1] - b[1], b[0] - a[0], a[0] * b[1] - b[0] * a[1]]))
    return {"edges": np.stack(coefficients, axis=1).astype(dtype),
            "depths": np.array([depths], dtype=dtype),
            "colors": np.array([[0.875, 0.125, 0.25], [0.125, 0.75, 0.25], [0.125, 0.25, 0.875]], dtype=dtype),
            "background": np.array([[0.03125, 0.0625, 0.125]], dtype=dtype)}


def triangle_cases(dtype):
    vertices = np.array([[[0.125, 0.125], [0.875, 0.125], [0.5, 0.875]],
                         [[0.25, 0.25], [0.75, 0.25], [0.5, 0.75]],
                         [[0.125, 0.75], [0.875, 0.75], [0.5, 0.25]]])
    return [("overlapping", vertices, triangle_inputs(vertices, [0.75, 0.5, 0.25], dtype)),
            ("reversed_depth", vertices, triangle_inputs(vertices, [0.25, 0.5, 0.75], dtype)),
            ("offscreen", vertices + 2, triangle_inputs(vertices + 2, [0.75, 0.5, 0.25], dtype))]


def triangle_reference(height, width, vertices, inputs):
    # Barycentric linear solve; does not use the graph's supplied edge equations.
    y, x = np.mgrid[:height, :width]
    points = np.stack(((x.ravel() + 0.5) / width, (y.ravel() + 0.5) / height), axis=1)
    rgb = np.broadcast_to(inputs["background"].astype(np.float64), (height * width, 3)).copy()
    zbuffer = np.full(height * width, 2.0)
    for i, v in enumerate(vertices):
        uv = np.linalg.solve(np.stack((v[1] - v[0], v[2] - v[0]), axis=1), (points - v[0]).T).T
        inside = (uv[:, 0] >= -1e-12) & (uv[:, 1] >= -1e-12) & (uv.sum(axis=1) <= 1 + 1e-12)
        visible = inside & (float(inputs["depths"][0, i]) < zbuffer)
        rgb[visible] = inputs["colors"][i]
        zbuffer[visible] = inputs["depths"][0, i]
    return rgb.reshape(1, height, width, 3).transpose(0, 3, 1, 2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", required=True, help="Explicit CPU, GPU[.N], or NPU[.N]; no AUTO/fallback")
    parser.add_argument("--scene", choices=["all", "composite", "triangles"], default="all")
    parser.add_argument("--precision", choices=["f32", "f16"], default="f32")
    parser.add_argument("--layout", choices=["NCHW", "NHWC"], default="NCHW",
                        help="Composition input/output layout; triangle probe uses NCHW")
    parser.add_argument("--width", type=int, default=96)
    parser.add_argument("--height", type=int, default=64)
    parser.add_argument("--layers", type=int, default=4)
    parser.add_argument("--iterations", type=int, default=20)
    parser.add_argument("--share-host-tensors", action="store_true",
                        help="Ask Python bindings to share NumPy buffers; does not prove device zero-copy")
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--export-dir", type=Path)
    args = parser.parse_args()
    if args.device.split(".")[0] not in {"CPU", "GPU", "NPU"} or ":" in args.device:
        parser.error("choose one explicit device; AUTO/HETERO are intentionally excluded")
    if min(args.width, args.height, args.layers, args.iterations) < 1:
        parser.error("dimensions, layers, and iterations must be positive")
    if args.layout == "NHWC" and args.scene != "composite":
        parser.error("NHWC currently requires --scene composite")
    dtype = np.float16 if args.precision == "f16" else np.float32
    core = ov.Core()
    report = {"schema_version": 1, "openvino": ov.__version__, "os": platform.platform(),
              "device_requested": args.device, "devices_available": core.available_devices,
              "precision": args.precision, "layout": args.layout, "width": args.width, "height": args.height,
              "layers": args.layers, "iterations": args.iterations,
              "share_host_tensors": args.share_host_tensors,
              "timing_scope": ("synchronous host inference call; Python buffer sharing requested; device copies may remain"
                               if args.share_host_tensors else
                               "synchronous host inference call including default input/output copying")
                              + "; excludes frame preparation and display",
              "hardware_telemetry": "not collected", "scenes": []}
    scenes = ["composite", "triangles"] if args.scene == "all" else [args.scene]
    passed = True
    for name in scenes:
        record = {"scene": name, "status": "not_run"}
        report["scenes"].append(record)
        try:
            if name == "composite":
                model = composite_graph(args.height, args.width, args.layers, dtype, args.layout)
                cases = [(label, values, composite_reference(values, args.layers))
                         for label, values in composite_cases(args.height, args.width, args.layers, dtype)]
                if args.layout == "NHWC":
                    cases = [(label, {k: np.ascontiguousarray(v.transpose(0, 2, 3, 1)) for k, v in values.items()},
                              expected.transpose(0, 2, 3, 1)) for label, values, expected in cases]
            else:
                model = triangle_graph(args.height, args.width, dtype)
                cases = [(label, values, triangle_reference(args.height, args.width, vertices, values))
                         for label, vertices, values in triangle_cases(dtype)]
            if args.export_dir:
                args.export_dir.mkdir(parents=True, exist_ok=True)
                xml = args.export_dir / f"{name}_{args.precision}_{args.layout}.xml"
                ov.save_model(model, xml, compress_to_fp16=False)
                record["export_sha256"] = {p.suffix: hashlib.sha256(p.read_bytes()).hexdigest()
                                           for p in [xml, xml.with_suffix(".bin")]}
            try:
                supported = core.query_model(model, args.device)
                record["query_unsupported_ops"] = [n.get_friendly_name() for n in model.get_ordered_ops()
                                                   if n.get_friendly_name() not in supported]
            except Exception as error:
                record["query_error"] = str(error)
            start = time.perf_counter()
            config = {"PERF_COUNT": True}
            if args.device.split(".")[0] in {"CPU", "GPU"}:
                config["INFERENCE_PRECISION_HINT"] = args.precision
            record["compile_config"] = config
            compiled = core.compile_model(model, args.device, config)
            record["compile_ms"] = (time.perf_counter() - start) * 1000
            execution_devices = compiled.get_property("EXECUTION_DEVICES")
            # Some NPU plugin versions return a string; CPU/GPU return a list.
            record["execution_devices"] = ([execution_devices] if isinstance(execution_devices, str)
                                           else list(execution_devices))
            if not record["execution_devices"] or any(
                    device.split(".")[0] != args.device.split(".")[0] for device in record["execution_devices"]):
                raise RuntimeError("runtime reported a different execution device")
            record["device_full_name"] = str(core.get_property(args.device, "FULL_DEVICE_NAME"))
            request = compiled.create_infer_request()
            record["correctness"] = []
            # Float16 has a 2/255 budget for this probe. This is not a display
            # conformance tolerance; whole-color coverage errors must still fail.
            tolerance = 2 / 255 if dtype == np.float16 else 1e-5
            for label, values, expected in cases:
                result = next(iter(request.infer(values, share_inputs=args.share_host_tensors,
                                                share_outputs=args.share_host_tensors).values()))
                error = np.abs(result.astype(np.float64) - expected)
                case_passed = bool(np.isfinite(result).all() and np.max(error) <= tolerance)
                record["correctness"].append({"case": label, "pass": case_passed,
                                               "max_abs_error": float(np.max(error)),
                                               "channels_outside_tolerance": int(np.count_nonzero(error > tolerance)),
                                               "tolerance": tolerance})
            record["status"] = "pass" if all(c["pass"] for c in record["correctness"]) else "correctness_failed"
            values = cases[0][1]
            for _ in range(3):
                request.infer(values, share_inputs=args.share_host_tensors, share_outputs=args.share_host_tensors)
            times = []
            for _ in range(args.iterations):
                start = time.perf_counter()
                request.infer(values, share_inputs=args.share_host_tensors, share_outputs=args.share_host_tensors)
                times.append((time.perf_counter() - start) * 1000)
            record["host_call_ms"] = {"median": float(np.median(times)), "p95": float(np.percentile(times, 95)),
                                       "min": min(times), "samples": times}
            record["profiling"] = [{"node": item.node_name, "type": item.node_type,
                                     "execution_type": item.exec_type, "status": str(item.status),
                                     "real_us": item.real_time.total_seconds() * 1e6}
                                    for item in request.get_profiling_info()]
        except Exception as error:
            record["status"] = "error"
            record["error"] = str(error)
        passed &= record["status"] == "pass"
        print(name, record["status"], record.get("host_call_ms", {}).get("median", ""))
    report["pass"] = passed
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
