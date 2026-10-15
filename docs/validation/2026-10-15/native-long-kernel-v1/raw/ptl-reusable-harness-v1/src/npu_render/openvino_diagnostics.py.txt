"""OpenVINO-provider diagnostics, independent of installed driver-package metadata."""

import openvino as ov


def inspect_stack(core=None) -> dict:
    core = ov.Core() if core is None else core
    report = {"runtime": ov.get_version(), "devices": []}
    for device in core.available_devices:
        row = {"id": device, "properties": {}, "errors": {}}
        try:
            supported = {str(key) for key in core.get_property(device, "SUPPORTED_PROPERTIES")}
            keys = {"FULL_DEVICE_NAME", "DEVICE_ARCHITECTURE"}
            if device.split(".")[0] == "NPU":
                keys |= {"NPU_DRIVER_VERSION", "NPU_COMPILER_TYPE", "NPU_COMPILER_VERSION"}
            for key in sorted(keys):
                if key not in supported:
                    row["errors"][key] = "not advertised by the loaded plugin"
                    continue
                try:
                    row["properties"][key] = str(core.get_property(device, key))
                except RuntimeError as error:
                    row["errors"][key] = str(error)
            row["plugins"] = {
                name: {"build": version.build_number, "description": version.description}
                for name, version in core.get_versions(device).items()
            }
        except RuntimeError as error:
            row["errors"]["inspection"] = str(error)
        report["devices"].append(row)
    report["scope"] = (
        "Loaded runtime properties; installed-package versions, pixel correctness and "
        "presentation are separate checks."
    )
    return report


def require_npu_driver(report: dict, expected: str) -> None:
    """Match the runtime's own version spelling; do not infer it from an INF file."""
    rows = [row for row in report["devices"] if row["id"].split(".")[0] == "NPU"]
    if not rows:
        raise RuntimeError("no NPU was reported by the loaded runtime")
    versions = [row["properties"].get("NPU_DRIVER_VERSION") for row in rows]
    if any(version != expected for version in versions):
        raise RuntimeError(f"expected loaded NPU driver {expected}, runtime reported {versions}")
