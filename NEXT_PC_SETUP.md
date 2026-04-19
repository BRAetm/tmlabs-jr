# Next PC setup

Steps to bring this repo up on the other PC after cloning.

## 1. Clone

```bash
gh auth login   # auth as BRAetm if not already
gh repo clone BRAetm/labs-engine-final
cd labs-engine-final
```

## 2. Re-hydrate `refence/`

Not committed — clone the two prior-art repos side-by-side:

```bash
mkdir refence && cd refence
gh repo clone BRAetm/labs-engine
gh repo clone BRAetm/helios-source
cd ..
```

`refence/zp-higher-lite/` was local RE output from the previous PC; if you need it, copy it over manually. Not on GitHub.

## 3. Toolchain

- **Python 3.11** (for `cv-scripts/`)
- **.NET 8 SDK** (for `labs-engine/LabsSharp/Labs.sln`)
- **Visual Studio 2022** or **VS Build Tools 2022** (MSVC 14.44) if rebuilding chiaki-ng native
- **vcpkg** (pre-bootstrapped dir is gitignored; re-bootstrap under `labs-engine/.vcpkg/` if needed)

Python deps per script (`cv-scripts/*.py`) — no requirements.txt at root; each script has its own imports. Install as needed: `opencv-python`, `numpy`, `pywin32`, `frida`, etc.

## 4. Xbox Remote Play — HEVC codec

Before anything else, check codec state:

```powershell
Get-AppxPackage | ? { $_.Name -match 'HEVC' } | Select Name,Version
```

- Empty → black screen on Xbox Remote Play. Try the free OEM extension first: `ms-windows-store://pdp/?productid=9N4WGH0Z6VHQ`. If "won't work on this device," fall back to the paid one: `ms-windows-store://pdp/?productid=9NMZLZ57R3T7` ($0.99).
- Also verify Xbox services are running:
  ```powershell
  Get-Service XblAuthManager,XblGameSave,XboxGipSvc,XboxNetApiSvc
  ```
  All four should say `Running`. If stopped, elevated PowerShell: `Start-Service XboxNetApiSvc,XboxGipSvc,XblGameSave`.

## 5. Build & run LabsSharp

```bash
cd labs-engine/LabsSharp
dotnet build Labs.sln -c Debug
dotnet run --project Labs.RemotePlay.Wpf
```

## 6. Python CV side

```bash
cd labs-engine/cv-scripts
python labs2kmain_runner.py
```

## Known state at handoff

See `HANDOFF.md` for full details. TL;DR:

- Xbox Remote Play black screen = missing HEVC codec on source PC (unresolved — deferred to next PC).
- C# / LabsSharp is off-limits to Claude (owned by another AI).
- PS5 side via chiaki-ng is intact but not actively being worked.
