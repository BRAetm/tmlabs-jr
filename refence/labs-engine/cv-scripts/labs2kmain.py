"""
labs2kmain.py — NBA 2K cv-script
LabsSharp hook pattern: on_start / on_frame / on_tick / on_stop
"""

import ctypes
import ctypes.wintypes
import cv2
import numpy as np
import json
import time
import os
import sys
import subprocess
import threading
from pathlib import Path

# ── XInput direct read (works for Xbox + any XInput controller) ───────────────
class _XINPUT_GAMEPAD(ctypes.Structure):
    _fields_ = [
        ("wButtons",      ctypes.c_uint16),
        ("bLeftTrigger",  ctypes.c_uint8),
        ("bRightTrigger", ctypes.c_uint8),
        ("sThumbLX",      ctypes.c_int16),
        ("sThumbLY",      ctypes.c_int16),
        ("sThumbRX",      ctypes.c_int16),
        ("sThumbRY",      ctypes.c_int16),
    ]

class _XINPUT_STATE(ctypes.Structure):
    _fields_ = [("dwPacketNumber", ctypes.c_uint32), ("Gamepad", _XINPUT_GAMEPAD)]

# XInput button masks
_XI_A      = 0x1000
_XI_B      = 0x2000
_XI_X      = 0x4000  # Xbox X = shoot in 2K
_XI_Y      = 0x8000
_XI_LB     = 0x0100
_XI_RB     = 0x0200

try:
    _xinput = ctypes.windll.xinput1_4
except Exception:
    try:
        _xinput = ctypes.windll.xinput9_1_0
    except Exception:
        _xinput = None

def _xi_state(pad_index: int = 0):
    """Return raw XInput state or None if not connected."""
    if _xinput is None:
        return None
    s = _XINPUT_STATE()
    return s if _xinput.XInputGetState(pad_index, ctypes.byref(s)) == 0 else None

def _xi_btn(state, mask: int) -> bool:
    return bool(state.Gamepad.wButtons & mask) if state else False

def _xi_axis(state, attr: str) -> float:
    """Return stick axis as -1.0 to 1.0."""
    raw = getattr(state.Gamepad, attr, 0)
    return max(-1.0, raw / 32767.0) if raw >= 0 else raw / 32768.0

ROOT  = Path(__file__).resolve().parent.parent
SUPER = ROOT / "super source"
sys.path.insert(0, str(SUPER))

# Worker reads this to locate the LabsSharp engine window for direct capture.
CAPTURE_WINDOW = "labs engine"

# ── UI launcher ───────────────────────────────────────────────────────────────
_ui_proc = None
_ui_lock = threading.Lock()
_UI_PID  = Path(os.environ.get("TEMP", "/tmp")) / "labs2kmain_ui.pid"

def _launch_ui():
    global _ui_proc
    with _ui_lock:
        if _ui_proc and _ui_proc.poll() is None:
            return
        try:
            if _UI_PID.exists():
                pid = int(_UI_PID.read_text().strip())
                out = subprocess.run(["tasklist", "/FI", f"PID eq {pid}"],
                                     capture_output=True, text=True, timeout=2)
                if str(pid) in out.stdout:
                    return
        except Exception:
            pass
        entry = ROOT / "labs2kmain.py"
        if not entry.exists():
            return
        _ui_proc = subprocess.Popen([sys.executable, str(entry)])
        try: _UI_PID.write_text(str(_ui_proc.pid))
        except Exception: pass

# ── button layout (Web Gamepad API standard mapping) ─────────────────────────
# PS:  Cross=0  Circle=1  Square=2  Triangle=3
# Xbox: A=0     B=1       X=2       Y=3
_AXES_N = [0.0] * 8
_BTNS_N = [False] * 17
BTN_SQUARE = 2   # PS Square / Xbox X  (shoot)
BTN_CIRCLE = 1   # PS Circle / Xbox B
BTN_X      = 0   # PS Cross  / Xbox A
BTN_TRI    = 3   # PS Tri    / Xbox Y
AX_LX, AX_LY, AX_RX, AX_RY = 0, 1, 2, 3

# resolved at on_start from controller_type setting
_BTN_SHOOT = BTN_SQUARE

# ── session globals ───────────────────────────────────────────────────────────
_cfg        = {}
_emit_fn    = None
_session_id = None
_enabled    = True
_mode       = "shot"
_meter      = None
_releaser   = None
_rhythm     = None
_bot        = None
_decision   = None
_dbg        = 0

# rhythm / flick state
_shooting        = False   # True while holding RY down during shot
_flick_active    = False   # True while flick thread is running
_flick_thread    = None
_rhythm_delay_ms = 50      # delay_duration from UI "Tempo" setting
_rhythm_reverse  = False   # False=Fade(up), True=Hop(down)


# ── gamepad proxy ─────────────────────────────────────────────────────────────

class _GCVData:
    """Thin proxy so Releaser.run() / Skeleaser.run() can call release_shot()."""
    def __init__(self, emit_fn, session_id):
        self._emit = emit_fn
        self._sid  = session_id
        self.released = False

    def release_shot(self):
        self.released = True
        btns = list(_BTNS_N); btns[_BTN_SHOOT] = True
        self._emit({"session_id": self._sid, "axes": list(_AXES_N), "buttons": btns})
        time.sleep(0.032)
        self._emit({"session_id": self._sid, "axes": list(_AXES_N), "buttons": list(_BTNS_N)})

    def hold_shot(self):
        btns = list(_BTNS_N); btns[_BTN_SHOOT] = True
        self._emit({"session_id": self._sid, "axes": list(_AXES_N), "buttons": btns})

    def get_button(self, idx):
        return 0


# ── helpers ───────────────────────────────────────────────────────────────────

def _load_settings():
    p = ROOT / "userdata" / "settings" / "nba2k_settings.current"
    if p.exists():
        try: return json.loads(p.read_text())
        except Exception: pass
    return {}


def _load_meter(settings):
    import shot as _shot
    name      = settings.get("meter_type", "Arrow2")
    cfg_path  = ROOT / "userdata" / "settings" / "meters" / f"{name}.json"
    meter_cfg = {}
    if cfg_path.exists():
        try: meter_cfg = json.loads(cfg_path.read_text())
        except Exception: pass

    # timing from UI
    meter_cfg.setdefault("timing", {})["value"] = settings.get("timing_value", 50)

    # custom color from UI color picker
    hex_col = settings.get("meter_color", "")
    sel_color = settings.get("selected_color", "Purple")
    if isinstance(hex_col, str) and hex_col.startswith("#") and len(hex_col) == 7:
        try:
            r = int(hex_col[1:3], 16); g = int(hex_col[3:5], 16); b = int(hex_col[5:7], 16)
            t = 40
            low  = [max(0,b-t), max(0,g-t), max(0,r-t)]
            high = [min(255,b+t), min(255,g+t), min(255,r+t)]
            opts = meter_cfg.setdefault("options", {})
            k    = next(iter(opts), "Large/Side")
            opts.setdefault(k, {}).setdefault("color", {})["Custom"] = {"low": low, "high": high}
            sel_color = "Custom"
        except Exception: pass

    meter_cfg.setdefault("selected", {})["color"] = sel_color
    cls = _shot.METER_CLASSES.get(name, _shot.Arrow2)
    return cls(meter_cfg)


def _scale_roi(meter, w, h):
    """Scale meter search ROI to actual frame dimensions."""
    sx = w / 1920.0; sy = h / 1080.0
    meter.search["top"]    = int(550 * sy)
    meter.search["bottom"] = h
    meter.search["left"]   = int(500 * sx)
    meter.search["right"]  = int(1420 * sx)


def _fill_from_bbox(bbox, meter, h):
    """Convert bounding box Y position to 0-1 fill ratio."""
    x, y, bw, bh = bbox
    roi_top    = meter.search.get("top", 0)
    roi_bottom = meter.search.get("bottom", h)
    roi_h      = max(roi_bottom - roi_top, 1)
    return float(np.clip(1.0 - (y / roi_h), 0.0, 1.0))


# ── LabsSharp hooks ───────────────────────────────────────────────────────────

def on_start(config: dict):
    global _cfg, _mode, _enabled, _meter, _releaser, _rhythm, _bot, _decision
    global _rhythm_delay_ms, _rhythm_reverse, _BTN_SHOOT

    import importlib
    extra = importlib.import_module("extra")
    bot_m = importlib.import_module("bot")

    _launch_ui()

    _cfg     = {**_load_settings(), **config}
    _mode    = _cfg.get("mode", "shot")
    _enabled = _cfg.get("enabled", True)

    _meter    = _load_meter(_cfg)
    _releaser = extra.Releaser(timing_value=_cfg.get("timing_value", 50))

    # XInput shoot button mask (X=0x4000 for Xbox, A=0x1000 for alternate layouts)
    ctrl = _cfg.get("controller", "Auto")
    _BTN_SHOOT = 2  # Web Gamepad API index (used for emit)
    print(f"[labs2kmain] controller={ctrl} XInput shoot=X(0x4000)")

    # rhythm flick settings (delay_duration in ms, 0-300)
    _rhythm_delay_ms = int(_cfg.get("rhythm_tempo_ms", 50))
    _rhythm_reverse  = _cfg.get("rhythm_mode", "Fade") == "Hop"

    # bot
    if _mode == "bot":
        ai_cfg    = bot_m.AIConfig(role=_cfg.get("role", "SG"))
        _decision = bot_m.DecisionEngine(ai_cfg)
        _bot      = bot_m.Bot(_decision)

    print(f"[labs2kmain] started mode={_mode} meter={type(_meter).__name__} timing={_releaser.timing_value} rhythm_delay={_rhythm_delay_ms}ms")


def _do_flick(emit_fn, session_id):
    """GPC flick_up_rs_smooth converted to Python — runs in its own thread."""
    global _flick_active, _shooting

    import random

    def _e(axes, btns=None):
        emit_fn({"session_id": session_id,
                 "axes": axes,
                 "buttons": btns if btns is not None else list(_BTNS_N)})

    rv = lambda: random.uniform(-0.07, 0.07)  # ±7% variation

    # step 1: go neutral briefly
    _e(list(_AXES_N))
    time.sleep(0.016)

    # step 2: wait delay_duration (tempo)
    time.sleep(_rhythm_delay_ms / 1000.0)

    # step 3: flick — Fade=up(-1.0), Hop=down(+1.0)
    ry_flick = 1.0 if _rhythm_reverse else -1.0
    axes = list(_AXES_N)
    axes[AX_RY] = float(np.clip(ry_flick + rv(), -1, 1))
    axes[AX_RX] = float(np.clip(0.25 + rv(), -1, 1))
    btns = list(_BTNS_N); btns[_BTN_SHOOT] = True
    print(f"[labs2kmain] flick RY={axes[AX_RY]:.2f} btn={_BTN_SHOOT}")
    _e(axes, btns)
    time.sleep(0.200)

    # step 4: neutral
    _e(list(_AXES_N))
    _shooting     = False
    _flick_active = False


def _read_shoot_held() -> bool:
    """Read physical shoot button directly from XInput (Xbox X button)."""
    xi = _xi_state(0)
    if xi is None:
        # try other pads
        for i in range(1, 4):
            xi = _xi_state(i)
            if xi is not None:
                break
    return _xi_btn(xi, _XI_X)


def on_frame(frame: np.ndarray, session_id: int, emit_fn):
    global _emit_fn, _session_id, _dbg, _shooting, _flick_active, _flick_thread

    if not _enabled or frame is None:
        return

    _emit_fn    = emit_fn
    _session_id = session_id
    gcv         = _GCVData(emit_fn, session_id)

    # ── rhythm — pure input mode, no CV needed ────────────────────────────────
    if _mode == "rhythm":
        import random
        shoot_held = _read_shoot_held()

        if shoot_held and not _flick_active:
            # user holding shoot — push RS down
            rv = random.uniform(-0.02, 0.02)
            axes = list(_AXES_N)
            axes[AX_RY] = float(np.clip(1.0 + rv, -1, 1))
            axes[AX_RX] = float(np.clip(0.20 + rv, -1, 1))
            btns = list(_BTNS_N); btns[_BTN_SHOOT] = True
            emit_fn({"session_id": session_id, "axes": axes, "buttons": btns})
            if not _shooting:
                print(f"[labs2kmain] rhythm: shoot held → RS down")
            _shooting = True

        elif not shoot_held and _shooting and not _flick_active:
            # user released shoot — fire flick
            _shooting     = False
            _flick_active = True
            _flick_thread = threading.Thread(
                target=_do_flick, args=(emit_fn, session_id), daemon=True)
            _flick_thread.start()
            print(f"[labs2kmain] rhythm: shoot released → flick")

        return

    # ── shot / skele — CV-based ───────────────────────────────────────────────
    if _mode in ("shot", "skele"):
        h, w = frame.shape[:2]
        _scale_roi(_meter, w, h)

        try:
            bbox = _meter.detect(frame)
        except Exception as e:
            bbox = None
            print(f"[labs2kmain] detect error: {e}")

        _dbg += 1
        if _dbg % 90 == 1:
            print(f"[labs2kmain] frame={_dbg} res={w}x{h} mode={_mode} bbox={bbox}")

        if bbox is not None:
            fill = _fill_from_bbox(bbox, _meter, h)
            threshold = _releaser.timing_value / 100.0
            if fill >= threshold:
                gcv.release_shot()
                _shooting = False
            elif not _shooting:
                gcv.hold_shot()
                _shooting = True
        else:
            if _shooting:
                emit_fn({"session_id": session_id, "axes": list(_AXES_N), "buttons": list(_BTNS_N)})
                _shooting = False

    # ── bot ───────────────────────────────────────────────────────────────────
    elif _mode == "bot" and _bot and _decision:
        try:
            state  = _bot.observe(frame)
            action = _decision.decide(state)
            if action:
                _do_bot_action(action, gcv)
        except Exception as e:
            print(f"[labs2kmain] bot error: {e}")


def on_tick(session_id: int, emit_fn):
    # rhythm manages its own state in on_frame — never override here
    if _mode == "rhythm":
        return
    if not _shooting and not _flick_active:
        emit_fn({"session_id": session_id, "axes": list(_AXES_N), "buttons": list(_BTNS_N)})


def on_stop():
    global _enabled, _shooting, _flick_active, _ui_proc
    _enabled      = False
    _shooting     = False
    _flick_active = False
    if _emit_fn and _session_id is not None:
        _emit_fn({"session_id": _session_id, "axes": list(_AXES_N), "buttons": list(_BTNS_N)})
    if _ui_proc and _ui_proc.poll() is None:
        _ui_proc.terminate()
    try: _UI_PID.unlink()
    except Exception: pass
    print("[labs2kmain] stopped")


# ── bot action dispatch ───────────────────────────────────────────────────────

def _do_bot_action(action, gcv: _GCVData):
    try:
        import bot as _bot_mod
        axes = list(_AXES_N); btns = list(_BTNS_N)

        if isinstance(action, _bot_mod.ShootAction):
            gcv.hold_shot()
            time.sleep(getattr(action, "timing", 0.5))
            gcv.release_shot()

        elif isinstance(action, _bot_mod.PassAction):
            btns[BTN_X] = True
            gcv._emit({"session_id": gcv._sid, "axes": axes, "buttons": btns})
            time.sleep(0.08)
            gcv._emit({"session_id": gcv._sid, "axes": list(_AXES_N), "buttons": list(_BTNS_N)})

        elif isinstance(action, _bot_mod.MoveAction):
            axes[AX_LX] = float(np.clip(getattr(action, "dx", 0.0), -1, 1))
            axes[AX_LY] = float(np.clip(getattr(action, "dy", 0.0), -1, 1))
            gcv._emit({"session_id": gcv._sid, "axes": axes, "buttons": list(_BTNS_N)})
            time.sleep(getattr(action, "duration", 0.1))
            gcv._emit({"session_id": gcv._sid, "axes": list(_AXES_N), "buttons": list(_BTNS_N)})

        elif isinstance(action, _bot_mod.DefendAction):
            btns[BTN_CIRCLE] = True
            gcv._emit({"session_id": gcv._sid, "axes": axes, "buttons": btns})
            time.sleep(0.05)
            gcv._emit({"session_id": gcv._sid, "axes": list(_AXES_N), "buttons": list(_BTNS_N)})

        elif isinstance(action, _bot_mod.DribbleMove):
            axes[AX_RX] = float(np.clip(getattr(action, "rx", 0.0), -1, 1))
            axes[AX_RY] = float(np.clip(getattr(action, "ry", 0.0), -1, 1))
            gcv._emit({"session_id": gcv._sid, "axes": axes, "buttons": list(_BTNS_N)})
            time.sleep(getattr(action, "duration", 0.08))
            gcv._emit({"session_id": gcv._sid, "axes": list(_AXES_N), "buttons": list(_BTNS_N)})

    except Exception as e:
        print(f"[labs2kmain] bot action error: {e}")
