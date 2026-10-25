# Known differences and support limits

This experimental engine supports the German disk version 1.4 and the German and
English GOG CD editions. It identifies those builds and refuses unknown versions.
It needs the player's own files; no game or manual download is provided.

## Differences from the original

| Area | What differs |
|---|---|
| Random simulation | The generator uses measured inputs, but individual games do not follow the original's exact random sequence. Statistics and individual actions have been compared; identical outcomes are not promised. |
| Original bugs | Most original behavior, including bugs, is retained by default. `tools/play.py --fixes` enables optional corrections. Guards against hangs, crashes and invalid memory accesses are always active. |
| Manual query and saves | The original's manual question and related rewards/penalties are not implemented. Save files retain associated counters using the disk-edition rule on all editions. Loading an engine save in the original can activate penalties until its manual question is answered. Keep backups and use the same game edition when transferring saves. |
| Extra programs | The external miniature-golf game, its handover, DOS setup/helper programs and the shooter are not implemented. Existing configuration files and defaults supply settings. |
| Intro and DOS behavior | DOS text-mode moments appear black. Hardware setup, DOS error paths and some transition details are not reproduced; CD audio timing has a small lead-in difference. |
| Timing and pointer | The main loop runs at a fixed 50 Hz. Machine-speed-dependent ambience, intro fast-forward and stalls can differ. Mouse movement follows the host pointer rather than DOS relative-motion behavior. |
| Invalid or unsaved original state | Some rare newspaper wording, pathology and torture behavior depends on original memory outside saved records. The engine uses defined fallbacks. An unreachable large one-shot sound-driver bug is also not reproduced. |

## What has been verified

Automated checks cover synthetic formats, game rules, input adapters, audio-buffer
handling, save operations, the launcher and trainer protocol. Separate local checks
using owned game files cover import and engine output for all three editions on
macOS, and disk-edition import on Windows. The source CI runs on macOS and Windows
without original game files.

The current native build environments are Apple silicon/macOS 27 and Windows 11
x86-64. A macOS disk-edition manual smoke test covered mouse, keyboard, focus return
and digital audio. Native Windows input/focus and MIDI/CD listening have not received
the same acceptance check. Automated audio checks do not establish speaker output.
Older macOS, Intel Macs and Linux are not claimed as tested platforms.

The trainer's German/English labels, named records, validated editing, undo,
checkpoint restore and session reopening have automated coverage. A complete native browser layout/reconnect
acceptance run remains open. Undo reverses one validated field edit; it does not
rewind time or all consequences of that edit.

## Downloads and support

The [0.1.0-rc.1 prerelease](https://github.com/tstone-1/biing-engine/releases/tag/v0.1.0-rc.1)
provides signed Windows 11 x86-64 and notarized macOS 27+ Apple silicon apps, with
dependency notices and matching source archives. Fresh standard-account import,
save/load, update and reinstall preserve saves on both platforms. Native UI and audio
acceptance remain incomplete as described above; these are experimental packages.
The packaging recipe's default macOS build is only ad-hoc signed. Notarization applies
to the explicitly labelled published archive, not every locally built test ZIP.

Back up saves before testing a new version or using the trainer. Report reproducible
problems through this repository's issues with edition, platform and revision.
Original game files and extracted media must stay out of issues and contributions.
