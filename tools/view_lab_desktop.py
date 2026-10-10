"""Show received IDD frames in an SDL software window; optional owned-VM input.

The host's window compositor and scanout still present this window. A submitted
SDL frame is not proof that a physical display showed it. Input uses the existing
local QEMU monitor and is intended for a verified sole-indirect-display trial.
"""

import argparse
import json
import os
import socket
import subprocess
import time
from pathlib import Path

import numpy as np

from npu_render.indirect_frame import MAX_BYTES, parse_frame
from npu_render.presentation import WindowPresenter


class LabInput:
    def __init__(self, pid, port):
        if pid <= 0 or not 1 <= port <= 65535:
            raise ValueError("expected positive VM PID and valid QMP port")
        self.pid, self.port = pid, port
        self.identity = self.process_identity()
        self.held_keys, self.held_buttons = set(), set()

    def process_identity(self):
        result = subprocess.run(
            ["ps", "-p", str(self.pid), "-o", "lstart=,command="],
            check=True,
            capture_output=True,
            text=True,
            timeout=3,
        ).stdout.strip()
        if "qemu-system-x86_64" not in result or f"127.0.0.1:{self.port}" not in result:
            raise ValueError("QMP endpoint must belong to the supplied local QEMU process")
        if "nbd://127.0.0.1:" not in result:
            raise ValueError("expected the disposable network-backed lab VM")
        return result

    def send(self, events):
        if self.process_identity() != self.identity:
            raise RuntimeError("VM process identity changed")
        with socket.create_connection(("127.0.0.1", self.port), timeout=3) as connection:
            reader = connection.makefile("rb")
            greeting = json.loads(reader.readline(65536))
            if "QMP" not in greeting:
                raise ValueError("unexpected monitor protocol")
            for request in (
                {"execute": "qmp_capabilities"},
                {"execute": "input-send-event", "arguments": {"events": events}},
            ):
                connection.sendall(json.dumps(request).encode() + b"\n")
                for _ in range(64):
                    reply = json.loads(reader.readline(65536))
                    if "error" in reply:
                        raise RuntimeError(str(reply["error"]))
                    if "return" in reply:
                        break
                else:
                    raise RuntimeError("monitor response missing")

    def key(self, code, down):
        if (code in self.held_keys) == down:
            return
        # Track before transmission, so cleanup also releases an ambiguously
        # acknowledged press if the monitor connection drops after delivery.
        if down:
            self.held_keys.add(code)
        self.send([{"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": code}}}])
        if not down:
            self.held_keys.discard(code)

    def pointer(self, x, y, width, height, button=None, down=False):
        events = [
            {"type": "abs", "data": {"axis": "x", "value": round(x * 32767 / max(1, width - 1))}},
            {"type": "abs", "data": {"axis": "y", "value": round(y * 32767 / max(1, height - 1))}},
        ]
        if button:
            if down:
                self.held_buttons.add(button)
            events.append({"type": "btn", "data": {"button": button, "down": down}})
        self.send(events)
        if button and not down:
            self.held_buttons.discard(button)

    def release(self):
        for code in tuple(self.held_keys):
            self.key(code, False)
        for button in tuple(self.held_buttons):
            self.send([{"type": "btn", "data": {"button": button, "down": False}}])
            self.held_buttons.discard(button)


KEYS = {
    "return": "ret",
    "escape": "esc",
    "backspace": "backspace",
    "tab": "tab",
    "space": "spc",
    "left ctrl": "ctrl",
    "right ctrl": "ctrl_r",
    "left shift": "shift",
    "right shift": "shift_r",
    "left alt": "alt",
    "right alt": "alt_r",
    "left meta": "meta_l",
    "right meta": "meta_r",
    "left gui": "meta_l",
    "right gui": "meta_r",
    "delete": "delete",
    "insert": "insert",
    "home": "home",
    "end": "end",
    "page up": "pgup",
    "page down": "pgdn",
    "left": "left",
    "right": "right",
    "up": "up",
    "down": "down",
    "-": "minus",
    "=": "equal",
    "[": "bracket_left",
    "]": "bracket_right",
    ";": "semicolon",
    "'": "apostrophe",
    "`": "grave_accent",
    "\\": "backslash",
    ",": "comma",
    ".": "dot",
    "/": "slash",
}
KEYS.update({c: c for c in "abcdefghijklmnopqrstuvwxyz0123456789"})
KEYS.update({f"f{i}": f"f{i}" for i in range(1, 13)})


def run(args):
    if not 1 <= args.seconds <= 300 or bool(args.vm_pid) != bool(args.qmp_port):
        raise ValueError("duration 1–300 seconds; input needs both VM PID and QMP port")
    control = LabInput(args.vm_pid, args.qmp_port) if args.vm_pid else None
    presenter, identity, count, frame = None, None, 0, None
    started = time.monotonic()
    try:
        with args.report.open("x", encoding="utf-8") as log:
            while time.monotonic() - started < args.seconds:
                if args.frame.exists():
                    with args.frame.open("rb") as source:
                        if os.fstat(source.fileno()).st_size > MAX_BYTES:
                            raise ValueError("frame file too large")
                        current = parse_frame(source.read(MAX_BYTES + 1))
                    if current.identity != identity:
                        if presenter and (frame.width, frame.height) != (
                            current.width,
                            current.height,
                        ):
                            if control:
                                control.release()
                            presenter.close()
                            presenter = None
                        frame = current
                        if presenter is None:
                            presenter = WindowPresenter(frame.width, frame.height)
                            if presenter.metadata["kind"] != "sdl_software_window":
                                raise RuntimeError("a real window presentation driver is required")
                            log.write(json.dumps({"presenter": presenter.metadata}) + "\n")
                        pixels = np.frombuffer(frame.pixels, dtype=np.uint8).reshape(
                            frame.height, frame.width, 4
                        )
                        rgb = pixels[:, :, [2, 1, 0] if frame.format == 87 else [0, 1, 2]]
                        presenter.submit(rgb)
                        identity = frame.identity
                        count += 1
                        presenter.caption(f"NPU lab desktop — received frame {frame.sequence}")
                        record = {
                            "frame": identity,
                            "window_submissions": count,
                            "displayed_frames_verified": 0,
                            "input_enabled": bool(control),
                        }
                        log.write(json.dumps(record) + "\n")
                        log.flush()
                        print(json.dumps(record), flush=True)
                if presenter:
                    pg = presenter.pygame
                    for event in pg.event.get():
                        if event.type == pg.QUIT:
                            return
                        if not control:
                            continue
                        if event.type in (pg.KEYDOWN, pg.KEYUP):
                            code = KEYS.get(pg.key.name(event.key))
                            if code:
                                control.key(code, event.type == pg.KEYDOWN)
                        elif event.type in (pg.MOUSEMOTION, pg.MOUSEBUTTONDOWN, pg.MOUSEBUTTONUP):
                            x, y = event.pos
                            x = min(max(0, x), frame.width - 1)
                            y = min(max(0, y), frame.height - 1)
                            button = {1: "left", 2: "middle", 3: "right"}.get(
                                getattr(event, "button", 0)
                            )
                            control.pointer(
                                x,
                                y,
                                frame.width,
                                frame.height,
                                button,
                                event.type == pg.MOUSEBUTTONDOWN,
                            )
                        elif event.type == pg.WINDOWFOCUSLOST:
                            control.release()
                time.sleep(0.05)
    finally:
        try:
            if control:
                control.release()
        finally:
            if presenter:
                presenter.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frame", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--vm-pid", type=int)
    parser.add_argument("--qmp-port", type=int)
    run(parser.parse_args())
