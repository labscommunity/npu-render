import hashlib
import json
import sys
import time
from datetime import UTC, datetime
from pathlib import Path

sys.path.insert(0, str(Path.cwd()))
from npu_render.indirect_frame import confirmed_sole_display, parse_frame
from tools.view_lab_desktop import LabInput

root = Path("results/render-driver")
out = root / "input-v95"
out.mkdir(exist_ok=False)
source = root / "collector-v95-live/latest.bin"
gate = source.parent / "input-ready.json"
control = LabInput(66589, 49400)
report = {
    "started": datetime.now(UTC).isoformat(),
    "actions": [],
    "snapshots": [],
    "visual_response_verified": False,
    "sole_gate_observed": False,
}


def persist():
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")


def snapshot(name):
    if source.exists():
        data = source.read_bytes()
        frame = parse_frame(data)
        (out / (name + ".bin")).write_bytes(data)
        report["snapshots"].append(
            {
                "name": name,
                "captured_at": datetime.now(UTC).isoformat(),
                "sole_indirect_at_snapshot": confirmed_sole_display(gate),
                "identity": frame.identity,
                "pixel_sha256": hashlib.sha256(frame.pixels).hexdigest(),
            }
        )


try:
    deadline = time.monotonic() + 480
    woke = False
    while time.monotonic() < deadline:
        if confirmed_sole_display(gate):
            report["sole_gate_observed"] = True
            if not woke:
                control.key("ctrl", True)
                time.sleep(0.2)
                control.key("ctrl", False)
                woke = True
                report["actions"].append(
                    {"action": "wake-control", "at": datetime.now(UTC).isoformat()}
                )
            if not source.exists():
                time.sleep(0.5)
                continue
            candidate = parse_frame(source.read_bytes())
            receipts = []
            for line in (source.parent / "frames.jsonl").read_text().splitlines():
                try:
                    receipts.append(json.loads(line))
                except json.JSONDecodeError:
                    pass
            if not any(
                tuple(r["identity"]) == candidate.identity and r["sole_indirect_at_receive"]
                for r in receipts
            ):
                time.sleep(0.5)
                continue
            # Wait for nonuniform visible content; a black wallpaper must not block input testing.
            if (
                len(
                    {
                        bytes(candidate.pixels[n : n + 3])
                        for n in range(0, len(candidate.pixels), 64)
                    }
                )
                < 16
            ):
                time.sleep(0.5)
                continue
            # This VM's light taskbar has a mostly bright neutral bottom band.
            # Colored wallpaper alone must not qualify as a rendered taskbar.
            band = candidate.pixels[(candidate.height - 32) * candidate.width * 4 :]
            neutral = sum(
                max(band[n : n + 3]) - min(band[n : n + 3]) < 12 and min(band[n : n + 3]) > 170
                for n in range(0, len(band), 16)
            )
            if neutral < 0.7 * (len(band) // 16):
                time.sleep(0.5)
                continue
            snapshot("before-start")
            control.key("meta_l", True)
            time.sleep(0.2)
            control.key("meta_l", False)
            report["actions"].append({"action": "open-start", "at": datetime.now(UTC).isoformat()})
            persist()
            time.sleep(45)
            snapshot("after-start")
            persist()
            if confirmed_sole_display(gate):
                control.key("esc", True)
                time.sleep(0.2)
                control.key("esc", False)
                report["actions"].append({"action": "escape", "at": datetime.now(UTC).isoformat()})
                persist()
                time.sleep(45)
                snapshot("after-escape")
                persist()
            break
        elif report["sole_gate_observed"]:
            break
        time.sleep(0.5)
finally:
    control.release()
    report["finished"] = datetime.now(UTC).isoformat()
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))
