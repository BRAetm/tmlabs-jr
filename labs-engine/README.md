# PS Remote Play (Labs)

Windows-only PS5 remote-play feature. C# WPF frontend over a native engine
(forked from chiaki-ng, rebranded `labs`, stripped to Windows/PS5 only).

## Layout

```
remote jr/
├── chiaki-ng/              native C engine source (→ labs.dll)
│   ├── CMakeLists.txt
│   ├── lib/                engine source (session, crypto, PSN regist, decoders)
│   ├── third-party/        vendored deps (nanopb, jerasure, curl)
│   └── scripts/            helper scripts
├── LabsSharp/              C# solution
│   ├── Labs.sln
│   ├── Native/             LabsNative.cs  (P/Invoke for labs.dll)
│   ├── Labs.Engine/        managed wrappers (discovery, regist, session)
│   ├── Labs.RemotePlay.Wpf/ WPF app (the UI)
│   └── Labs.Tests/         console smoke test
└── scripts/
    └── build-native.ps1    builds labs.dll via vcpkg + CMake + MSBuild
```

> The folder is still on disk as `chiaki-ng/` because it was locked during
> the rename. Feel free to `mv chiaki-ng labs-engine` when no process is
> holding it open — then update `build-native.ps1` to match.

## Prerequisites

- **Visual Studio 2022** with "Desktop development with C++" workload
- **.NET 8 SDK** (already installed on this machine ✓)
- **Python 3** on PATH (nanopb protobuf generator)
- **Git** (already installed ✓)

## Build

### 1. Native engine → `labs.dll`

```powershell
.\scripts\build-native.ps1
```

Clones vcpkg into `.vcpkg/`, installs deps (openssl, curl, opus, ffmpeg,
miniupnpc, libevent, json-c, protobuf), configures CMake, builds Release,
copies `labs.dll` + runtime DLLs to `chiaki-ng/build/bin/`.

First run takes ~30 minutes (vcpkg compiles deps from source). Subsequent
runs are fast.

### 2. C# solution

```powershell
cd LabsSharp
dotnet build -c Release
```

Labs.Engine.csproj auto-copies `chiaki-ng/build/bin/labs.dll` next to the
output binary.

### 3. Run

```powershell
dotnet run --project Labs.RemotePlay.Wpf -c Release
```

Or open `LabsSharp\Labs.sln` in Visual Studio.

## Using it

1. Turn on the PS5 (or leave in rest mode with Remote Play enabled).
2. Launch the WPF app — it broadcasts discovery on UDP/9302 and lists any
   consoles it sees.
3. Select a console → **Register**. Enter:
   - **PSN Account ID** (base64). One-time retrieval: any of the community
     helpers will produce this, e.g. `scripts/psn-account-id.py` in the
     native engine tree, or <https://psn.flipscreen.games/>.
   - **PIN** — on the PS5: *Settings → System → Remote Play → Link Device*.
4. **Connect** to start streaming.

## Smoke test without the native DLL

```powershell
dotnet run --project LabsSharp\Labs.Tests -c Release
```

Runs pure-managed PS5 LAN discovery for 10 seconds. Does NOT need `labs.dll`.
Useful to sanity-check your network and that the app can see the console.

## Status

See `BUILD_NOTES.md` for what's wired, what's stubbed, and the next
work items to take streaming end-to-end.

## License

Native engine is AGPL-3.0 (inherited from chiaki-ng). The managed
`LabsSharp/` code is yours.
