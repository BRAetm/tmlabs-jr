"""
ZP HIGHER Lite — Engine (reconstructed from live diagnostic output)

Real detection: BGR pixel-based shot meter reader, NOT TensorFlow.
TF saved_model.pb exists but is unused at runtime.

Architecture:
  BetterCam (GPU, 240fps) → BGR meter detector → velocity predictor
  → XInput read → VX360/VDS4 virtual controller output
"""
import threading, time, queue
import numpy as np
import cv2
import bettercam

try:
    import vgamepad as vg
    VG_OK = True
except ImportError:
    VG_OK = False

try:
    import ctypes
    xinput = ctypes.windll.xinput1_4
    XINPUT_OK = True
except Exception:
    XINPUT_OK = False

# ── Config ────────────────────────────────────────────────────────────────

DEFAULT_CONFIG = {
    "controller":       "xbox",      # xbox | ps
    "trigger_normal":   0.95,        # BGR meter % to fire on normal shot
    "trigger_l2":       0.75,        # BGR meter % to fire on L2 shot
    "calib_shots":      4,           # auto-lock calibration after N shots
    "tempo":            False,       # tempo shooting mode
    "tempo_ms":         39,          # tempo delay ms
    "capture_fps":      240,
    "capture_gpu":      0,
}

# ── XInput state ──────────────────────────────────────────────────────────

class XINPUT_GAMEPAD(ctypes.Structure):
    _fields_ = [
        ("wButtons",      ctypes.c_ushort),
        ("bLeftTrigger",  ctypes.c_ubyte),
        ("bRightTrigger", ctypes.c_ubyte),
        ("sThumbLX",      ctypes.c_short),
        ("sThumbLY",      ctypes.c_short),
        ("sThumbRX",      ctypes.c_short),
        ("sThumbRY",      ctypes.c_short),
    ]

class XINPUT_STATE(ctypes.Structure):
    _fields_ = [("dwPacketNumber", ctypes.c_ulong), ("Gamepad", XINPUT_GAMEPAD)]

def read_xinput(user_index=0):
    if not XINPUT_OK:
        return None
    state = XINPUT_STATE()
    if xinput.XInputGetState(user_index, ctypes.byref(state)) == 0:
        return state.Gamepad
    return None


# ── BGR meter detector ────────────────────────────────────────────────────

class BGRMeterDetector:
    """
    Reads NBA 2K shot meter by detecting green pixel fill %.
    Uses velocity prediction to fire slightly before threshold.
    """

    def __init__(self, trigger_normal=0.95, trigger_l2=0.75, calib_shots=4):
        self.trigger_normal = trigger_normal
        self.trigger_l2     = trigger_l2
        self.calib_shots    = calib_shots
        self.shots_fired    = 0
        self._history       = []

        print(f"[ENGINE] BGR meter detector initialized")
        print(f"[ENGINE]   Normal trigger: {trigger_normal*100:.1f}%")
        print(f"[ENGINE]   L2 trigger:     {trigger_l2*100:.1f}%")
        print(f"[ENGINE]   Calibration: auto-lock after {calib_shots} shots")

    def _find_meter_region(self, frame):
        g = frame[:, :, 1].astype(int)
        r = frame[:, :, 2].astype(int)
        b = frame[:, :, 0].astype(int)
        mask = ((g - r > 40) & (g - b > 20) & (g > 120)).astype(np.uint8) * 255
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        if not contours:
            return None
        best = max(contours, key=lambda c: cv2.boundingRect(c)[2])
        x, y, w, h = cv2.boundingRect(best)
        if w < 40 or h > 30:
            return None
        return (x, y, x+w, y+h)

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

    def predict_fill(self, current_fill):
        self._history.append(current_fill)
        if len(self._history) > 8:
            self._history.pop(0)
        if len(self._history) < 2:
            return current_fill
        velocity = self._history[-1] - self._history[-2]
        return current_fill + velocity

    def should_fire(self, frame, l2_held=False):
        fill = self.fill_percent(frame)
        if fill is None:
            return False
        predicted = self.predict_fill(fill)
        threshold = self.trigger_l2 if l2_held else self.trigger_normal
        return predicted >= threshold


# ── Virtual controller output ─────────────────────────────────────────────

class ControllerBridge:
    def __init__(self, config):
        self.config   = config
        self.vx360    = None
        self.vds4     = None
        self._running = False

    def start(self):
        if VG_OK:
            self.vx360 = vg.VX360Gamepad()
            self.vds4  = vg.VDS4Gamepad()
            self.vx360.reset(); self.vx360.update()
            self.vds4.reset();  self.vds4.update()

        print(f"[ENGINE] ═══ Controller bridge started ({self.config['controller']}) ═══")
        print(f"[ENGINE]   Input:  XINPUT")
        print(f"[ENGINE]   VX360:  {'YES' if self.vx360 else 'NO'}")
        print(f"[ENGINE]   VDS4:   {'YES' if self.vds4 else 'NO'}")
        print(f"[ENGINE]   Tempo:  {'ON' if self.config['tempo'] else 'OFF'} ({self.config['tempo_ms']}ms)")

        if self.vds4:
            self.vds4.press_button(button=vg.DS4_BUTTONS.DS4_BUTTON_OPTIONS)
            self.vds4.update()
            time.sleep(0.05)
            self.vds4.release_button(button=vg.DS4_BUTTONS.DS4_BUTTON_OPTIONS)
            self.vds4.update()
            print("[PS-CTRL] Wake pulse sent to VDS4 (Chiaki SDL detection)")

        self._running = True
        threading.Thread(target=self._input_loop, daemon=True).start()
        print("[PS-CTRL] Input loop started (500Hz)")

    def _input_loop(self):
        interval = 1 / 500
        while self._running:
            gp = read_xinput()
            if gp and self.vds4:
                self.vds4.left_joystick_float(gp.sThumbLX / 32768.0, gp.sThumbLY / 32768.0)
                self.vds4.right_joystick_float(gp.sThumbRX / 32768.0, gp.sThumbRY / 32768.0)
                self.vds4.left_trigger_float(gp.bLeftTrigger / 255.0)
                self.vds4.right_trigger_float(gp.bRightTrigger / 255.0)
                self.vds4.update()
            time.sleep(interval)

    def fire(self, l2=False):
        if not self.vds4:
            return
        if self.config["tempo"] and not l2:
            time.sleep(self.config["tempo_ms"] / 1000)
        self.vds4.right_trigger_float(1.0)
        self.vds4.update()
        time.sleep(0.05)
        self.vds4.right_trigger_float(0.0)
        self.vds4.update()

    def stop(self):
        self._running = False


# ── Main engine ───────────────────────────────────────────────────────────

class Engine:
    def __init__(self, config=None):
        self.config   = {**DEFAULT_CONFIG, **(config or {})}
        self.detector = BGRMeterDetector(
            trigger_normal=self.config["trigger_normal"],
            trigger_l2=    self.config["trigger_l2"],
            calib_shots=   self.config["calib_shots"],
        )
        self.bridge   = ControllerBridge(self.config)
        self.camera   = None
        self._running = False
        self._frame_q = queue.Queue(maxsize=2)

    def _find_target_window(self):
        try:
            import win32gui
            results = []
            def cb(hwnd, _):
                t = win32gui.GetWindowText(hwnd)
                if any(x in t for x in ('Xbox', 'Chiaki', 'PS Remote')):
                    results.append((t, win32gui.GetWindowRect(hwnd)))
            win32gui.EnumWindows(cb, None)
            if results:
                title, rect = results[0]
                print(f"[CAPTURE] Targeting window: '{title}' (BetterCam)")
                return rect
        except Exception:
            pass
        return None

    def _setup_capture(self):
        region = self._find_target_window()
        self.camera = bettercam.create(
            output_idx=self.config["capture_gpu"],
            region=region,
            output_color="BGR",
        )
        self.camera.start(target_fps=self.config["capture_fps"], video_mode=True)
        print(f"[CAPTURE] BetterCam GPU={self.config['capture_gpu']} (on-demand mode)")
        print(f"[CAPTURE] Continuous mode started (region={region}, target {self.config['capture_fps']}fps)")

    def _capture_thread(self):
        print("[ENGINE] Pipelined capture thread started")
        while self._running:
            frame = self.camera.get_latest_frame()
            if frame is not None:
                if self._frame_q.full():
                    try: self._frame_q.get_nowait()
                    except: pass
                self._frame_q.put(frame)

    def _detection_loop(self):
        print("[ENGINE] Detection loop started (PIPELINED)")
        print("[ENGINE] BGR meter detection active — auto-green + velocity prediction enabled")
        while self._running:
            try:
                frame = self._frame_q.get(timeout=0.1)
            except queue.Empty:
                continue
            gp = read_xinput()
            l2 = (gp.bLeftTrigger > 128) if gp else False
            if self.detector.should_fire(frame, l2_held=l2):
                self.bridge.fire(l2=l2)
                self.detector.shots_fired += 1

    def start(self):
        self.bridge.start()
        self._setup_capture()
        self._running = True
        threading.Thread(target=self._capture_thread, daemon=True).start()
        threading.Thread(target=self._detection_loop, daemon=True).start()

    def stop(self):
        self._running = False
        if self.camera:
            self.camera.stop()
        self.bridge.stop()
