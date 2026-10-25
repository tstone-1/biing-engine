# Biing! engine and trainer

[Deutsche Anleitung](README.de.md)

A C11/SDL3 reimplementation that reads your own Biing! game files, with a
German/English desktop launcher and a browser trainer for editing live game values.
Supported inputs are the German disk version 1.4 and the German and English GOG
CD editions. No original game files are included.

This is an experimental source release. It is playable, but it is not a claim of
complete parity with the original. Read [known differences and support limits](KNOWN-LIMITATIONS.md),
especially before moving saves between this engine and the original game.

## Start from source

Install the prerequisites once:

- **macOS:** install [Homebrew](https://brew.sh/), then run `brew install cmake ninja sdl3 uv`.
  Homebrew's setup includes the compiler tools. [SDL3 formula](https://formulae.brew.sh/formula/sdl3).
- **Windows:** install and update [MSYS2](https://www.msys2.org/), open its **CLANG64** terminal,
  and run `pacman -S --needed mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-sdl3`.
  Install [uv](https://docs.astral.sh/uv/getting-started/installation/) and reopen the terminal so `uv --version` works.

Download and extract this repository's source ZIP, or clone it. Open a terminal in
that folder (CLANG64 on Windows), then run:

```sh
uv run --no-project python tools/play.py
```

On the first start, paste the path to your disk ZIP, GOG macOS installer/app or
installed game folder when prompted. The script builds the engine and prepares
a separate game copy. Later starts reuse that copy and its saved games. GOG
macOS installers require macOS; an installed game folder works on both systems.
Use `--prepare-only` to build and import without opening the game window.
The game copy and saves live in a `game` folder beside the source folder. Keep that
folder when updating the source. `--game PATH` chooses a different destination.
The project pins Python 3.15.1; uv installs it when needed. The short launcher also
works with an existing Python 3.9+ via `python3 tools/play.py`.

If a build fails, read `build/startup.log` and check the prerequisites above.
An unknown edition is refused. A nonempty destination is kept rather than replaced;
use another `--game` folder to import a different edition.

## Trainer

The desktop launcher has a **Start trainer** button and a session selector. Choose
**New trainer session**, or a dated previous session to continue from its latest
saved/restored checkpoint. Reopening creates a new copy and keeps the previous session.
The **Trainer sessions** button opens their folder. Start the source launcher with
`uv run --no-project python tools/launcher.py` after building; it manages its own
imported-game library, separate from the short source-startup script's `../game` folder.

After building, run:

```sh
uv run tools/biing_inspector.py --game ../game
```

Open the printed local URL beside the game. The trainer uses a separate game copy
under `extracted/inspector-sessions/`; edits and saves stay in that session. Pause
before changing a value. Undo checks the current value before restoring it;
it does not rewind game time. Checkpoints save the game state, not the entire
running process. German and English interfaces are included.
Use your chosen `--game` path if you changed the default. Save a checkpoint before
experimenting; the trainer's game copy is separate from the source launcher's copy.

To recover progress, pause and choose a saved checkpoint, then **Restore checkpoint**.
The trainer first saves the current state as a recovery checkpoint. The restored game
opens at the office (or the bankruptcy screen for negative cash), paused, with the old
undo history cleared. The previous screen and unsaved process caches are not restored.
**Stop session** saves an exit checkpoint. Save a checkpoint before closing the game
window or terminal directly; those exits do not promise an automatic save.
**Open session folder** shows checkpoints (`save-*.sav`), the game copy and edit log.

Reopen a command-line session with:

```sh
uv run tools/biing_inspector.py --resume PATH-TO-SESSION
```

Reopening loads the last saved/restored checkpoint and keeps the session's optional-fix
setting. Without a checkpoint, use the game's Load screen for its retained `GAMES/`
saves. Checkpoints are ordinary edition-specific saves; nothing automatically copies
trainer progress back to your normal game. Keep both copies until you have verified
any manual save transfer. This trainer controls this engine, not the original DOS game.

## Settings and audio

The default preserves original behavior. **Enable optional game bug fixes** in the
launcher, or `tools/play.py --fixes`, enables all switchable corrections. These cover
original UI, calculation, equipment and simulation bugs; they can change game outcomes
and the random sequence. They do not repair every already-damaged save. Hang/crash
guards remain active in either mode. A new CLI trainer accepts `--fixes` too.

For music, close the game and edit only the value after the dash on the `MCARD` line
in the working game copy's `SOUND.CFG`:

| Value | Music |
|---|---|
| `SB`, `SBP`, `SB16` | Sampled game music |
| `CD` | Imported CD tracks, normally used by the GOG editions |
| `GM` | General MIDI through the system synthesizer |
| `ROL` | Roland MT-32 output; requires a matching device or emulator |

The `DCARD` setting controls digital effects/speech. Keep other configuration entries
unchanged. `BIING_MIDI_OUT=list` lists MIDI outputs at engine startup; `none` disables
MIDI output, and a device-name substring chooses an output. General MIDI and MT-32
instrument mappings differ. Native MIDI/CD listening remains a documented test gap.
The launcher offers **Skip intro**; the source startup accepts `--no-intro`.

## Build and test without game files

```sh
cmake -S engine -B build -G Ninja
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
uv run --no-project python -I tools/engine_editions.py --check
uv run --no-project --with pytest python -m pytest -q -p no:cacheprovider tests
```

The C and included Python tests use synthetic inputs. Some host-dependent checks
can skip when their device is unavailable. They do not test native player input,
speaker output or the original-data import route on a clean computer.

`docs/editions.md` is the editable input for `engine/src/editions_tables.h`.
Run its generator after changing the measurements. Historical references in code
comments may refer to research documents outside this source candidate; they
are not required to compile the engine.

## Desktop packages

Building a package also requires Python 3.15.1 with Tcl/Tk and uv. Build on the target
operating system:

```sh
mkdir -p extracted
git archive --format=zip --prefix=biing-engine/ --output=extracted/project-source.zip HEAD
uv run --no-project --with-requirements packaging/requirements.txt python tools/package_app.py --source-archive extracted/project-source.zip --output extracted/releases/local-test
```

Run these commands from the public source checkout after committing changes. Never
archive a private research repository for distribution. The output directory must not
already exist. Packaging checks the source ZIP against its build inputs and bundles it
with the application. Windows also includes libiconv's corresponding source, downloaded
from its pinned upstream and combined with the included MSYS2 recipe and patches.
`--libiconv-source PATH` can supply the exact reviewed ZIP for an offline build.
Changed runtime binaries require a reviewed update to `packaging/licenses/`.
The recipe expects a Git checkout for its commit/source manifest. Packages bundle the engine, launcher and
runtime, so players need neither a compiler nor Python. Current private package
checks cover Apple silicon on macOS 27+ and Windows x86-64 on Windows 11. The
default macOS build has an ad-hoc signature. Dependency notices and matching sources
are included. Fresh standard-account import, save/load, update and reinstall retain
saves on both platforms; native UI and audio acceptance remain separate checks.

For Developer ID distribution, `tools/sign_macos.py prepare --app <Biing.app>
--manifest <manifest.json> --output <new-folder>` verifies the packaged files and signs
a copy using `APPLE_SIGNING_IDENTITY`. It preserves bundled sources and notices and
requires a secure timestamp and hardened runtime without runtime exceptions.
`tools/sign_macos.py notarize --output <same-folder> --keychain-profile <profile>`
uploads that signed copy to Apple, retains the submission ID for resuming, staples
the accepted ticket and checks Gatekeeper. Keep credentials outside the repository.
Run package acceptance again on the signed artifact. Signing does not publish a release.

On Windows, `tools/sign_windows.py prepare --app <dist/Biing> --manifest <manifest.json>
--output <new-folder> --signer <certificate-common-name>` validates and copies the package
without logging in. `sign --output <same-folder> --ssign <client.exe>
--ssign-sha256 <reviewed-client-sha256>` signs the remaining native files in one batch.
It preserves valid vendor signatures and requires Windows trust verification, timestamps
and the expected signer for new signatures. PowerShell 7 and Windows SDK signtool are
required. Authentication and the pinned client build are described in the tool's docstring;
keep credentials outside the repository and serialize use of a shared signing account.

## License

The project's own code is GPL-3.0-only. See [LICENSE](LICENSE) and
[COPYING.txt](COPYING.txt). Original game content is excluded. Third-party
libraries and runtimes retain their own licenses. This is an independent project.

The engine and tools are written from documented formats and measured behavior.
Original graphics, text, music, scene assets and executable files are supplied by
the player at runtime. The source includes functional constants and edition mappings;
these are not a bundled copy of the game. Test media and records are generated
synthetically. The development rules are in [AGENTS.md](AGENTS.md).

For a bug report, include your operating system, game edition, source revision and
steps to reproduce it. Include relevant error text, but do not upload original game
files, installers, manuals or extracted game content to an issue.
