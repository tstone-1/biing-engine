# /// script
# requires-python = ">=3.15.1"
# dependencies = []
# ///
"""Live companion for the normal playable engine window.

Build the engine, then run:
  uv run tools/biing_inspector.py --game <BIING!-folder>
Open the printed local URL in a browser beside the game. The source game is copied
into extracted/inspector-sessions; edits, in-game saves and checkpoints stay there.
Pause to edit named scalar fields. Structural fields and raw bytes are read-only.
Undo is a checked reverse write, not a rewind of time. Checkpoints are ordinary game
saves, not complete process snapshots. Close with Ctrl+C or the Stop session button.
Only loopback is served, with a per-session token and no external assets or uploads.
The default port stays fixed so a browser's site permission survives a new session;
use --port 0 to select a free port when running multiple companions.
"""
import argparse
from datetime import datetime, timezone
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import os
import re
import secrets
import shutil
import subprocess
import sys
import webbrowser
from urllib.parse import parse_qs, urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parent))
from biing_control import ControlError, Session, session_directory


def open_folder(path):
    if os.name == "nt":
        os.startfile(path)
    else:
        subprocess.Popen(["open" if sys.platform == "darwin" else "xdg-open", str(path)])


def session_info(folder):
    """Recognize retained sessions, including those made before session metadata existed."""
    folder = Path(folder).resolve(strict=True)
    with (folder / "commands.jsonl").open(encoding="utf-8") as log:
        first = json.loads(log.readline(65536))
    if not isinstance(first, dict) or first.get("event") != "start" or "--inspect" not in first.get("argv", []):
        raise ValueError("Not a trainer session")
    if not (folder / "game").is_dir():
        raise ValueError("Session game copy is missing")
    meta = folder / "session.json"
    data = json.loads(meta.read_text(encoding="utf-8")) if meta.is_file() else {"source": first["source"]}
    if (not isinstance(data, dict) or not isinstance(data.get("source"), str) or type(data.get("fixes", False)) is not bool
            or data.get("latest") is not None and not isinstance(data["latest"], str)):
        raise ValueError("Invalid trainer session metadata")
    return {"path": str(folder), "source": data["source"], "created": folder.stat().st_mtime,
            "latest": data.get("latest"), "fixes": data.get("fixes", False)}


def retained_sessions(root, game=None):
    entries = []
    for path in Path(root).glob("session-*"):
        try:
            entry = session_info(path)
            if game is None or Path(entry["source"]).resolve() == Path(game).resolve():
                entries.append(entry)
        except (OSError, ValueError, KeyError, TypeError):
            continue
    return sorted(entries, key=lambda e: e["created"], reverse=True)


def checkpoint_files(folder):
    folder = Path(folder).resolve()
    result = {}
    for path in sorted(folder.glob("save-*.sav"), key=lambda p: p.stat().st_mtime):
        name = path.name[5:-4]
        if re.fullmatch(r"[A-Za-z0-9_-]{1,48}", name) and path.is_file() and path.resolve().parent == folder:
            result[name] = path
    return result


def integer(value):
    if type(value) is not int or not -(2**63) <= value < 2**63:
        raise ValueError("Expected an integer")
    return value


class InspectorSession(Session):
    def __init__(self, engine, game, output, *, previous=None, **kwargs):
        prior = session_info(previous) if previous else None
        if prior:
            game = Path(prior["path"]) / "game"
            kwargs["fixes"] = prior["fixes"]
        super().__init__(engine, game, output, inspector=True, **kwargs)
        self.history = []
        try:
            self.schema = self.command("schema")
            self.metadata = {"version": 1,
                "source": prior["source"] if prior else str(Path(game).resolve()),
                "previous": prior["path"] if prior else None, "latest": None, "fixes": kwargs.get("fixes", False)}
            self.save_metadata()
            if prior:
                self.command("pause")
                for name, path in checkpoint_files(prior["path"]).items():
                    dest = self.output / path.name
                    shutil.copy2(path, dest)
                    self.checkpoints[name] = dest
                if prior["latest"] and prior["latest"] not in self.checkpoints:
                    raise ValueError("Session checkpoint is missing")
                if self.checkpoints:
                    latest = prior["latest"] or next(reversed(self.checkpoints))
                    if latest not in self.checkpoints:
                        raise ValueError("Session checkpoint is missing")
                    self.command(f"load:{self.checkpoints[latest]}")
                    self.metadata["latest"] = latest
                    self.save_metadata()
                    self._record({"event": "reopened", "previous": prior["path"], "checkpoint": latest})
        except BaseException:
            self.close()
            raise

    def save_metadata(self):
        temporary = self.output / "session.json.tmp"
        temporary.write_text(json.dumps(self.metadata), encoding="utf-8")
        temporary.replace(self.output / "session.json")

    def checkpoint(self, name):
        with self.lock:
            result = super().checkpoint(name)
            self.metadata["latest"] = name
            self.save_metadata()
            return result

    def checkpoint_list(self):
        return [{"name": name, "created": datetime.fromtimestamp(path.stat().st_mtime, timezone.utc).isoformat()}
                for name, path in reversed(self.checkpoints.items())]

    def restore(self, name):
        with self.lock:
            if not isinstance(name, str) or name not in self.checkpoints:
                raise ValueError("Unknown checkpoint in this session")
            if self.checkpoints[name].resolve().parent != self.output:
                raise ValueError("Checkpoint is outside this session")
            # The engine enforces paused/idle state for the recovery save too.
            backup = self.checkpoint("before-restore-" + secrets.token_hex(8))
            state = self.command(f"load:{self.checkpoints[name]}")
            self.history.clear()
            self.metadata["latest"] = name
            self.save_metadata()
            self._record({"event": "restore", "name": name, "backup": backup["name"]})
            return {**state, "backup": backup["name"]}

    def change(self, data):
        keys = ("group", "record", "field", "before", "value")
        values = [integer(data[k]) for k in keys]
        with self.lock:
            state = self.command("set:" + ":".join(map(str, values)))
            entry = dict(zip(keys, values))
            entry["seq"] = state["seq"]
            self.history.append(entry)
            self._record({"event": "edit", **entry})
            return state

    def undo(self):
        with self.lock:
            if not self.history:
                raise ValueError("No edit to undo")
            entry = self.history[-1]
            args = [entry[k] for k in ("group", "record", "field", "value", "before")]
            state = self.command("set:" + ":".join(map(str, args)))
            self.history.pop()
            self._record({"event": "undo", **entry})
            return state


class InspectorServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, session, port=0):
        self.session = session
        self.token = secrets.token_urlsafe(32)
        self.assets = Path(__file__).with_name("inspector")
        super().__init__(("127.0.0.1", port), Handler)
        self.origin = f"http://127.0.0.1:{self.server_port}"


class Handler(BaseHTTPRequestHandler):
    timeout = 5

    # Do not write request URLs or tokens to access logs.
    def log_message(self, *_):
        pass

    def send(self, status, body, content_type="application/json; charset=utf-8"):
        payload = json.dumps(body, ensure_ascii=True).encode() if isinstance(body, (dict, list)) else body
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; "
                         "connect-src 'self'; img-src 'self'; frame-ancestors 'none'; base-uri 'none'")
        self.end_headers()
        self.wfile.write(payload)

    def allowed(self, api=False):
        if self.headers.get("Host") != self.server.origin.removeprefix("http://"):
            self.send(403, {"error": "Unexpected host"})
            return False
        origin = self.headers.get("Origin")
        if origin is not None and origin != self.server.origin:
            self.send(403, {"error": "Cross-origin access refused"})
            return False
        authorization = self.headers.get("Authorization", "").encode("latin-1")
        if api and not hmac.compare_digest(authorization, ("Bearer " + self.server.token).encode("ascii")):
            self.send(403, {"error": "Open this session's printed URL"})
            return False
        return True

    def do_GET(self):
        path = urlsplit(self.path)
        api = path.path.startswith("/api/")
        if not self.allowed(api):
            return
        files = {"/": ("index.html", "text/html; charset=utf-8"),
                 "/app.js": ("app.js", "text/javascript; charset=utf-8"),
                 "/style.css": ("style.css", "text/css; charset=utf-8")}
        if path.path in files:
            filename, mime = files[path.path]
            self.send(200, (self.server.assets / filename).read_bytes(), mime)
            return
        s = self.server.session
        try:
            params = parse_qs(path.query, strict_parsing=True)

            def number(key):
                values = params[key]
                if len(values) != 1 or not values[0].isascii() or not values[0].isdigit():
                    raise ValueError("Expected a non-negative integer parameter")
                return integer(int(values[0]))

            with s.lock:
                if path.path == "/api/schema":
                    result = {**s.schema, "session": str(s.output)}
                elif path.path == "/api/status":
                    result = {"closed": True} if s.closed else s.command("status")
                elif path.path == "/api/records":
                    result = s.command(f"list:{number('group')}")
                elif path.path == "/api/record":
                    result = s.command(f"read:{number('group')}:{number('record')}")
                elif path.path == "/api/raw":
                    result = s.command(f"raw:{number('block')}:{number('offset')}")
                elif path.path == "/api/history":
                    result = s.history.copy()
                elif path.path == "/api/checkpoints":
                    result = s.checkpoint_list()
                else:
                    self.send(404, {"error": "Not found"})
                    return
            self.send(200, result)
        except (ValueError, KeyError, OSError, ControlError) as exc:
            self.send(400, {"error": str(exc)})

    def do_POST(self):
        if not self.allowed(api=True):
            return
        try:
            if self.headers.get("Content-Type") != "application/json":
                raise ValueError("Expected application/json")
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 4096:
                raise ValueError("Request body must be 1..4096 bytes")
            data = json.loads(self.rfile.read(length))
            if not isinstance(data, dict):
                raise ValueError("Expected an object")
            s = self.server.session
            with s.lock:
                if self.path == "/api/edit":
                    result = s.change(data)
                elif self.path == "/api/undo":
                    result = s.undo()
                elif self.path == "/api/action":
                    action = data["action"]
                    if action in ("pause", "resume"):
                        result = s.command(action)
                    elif action == "step":
                        result = s.command(f"step:{integer(data['frames'])}")
                    elif action == "checkpoint":
                        result = s.checkpoint("inspector-" + secrets.token_hex(8))
                    elif action == "restore":
                        result = s.restore(data["name"])
                    elif action == "folder":
                        open_folder(s.output)
                        result = {"opened": True}
                    elif action == "stop":
                        s.command("pause")
                        s.checkpoint("exit-" + secrets.token_hex(8))
                        s.close()
                        result = {"closed": True}
                    else:
                        raise ValueError("Unknown action")
                else:
                    self.send(404, {"error": "Not found"})
                    return
            self.send(200, result)
            if self.path == "/api/action" and data.get("action") == "stop":
                if hasattr(self.server, "stopping"):
                    self.server.stopping = True
                else:
                    self.server.shutdown()
        except (ValueError, KeyError, TypeError, OSError, ControlError) as exc:
            self.send(400, {"error": str(exc)})


def serve(session, port=65376, ready=None):
    """Serve in the caller's worker thread; closing the game also ends the companion."""
    try:
        with InspectorServer(session, port) as server:
            url = f"{server.origin}/#{server.token}"
            if ready:
                ready(url)
            else:
                print(f"[OK] Inspector: {url}", flush=True)
                print(f"[OK] Owned game copy and edit log: {session.output}", flush=True)
            server.timeout = 0.25
            server.stopping = False
            while not server.stopping and not session.closed and session.process.poll() is None:
                server.handle_request()
    except KeyboardInterrupt:
        pass
    finally:
        session.close()


def main(argv=None):
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--game", type=Path)
    source.add_argument("--resume", type=Path, help="reopen a retained trainer session in a new copy; load its latest checkpoint")
    parser.add_argument("--engine", type=Path, default=root / "build" / ("biing.exe" if sys.platform == "win32" else "biing"))
    parser.add_argument("--output", type=Path, default=root / "extracted/inspector-sessions")
    parser.add_argument("--port", type=int, default=65376)
    parser.add_argument("--open-browser", action="store_true")
    parser.add_argument("--fixes", action="store_true", help="enable optional fixes for a new session; reopened sessions keep their setting")
    args = parser.parse_args(argv)
    session = InspectorSession(args.engine, args.game, session_directory(args.output), previous=args.resume, fixes=args.fixes)
    def ready(url):
        print(f"[OK] Inspector: {url}", flush=True)
        print(f"[OK] Owned game copy and edit log: {session.output}", flush=True)
        if args.open_browser:
            webbrowser.open(url)
    serve(session, args.port, ready)


if __name__ == "__main__":
    main()
