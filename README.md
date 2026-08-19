# Brawlhalla Autododge

Copyright (c) 2026 Shyluko

Brawlhalla Autododge is a Windows-focused research and automation project built around a local process monitor, live attack/telemetry logic, and a Dear ImGui control interface for tuning behavior.

This repository is intentionally split into two publishable package types:

1. Source release for developers and community builders
2. Runtime release for end users who only want the program and required assets

## Release split

### Source release
This package is for developers, researchers, and contributors who want to inspect, modify, compile, or rebuild the project.

It includes:
- full source code under `src/`
- configuration and data files for local builds
- third-party dependencies
- documentation and notes for rebuilds and research

### Public runtime release
This package is for users who just want to run the tool. It should be shipped as a plain folder containing:
- `brawlhalla_autododge.exe`
- `brawlhalla_autododge.exe.debug`
- required DLLs
- `config/`
- `data/`
- `README.txt`
- `README-UPLOAD.txt`
- `LICENSE.txt`

Do not mix these two packages into one upload.

## Build requirements

* Windows 10 or newer
* MinGW-w64 with `g++` and `objcopy` on PATH, or another compatible C++17/20 toolchain
* Brawlhalla installed locally for runtime testing
* Optional: Visual Studio tools for debugging and auxiliary builds

## Build instructions

From the project root:

```powershell
.\build.bat
```

This produces the main app binary in:

```text
bin\brawlhalla_autododge.exe
```

The release script also produces a symbol output for packaging:

```text
bin\brawlhalla_autododge.exe.debug
```

For Visual Studio, the equivalent release setting is:

- Project Properties -> Linker -> Debugging
- Set `Generate Debug Information (/DEBUG)`

## Packaging rules

For a clean public upload:

- build a release binary, not a debug build
- include the `.debug` symbol file next to the EXE
- keep the runtime folder plain and installable
- do not pack, password-protect, or obfuscate the archive
- keep `config/` and `data/` next to the EXE
- do not include Discord, Telegram, or other recruiting/social links in the public package
- remove personal machine paths, local developer paths, and private environment details

## Credits and attribution

This project incorporates reference material and research from:

* neiroXzz
* kingcar3125
* hwlee

Their work was referenced during development and should remain credited in forks and derived projects.

## License

This project is licensed under the MIT License. See `LICENSE.txt` for the complete license text.