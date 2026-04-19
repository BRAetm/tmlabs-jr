"""
zp_shot_meter.py — NBA 2K shot-meter detection for Labs Engine.

Reconstructed from ZP HIGHER Lite static analysis. Reads BGR frames
from the Labs Engine PS Remote Play stream, detects shot-meter fill %
via contour+pixel analysis, fires R2 via Labs gamepad SHM when the
velocity-predicted fill reaches the configured threshold.

Labs Vision API: on_start / on_frame / on_stop
"""
import ctypes
import time

import cv2
import numpy as np

# ── Thresholds (from ZP HIGHER Lite binary constants) ────────────────────────
TRIGGER_NORMAL = 0.95   # predicted fill >= this fires R2 on normal shot
TRIGGER_L2     = 0.75   # predicted fill >= this fires R2 when L2 held
REFRACTORY_S   = 0.5    # minimum seconds between consecutive fires

# DS4 button indices (Web Gamepad API layout used by Labs Engine)
BTN_L2 = 6
BTN_R2 = 7


# ── Meter detector ────────────────────────────────────────────────────────────

class BGRMeterDetector:
    def __init__(self):
        self._history = []

    def _find_meter_region(self, frame):
        g = frame[:, :, 1].astype(int)
        r = frame[:, :, 2].astype(int)
        b = frame[:, :, 0].astype(int)
        mask = ((g - r > 40) & (g - b > 20) & (g > 120)).astype(np.uint8) * 255
        cnts, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        if not cnts:
            return None
        best = max(cnts, key=lambda c: cv2.boundingRect(c)[2])
        x, y, w, h = cv2.boundingRect(best)
        if w < 40 or h > 30:
            return None
        return (x, y, x + w, y + h)

    def fill_percent(self, frame):
        region = self._find_meter_region(frame)
        if region is None:
            return None
        x1, y1, x2, y2 = region
        strip = frame[y1:y2, x1:x2]
        g = strip[:, :, 1].astype(int)
        r = strip[:, :, 2].astype(int)
        lit = np.sum((g - r > 30) & (g > 100))
        total = strip.shape[0] * strip.shape[1]
        return lit / total if total > 0 else 0.0

    def predict_fill(self, fill):
        self._history.append(fill)
        if len(self._history) > 8:
            self._history.pop(0)
        if len(self._history) < 2:
            return fill
        return fill + (self._history[-1] - self._history[-2])

    def should_fire(self, frame, l2_held=False):
        fill = self.fill_percent(frame)
        if fill is None:
            return False
        predicted = self.predict_fill(fill)
        return predicted >= (TRIGGER_L2 if l2_held else TRIGGER_NORMAL)


# ── XInput helper (read L2 from physical pad) ─────────────────────────────────

class _GP(ctypes.Structure):
    _fields_ = [("wButtons", ctypes.c_ushort), ("bLeftTrigger", ctypes.c_ubyte),
                ("bRightTrigger", ctypes.c_ubyte), ("sThumbLX", ctypes.c_short),
                ("sThumbLY", ctypes.c_short), ("sThumbRX", ctypes.c_short),
                ("sThumbRY", ctypes.c_short)]

class _ST(ctypes.Structure):
    _fields_ = [("dwPacketNumber", ctypes.c_ulong), ("Gamepad", _GP)]

_xi = None

def _read_l2():
    global _xi
    try:
        if _xi is None:
            _xi = ctypes.windll.xinput1_4
        st = _ST()
        if _xi.XInputGetState(0, ctypes.byref(st)) == 0:
            return st.Gamepad.bLeftTrigger > 128
    except Exception:
        pass
    return False


# ── Module state ──────────────────────────────────────────────────────────────

_detector    = None
_last_fire   = 0.0
_shots_fired = 0
_frame_count = 0
_last_diag   = 0.0


# ── Labs Vision API ───────────────────────────────────────────────────────────

def on_start(config: dict) -> None:
    global _detector, _last_fire, _shots_fired, _frame_count, _last_diag
    _detector    = BGRMeterDetector()
    _last_fire   = 0.0
    _shots_fired = 0
    _frame_count = 0
    _last_diag   = 0.0
    print(f"[shot_meter] started — session={config['session_id']} "
          f"normal={TRIGGER_NORMAL*100:.0f}% L2={TRIGGER_L2*100:.0f}%")
    print("[shot_meter] waiting for frames...")


def on_frame(frame, session_id: int, emit) -> None:
    global _last_fire, _shots_fired, _frame_count, _last_diag

    if _detector is None:
        return

    _frame_count += 1

    now = time.monotonic()
    if now - _last_diag >= 3.0:
        _last_diag = now
        fill = _detector.fill_percent(frame)
        h, w = frame.shape[:2]
        if fill is None:
            print(f"[shot_meter] #{_frame_count} {w}x{h} — meter NOT DETECTED (no green bar)")
        else:
            print(f"[shot_meter] #{_frame_count} {w}x{h} — fill: {fill*100:.1f}%")

    l2 = _read_l2()
    if not _detector.should_fire(frame, l2_held=l2):
        return

    now = time.monotonic()
    if now - _last_fire < REFRACTORY_S:
        return

    _last_fire    = now
    _shots_fired += 1
    print(f"[shot_meter] FIRE #{_shots_fired}  l2={l2}")

    btns = [False] * 17
    btns[BTN_R2] = True
    emit({"axes": [0.0, 0.0, 0.0, 0.0], "buttons": btns})
    time.sleep(0.05)
    emit({"axes": [0.0, 0.0, 0.0, 0.0], "buttons": [False] * 17})


def on_stop() -> None:
    print(f"[shot_meter] stopped — {_shots_fired} shots fired total")
