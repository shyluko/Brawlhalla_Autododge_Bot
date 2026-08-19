# Project Guide

## Scope

This is a Windows C++ research prototype for Brawlhalla process observation,
fighter telemetry, automated input decisions, and a Slint desktop UI. Active
source files are under `src/` and `ui/`.

## Important Files

- `src/AutododgeBot.cpp`: capture hook, actor identification, bot decisions,
  watchdog recovery, validation, and logs.
- `src/gui.cpp` and `ui/`: Slint UI bridge and declarative UI pages.
- `src/attack_table.h`: coarse move timing and selection metadata.
- `src/frame_hitbox_table.h`: frame-specific predictive hitboxes.
- `src/hitbox_table.h`: aggregate fallback attack bounds.
- `src/memory.h`: Win32 process-memory utility.
- `docs/OFFSETS.md`: offset confidence and compatibility notes.
- `README.md`: local build and GitHub release workflow.

## Build

Run `scripts/build.ps1` (or the `build.bat` wrapper) with Visual Studio Build
Tools/MSBuild and Slint C++ 1.17.1 installed. Tagged `v*` pushes build and
publish a release automatically through GitHub Actions.

## Development Rules

- Prefer the strongest available coding model for C++/Win32 debugging,
  reverse-engineering, and validation work in this project.
- Preserve the distinction between confirmed, conditional, candidate, and
  unconfirmed offsets.
- Treat attack and hitbox tables as game data, not memory offsets.
- Prefer derived velocity for online behavior.
- Restore the installed hook before normal process exit.
- Keep generated executables, object files, runtime logs, release archives,
  and generated Slint bindings out of source control.
