"""
xinput_test.py — quick diagnostic for the XInput pipeline SecretK.py uses.

Run:
  python xinput_test.py

It prints the resolved DLL, scans all 4 slots, and then live-prints stick/trigger/
button changes for 10 seconds so you can confirm the controller is being read.
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "cv-scripts"))
from shot_meter import _read_xinput, xinput_status, _XI, _XI_NAME  # noqa: E402

st = xinput_status()
print(f"XInput DLL : {st['dll'] or 'NOT FOUND'}")
print(f"Connected  : slots {st['slots'] if st['slots'] else '(none)'}")
if _XI is None:
    print("ERROR: no XInput DLL loaded. Install DirectX runtime or run on Win8+.")
    sys.exit(1)
if not st["slots"]:
    print("WARN: no controller plugged in. Plug one in and re-run, "
          "OR press something now — live poll runs for 10s.")

print("\nLive poll (10s) — move sticks / press buttons:")
print("-" * 60)

last = None
end  = time.perf_counter() + 10.0
while time.perf_counter() < end:
    gp = _read_xinput(0)
    if gp:
        sig = (gp.wButtons,
               gp.bLeftTrigger // 32, gp.bRightTrigger // 32,
               gp.sThumbLX // 4096, gp.sThumbLY // 4096,
               gp.sThumbRX // 4096, gp.sThumbRY // 4096)
        if sig != last:
            print(f"btn=0x{gp.wButtons:04X}  "
                  f"LT={gp.bLeftTrigger:3d} RT={gp.bRightTrigger:3d}  "
                  f"LS=({gp.sThumbLX:+6d},{gp.sThumbLY:+6d})  "
                  f"RS=({gp.sThumbRX:+6d},{gp.sThumbRY:+6d})")
            last = sig
    time.sleep(1.0 / 100)

print("-" * 60)
print("Done.")
