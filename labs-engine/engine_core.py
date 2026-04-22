"""
engine_core.py — Headless shot-meter engine for LabsEngine.exe.

Spawned by LabsEngine's CV Python plugin with:
  python engine_core.py --labs-pid <LABS_PID> --session <SID>

What it does:
  • Reads BGRA frames from LabsEngine's Frame SHM (Labs_<labsPid>_Frame_<sid>)
  • ShotMeterDetector watches the meter and fires on release
  • 500Hz XInput → SHM passthrough writes gamepad state back to LabsEngine
  • LabsEngine's GamepadShmReader picks it up and routes via ViGEm

IPC contract (matches app/engine/.../ShmBus):
  GAMEPAD (we write):
    • block      : Labs_<mypid>_Gamepad          (96 bytes)
    • magic      : 0x5342414C ("LABS")           at offset 0
    • version    : 1                              at offset 4
    • writer_pid : our PID                        at offset 8
    • sequence   : written LAST                   at offset 12
    • axes       : 4×float32                      at offset 32 (lx, ly, rx, ry)
    • buttons    : 17×uint8                       at offset 48
    • session    : int32                          at offset 68
    • event      : Global\\Labs_<mypid>_Gamepad_Written

  FRAME (we read):
    • block      : Labs_<labsPid>_Frame_<sid>    (1,966,144 bytes)
    • magic      : 0x4D52464C ("FRML")            at offset 0
    • sequence   : polled for new frames          at offset 12
    • width      : uint32                          at offset 20
    • height     : uint32                          at offset 24
    • stride     : uint32                          at offset 28
    • payload    : BGRA row-major                  at offset 64
    • event      : Global\\Labs_<labsPid>_Frame_<sid>_Written

Button order (SHM index):
  0:A 1:B 2:X 3:Y 4:LB 5:RB 6:Back 7:Start 8:LS 9:RS
  10:DUp 11:DDown 12:DLeft 13:DRight 14:Guide 15:LT 16:RT
"""
import argparse
import ctypes
import mmap
import os
import signal
import struct
import sys
import threading
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "cv-scripts"))

from shot_meter import ShotMeterDetector  # noqa: E402


# ── XInput ────────────────────────────────────────────────────────────────────
class _XGP(ctypes.Structure):
    _fields_ = [("wButtons", ctypes.c_ushort), ("bLeftTrigger", ctypes.c_ubyte),
                ("bRightTrigger", ctypes.c_ubyte), ("sThumbLX", ctypes.c_short),
                ("sThumbLY", ctypes.c_short), ("sThumbRX", ctypes.c_short),
                ("sThumbRY", ctypes.c_short)]


class _XS(ctypes.Structure):
    _fields_ = [("dwPacketNumber", ctypes.c_ulong), ("Gamepad", _XGP)]


_XI = ctypes.windll.xinput1_4


def _read_xi(idx: int = 0):
    s = _XS()
    return s.Gamepad if _XI.XInputGetState(idx, ctypes.byref(s)) == 0 else None


# ── Win32 event / mapping ─────────────────────────────────────────────────────
_k32 = ctypes.windll.kernel32
_k32.CreateEventW.restype  = ctypes.c_void_p
_k32.CreateEventW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_wchar_p]
_k32.OpenEventW.restype    = ctypes.c_void_p
_k32.OpenEventW.argtypes   = [ctypes.c_ulong, ctypes.c_int, ctypes.c_wchar_p]
_k32.SetEvent.restype      = ctypes.c_int
_k32.SetEvent.argtypes     = [ctypes.c_void_p]
_k32.WaitForSingleObject.restype  = ctypes.c_ulong
_k32.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
_k32.CloseHandle.argtypes  = [ctypes.c_void_p]

SYNCHRONIZE    = 0x00100000
EVENT_MODIFY   = 0x00000002
WAIT_TIMEOUT   = 0x00000102


def _create_event(name: str):
    h = _k32.CreateEventW(None, False, False, name)
    return h if h else None


def _open_event(name: str):
    h = _k32.OpenEventW(SYNCHRONIZE | EVENT_MODIFY, False, name)
    return h if h else None


# ── XInput wButtons bit → SHM button index ────────────────────────────────────
_XI_TO_SHM = [
    (0x1000, 0), (0x2000, 1), (0x4000, 2), (0x8000, 3),   # A B X Y
    (0x0100, 4), (0x0200, 5),                              # LB RB
    (0x0020, 6), (0x0010, 7),                              # Back Start
    (0x0040, 8), (0x0080, 9),                              # LS RS
    (0x0001, 10), (0x0002, 11), (0x0004, 12), (0x0008, 13),  # DPad
]


# ── ShmFrameReader ────────────────────────────────────────────────────────────
class ShmFrameReader:
    """Reads frames from Labs_<labsPid>_Frame_<sid> SHM block (published by LabsEngine)."""
    SIZE  = 1_966_144           # 64 header + 1920×1080×4 BGRA
    MAGIC = 0x4D52464C          # "FRML"

    def __init__(self, labs_pid: int, session_id: int, wait_timeout_s: float = 30.0):
        self.labs_pid   = labs_pid
        self.session_id = session_id
        self.name       = f"Labs_{labs_pid}_Frame_{session_id}"

        deadline = time.perf_counter() + wait_timeout_s
        while True:
            self._shm = mmap.mmap(-1, self.SIZE, tagname=self.name, access=mmap.ACCESS_READ)
            magic, = struct.unpack_from("<I", self._shm, 0)
            if magic == self.MAGIC:
                break
            self._shm.close()
            if time.perf_counter() > deadline:
                raise RuntimeError(f"Frame SHM {self.name} never became available")
            time.sleep(0.25)

        gname = f"Global\\Labs_{labs_pid}_Frame_{session_id}_Written"
        lname = f"Labs_{labs_pid}_Frame_{session_id}_Written"
        self._evt = _open_event(gname) or _open_event(lname)
        if not self._evt:
            print(f"[FRAME-SHM] WARN: could not open event — falling back to polling")

        self._last_seq = 0
        print(f"[FRAME-SHM] attached: {self.name}")

    def get_frame(self, timeout_ms: int = 100):
        """Block until new frame, return (BGR ndarray, sequence) or (None, seq) on timeout."""
        if self._evt:
            if _k32.WaitForSingleObject(self._evt, timeout_ms) == WAIT_TIMEOUT:
                return None, self._last_seq
        else:
            time.sleep(timeout_ms / 1000.0)

        seq, = struct.unpack_from("<I", self._shm, 12)
        if seq == self._last_seq:
            return None, seq
        self._last_seq = seq

        width,  = struct.unpack_from("<I", self._shm, 20)
        height, = struct.unpack_from("<I", self._shm, 24)
        stride, = struct.unpack_from("<I", self._shm, 28)
        if width == 0 or height == 0:
            return None, seq

        bgra_size = stride * height
        bgra = np.frombuffer(self._shm, dtype=np.uint8, count=bgra_size, offset=64)
        bgra = bgra.reshape((height, stride // 4, 4))[:, :width]
        return np.ascontiguousarray(bgra[:, :, :3]), seq  # BGRA → BGR, copy out of SHM

    def close(self):
        if self._evt:
            _k32.CloseHandle(self._evt)
        self._shm.close()


# ── ShmGamepad ────────────────────────────────────────────────────────────────
class ShmGamepad:
    """Writes the 96-byte gamepad block per LabsEngine's ShmBus contract."""
    SIZE  = 96
    MAGIC = 0x5342414C  # "LABS"

    def __init__(self, session_id: int):
        self.pid        = os.getpid()
        self.session_id = int(session_id)
        self.name       = f"Labs_{self.pid}_Gamepad"

        self._shm  = mmap.mmap(-1, self.SIZE, tagname=self.name, access=mmap.ACCESS_WRITE)
        self._lock = threading.Lock()
        self._seq  = 0

        struct.pack_into("<I", self._shm,  0, self.MAGIC)
        struct.pack_into("<I", self._shm,  4, 1)              # version
        struct.pack_into("<I", self._shm,  8, self.pid)
        struct.pack_into("<I", self._shm, 12, 0)              # sequence
        struct.pack_into("<i", self._shm, 68, self.session_id)

        gname = f"Global\\Labs_{self.pid}_Gamepad_Written"
        lname = f"Labs_{self.pid}_Gamepad_Written"
        self._evt = _create_event(gname) or _create_event(lname)
        if not self._evt:
            print("[SHM] WARN: CreateEventW failed for both names")

        print(f"[SHM] block={self.name}  size={self.SIZE}  session={self.session_id}")

    def write(self, lx: float, ly: float, rx: float, ry: float,
              lt: float, rt: float, buttons_mask: int):
        with self._lock:
            struct.pack_into("<ffff", self._shm, 32, lx, ly, rx, ry)

            btns = bytearray(17)
            for bit, idx in _XI_TO_SHM:
                if buttons_mask & bit:
                    btns[idx] = 1
            btns[15] = 1 if lt >= 0.5 else 0
            btns[16] = 1 if rt >= 0.5 else 0
            self._shm[48:65] = bytes(btns)

            struct.pack_into("<i", self._shm, 68, self.session_id)
            self._seq = (self._seq + 1) & 0xFFFFFFFF
            struct.pack_into("<I", self._shm, 12, self._seq)

            if self._evt:
                _k32.SetEvent(self._evt)

    def close(self):
        if self._evt:
            _k32.CloseHandle(self._evt)
        self._shm.close()


# ── FeatureBridge ─────────────────────────────────────────────────────────────
class FeatureBridge:
    """
    Manages the stateful features that mutate the passthrough output:
      • stick tempo  — RS-down for tempo_ms, then fire RT, then 1s cooldown
      • quickstop    — RS-down 20ms, neutral 15ms, fire RT
      • contest flick (defense) — RS-up for 80ms with 600ms refractory
      • DPAD_UP      — rising edge toggles quickstop on/off in-game
      • RT fire      — 30ms pulse on shot release
    """
    _CONTEST_DUR  = 0.080
    _CONTEST_COOL = 0.600
    _RT_PULSE_S   = 0.030
    _TEMPO_COOL_S = 1.000
    _QS_DOWN_S    = 0.020
    _QS_NEUTRAL_S = 0.015
    _HANDS_UP_DUR = 0.100   # defense_auto_hands_up RS-up duration

    def __init__(self, args):
        self.args = args
        self.defense_enabled     = args.defense
        self.stamina_enabled     = args.stamina
        self.stick_tempo_enabled = args.stick_tempo
        self.quickstop_enabled   = args.quickstop
        self.auto_hands_up       = not args.no_hands_up

        self._rs_override     = None  # (rx, ry) or None
        self._rs_lock         = threading.Lock()

        self._rt_fire_until   = 0.0
        self._contest_until   = 0.0
        self._contest_cool    = 0.0
        self._tempo_cool_end  = 0.0
        self._hands_up_until  = 0.0
        self._dpad_up_prev    = False

    # ── called from passthrough loop ──────────────────────────────────────────
    def check_dpad_toggle(self, wButtons: int):
        """DPAD_UP alone (rising edge) toggles defense mode — matches ZP UI."""
        dpad_up = bool(wButtons == 0x0001)
        if dpad_up and not self._dpad_up_prev:
            self.defense_enabled = not self.defense_enabled
            print(f"[BRIDGE] defense {'ON' if self.defense_enabled else 'OFF'}")
            if self.defense_enabled and self.auto_hands_up:
                self._hands_up_until = time.perf_counter() + self._HANDS_UP_DUR
        self._dpad_up_prev = dpad_up

    def apply_overrides(self, rx: float, ry: float, rt: float):
        now = time.perf_counter()
        # contest flick OR auto-hands-up both pull RS up
        if (self._contest_until > 0 and now < self._contest_until) or now < self._hands_up_until:
            ry = 1.0
        with self._rs_lock:
            if self._rs_override is not None:
                rx, ry = self._rs_override
        if now < self._rt_fire_until:
            rt = 1.0
        return rx, ry, rt

    # ── called from detection loop ────────────────────────────────────────────
    def fire(self, l2: bool):
        if self.defense_enabled:
            self._contest_flick()
            return
        if self.stick_tempo_enabled and not l2:
            if time.perf_counter() < self._tempo_cool_end:
                return
            threading.Thread(target=self._stick_tempo_sequence, daemon=True).start()
            return
        if self.quickstop_enabled and not l2:
            threading.Thread(target=self._quickstop_sequence, daemon=True).start()
            return
        if self.args.tempo and not l2:
            time.sleep(self.args.tempo_ms / 1000.0)
        self._fire_rt()

    # ── sequences ─────────────────────────────────────────────────────────────
    def _fire_rt(self):
        self._rt_fire_until = time.perf_counter() + self._RT_PULSE_S

    def _contest_flick(self):
        now = time.perf_counter()
        if now < self._contest_cool:
            return
        self._contest_until = now + self._CONTEST_DUR
        self._contest_cool  = now + self._CONTEST_COOL

    def _stick_tempo_sequence(self):
        with self._rs_lock:
            self._rs_override = (0.0, -1.0)
        time.sleep(self.args.tempo_ms / 1000.0)
        with self._rs_lock:
            self._rs_override = None
        self._fire_rt()
        self._tempo_cool_end = time.perf_counter() + self._TEMPO_COOL_S

    def _quickstop_sequence(self):
        with self._rs_lock:
            self._rs_override = (0.0, -1.0)
        time.sleep(self._QS_DOWN_S)
        with self._rs_lock:
            self._rs_override = (0.0, 0.0)
        time.sleep(self._QS_NEUTRAL_S)
        with self._rs_lock:
            self._rs_override = None
        self._fire_rt()


# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    p = argparse.ArgumentParser()
    p.add_argument("--labs-pid",     type=int,   required=True)
    p.add_argument("--session",      type=int,   default=1)
    p.add_argument("--threshold",    type=float, default=0.95)
    p.add_argument("--threshold-l2", type=float, default=0.75)
    p.add_argument("--tempo-ms",     type=int,   default=39)
    p.add_argument("--tempo",        action="store_true")
    p.add_argument("--stick-tempo",  action="store_true", dest="stick_tempo")
    p.add_argument("--quickstop",    action="store_true")
    p.add_argument("--defense",      action="store_true")
    p.add_argument("--stamina",      action="store_true")
    p.add_argument("--no-hands-up",  action="store_true", dest="no_hands_up",
                   help="Disable auto-hands-up RS flick on defense toggle")
    p.add_argument("--xi-index",     type=int,   default=0, dest="xi_index",
                   help="XInput controller index (0-3)")
    args = p.parse_args()

    print(f"[ENGINE] pid={os.getpid()}  labs-pid={args.labs_pid}  session={args.session}")

    gp_out = ShmGamepad(args.session)
    cam    = ShmFrameReader(args.labs_pid, args.session)
    bridge = FeatureBridge(args)

    detector = ShotMeterDetector(args.threshold, args.threshold_l2)
    print(f"[ENGINE] thresholds: normal={args.threshold*100:.0f}%  L2={args.threshold_l2*100:.0f}%")
    print(f"[ENGINE] flags: defense={args.defense} stamina={args.stamina} "
          f"tempo={args.tempo} stick_tempo={args.stick_tempo} quickstop={args.quickstop}")

    _stop = threading.Event()

    def _handle_signal(signum, _frame):
        print(f"[ENGINE] signal {signum} — stopping")
        _stop.set()
    signal.signal(signal.SIGINT,  _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)
    if hasattr(signal, "SIGBREAK"):
        signal.signal(signal.SIGBREAK, _handle_signal)

    xi_idx = args.xi_index

    def passthrough_loop():
        interval = 1.0 / 500
        while not _stop.is_set():
            gp = _read_xi(xi_idx)
            if gp:
                lx = gp.sThumbLX / 32768.0
                ly = gp.sThumbLY / 32768.0
                rx = gp.sThumbRX / 32768.0
                ry = gp.sThumbRY / 32768.0
                lt = gp.bLeftTrigger  / 255.0
                rt = gp.bRightTrigger / 255.0

                if bridge.defense_enabled:
                    if abs(rx) > 0.05: rx = max(-1.0, min(1.0, rx * 1.20))
                    if abs(ry) > 0.05: ry = max(-1.0, min(1.0, ry * 1.30))
                    rt = min(rt, 0.40)

                if bridge.stamina_enabled:
                    rt *= 0.70

                rx, ry, rt = bridge.apply_overrides(rx, ry, rt)
                bridge.check_dpad_toggle(gp.wButtons)

                gp_out.write(lx, ly, rx, ry, lt, rt, gp.wButtons)
            time.sleep(interval)

    threading.Thread(target=passthrough_loop, daemon=True).start()

    print("[ENGINE] Running — Ctrl-C to stop")
    fc          = 0
    status_tw   = time.perf_counter()
    last_seq    = 0
    cal_reported_n  = 0
    cal_reported_l2 = 0
    try:
        while not _stop.is_set():
            bgr, seq = cam.get_frame(timeout_ms=100)
            if bgr is None:
                continue
            gp = _read_xi(xi_idx)
            l2 = bool(gp and gp.bLeftTrigger > 128)
            if detector.check(bgr, l2=l2):
                bridge.fire(l2=l2)
                print(f"[ENGINE] SHOT #{detector.shots_fired}  type={'L2' if l2 else 'NORM'}",
                      flush=True)

            # calibration progress (1/4 ... 4/4)
            np_n  = len(detector._peak_history)
            np_l2 = len(detector._peak_history_l2)
            if np_n  != cal_reported_n and not detector._calibration_locked:
                print(f"[CAL] normal {np_n}/{detector._MIN_CALIBRATION_PEAKS}", flush=True)
                cal_reported_n = np_n
            if np_l2 != cal_reported_l2 and not detector._calibration_locked_l2:
                print(f"[CAL] L2 {np_l2}/{detector._MIN_CALIBRATION_PEAKS}", flush=True)
                cal_reported_l2 = np_l2

            fc += 1
            now = time.perf_counter()
            if now - status_tw >= 1.0:
                fps = fc / (now - status_tw)
                frame_delta = seq - last_seq
                last_seq = seq
                cal_n  = "LOCK" if detector._calibration_locked    else f"{np_n}/4"
                cal_l2 = "LOCK" if detector._calibration_locked_l2 else f"{np_l2}/4"
                print(f"[STATUS] fps={fps:.0f} frames={frame_delta} "
                      f"shots={detector.shots_fired} cal=(n:{cal_n} l2:{cal_l2}) "
                      f"defense={bridge.defense_enabled}", flush=True)
                fc = 0
                status_tw = now
    finally:
        _stop.set()
        cam.close()
        gp_out.close()
        print("[ENGINE] Stopped", flush=True)


if __name__ == "__main__":
    main()
