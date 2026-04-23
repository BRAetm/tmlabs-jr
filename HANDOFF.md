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

## 2026-04-23 — Architecture pivot to Path A (wrap, don't build Remote Play)

**Decision:** LabsEngine no longer builds its own Remote Play stack. Instead it wraps
whatever Remote Play app the user already has open (Chiaki, Xbox Remote Play, or the
official PS Remote Play). LabsEngine becomes a "scripts + capture + virtual controller
routing" hub. ZP HIGHER Lite proved this model works — they don't ship Remote Play either.

**Why:** building consumer-grade Remote Play is a quarter of work (audio sink,
auto-reconnect, UDP discovery, adaptive bitrate, pairing without LabsSharp, FFmpeg
decode threading, WoL, error states, profiles per console). The shot timer is the
differentiator — that's where the time should go.

**What this means for the C++ side (other AI's TODO):**
- **Rip the entire `app/plugins/ps_remote_play/` plugin.** Including FFmpeg, `labs.dll`,
  PSN OAuth, pairing dialogs, all of it.
- **Rip `importLabsSharpPairing()`** from `app/engine/LabsMainWindow.cpp` — no more
  reading `%APPDATA%\PSRemotePlay\hosts.json`.
- **Keep:** XInput plugin (with skip mask 0x01), ViGEm output plugin, WGC capture
  plugin (still useful for Xbox Remote Play window mode), Display plugin, CV Python
  plugin, all the engine settings UI.
- **Update:** Mode switcher becomes "What's the streaming app?" → Chiaki / Xbox Remote
  Play / Official PS. Each just sets the `find_window()` keyword the script uses.
- **First-run flow:** "1. Open your Remote Play app and start streaming. 2. Click
  Start Engine in Labs Engine." That's it. No pairing, no PSN login, no codec settings.
- **Settings UI** (Shooting / Defense / Features / Engine tabs) belongs in the C++ app
  now, not in any Python UI. Pass values to SecretK.py via CLI args.

## 2026-04-23 — SecretK.py is now ONE script (PySide6 UI deleted)

The PySide6 UI version of SecretK.py was a "second app" mistake — SecretK was supposed
to be just a script run by LabsEngine. Deleted that file. The headless `engine_core.py`
got renamed to `labs-engine/scripts/SecretK.py` (the path LabsEngine's CV plugin
already looks at).

The script side is done — accepts CLI args from LabsEngine, captures via BetterCam or
mss, runs shot meter detection, fires gamepad via vgamepad. ~232 lines. No UI.

## 2026-04-22 — Earlier update (engine_core.py built)

After exploring two architectures, settled on the simpler one: `engine_core.py` does its own window-finding + BetterCam capture + vgamepad output, exactly like SecretK.py but headless. **No SHM channel for frames or gamepad** — vgamepad is the controller transport, LabsEngine's XInput plugin picks up the virtual pad on slot 1+.

### Where SecretK.py lives now
Not deleted — moved to `labs-engine/scripts/SecretK.py`. Still importable / runnable as the standalone PySide6 app. `engine_core.py` is the headless equivalent that LabsEngine's `CvPythonPlugin` auto-spawns.

### What this session built
- **`labs-engine/engine_core.py`** — 153 lines. Spawned by LabsEngine with `--labs-pid <PID> --session <SID>` (both args accepted but ignored — no SHM channel needed):
  - `find_window()` from `shot_meter.py` locates LabsEngine's "Remote Play" window
  - BetterCam captures that region at 240fps target
  - `ShotMeterDetector` (cv-scripts/shot_meter.py) does the BGR meter detection
  - `PSControllerBridge` (cv-scripts/features.py) handles all gamepad output through VDS4 + VX360 virtual pads
  - Detection loop fires `bridge.fire_shot()` (or `bridge.contest_flick()` in defense mode)
  - 500Hz `bridge.passthrough()` thread mirrors physical XInput onto virtual pads
  - DPAD_UP rising edge in passthrough toggles defense mode (consumes `bridge._qs_toggle_requested` flag)
  - SIGINT/SIGTERM/SIGBREAK clean shutdown
  - Stdout lines for LabsEngine UI: `[ENGINE] SHOT #N`, `[CAL] normal N/4`, `[STATUS] fps=N shots=N cal=(...) defense=...`
  - Flags: `--threshold`, `--threshold-l2`, `--tempo`, `--tempo-ms`, `--stick-tempo`, `--quickstop`, `--defense`, `--stamina`, `--no-hands-up`, `--xi-index`, `--gpu`

### How the integration works
1. LabsEngine starts → window title contains "Remote Play" so `find_window()` finds it
2. CvPythonPlugin auto-spawns `engine_core.py` with `--labs-pid`/`--session` args (ignored)
3. engine_core BetterCam-captures LabsEngine's window
4. PSControllerBridge spins up VDS4 + VX360 virtual gamepads via vgamepad/ViGEm
5. LabsEngine's XInput plugin reads the virtual pad (skip mask 0x01 skips physical slot 0)
6. LabsEngine forwards the virtual pad state to PS5 via `labs_session_set_controller_state`

### Integration test steps
1. `pip install bettercam vgamepad opencv-python numpy pywin32` on the PC
2. In LabsEngine settings, set `cv/scriptPath` = `labs-engine/engine_core.py` (and `cv/pythonPath` if Python isn't on PATH)
3. Start LabsEngine → PS mode → engine_core.py spawns (visible as child python.exe in Task Manager)
4. Watch the log strip for `[ENGINE] BetterCam region=...` and `[STATUS] fps=N` each second
5. Load NBA 2K, hold 4 shots → `[CAL] normal 1/4`...`4/4` → `[BGR-METER] Calibration LOCKED`
6. Release a shot → `[ENGINE] SHOT #1`, RT pulses on virtual pad → forwarded to PS5
7. Tap DPAD_UP → `[BRIDGE] defense ON`, RS auto-hands-up flick fires

### Known risks for live testing
- **Virtual pad slot assignment**: vgamepad usually lands the VX360 on slot 1 if physical is on 0. If something else is plugged in (real Xbox controller, joystick), the virtual pad could land on slot 2 or 3. LabsEngine's skip mask is 0x01 (skip slot 0 only) so anything on 1, 2, or 3 still gets read — should be fine.
- **VDS4 detection by Chiaki/SDL**: PSControllerBridge sends an OPTIONS-button pulse on init to wake the VDS4 enumeration. If LabsEngine's PS Remote Play plugin reads via XInput (VX360 path), the VDS4 is unused — could remove that pulse if it confuses anything.
- **find_window() before LabsEngine displays**: If engine_core.py runs before LabsEngine's window has its title set, `find_window()` falls back to the full primary monitor. Restart engine_core.py after LabsEngine's title is `"Labs Engine - PS Remote Play"`.

### Files of interest
- [labs-engine/engine_core.py](labs-engine/engine_core.py) — entry point
- [labs-engine/cv-scripts/shot_meter.py](labs-engine/cv-scripts/shot_meter.py) — BGRMeterDetector + find_window + _read_xinput
- [labs-engine/cv-scripts/features.py](labs-engine/cv-scripts/features.py) — PSControllerBridge (VDS4 + VX360, defense, tempo, quickstop, contest flick)
- [labs-engine/cv-scripts/network_optimizer.py](labs-engine/cv-scripts/network_optimizer.py) — 7 TCP registry tweaks, not wired in yet
- [labs-engine/scripts/SecretK.py](labs-engine/scripts/SecretK.py) — standalone PySide6 app version (kept around)
- [app/plugins/cv_python/CvPythonPlugin.cpp](app/plugins/cv_python/CvPythonPlugin.cpp) — spawn logic (still passes `--labs-pid`/`--session`, harmless)
- [app/plugins/xinput_input/XInputPlugin.cpp](app/plugins/xinput_input/XInputPlugin.cpp) — reads the virtual pad
- Plan file: `C:\Users\TM\.claude\plans\tingly-kindling-meerkat.md` (slightly stale — describes the SHM version)

### GitHub
Pushed to `https://github.com/BRAetm/tmlabs-jr` (public). Origin remote name is `tmlabs`. Old origin `labs-engine-final` still exists too.

### Optional future work (not blocking)
- Wire `network_optimizer.apply()/restore()` into engine_core.py startup/shutdown
- `--fire-button` flag for RB/A instead of RT
- Live settings (read settings file or stdin) so LabsEngine UI can tweak thresholds without restart
- Delete `labs-engine/cv-scripts/zp_shot_meter.py` if confirmed unused

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
