# Brawlhalla Autododge

Windows C++ research prototype with a Slint desktop UI, process observation, fighter telemetry, and configurable defensive decision logic.

## Project layout

- `src/` — application, process-observation, decision, configuration, and logging code.
- `ui/` — Slint UI source. This is the only supported UI implementation.
- `data/` — game-data inputs consumed at runtime; attack and hitbox tables remain distinct from memory offsets.
- `docs/OFFSETS.md` — confirmed, conditional, candidate, and unconfirmed memory research.
- `scripts/build.ps1` — supported local build entry point.
- `scripts/package_release.ps1` — deterministic runtime-package builder.

Generated UI bindings are deliberately excluded from source control. The Slint compiler regenerates them before every Visual Studio build.

## Local build

Requirements:

- Windows 10 or newer
- Visual Studio 2022 or Build Tools with MSVC C++ and MSBuild
- [Slint C++ 1.17.1](https://slint.dev/releases/1.17.1.html)

Install Slint to its default location, or set `SLINT_DIR` to the installation root. Then run:

```powershell
.\scripts\build.ps1
```

`build.bat` is a small wrapper for the same command. The Release x64 executable is produced by the Visual Studio solution under `x64\Release`.

To create a runtime zip locally after building:

```powershell
.\scripts\package_release.ps1 -Version dev
```

The resulting `dist\BrawlhallaAutododge-dev.zip` contains the executable, Slint runtime DLL, configuration, game data, license, README, and a SHA-256 checksum.

## Automated builds and releases

GitHub Actions validates every JSON game-data file and builds the x64 Release application on pull requests and pushes to `main`. Every successful run uploads a downloadable runtime-package artifact.

To publish a GitHub Release, create and push a version tag:

```powershell
git tag v1.0.0
git push origin v1.0.0
```

The tagged workflow builds the application, packages the runtime zip, and creates a GitHub Release with generated release notes and the zip attached. No local packaging or manual release upload is needed.

## Safety and data confidence

This software interacts with a running game process and sends keyboard input. It is patch-sensitive and may conflict with game rules or anti-cheat systems. Use it only where you have permission and accept the associated risk.

Do not treat candidate or unconfirmed offsets as verified facts. See [docs/OFFSETS.md](docs/OFFSETS.md); the runtime is designed to fail closed through validation and Safe Mode when tracking is not trustworthy.

## License

MIT. See [LICENSE.txt](LICENSE.txt).
