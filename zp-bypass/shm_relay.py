"""
Watches bypass_out.txt for ZP release events and forwards RT press
to Labs Engine's gamepad SHM so Xbox Remote Play receives the input.
"""
import mmap, struct, ctypes, os, time, threading

LABS_PID   = 27656
LOG_PATH   = r"C:\ghidra_work\bypass_out.txt"

_MAGIC = 0x5342414C
_HDR, _PAY, _BLK = 32, 64, 96
_OFF_AXS, _OFF_BTN, _OFF_SID = 32, 48, 68
BTN_R2 = 7

# ── SHM setup ─────────────────────────────────────────────────────────────────
shm = mmap.mmap(-1, _BLK, tagname=f"Labs_{LABS_PID}_Gamepad")
if struct.unpack_from("<I", shm, 0)[0] != _MAGIC:
    struct.pack_into("<I", shm, 0, _MAGIC)
    struct.pack_into("<I", shm, 4, 1)
    struct.pack_into("<I", shm, 8, os.getpid())
    struct.pack_into("<I", shm, 16, _PAY)

k32  = ctypes.windll.kernel32
_seq = [0]

def fire_rt():
    ev = k32.OpenEventW(0x1F0003, False, f"Global\\Labs_{LABS_PID}_Gamepad_Written")
    for pressed in [True, False]:
        for i in range(4):
            struct.pack_into("<f", shm, _OFF_AXS + i * 4, 0.0)
        for i in range(17):
            struct.pack_into("<B", shm, _OFF_BTN + i,
                             1 if (pressed and i == BTN_R2) else 0)
        struct.pack_into("<i", shm, _OFF_SID, 0)
        _seq[0] = (_seq[0] + 1) & 0xFFFFFFFF
        struct.pack_into("<I", shm, 12, _seq[0])
        if ev:
            k32.SetEvent(ev)
        if pressed:
            time.sleep(0.05)
    if ev:
        k32.CloseHandle(ev)
    print(f"[RELAY] RT fired -> Labs Engine")

print(f"[RELAY] Watching {LOG_PATH} for ZP releases (Labs PID={LABS_PID})")
print(f"[RELAY] SHM: Labs_{LABS_PID}_Gamepad opened")

# ── Watch log for release count changes ───────────────────────────────────────
last_releases = -1

with open(LOG_PATH, "r", encoding="utf-8", errors="ignore") as f:
    f.seek(0, 2)  # seek to end
    while True:
        line = f.readline()
        if not line:
            time.sleep(0.01)
            continue
        if "releases=" in line:
            try:
                val = int(line.split("releases=")[1].split()[0])
                if last_releases >= 0 and val > last_releases:
                    threading.Thread(target=fire_rt, daemon=True).start()
                last_releases = val
            except Exception:
                pass
