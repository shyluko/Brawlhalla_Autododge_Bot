# Project Guide

## Scope

This is a Windows C++ research prototype for Brawlhalla process observation,
fighter ESP, and automated input decisions. Active source files are in the
project root.

## Important Files

- `src/autoplay_main.cpp`: capture hook, actor identification, bot decisions,
  ESP, dashboard, and logs.
- `src/attack_table.h`: coarse move timing and selection metadata.
- `src/frame_hitbox_table.h`: frame-specific predictive hitboxes.
- `src/hitbox_table.h`: aggregate fallback attack bounds.
- `src/memory.h`: Win32 process-memory utility.
- `src/find_items_main.cpp`: optional actor/item inspection utility.
- `docs/OFFSETS.md`: offset confidence and compatibility notes.
- `README.md`: GUI build and consolidated-source notes.

## Build

Run `scripts/build_autoplay.bat` from a Visual Studio developer environment, or edit
the script's `vcvars64.bat` path for the installed Visual Studio version.

## Development Rules

- Prefer the strongest available coding model for C++/Win32 debugging,
  reverse-engineering, and validation work in this project.
- Preserve the distinction between confirmed, conditional, candidate, and
  unconfirmed offsets.
- Treat attack and hitbox tables as game data, not memory offsets.
- Prefer derived velocity for online behavior.
- Restore the installed hook before normal process exit.
- Keep generated executables, object files, and runtime logs out of public
  source packages.
