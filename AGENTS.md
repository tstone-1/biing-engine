# Development instructions

This file is the shared instruction entry point. Do not add a CLAUDE.md wrapper.

- Keep original game files, extracted media, saves and screenshots out of Git.
  Use ignored `original/` and `extracted/` directories for local inputs and output.
- Implement behavior from format specifications and observations. Do not copy
  disassembly or another Biing project's source, tools or data into this project.
- Load original graphics, texts, audio and intro scene data from the player's files.
  Test fixtures must be visibly synthetic or generated, never cut from game data.
- Default behavior follows the original, including documented bugs. Optional fixes
  use the existing `BIING_FIXES` mechanism. Keep hang/crash guards active.
- Python development uses uv and the exact version in `.python-version`.
  Keep `tools/play.py` and its import path compatible with Python 3.9+.
- `docs/editions.md` is the editable input for `engine/src/editions_tables.h`.
  Regenerate with `uv run --no-project python -I tools/engine_editions.py`.
  Do not edit the generated header directly. Program-image reads use `program_*`
  edition mappings, not direct assumptions about one executable layout.
- Run the owning regression while iterating, then the source checks documented in
  README.md once the change is complete. CI must work without original game files.
- Keep native input/listening claims separate from headless state, image and buffer
  checks. Update KNOWN-LIMITATIONS.md when supported scope or known behavior changes.
- Preserve player saves. Import into separate game copies and never overwrite the
  original installation. Test writes belong in temporary or ignored owned folders.
- Store durable project knowledge here or in referenced documentation. Historical
  research references in code comments are context, not required build inputs.
