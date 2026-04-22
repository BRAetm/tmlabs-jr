# HANDOFF — Labs Engine

Snapshot for continuing work after a PC switch. Read this first before touching anything.

---

## What this project is

Windows-only hub for **PS5 Remote Play + NBA 2K shot-timing automation**.

- **`LabsEngine.exe`** (Qt C++, MSVC, CMake + Ninja) — streams PS5 Remote Play into a window, exposes a plugin architecture (XInput, ViGEm, WGC, CV Python, Display, PS Remote Play), launches Python scripts via a subprocess.
- **`SecretK.py`** (PySide6 Python) — separate window that captures the Labs Engine streaming window, watches the shot meter with OpenCV, and fires R2 at peak timing through a `vgamepad` virtual X360 controller.

The two apps are independent processes. The integration contract:
1. LabsEngine's window title contains `"Remote Play"` so SecretK's `find_window()` can find it.
2. SecretK's virtual X360 gamepad (slot 1+) is picked up by LabsEngine's XInput plugin, which forwards the augmented state to the PS5 via `labs_session_set_controller_state`.
3. In PS mode, LabsEngine auto-skips XInput slot 0 (the real pad) so the virtual-pad flow isn't shadowed.

---

## How to build + run

```powershell
powershell -ExecutionPolicy Bypass -File app/scripts/build.ps1 -Release
# Launch:
Start-Process "app/build/msvc-release/bin/LabsEngine.exe"
```

Debug build: drop `-Release`. Clean rebuild: add `-Clean`.

Paths on the machine currently have a space (`labs engine final`) — quote them in shell commands.

---

## What works end-to-end (verified in code, pipeline plumbed)

- Plugin architecture loads 7 plugins (`app/plugins/`). Mode switcher: **Xbox (WGC)** and **PS Remote Play**.
- Credentials auto-import from LabsSharp on startup: reads `%APPDATA%\PSRemotePlay\hosts.json` + `account.txt` → writes `ps/registKey`, `ps/morning`, `ps/psnAccountId`, `ps/isPs5` into our settings. See `importLabsSharpPairing()` in `app/engine/LabsMainWindow.cpp`.
- PS Remote Play session config aligned to LabsSharp's proven values: H.264, 15 Mbps, DualSense on, `packet_loss_max=0.05`, `psn_account_id` zeroed for LAN. See `PSRemotePlayPlugin::start()`.
- Scripts rail: default-fills `labs-engine/scripts/SecretK.py`, `run`/`stop` buttons, QProcess launches it, stdout pipes to the log strip. Run button is gated on `haveSource && haveScript && !running`.
- Window title set to `"Labs Engine - PS Remote Play"` so SecretK's `find_window()` matches the `"Remote Play"` substring.
- XInput plugin has a `Q_INVOKABLE setSkipMask(int)`; `LabsMainWindow::applyMode()` sets mask=0x01 in PS mode (skip slot 0) and 0x00 in Xbox mode.
- DisplayPlugin renders BGRA frames into a `DisplaySurface` in the center stage.
- PSN OAuth: embedded WebView2 was removed (Sony blocked it). Replaced with "open in default browser + paste redirect URL" flow. See `app/plugins/ps_remote_play/PsnLoginDialog.cpp`.

---

## What's unverified on real hardware

The code path is correct, but nothing below has been run against a live PS5 on this machine since the last changes:

1. **PS5 streaming** — pairing credentials are imported from LabsSharp; `labs_session_start` should succeed. Last known-bad state: pairing from the in-app dialog was failing with `LABS_REGIST_EVENT_TYPE_FINISHED_FAILED` (likely PIN timeout / wrong IP). Auto-import sidesteps this.
2. **SecretK → PS5 via virtual pad** — the full chain requires vgamepad to actually land on slot 1 (not 2/3) with a real pad on slot 0. If the virtual pad ends up on a slot other than 1, expand the skip mask in `applyMode()` to cover more slots (e.g. `0x01 | 0x04` would skip 0 and 2 but keep 1 and 3 live — not usually what you want; adjust as needed after observing real slot assignment).
3. **FFmpeg decode under load** — decode runs on the video callback thread. May cause drops at high bitrates. Not a ship blocker but noted.

---

## Last thing I was doing

User hit multiple pairing failures ("Pairing failed — check IP, account link, and PIN"). Ended up discovering LabsSharp (archived) had already paired the user's PS5, so wired in `importLabsSharpPairing()` to skip pairing entirely. Then aligned our session config to match LabsSharp's proven values. Then wrote/executed the ship plan covering the three remaining integration gaps:

- **P1.1** title fix ✅ applied
- **P1.2** XInput skip mask ✅ applied
- **P1.3** dead `ci.host` removed ✅ applied

Built cleanly, launched, title verified. Awaiting the user's end-to-end test on the PS5.

---

## 2026-04-22 update — engine_core.py built, SHM integration ready

Shift in architecture: SecretK.py (standalone Python app with its own UI + BetterCam capture + vgamepad output) is **replaced** by a headless `engine_core.py` that LabsEngine's CvPythonPlugin auto-spawns and talks to via SHM. User deleted `SecretK.py`, `userdata/`, `refence/`, and `zp-higher-lite-full/` from the tree to clean up.

### What this session built
- **`labs-engine/engine_core.py`** — headless entry point. Auto-spawned by LabsEngine with `--labs-pid <PID> --session <SID>`. Reads frames from `Labs_<labsPid>_Frame_<sid>` SHM, writes gamepad to `Labs_<mypid>_Gamepad` SHM, signals via named events. Feature-complete:
  - Frame SHM reader with magic validation + 30s retry
  - Gamepad SHM writer with proper magic, sequence, event signaling
  - 500Hz XInput passthrough
  - Shot meter detection via `cv-scripts/shot_meter.py::ShotMeterDetector`
  - `FeatureBridge` class with defense AI, stamina scaling, tempo, stick tempo, quickstop, contest flick, auto-hands-up
  - DPAD_UP rising edge → toggles defense (matches ZP reference UI)
  - SIGINT/SIGTERM/SIGBREAK clean shutdown
  - Periodic `[STATUS]` + `[CAL]` + `[ENGINE] SHOT #N` stdout lines for LabsEngine to parse
  - Flags: `--threshold`, `--threshold-l2`, `--tempo-ms`, `--tempo`, `--stick-tempo`, `--quickstop`, `--defense`, `--stamina`, `--no-hands-up`, `--xi-index`

### C++ side status (verified by exploration)
- **Frame SHM publishing**: `FrameShmWriter::write()` in [app/plugins/cv_python/ShmBus.cpp:84-107](app/plugins/cv_python/ShmBus.cpp) — active, publishes BGRA at offset 64 with dynamic stride. WGC source at display refresh; PS Remote Play at `ps/fps` setting (default 60).
- **Auto-spawn**: `CvPythonPlugin::launchPython()` at [app/plugins/cv_python/CvPythonPlugin.cpp:84-113](app/plugins/cv_python/CvPythonPlugin.cpp) — launches configured `cv/scriptPath` with `--labs-pid`/`--session` on plugin start.
- **Gamepad → ViGEm**: `GamepadShmReader` → `CvPythonPlugin::setSink` → fan-out → `ViGEmSink::pushState` wiring is complete. Fan-out is last-writer-wins.

### Known risks for live testing
- **Dual gamepad sources**: LabsEngine's XInput source plugin reads the physical pad AND our SHM writes it too. Both feed the same fan-out. RT fire pulse could be stomped by the physical pad's current RT. First thing to check if fires don't land in-game.
- **Frame format**: Script assumes BGRA at offset 64. Cross-check first frame with `width × height × 4 == payload_size` from header.
- **Session ID drift**: LabsEngine builds the frame block name from `--session` it spawned us with. If settings change mid-run, block name won't match.

### Integration test steps
1. In LabsEngine settings, set `cv/scriptPath` = `labs-engine/engine_core.py` and `cv/pythonPath` = working Python 3.11 with `numpy`, `opencv-python`, `pywin32` (for `shot_meter.find_window`, though we don't call it now — can drop pywin32 if unused).
2. Start LabsEngine → pick Xbox or PS mode → engine_core.py should spawn (child python.exe under LabsEngine in Task Manager).
3. In the log strip, watch for `[FRAME-SHM] attached`, `[STATUS] fps=N frames=N` each second.
4. Load NBA 2K via the stream, hold 4 shots to calibrate → `[CAL] normal N/4` then `[BGR-METER] Calibration LOCKED`.
5. Release a shot → `[ENGINE] SHOT #1` → virtual Xbox pad should briefly press RT.
6. Tap DPAD_UP → `[BRIDGE] defense ON` and a brief RS-up flick (auto-hands-up).

### Files of interest
- [labs-engine/engine_core.py](labs-engine/engine_core.py) — no changes needed unless a bug surfaces
- [labs-engine/cv-scripts/shot_meter.py](labs-engine/cv-scripts/shot_meter.py) — BGRMeterDetector (all ZP bytecode constants)
- [labs-engine/cv-scripts/features.py](labs-engine/cv-scripts/features.py) — old vgamepad-based PSControllerBridge, now unused by engine_core.py (kept for reference; could be deleted later)
- [labs-engine/cv-scripts/network_optimizer.py](labs-engine/cv-scripts/network_optimizer.py) — 7 TCP registry tweaks, not wired into engine_core.py yet
- [app/plugins/cv_python/ShmBus.cpp](app/plugins/cv_python/ShmBus.cpp) — byte layout reference
- [app/plugins/cv_python/CvPythonPlugin.cpp](app/plugins/cv_python/CvPythonPlugin.cpp) — spawn + sink wiring
- Plan file: `C:\Users\TM\.claude\plans\tingly-kindling-meerkat.md`

### Optional future work (not blocking the test)
- Live control SHM so LabsEngine's UI can adjust thresholds/flags without restart
- Stats SHM for richer UI display (stdout parsing is enough for now)
- Recalibrate signal (reset `_peak_history` mid-session)
- `--fire-button` flag to fire RB/A instead of RT
- Wire `network_optimizer.apply()/restore()` into engine_core.py startup/shutdown if low-latency toggle should follow the engine lifecycle
- Delete `labs-engine/cv-scripts/features.py` and `zp_shot_meter.py` once confirmed unused

---

## On first launch after PC switch

1. Read this file.
2. Read `CLAUDE.md` (project-specific conventions: no beginner hand-holding, MSVC + Qt 6.8.3, never edit `refence/`, etc.).
3. Check memory at `.claude/memory/MEMORY.md` — has project context, user preferences, and past feedback.
4. Verify environment:
   ```powershell
   python -c "import PySide6, cv2, mss, vgamepad, bettercam; print('ok')"
   ```
   If any are missing, `pip install` them. SecretK requires all of the above.
5. Verify Qt + MSVC tooling:
   - Qt 6.8.3 at `C:\Qt\6.8.3\msvc2022_64`
   - VS Build Tools 2022 + CMake + Ninja on PATH
6. Run a rebuild to sanity-check: `powershell -ExecutionPolicy Bypass -File app/scripts/build.ps1 -Release`. Should link `LabsEngine.exe` + 7 plugin DLLs.

---

## Critical files / paths

### Code to know
- `app/engine/LabsMainWindow.cpp` — main window, top bar, scripts rail, mode wiring, credential auto-import
- `app/engine/LabsMainWindow.h`
- `app/plugins/ps_remote_play/PSRemotePlayPlugin.cpp` — session lifecycle, FFmpeg decode, controller forward
- `app/plugins/ps_remote_play/PairPSDialog.cpp` — in-app pairing (not needed thanks to auto-import)
- `app/plugins/ps_remote_play/PsnLoginDialog.cpp` — "paste redirect URL" PSN login
- `app/plugins/xinput_input/XInputPlugin.{h,cpp}` — gamepad polling + skip mask
- `app/plugins/vigem_output/ViGEmPlugin.cpp` — virtual X360 for Xbox mode
- `app/plugins/display/DisplayPlugin.cpp` — frame renderer
- `labs-engine/scripts/SecretK.py` — the 2K automation script
- `labs-engine/cv-scripts/{shot_meter,features,network_optimizer}.py` — SecretK's deps

### Settings / persistence
- Our settings (Qt QSettings): written by `SettingsManager`. Check via `.filePath()` shown in the left rail.
- LabsSharp paired hosts: `%APPDATA%\PSRemotePlay\hosts.json`
- LabsSharp PSN account: `%APPDATA%\PSRemotePlay\account.txt`
- SecretK user settings: `labs-engine/scripts/userdata/settings/nba2k_settings.current`

### Build output
- Release: `app/build/msvc-release/bin/LabsEngine.exe` + `plugins/*.dll`
- Debug: `app/build/msvc-debug/bin/LabsEngine.exe`

---

## Archive contents

Moved out of the project to `C:\Users\TM\Desktop\labs-archive\` to keep the tree clean:

- `LabsSharp/` — the previous C# WPF version. **Keep around** — it's the proven-working reference for PS Remote Play session config (see `Labs.Engine/Session/StreamSession.cs`) and UDP discovery (see `Labs.Engine/Discovery/HostDiscovery.cs`).
- `refence/` — clones of `chiaki-ng` and `helios-source`. Read-only reference for protocol / API patterns.
- `zp-bypass/`, `zp-higher-lite-full/` — unrelated to 2K/PS Remote Play.
- `HANDOFF.md`, `NEXT_PC_SETUP.md`, `aqtinstall.log` — stale docs from the prior PC migration.

---

## Known stretch work (skipped for ship)

- **UDP PS5 discovery** in Pair dialog — LabsSharp broadcasts `"SRCH * HTTP/1.1\n..."` to `:9302`. Would eliminate manual IP entry. Reference impl: `labs-archive/LabsSharp/Labs.Engine/Discovery/HostDiscovery.cs`. ~60 LOC port to `PairPSDialog.cpp`.
- **Better error strings** — split the generic "settings incomplete" bailout in `PSRemotePlayPlugin::start()` into per-field messages. Surface last error in `LabsMainWindow::onStart()`.
- **Stall indicator** — in `onFpsTick()`, flip FPS pill red when `delta == 0` for 3+ seconds while `isRunning()`.
- **Audio pipeline** — no audio sink today; `audio_video_disabled=0` but nothing consumes audio callbacks.
- **Auto-reconnect** on `LABS_EVENT_QUIT`.
- **Split decode thread** — currently runs on the video callback thread (`publishFrame`). LabsSharp had a bounded channel → dedicated MTA COM thread.

Full plan document: `C:\Users\TM\.claude\plans\bright-orbiting-steele.md` (on old PC — may need to re-save to new PC's `.claude` directory if continuing).

---

## Notes / gotchas

- `labs-engine/engine_core.py` is now the primary Python integration (see 2026-04-22 update section above). SecretK.py has been removed from the tree.
- Pre-commit: this dir **may or may not be a git repo** depending on PC. The CLAUDE.md from earlier said it wasn't; snapshots show it is on `main`. Confirm with `git status` before any commit action.
- **Never edit `refence/` (archived)** — read-only reference.
- **C# `LabsSharp/` is owned by another AI per the user's instructions** — don't modify it. Read it for reference only.
- User prefers terse responses, no trailing summaries, no praise. Answer direct.
- Paths with spaces (`labs engine final`) need quoting in shell commands.
- Use the Bash tool's Unix syntax (`/dev/null`, not `NUL`; forward slashes) per environment notes.
