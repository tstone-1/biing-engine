"""Desktop launcher for the packaged engine; no compiler or network needed by players.

Build with tools/package_app.py. Run from source with --engine-dir build.
Application updates replace binaries only; imported games and saves live in user data.
--verify-package SOURCE --report FILE --data-root FOLDER exercises the bundled import,
validation and render path without opening a window (not a native interaction check).
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime
import hashlib
import json
import locale
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import uuid
import webbrowser

from setup_game import SetupError, child, identify, setup
from biing_inspector import InspectorServer, InspectorSession, open_folder, retained_sessions, serve
from biing_control import Session, session_directory


def data_root() -> Path:
    if sys.platform == "darwin":
        return Path.home() / "Library/Application Support/Biing"
    if os.name == "nt":
        return Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local")) / "Biing"
    return Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "biing"


def package_root() -> Path:
    return Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[1]))


def version() -> str:
    return (package_root() / "packaging/version.txt").read_text(encoding="utf-8").strip()


@contextmanager
def library_lock(root: Path):
    """OS-released lock also covers a running game; no stale PID-file recovery needed."""
    root.mkdir(parents=True, exist_ok=True)
    with (root / ".launcher.lock").open("a+b") as lock:
        lock.seek(0)
        if os.name == "nt":
            import msvcrt
            try:
                # Windows byte-range locks also deny reads of the locked byte.
                # Treat that refusal as an occupied lock, not a raw I/O error.
                if not lock.read(1):
                    lock.write(b"0")
                    lock.flush()
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError:
                raise SetupError("Biing is already open. Close the other launcher or game first.") from None
        else:
            import fcntl
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError:
                raise SetupError("Biing is already open. Close the other launcher or game first.") from None
        yield


class Library:
    def __init__(self, root: Path, engine_dir: Path):
        self.root, self.engine_dir = root, engine_dir
        self.root.mkdir(parents=True, exist_ok=True)
        self.file = root / "library.json"
        self.log = root / "launcher.log"
        self.state = {"version": 1, "language": "de" if (locale.getlocale()[0] or "").startswith("de") else "en", "selected": "", "games": []}
        if self.file.exists():
            try:
                state = json.loads(self.file.read_text(encoding="utf-8"))
                if state["version"] != 1 or state["language"] not in ("de", "en") or not isinstance(state["games"], list):
                    raise ValueError("unsupported library format")
                ids = set()
                for game in state["games"]:
                    if uuid.UUID(game["id"]).hex != game["id"] or game["id"] in ids or not isinstance(game["name"], str):
                        raise ValueError("invalid game entry")
                    ids.add(game["id"])
                if state["selected"] and state["selected"] not in ids:
                    raise ValueError("invalid selection")
                self.state = state
            except (ValueError, KeyError, TypeError, AttributeError) as exc:
                raise SetupError(f"Cannot read {self.file}: {exc}. Your game folders and saves have been kept.") from exc

    def save(self):
        temporary = self.file.with_suffix(".tmp")
        temporary.write_text(json.dumps(self.state, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        temporary.replace(self.file)

    def game_dir(self, game_id):
        if not any(g["id"] == game_id for g in self.state["games"]):
            raise SetupError("Choose an imported game first.")
        return self.root / "games" / game_id

    def binary(self, name):
        binary = self.engine_dir / (name + (".exe" if os.name == "nt" else ""))
        if not binary.is_file():
            raise SetupError(f"The application is incomplete ({name} missing). Reinstall the application; keep your game data.")
        return binary

    def run(self, name, args, *, timeout=None):
        with self.log.open("w", encoding="utf-8") as out:
            result = subprocess.run([str(self.binary(name)), *map(str, args)], stdout=out,
                                    stderr=subprocess.STDOUT, timeout=timeout,
                                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        if result.returncode:
            raise SetupError(f"The game could not complete this action. Details: {self.log}")

    def import_game(self, source: Path):
        game_id = uuid.uuid4().hex
        dest = self.root / "games" / game_id
        dest.parent.mkdir(exist_ok=True)
        setup(source, dest)
        self.run("biing-check", [dest], timeout=180)
        edition = identify(child(dest, "BIPRO.EXE"))
        entry = {"id": game_id, "name": edition.name, "edition": edition.key}
        # Publish only after preparation AND engine validation. Other game entries stay intact.
        self.state["games"].append(entry)
        self.state["selected"] = game_id
        self.save()
        return entry

    def play(self, game_id, skip_intro=False, fixes=False):
        game = self.game_dir(game_id)
        self.run("biing-check", [game], timeout=180)
        self.run("biing", [game, *(["--no-intro"] if skip_intro else []), *(["--fixes"] if fixes else [])])

    def trainer_sessions(self, game_id):
        return retained_sessions(self.root / "trainer-sessions", self.game_dir(game_id))

    def trainer(self, game_id, previous=None, fixes=False, ready=None):
        game = self.game_dir(game_id)
        if previous is not None and str(Path(previous).resolve()) not in {s["path"] for s in self.trainer_sessions(game_id)}:
            raise SetupError("Choose a trainer session for this game.")
        self.run("biing-check", [Path(previous) / "game" if previous else game], timeout=180)
        session = InspectorSession(self.binary("biing"), game, session_directory(self.root / "trainer-sessions"),
                                   previous=previous, fixes=fixes)
        def opened(url):
            if ready:
                ready(url)
            webbrowser.open(url)
        # Keep the browser origin stable so saved site permissions survive restarts.
        serve(session, port=65376, ready=opened)


TEXT = {
    "en": {"title": "Biing!", "intro": "Choose your own game files once. Then click Play.",
           "archive": "Add game files...", "folder": "Add game folder...", "play": "Play", "files": "Show saved games",
           "skip": "Skip intro", "fixes": "Enable optional game bug fixes", "ready": "Ready to play", "empty": "No game added yet",
           "importing": "Preparing your game copy. This can take a few minutes...", "playing": "Game running. Close the game to return here.",
           "choose": "Choose your game archive", "choosefolder": "Choose your installed game folder or GOG app",
           "error": "Could not complete the action", "busy": "Please finish importing or close the game before closing the launcher.",
           "storage": "Your original files stay unchanged. Saves are kept separately from this app.", "language": "Language", "test": "Test build"},
    "de": {"title": "Biing!", "intro": "Wähle einmal deine eigenen Spieldateien aus. Danach genügt Spielen.",
           "archive": "Spieldateien hinzufügen...", "folder": "Spielordner hinzufügen...", "play": "Spielen", "files": "Spielstände anzeigen",
           "skip": "Intro überspringen", "fixes": "Optionale Fehlerkorrekturen aktivieren", "ready": "Bereit zum Spielen", "empty": "Noch kein Spiel hinzugefügt",
           "importing": "Deine Spielkopie wird vorbereitet. Das kann einige Minuten dauern...", "playing": "Das Spiel läuft. Schließe es, um hierher zurückzukehren.",
           "choose": "Wähle dein Spielarchiv", "choosefolder": "Wähle deinen installierten Spielordner oder die GOG-App",
           "error": "Aktion konnte nicht abgeschlossen werden", "busy": "Warte auf den Import oder schließe das Spiel, bevor du den Starter schließt.",
           "storage": "Deine Originaldateien bleiben unverändert. Spielstände liegen außerhalb dieser App.", "language": "Sprache", "test": "Testversion"},
}
TEXT["en"].update(trainer="Start trainer", trainer_open="Open trainer browser", new_session="New trainer session",
    sessions="Trainer sessions", training="Trainer running. Stop the session in the browser to save and return.")
TEXT["de"].update(trainer="Trainer starten", trainer_open="Trainer im Browser öffnen", new_session="Neue Trainersitzung",
    sessions="Trainersitzungen", training="Der Trainer läuft. Beende die Sitzung im Browser, um zu sichern und zurückzukehren.")
EDITION_NAMES_DE = {"disk": "Deutsche Diskversion 1.4", "gog-de": "GOG, Deutsch", "gog-en": "GOG, Englisch"}


class Window:
    def __init__(self, library):
        import tkinter as tk
        from tkinter import ttk, filedialog, messagebox
        self.tk, self.ttk, self.dialog, self.message = tk, ttk, filedialog, messagebox
        self.library, self.busy, self.events = library, False, queue.Queue()
        self.trainer_url = None
        self.sessions = []
        self.root = tk.Tk()
        self.root.title("Biing!")
        self.root.geometry("740x540")
        self.root.minsize(700, 530)
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        if sys.platform == "darwin":
            # The application/Dock Quit event otherwise calls Tcl's exit directly,
            # bypassing WM_DELETE_WINDOW and the active import/game guard.
            self.root.createcommand("::tk::mac::Quit", self.close)
        self.frame = ttk.Frame(self.root, padding=24)
        self.frame.pack(fill="both", expand=True)
        self.frame.columnconfigure(0, weight=1)
        self.heading = ttk.Label(self.frame, text="Biing!", font=("Helvetica", 26, "bold"))
        self.heading.grid(row=0, column=0, sticky="w")
        self.lang = ttk.Combobox(self.frame, values=("Deutsch", "English"), state="readonly", width=10)
        self.lang.grid(row=0, column=1, sticky="e")
        self.lang.bind("<<ComboboxSelected>>", self.change_language)
        self.intro = ttk.Label(self.frame, wraplength=590)
        self.intro.grid(row=1, column=0, columnspan=2, sticky="w", pady=(12, 16))
        self.games = ttk.Combobox(self.frame, state="readonly")
        self.games.grid(row=2, column=0, columnspan=2, sticky="ew")
        self.games.bind("<<ComboboxSelected>>", self.select)
        imports = ttk.Frame(self.frame)
        imports.grid(row=3, column=0, columnspan=2, sticky="w", pady=12)
        self.archive = ttk.Button(imports, command=lambda: self.add(False))
        self.archive.pack(side="left", padx=(0, 8))
        self.folder = ttk.Button(imports, command=lambda: self.add(True))
        self.folder.pack(side="left")
        self.skip, self.fixes = tk.BooleanVar(), tk.BooleanVar()
        self.skip_box = ttk.Checkbutton(self.frame, variable=self.skip)
        self.skip_box.grid(row=4, column=0, columnspan=2, sticky="w")
        self.fixes_box = ttk.Checkbutton(self.frame, variable=self.fixes)
        self.fixes_box.grid(row=5, column=0, columnspan=2, sticky="w")
        self.session_choice = ttk.Combobox(self.frame, state="readonly")
        self.session_choice.grid(row=6, column=0, columnspan=2, sticky="ew", pady=(12, 0))
        actions = ttk.Frame(self.frame)
        actions.grid(row=7, column=0, columnspan=2, sticky="w", pady=14)
        self.play_button = ttk.Button(actions, command=self.play)
        self.play_button.pack(side="left", padx=(0, 8))
        self.files = ttk.Button(actions, command=self.show_files)
        self.files.pack(side="left")
        self.trainer_button = ttk.Button(actions, command=self.trainer)
        self.trainer_button.pack(side="left", padx=8)
        self.sessions_button = ttk.Button(self.frame, command=self.show_sessions)
        self.sessions_button.grid(row=8, column=0, columnspan=2, sticky="w")
        self.status = ttk.Label(self.frame, wraplength=590)
        self.status.grid(row=9, column=0, columnspan=2, sticky="w", pady=(10, 0))
        self.storage = ttk.Label(self.frame, wraplength=590, foreground="#555555")
        self.storage.grid(row=10, column=0, columnspan=2, sticky="w", pady=(8, 0))
        self.refresh()
        self.root.after(100, self.poll)

    def tr(self, key):
        return TEXT[self.library.state["language"]][key]

    def refresh(self):
        for widget, key in ((self.intro, "intro"), (self.archive, "archive"), (self.folder, "folder"),
                            (self.play_button, "play"), (self.files, "files"), (self.skip_box, "skip"), (self.fixes_box, "fixes"),
                            (self.trainer_button, "trainer"), (self.sessions_button, "sessions")):
            widget.configure(text=self.tr(key))
        self.storage.configure(text=self.tr("storage") + "\n" + self.tr("test") + " " + version())
        self.lang.set("Deutsch" if self.library.state["language"] == "de" else "English")
        entries = self.library.state["games"]
        names = EDITION_NAMES_DE if self.library.state["language"] == "de" else {}
        self.games.configure(values=[f"{names.get(g.get('edition'), g['name'])} ({i + 1})" for i, g in enumerate(entries)])
        if entries:
            selected = next((i for i, g in enumerate(entries) if g["id"] == self.library.state["selected"]), 0)
            self.games.current(selected)
        self.status.configure(text=self.tr("ready" if entries else "empty"))
        for button in (self.play_button, self.files, self.trainer_button, self.sessions_button):
            button.configure(state="normal" if entries else "disabled")
        self.refresh_sessions()

    def refresh_sessions(self):
        selected = self.library.state["selected"]
        self.sessions = self.library.trainer_sessions(selected) if selected else []
        labels = [self.tr("new_session")]
        labels.extend(datetime.fromtimestamp(s["created"]).strftime("%Y-%m-%d %H:%M") + " / " + Path(s["path"]).name[-8:]
                      for s in self.sessions)
        self.session_choice.configure(values=labels, state="readonly" if selected else "disabled")
        self.session_choice.current(0)

    def change_language(self, _=None):
        self.library.state["language"] = "de" if self.lang.get() == "Deutsch" else "en"
        self.library.save()
        self.refresh()

    def select(self, _=None):
        self.library.state["selected"] = self.library.state["games"][self.games.current()]["id"]
        self.library.save()
        self.refresh_sessions()

    def add(self, folder):
        if folder:
            path = self.dialog.askdirectory(title=self.tr("choosefolder"), parent=self.root)
        else:
            patterns = "*.zip *.pkg *.app" if sys.platform == "darwin" else "*.zip"
            path = self.dialog.askopenfilename(title=self.tr("choose"), parent=self.root,
                                             filetypes=[("Biing", patterns), ("All files", "*")])
        if path:
            self.work(lambda: self.library.import_game(Path(path)), "importing")

    def play(self):
        selected, skip, fixes = self.library.state["selected"], self.skip.get(), self.fixes.get()
        self.work(lambda: self.library.play(selected, skip, fixes), "playing")

    def trainer(self):
        if self.busy:
            if self.trainer_url:
                webbrowser.open(self.trainer_url)
            return
        index = self.session_choice.current()
        previous = self.sessions[index - 1]["path"] if index > 0 else None
        selected, fixes = self.library.state["selected"], self.fixes.get()
        self.work(lambda: self.library.trainer(selected, previous, fixes,
                  ready=lambda url: self.events.put(("trainer", url))), "training")

    def show_sessions(self):
        folder = self.library.root / "trainer-sessions"
        folder.mkdir(exist_ok=True)
        open_folder(folder)

    def work(self, action, status):
        self.busy = True
        for widget in (self.archive, self.folder, self.play_button, self.files, self.games, self.lang, self.skip_box, self.fixes_box,
                       self.trainer_button, self.session_choice, self.sessions_button):
            widget.configure(state="disabled")
        self.status.configure(text=self.tr(status))
        def run():
            try:
                action()
                self.events.put(None)
            except Exception as exc:
                self.events.put(str(exc))
        threading.Thread(target=run, daemon=False).start()

    def poll(self):
        try:
            error = self.events.get_nowait()
        except queue.Empty:
            pass
        else:
            if isinstance(error, tuple) and error[0] == "trainer":
                self.trainer_url = error[1]
                self.trainer_button.configure(text=self.tr("trainer_open"), state="normal")
                self.root.after(100, self.poll)
                return
            self.busy = False
            self.trainer_url = None
            for widget in (self.archive, self.folder, self.skip_box, self.fixes_box):
                widget.configure(state="normal")
            self.games.configure(state="readonly")
            self.lang.configure(state="readonly")
            self.refresh()
            if error:
                self.message.showerror(self.tr("error"), error, parent=self.root)
        self.root.after(100, self.poll)

    def show_files(self):
        path = self.library.game_dir(self.library.state["selected"]) / "GAMES"
        open_folder(path)

    def close(self):
        if self.busy:
            self.message.showinfo("Biing!", self.tr("busy"), parent=self.root)
        else:
            self.root.destroy()


def verify_package(library, source, report):
    import tkinter
    tcl = tkinter.Tcl()
    tcl_version = tcl.eval("info patchlevel")
    if not tcl.eval("info library"):
        raise SetupError("The bundled Tcl runtime has no library.")
    entry = library.import_game(source)
    game = library.game_dir(entry["id"])
    save = game / "GAMES/package-test.sav"
    save.write_bytes(b"synthetic saved-file sentinel\n")
    again = Library(library.root, library.engine_dir)
    assert again.state["selected"] == entry["id"]
    pictures = []
    for name, steps in (("title", []), ("office", ["click:300,120", "wait:5"])):
        picture = library.root / (name + ".ppm")
        again.run("biing", [game, "--shot", picture, *steps], timeout=60)
        pictures.append(picture.read_bytes())
    assert all(p.startswith(b"P6\n640 480\n255\n") and len(p) > 640 * 480 * 3 for p in pictures)
    assert pictures[0] != pictures[1]
    assert save.read_bytes() == b"synthetic saved-file sentinel\n"
    trainer = None
    if entry["edition"] == "disk":
        from urllib.request import urlopen
        # Exercise the frozen module/assets and real restore protocol without host UI/audio.
        old = {k: os.environ.get(k) for k in ("SDL_VIDEODRIVER", "SDL_AUDIODRIVER", "BIING_MIDI_OUT")}
        os.environ.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", BIING_MIDI_OUT="none")
        session = None
        try:
            session = InspectorSession(library.binary("biing"), game, library.root / "trainer-check")
            session.command("pause")
            cash = session.command("status")["cash"]
            session.checkpoint("package")
            session.change(dict(group=0, record=0, field=0, before=cash, value=12345))
            assert session.restore("package")["cash"] == cash
            with InspectorServer(session) as server:
                worker = threading.Thread(target=server.serve_forever, daemon=True)
                worker.start()
                try:
                    with urlopen(server.origin + "/", timeout=5) as response:
                        assert b'id="restore"' in response.read()
                    with urlopen(server.origin + "/app.js", timeout=5) as response:
                        assert b"refreshCheckpoints" in response.read()
                finally:
                    server.shutdown()
                    worker.join(5)
            previous = session.output
            session.close()
            session = InspectorSession(library.binary("biing"), None, library.root / "trainer-reopen", previous=previous)
            assert session.command("status")["cash"] == cash and session.state["paused"]
            trainer = True
        finally:
            if session:
                session.close()
            for key, value in old.items():
                if value is None:
                    os.environ.pop(key, None)
                else:
                    os.environ[key] = value
    report.write_text(json.dumps({"version": version(), "edition": entry["edition"], "frozen": bool(getattr(sys, "frozen", False)),
                                  "python": list(sys.version_info[:3]), "tcl": tcl_version,
                                  "trainer_restore_reopen_assets": trainer,
                                  "import": True, "reopen": True, "saved_file_kept": True, "pictures_differ": True}, indent=2), encoding="utf-8")


def verify_installation(library, stage, source, report):
    """Separate-process import/reopen probe; caller supplies a fresh OS account.

    Uses the ordinary default data-root and a real save in slot 9. No browser or
    physical-input acceptance is claimed. Reports include paths for private evidence.
    """
    import tkinter
    from biing_saves import read_save
    tcl = tkinter.Tcl()
    if stage == 'import':
        if library.state['games'] or source is None:
            raise SetupError('Installation import requires an empty library and --source')
        entry = library.import_game(source)
        game = library.game_dir(entry['id'])
        library.run('biing', [game, '--shot', library.root / 'install.ppm',
                    'click:300,120', 'wait:5', 'cash:12345', 'save:' + str(game / 'GAMES/BGAME9')], timeout=60)
    else:
        if len(library.state['games']) != 1:
            raise SetupError('Expected the previously imported installation game')
        entry = library.state['games'][0]
        game = library.game_dir(entry['id'])
    save = game / 'GAMES/BGAME9'
    checksum = hashlib.sha256(save.read_bytes()).hexdigest()
    session = Session(library.binary('biing'), game, session_directory(library.root / 'installation-probes'))
    try:
        session.command('load:' + str(session.game / 'GAMES/BGAME9'))
        session.checkpoint('loaded')
        # Observe a save produced AFTER loading, not the input file or an acknowledgement.
        blocks = read_save(session.checkpoints['loaded'].read_bytes())['blocks']
        loaded_cash = int.from_bytes(blocks[26][3][0x16:0x1A], 'little', signed=True)
        if loaded_cash != 12345:
            raise SetupError('Installed game did not load the retained save')
    finally:
        session.close()
    if hashlib.sha256(save.read_bytes()).hexdigest() != checksum:
        raise SetupError('Reopening changed the retained save')
    report.write_text(json.dumps(dict(stage=stage, frozen=bool(getattr(sys, 'frozen', False)),
        python=list(sys.version_info[:3]), tcl=tcl.eval('info patchlevel'),
        data_root=str(library.root), home=str(Path.home()), game_id=entry['id'],
        save_sha256=checksum, cash=loaded_cash, real_save_load=True), indent=2), encoding='utf-8')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--engine-dir", type=Path, default=package_root() / ("engine" if getattr(sys, "frozen", False) else "build"))
    ap.add_argument("--data-root", type=Path, default=data_root())
    ap.add_argument("--verify-package", type=Path)
    ap.add_argument("--report", type=Path)
    ap.add_argument("--verify-installation", choices=('import', 'reopen'))
    ap.add_argument("--source", type=Path)
    args = ap.parse_args(argv)
    if (args.verify_package or args.verify_installation) and args.report is None:
        ap.error("Verification requires --report")
    if args.verify_package and args.verify_installation:
        ap.error("Choose one verification mode")
    try:
        with library_lock(args.data_root):
            library = Library(args.data_root, args.engine_dir)
            if args.verify_installation:
                verify_installation(library, args.verify_installation, args.source, args.report)
            elif args.verify_package:
                verify_package(library, args.verify_package, args.report)
            else:
                Window(library).root.mainloop()
        return 0
    except Exception as exc:
        if args.verify_package or args.verify_installation:
            args.report.write_text(json.dumps({"error": str(exc)}), encoding="utf-8")
        else:
            from tkinter import messagebox
            messagebox.showerror("Biing!", str(exc))
        return 1


if __name__ == "__main__":
    sys.exit(main())
