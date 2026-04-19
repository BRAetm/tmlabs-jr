# CLAUDE.md — labs engine final

Instructions for Claude Code working in this repo.

## Collaborator profile

- User is a working developer. Skip beginner explanations, skip hand-holding, skip praise.
- No "here's what I did" recaps — the diff is the recap.
- No trailing summaries of obvious changes. State what's non-obvious, then stop.
- If a request is ambiguous, ask one short question instead of guessing.

## What this project is

Successor to [refence/labs-engine/](refence/labs-engine/) — a Windows-only PS5 Remote Play + capture hub.

**Stack decisions (locked in):**
- C++ 17, Qt 6.8.3 (`C:\Qt\6.8.3\msvc2022_64`), MSVC 14.44 (VS Build Tools 2022), CMake + Ninja.
- Architecture mirrors [refence/helios-source/source_reconstructed/](refence/helios-source/source_reconstructed/): a main exe + `*Core.dll` + plugin DLLs, each plugin exposing `createPlugin()` returning an `IPlugin*`.
- Branding: **Labs Engine** everywhere — namespace `Labs`, exe `LabsEngine.exe`, core `LabsCore.dll`, export macro `LABSCORE_API`, main window `LabsMainWindow`. Never emit Helios-named identifiers into [app/](app/).
- All finalized source lives under [app/](app/). Build output goes to `app/build/msvc-{debug,release}/bin/`.

**Build:** `powershell -ExecutionPolicy Bypass -File app/scripts/build.ps1 [-Release] [-Run] [-Clean]`

## Reference tree — READ-ONLY

`refence/` contains two cloned repos used only as prior art. **Never edit, build, or run anything inside `refence/`.** Read it freely for context.

- [refence/labs-engine/](refence/labs-engine/) — prior version. C# WPF over stripped `chiaki-ng` native engine (`labs.dll`), with Python CV scripts (`cv-scripts/`, `bot.py`, `decoderx.py`, `divertx.py`, `nba2k_helper.py`) and plugin tree. Read its `README.md` and `BUILD_NOTES.md` before proposing architecture.
- [refence/helios-source/](refence/helios-source/) — reconstructed Helios II API + Frida trace scripts + plugins. Useful for: reconstructed inference-core API shape (`reconstructed_InferenceCore_api.h`), plugin loading pattern, Frida instrumentation patterns (`frida_helios_trace.js`, `frida_nba2k_helper.js`).

When pulling an idea from `refence/`, cite the file path so the user can verify.

## Working conventions

- Prefer editing existing files over creating new ones. No speculative abstractions, no scaffolding for features that aren't requested.
- No comments unless the *why* is non-obvious. Never restate what the code does.
- No markdown docs beyond this file and `README.md` unless the user asks.
- `refence/` has its own `.git` dirs — do not stage or commit them from the parent.
- Paths on this machine contain spaces (`labs engine final`). Quote them in shell commands.

## Environment

- Windows 11, bash shell (Unix syntax — `/dev/null` not `NUL`, forward slashes).
- GitHub CLI is authenticated as `BRAetm`.
- This directory is **not** a git repo yet. Confirm with the user before `git init` or before pushing anything to GitHub.

## Before risky actions

Confirm first: `git init`, creating remotes, force pushes, mass deletes, anything touching `refence/`, installing system-level toolchains (vcpkg, MSVC workloads, CUDA). Local file edits and builds in this directory are fine without confirmation.
