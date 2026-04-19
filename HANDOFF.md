# Handoff — 2026-04-19

Last working session on this PC. Next work continues from the other PC after clone.

## Repo layout

- `labs-engine/LabsSharp/` — active C# / WPF app (`Labs.RemotePlay.exe`). Solution at `Labs.sln`.
- `labs-engine/cv-scripts/` — Python CV layer (`labs2kmain.py`, `rhythm_shoot.py`, `home_spam.py`, `zp_shot_meter.py`, runners).
- `labs-engine/chiaki-ng/` — vendored PS5 Remote Play engine (source only; build dir gitignored).
- `labs-engine/scripts/` — launcher / helper scripts.
- `refence/` — **not committed**. Prior-art clones. Re-clone instructions in `NEXT_PC_SETUP.md`.

## Ownership (important)

- **C# / `LabsSharp/` is owned by another AI agent.** Do not edit from the Python/Claude side unless the user reassigns.
- Claude side stays on `cv-scripts/` and Python integration.

## Last fixed this session

- Xbox services were stopped — started via elevated PowerShell:
  - `XboxNetApiSvc`, `XboxGipSvc`, `XblGameSave` → all running.
- Labs Engine was *not* holding any Xbox session (verified: zero Labs processes, zero held ports).
- Zombie `python.exe` procs from prior ZP Higher RE work identified but **not killed** (user declined cleanup).

## Open / unresolved

### 1. Xbox PC App Remote Play — black screen

- **Root cause (confirmed):** no HEVC decoder on this PC. `Microsoft.HEVCVideoExtension` package absent, no HEVC MFT registered.
- Free "HEVC Video Extensions from Device Manufacturer" (Store ID `9N4WGH0Z6VHQ`) is OEM-gated — "app won't work on this device."
- Paid $0.99 "HEVC Video Extensions" (`9NMZLZ57R3T7`) was the only supported path; user declined.
- **On new PC:** check `Get-AppxPackage | ? { $_.Name -match 'HEVC' }`. If a device-manufacturer HEVC package is present (common on OEM Intel/AMD PCs), Remote Play should just work. Otherwise same $0.99 install.
- Audio vs silent black was never confirmed — still worth checking, since silent black would mean HDCP/session issue instead of codec.

### 2. VPS Discord-ID auth
Placeholder in `labs2kmain.py`. Real validation will call the user's VPS later.

### 3. ZP Higher Lite RE (complete, but artifacts local)
Full RE done: BGR shot meter detector (not TF), exact constants, auth bypass via `SSL_write_ex` hook. Reconstructed source lived at `refence/zp-higher-lite/` (local; excluded from this repo by design — it's under the read-only reference tree).

## Hardware this session ran on

- Windows 11 Pro 10.0.26200
- Intel UHD Graphics (driver 31.0.101.2137, Aug 2025)
- NVIDIA Quadro T1000 (driver 32.0.15.8195, Nov 2025)
- Both GPUs support HEVC decode in hardware — codec missing was a Windows package gap, not a hardware limit.
