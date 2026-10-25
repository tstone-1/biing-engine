"""Persistent, local Biing test sessions. No OS desktop automation is used.

Each session owns a full game copy, stderr log, JSONL transcript and generated captures.
Game clicks use x=0..639, y=0..439; the image includes a 20-pixel top border.
The engine pauses between requests. A visible controlled window ignores physical input.
"""
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import shutil
import subprocess
import threading
import uuid


class ControlError(RuntimeError):
    """A command failed or its outcome could not be established."""


def validate_action(step):
    """Validate the whole public action before sending any part of a batch."""
    if not isinstance(step, str) or len(step) > 256 or any(ord(c) < 32 or ord(c) > 126 for c in step):
        raise ValueError("Actions must be short printable ASCII strings")
    if match := re.fullmatch(r"(?:move|click):(\d+),(\d+)", step):
        if int(match[1]) < 640 and int(match[2]) < 440:
            return step
    elif match := re.fullmatch(r"(?:wait|tick|hold|right):(\d+)", step):
        if int(match[1]) <= 30000:
            return step
    elif step.startswith("key:") and 0 < len(step[4:]) <= 128:
        return step
    elif re.fullmatch(r"code:[0-9a-fA-F]{1,2}(?:,[0-9a-fA-F]{1,2}){0,31}", step):
        return step
    elif match := re.fullmatch(r"intro:(?:start|relo|bint|pre(?::(?:first|again))?|encode(?::(\d{1,2})(?::[qd])?)?)", step):
        if match[1] is None or 1 <= int(match[1]) <= 13:
            return step
    raise ValueError(f"Unsupported or out-of-range action: {step!r}")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def game_digest(root):
    """Pin the complete starting copy, including saves/configuration, without storing its bytes in a report."""
    checksum = hashlib.sha256()
    for path in sorted(Path(root).rglob("*")):
        if path.is_file():
            checksum.update(path.relative_to(root).as_posix().encode() + b"\0")
            checksum.update(bytes.fromhex(digest(path)))
    return checksum.hexdigest()


class Session:
    """One engine process; serialized requests never reload between ordinary actions."""

    def __init__(self, engine, game, output, *, window=False, seed=1, timeout=30, inspector=False, fixes=False):
        self.lock = threading.RLock()
        self.process = None
        self.closed = False
        self.seq = -1
        self.frames = -1
        self.timeout = timeout
        self.response_limit = 1048576 if inspector else 65536
        self.capture_index = 0
        self.checkpoints = {}
        self.responses = queue.Queue()
        self.engine = Path(engine).resolve(strict=True)
        source = Path(game).resolve(strict=True)
        self.output = Path(output).resolve()
        if self.output == source or source in self.output.parents:
            raise ValueError("Session output must be outside the source game folder")
        if any(c in str(self.output) for c in "\r\n\0"):
            raise ValueError("Session path cannot contain line delimiters")
        self.output.mkdir(parents=True, exist_ok=False)
        self.game = self.output / "game"
        self.log = (self.output / "commands.jsonl").open("x", encoding="utf-8")
        self.stderr = (self.output / "engine.log").open("xb")
        try:
            shutil.copytree(source, self.game)
            # Reference inputs can be read-only; only this owned copy becomes writable.
            for path in self.game.rglob("*"):
                if path.is_file():
                    path.chmod(path.stat().st_mode | 0o200)
            if inspector:
                window = True
            self.argv = [str(self.engine), str(self.game)] + (
                ["--inspect"] if inspector else ["--control"] + (["--window"] if window else []))
            if fixes:
                self.argv.append("--fixes")
            env = os.environ.copy()
            # Ambient diagnostics must not silently change this session's random sequence.
            for key in ("BIING_RNG_SCRIPT", "BIING_RNG_TRACE", "BIING_FIXES", "BIING_RNG_LOG", "BIING_MIDI_LOG"):
                env.pop(key, None)
            env["BIING_SEED"] = str(seed)
            self._record({"event": "start", "argv": self.argv, "seed": seed,
                          "engine_sha256": digest(self.engine), "source": str(source),
                          "game_sha256": game_digest(self.game)})
            self.process = subprocess.Popen(self.argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                            stderr=self.stderr, env=env, bufsize=0)
            self.reader = threading.Thread(target=self._read, daemon=True)
            self.reader.start()
            self.state = self._response(0, "ready")
            if self.state.get("protocol") != 1 or not self.state["ok"]:
                raise ControlError("Unsupported engine control protocol")
            if type(self.state.get("window")) is not bool or self.state["window"] != window:
                raise ControlError("Engine did not acknowledge the requested display mode")
            if bool(self.state.get("inspector")) != inspector:
                raise ControlError("Engine did not acknowledge the requested inspector mode")
        except BaseException:
            self.close()
            raise

    def _record(self, item):
        self.log.write(json.dumps(item, ensure_ascii=True) + "\n")
        self.log.flush()

    def _read(self):
        try:
            while True:
                line = self.process.stdout.readline(self.response_limit + 1)
                if not line:
                    self.responses.put(ControlError("Engine closed its response stream"))
                    return
                if len(line) > self.response_limit or not line.endswith(b"\n"):
                    self.responses.put(ControlError("Invalid engine response framing"))
                    return
                self.responses.put(line)
        except (OSError, ValueError) as exc:
            self.responses.put(ControlError(str(exc)))

    def _response(self, expected, event="response"):
        try:
            line = self.responses.get(timeout=self.timeout)
        except queue.Empty as exc:
            raise ControlError("Engine response timed out; session stopped to avoid uncertain actions") from exc
        if isinstance(line, Exception):
            raise line
        try:
            state = json.loads(line)
        except (ValueError, UnicodeError) as exc:
            raise ControlError("Engine returned malformed JSON") from exc
        if not isinstance(state, dict):
            raise ControlError("Engine response is not an object")
        self._record({"event": "received", "response": state})
        integers = ("seq", "frames", "screen", "day", "hour", "minute")
        if (any(type(state.get(key)) is not int for key in integers)
                or any(type(state.get(key)) is not bool for key in ("ok", "quit", "intro_active"))
                or state["seq"] != expected or state["frames"] < max(0, self.frames)
                or state.get("event") != event):
            raise ControlError("Engine response has missing state or incorrect sequence/frame numbers")
        self.seq, self.frames = state["seq"], state["frames"]
        return state

    def command(self, command):
        with self.lock:
            if self.closed:
                raise ControlError("Session is closed")
            if any(c in command for c in "\r\n\0") or len(command.encode()) > 4095:
                raise ValueError("Invalid command framing")
            self._record({"event": "command", "command": command})
            try:
                self.process.stdin.write((command + "\n").encode())
                self.process.stdin.flush()
                self.state = self._response(self.seq + 1)
            except (OSError, ControlError) as exc:
                self.close()
                raise ControlError(str(exc)) from exc
            if not self.state["ok"]:
                raise ControlError(self.state.get("error", "Engine rejected command"))
            return self.state.copy()

    def act(self, steps):
        with self.lock:
            if not isinstance(steps, list) or not 1 <= len(steps) <= 100:
                raise ValueError("Supply 1 to 100 actions")
            for step in steps:
                validate_action(step)
            for index, step in enumerate(steps):
                state = self.command(step)
                if state["quit"]:
                    self.close()
                    return {"state": state, "closed": True, "completed_actions": index + 1,
                            "remaining_actions": len(steps) - index - 1}
            return self.observe()

    def observe(self):
        """Capture without advancing game time. Paths are generated, never supplied by a tool caller."""
        from PIL import Image
        with self.lock:
            self.capture_index += 1
            stem = self.output / f"capture-{self.capture_index:06d}"
            ppm, png = stem.with_suffix(".ppm"), stem.with_suffix(".png")
            state = self.command(f"shot:{ppm}")
            with Image.open(ppm) as picture:
                if picture.size != (640, 480):
                    raise ControlError("Unexpected framebuffer size")
                picture.save(png)
            result = {"state": state, "image": str(png), "sha256": digest(ppm),
                      "coordinates": "Game x=0..639,y=0..439; image game area starts at y=20"}
            self._record({"event": "capture", **result})
            return result

    def checkpoint(self, name):
        with self.lock:
            if not re.fullmatch(r"[A-Za-z0-9_-]{1,48}", name) or name in self.checkpoints:
                raise ValueError("Use a new checkpoint name: 1..48 letters, digits, underscores or hyphens")
            path = self.output / ("save-" + name + ".sav")
            state = self.command(f"save:{path}")
            self.checkpoints[name] = path
            result = {"name": name, "sha256": digest(path), "state": state}
            self._record({"event": "checkpoint", "path": str(path), **result})
            return result

    def reload(self, name):
        with self.lock:
            if name not in self.checkpoints:
                raise ValueError("Unknown checkpoint in this session")
            self.command(f"load:{self.checkpoints[name]}")
            return self.observe()

    def close(self):
        with self.lock:
            if self.closed:
                return
            self.closed = True
            if self.process is not None:
                self.process.stdin.close()
                try:
                    self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.process.terminate()
                    try:
                        self.process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        self.process.kill()
                        self.process.wait(timeout=3)
                self.process.stdout.close()
            self.stderr.close()
            self.log.close()


def session_directory(root):
    return Path(root) / ("session-" + uuid.uuid4().hex)
