"""
labs2kmain.py — backend engine for the NBA 2K shot-timing scripts.

This is the engine. It owns the capture, detection, and gamepad pipeline.
Scripts (SecretK.py and friends) import `run` and pass their settings.

Public API:
    run(settings) -> int
        settings: a dict-like with the keys defined in DEFAULT_SETTINGS.
        Returns 0 on clean exit, non-zero on error.

No UI, no argparse — those belong in the script that calls this.
"""
import signal
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent / "cv-scripts"))

from shot_meter import ShotMeterDetector, find_window, _read_xinput  # noqa: E402
from features  import PSControllerBridge                              # noqa: E402

try:
    import bettercam
    BC_OK = True
except ImportError:
    BC_OK = False

try:
    import mss
    import cv2  # noqa: F401  (used in mss path)
    import numpy as np
    MSS_OK = True
except ImportError:
    MSS_OK = False


DEFAULT_SETTINGS = {
    "threshold":        0.95,
    "threshold_l2":     0.75,
    "tempo":            False,
    "tempo_ms":         39,
    "stick_tempo":      False,
    "quickstop":        False,
    "defense":          False,
    "stamina":          False,
    "no_hands_up":      False,
    "xi_index":         0,
    "gpu":              0,
    "target_fps":       120,
    "passthrough_hz":   500,
    "detect_every_n":   1,
    "low_end":          False,
    "capture":          "bettercam",   # bettercam | mss
}


def _resolve_settings(s: dict) -> dict:
    out = dict(DEFAULT_SETTINGS)
    out.update(s or {})
    if out["low_end"]:
        if out["target_fps"]     == DEFAULT_SETTINGS["target_fps"]:     out["target_fps"]     = 60
        if out["passthrough_hz"] == DEFAULT_SETTINGS["passthrough_hz"]: out["passthrough_hz"] = 250
        if out["detect_every_n"] == DEFAULT_SETTINGS["detect_every_n"]: out["detect_every_n"] = 2
    return out


def run(settings: dict | None = None,
        stop_event: "threading.Event | None" = None,
        on_shot=None,
        on_status=None) -> int:
    """
    Block on the engine. Returns when stop_event is set or capture errors fatally.

    settings : dict (see DEFAULT_SETTINGS for keys)
    stop_event : optional Event the caller sets to stop the engine cleanly.
                 If None, the function installs SIGINT/SIGTERM handlers instead.
    on_shot    : optional callback(shots_fired:int, l2:bool) called on each shot.
    on_status  : optional callback(state:dict) called whenever calibration progresses.
    """
    cfg = _resolve_settings(settings or {})

    # Locate the capture window.
    region = find_window()
    reg = (region["left"], region["top"],
           region["left"] + region["width"],
           region["top"]  + region["height"])

    # Capture backend selection (BetterCam → mss fallback).
    cam = None
    sct = None
    mss_region = None
    capture_mode = cfg["capture"]
    if capture_mode == "bettercam":
        if not BC_OK:
            capture_mode = "mss"
        else:
            try:
                cam = bettercam.create(device_idx=cfg["gpu"], output_color="BGR")
                cam.start(region=reg, target_fps=cfg["target_fps"], video_mode=True)
            except Exception as ex:
                print(f"BetterCam unavailable ({ex}) — using CPU capture", flush=True)
                capture_mode = "mss"
    if capture_mode == "mss":
        if not MSS_OK:
            print("ERROR: install bettercam or mss", flush=True)
            return 2
        sct = mss.mss()
        mss_region = {"left": reg[0], "top": reg[1],
                      "width": reg[2] - reg[0], "height": reg[3] - reg[1]}

    # Detector + virtual-pad bridge.
    detector = ShotMeterDetector(cfg["threshold"], cfg["threshold_l2"])
    bridge   = PSControllerBridge()
    bridge.defense_enabled     = cfg["defense"]
    bridge.infinite_stamina    = cfg["stamina"]
    bridge.stick_tempo_enabled = cfg["stick_tempo"]
    bridge.quickstop_enabled   = cfg["quickstop"]

    _stop = stop_event if stop_event is not None else threading.Event()
    # Only install signal handlers when running in main thread (CLI invocation).
    # When called from a UI thread, the caller manages stop via the event.
    if stop_event is None:
        try:
            def _handle_signal(_sig, _frame): _stop.set()
            signal.signal(signal.SIGINT,  _handle_signal)
            signal.signal(signal.SIGTERM, _handle_signal)
            if hasattr(signal, "SIGBREAK"):
                signal.signal(signal.SIGBREAK, _handle_signal)
        except ValueError:
            # signal.signal raises in non-main threads — fine, caller has the event.
            pass

    # Passthrough thread: physical XInput → virtual VDS4/VX360.
    pt_interval = 1.0 / max(1, cfg["passthrough_hz"])
    def passthrough_loop():
        while not _stop.is_set():
            gp = _read_xinput(cfg["xi_index"])
            bridge.passthrough(gp)
            if bridge._qs_toggle_requested:
                bridge.defense_enabled = not bridge.defense_enabled
                bridge._qs_toggle_requested = False
            time.sleep(pt_interval)
    threading.Thread(target=passthrough_loop, daemon=True).start()

    # Detection loop.
    cal_n_seen  = 0
    cal_l2_seen = 0
    every_n     = max(1, cfg["detect_every_n"])
    frame_n     = 0
    try:
        while not _stop.is_set():
            try:
                frame_n += 1
                if frame_n % every_n != 0:
                    if capture_mode == "bettercam":
                        cam.get_latest_frame()
                    continue

                if capture_mode == "bettercam":
                    bgr = cam.get_latest_frame()
                else:
                    bgra = np.asarray(sct.grab(mss_region))
                    bgr  = bgra[:, :, :3]
                if bgr is None:
                    continue

                gp = _read_xinput(cfg["xi_index"])
                l2 = bool(gp and gp.bLeftTrigger > 128)

                if detector.check(bgr, l2=l2):
                    if bridge.defense_enabled:
                        bridge.contest_flick()
                    else:
                        bridge.fire_shot(l2=l2, tempo=cfg["tempo"], tempo_ms=cfg["tempo_ms"])
                    print(f"Shot #{detector.shots_fired}", flush=True)
                    if on_shot:
                        try: on_shot(detector.shots_fired, l2)
                        except Exception: pass

                # Calibration progress (changes only — not per frame).
                np_n  = len(detector._peak_history)
                np_l2 = len(detector._peak_history_l2)
                if np_n != cal_n_seen and not detector._calibration_locked:
                    print(f"Calibrating... {np_n}/4", flush=True)
                    cal_n_seen = np_n
                if np_l2 != cal_l2_seen and not detector._calibration_locked_l2:
                    print(f"Calibrating L2... {np_l2}/4", flush=True)
                    cal_l2_seen = np_l2
            except Exception as ex:
                print(f"Frame error: {ex}", flush=True)
                continue
    finally:
        _stop.set()
        if cam is not None:
            try: cam.stop()
            except Exception: pass
            try: cam.release()
            except Exception: pass
        if sct is not None:
            try: sct.close()
            except Exception: pass

    return 0
