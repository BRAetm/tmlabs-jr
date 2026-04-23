"""
SecretK.py — Headless NBA 2K shot-timing script.

Spawned by LabsEngine's CV Python plugin with:
  python SecretK.py [--labs-pid N] [--session N] [--threshold 0.95] ...

Architecture (Path A — wrap existing Remote Play):
  • find_window()        — locates whatever Remote Play window the user has open
                           (Chiaki, Xbox Remote Play, official PS Remote Play)
  • BetterCam (or mss)   — captures that window as BGR frames
  • ShotMeterDetector    — meter fill + velocity prediction
  • PSControllerBridge   — VDS4 + VX360 virtual pads with defense AI,
                           stick tempo, quickstop, contest flick, auto-hands-up
  • LabsEngine's XInput  — reads our virtual pad (slot 1+ with skip mask)
    plugin                 and forwards to the stream.

--labs-pid / --session are accepted for LabsEngine CvPythonPlugin compatibility
but ignored — gamepad transport is vgamepad, not SHM.
"""
import argparse
import os
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
    import cv2
    import numpy as np
    MSS_OK = True
except ImportError:
    MSS_OK = False


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--labs-pid",     type=int,   default=None, help="(unused, compat)")
    p.add_argument("--session",      type=int,   default=None, help="(unused, compat)")
    p.add_argument("--threshold",    type=float, default=0.95)
    p.add_argument("--threshold-l2", type=float, default=0.75)
    p.add_argument("--tempo-ms",     type=int,   default=39)
    p.add_argument("--tempo",        action="store_true")
    p.add_argument("--stick-tempo",  action="store_true", dest="stick_tempo")
    p.add_argument("--quickstop",    action="store_true")
    p.add_argument("--defense",      action="store_true")
    p.add_argument("--stamina",      action="store_true")
    p.add_argument("--no-hands-up",  action="store_true", dest="no_hands_up")
    p.add_argument("--xi-index",     type=int,   default=0, dest="xi_index")
    p.add_argument("--gpu",          type=int,   default=0, help="BetterCam device_idx")
    p.add_argument("--target-fps",     type=int, default=120, dest="target_fps",
                   help="BetterCam capture target FPS (default 120; 60 for low-end)")
    p.add_argument("--passthrough-hz", type=int, default=500, dest="passthrough_hz",
                   help="XInput passthrough rate Hz (default 500; 250 for low-end)")
    p.add_argument("--detect-every-n", type=int, default=1, dest="detect_every_n",
                   help="Process every Nth captured frame (default 1; 2 for low-end)")
    p.add_argument("--low-end",        action="store_true", dest="low_end",
                   help="Preset: 60fps capture / 250Hz passthrough / every-2 frames")
    p.add_argument("--capture",      choices=["bettercam", "mss"], default="bettercam",
                   help="Capture backend (mss is slower but works without DXGI/GPU)")
    args = p.parse_args()

    print(f"[ENGINE] pid={os.getpid()}", flush=True)

    # --low-end shortcut: only applies to values still at their defaults
    if args.low_end:
        if args.target_fps     == 120: args.target_fps     = 60
        if args.passthrough_hz == 500: args.passthrough_hz = 250
        if args.detect_every_n == 1:   args.detect_every_n = 2

    # find LabsEngine's Remote Play window
    region = find_window()
    reg = (region["left"], region["top"],
           region["left"] + region["width"],
           region["top"]  + region["height"])

    # capture backend selection: bettercam (GPU) → mss (CPU) fallback
    cam = None
    sct = None
    capture_mode = args.capture
    if capture_mode == "bettercam":
        if not BC_OK:
            print("[ENGINE] BetterCam not installed — falling back to mss", flush=True)
            capture_mode = "mss"
        else:
            try:
                cam = bettercam.create(device_idx=args.gpu, output_color="BGR")
                cam.start(region=reg, target_fps=args.target_fps, video_mode=True)
                print(f"[ENGINE] BetterCam region={reg} target_fps={args.target_fps} gpu={args.gpu}",
                      flush=True)
            except Exception as ex:
                print(f"[ENGINE] BetterCam init failed: {ex} — falling back to mss", flush=True)
                capture_mode = "mss"
    if capture_mode == "mss":
        if not MSS_OK:
            print("[ENGINE] ERROR: neither bettercam nor mss available — pip install mss",
                  flush=True)
            sys.exit(1)
        sct = mss.mss()
        mss_region = {"left": reg[0], "top": reg[1],
                      "width": reg[2]-reg[0], "height": reg[3]-reg[1]}
        print(f"[ENGINE] mss region={mss_region}  (CPU capture)", flush=True)

    print(f"[ENGINE] perf: target_fps={args.target_fps} "
          f"passthrough_hz={args.passthrough_hz} detect_every_n={args.detect_every_n}",
          flush=True)

    detector = ShotMeterDetector(args.threshold, args.threshold_l2)
    bridge   = PSControllerBridge()
    bridge.defense_enabled     = args.defense
    bridge.infinite_stamina    = args.stamina
    bridge.stick_tempo_enabled = args.stick_tempo
    bridge.quickstop_enabled   = args.quickstop

    print(f"[ENGINE] thresholds: normal={args.threshold*100:.0f}%  L2={args.threshold_l2*100:.0f}%",
          flush=True)
    print(f"[ENGINE] flags: defense={args.defense} stamina={args.stamina} "
          f"tempo={args.tempo} stick_tempo={args.stick_tempo} quickstop={args.quickstop}",
          flush=True)

    _stop = threading.Event()

    def _handle_signal(signum, _frame):
        print(f"[ENGINE] signal {signum} — stopping", flush=True)
        _stop.set()
    signal.signal(signal.SIGINT,  _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)
    if hasattr(signal, "SIGBREAK"):
        signal.signal(signal.SIGBREAK, _handle_signal)

    # XInput → virtual pads via PSControllerBridge.passthrough at args.passthrough_hz
    pt_interval = 1.0 / max(1, args.passthrough_hz)
    def passthrough_loop():
        while not _stop.is_set():
            gp = _read_xinput(args.xi_index)
            bridge.passthrough(gp)
            # DPAD_UP rising-edge signal from the bridge → toggle defense mode
            if bridge._qs_toggle_requested:
                bridge.defense_enabled = not bridge.defense_enabled
                bridge._qs_toggle_requested = False
                print(f"[BRIDGE] defense {'ON' if bridge.defense_enabled else 'OFF'}", flush=True)
            time.sleep(pt_interval)

    threading.Thread(target=passthrough_loop, daemon=True).start()

    # detection loop
    print(f"[ENGINE] Running — capture={capture_mode} passthrough={args.passthrough_hz}Hz "
          f"detect_every_n={args.detect_every_n}", flush=True)
    fc              = 0
    frame_n         = 0
    status_tw       = time.perf_counter()
    cal_reported_n  = 0
    cal_reported_l2 = 0
    every_n         = max(1, args.detect_every_n)
    try:
        while not _stop.is_set():
            try:
                # skip-before-capture: avoid expensive mss grab on dropped frames
                frame_n += 1
                if frame_n % every_n != 0:
                    if capture_mode == "bettercam":
                        cam.get_latest_frame()  # drain so we get fresh frame next tick
                    continue

                if capture_mode == "bettercam":
                    bgr = cam.get_latest_frame()
                else:
                    bgra = np.asarray(sct.grab(mss_region))
                    bgr  = cv2.cvtColor(bgra, cv2.COLOR_BGRA2BGR)
                if bgr is None:
                    continue

                gp = _read_xinput(args.xi_index)
                l2 = bool(gp and gp.bLeftTrigger > 128)

                if detector.check(bgr, l2=l2):
                    if bridge.defense_enabled:
                        bridge.contest_flick()
                    else:
                        bridge.fire_shot(l2=l2, tempo=args.tempo, tempo_ms=args.tempo_ms)
                    print(f"[ENGINE] SHOT #{detector.shots_fired} type={'L2' if l2 else 'NORM'}",
                          flush=True)

                np_n  = len(detector._peak_history)
                np_l2 = len(detector._peak_history_l2)
                if np_n  != cal_reported_n  and not detector._calibration_locked:
                    print(f"[CAL] normal {np_n}/4", flush=True)
                    cal_reported_n = np_n
                if np_l2 != cal_reported_l2 and not detector._calibration_locked_l2:
                    print(f"[CAL] L2 {np_l2}/4", flush=True)
                    cal_reported_l2 = np_l2

                fc += 1
                now = time.perf_counter()
                if now - status_tw >= 1.0:
                    fps    = fc / (now - status_tw)
                    cal_n  = "LOCK" if detector._calibration_locked    else f"{np_n}/4"
                    cal_l2 = "LOCK" if detector._calibration_locked_l2 else f"{np_l2}/4"
                    print(f"[STATUS] fps={fps:.0f} shots={detector.shots_fired} "
                          f"cal=(n:{cal_n} l2:{cal_l2}) defense={bridge.defense_enabled}",
                          flush=True)
                    fc = 0
                    status_tw = now
            except Exception as ex:
                print(f"[ENGINE] frame error: {ex}", flush=True)
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
        print("[ENGINE] Stopped", flush=True)


if __name__ == "__main__":
    main()
