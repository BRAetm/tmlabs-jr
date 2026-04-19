"""
boost_court_yolo.py — Park Got Next pad finder (PURE YOLO)

Strategy:
  1. Run YOLO detection ONLY — no HSV pollution
  2. Pick the BEST pad (closest to player + biggest)
  3. Lock onto it for ~3 seconds
  4. Walk straight at it with LS
  5. Press A when "Got Next" prompt appears

If you want HSV detection, use boost_court.py instead.
This script trusts the trained model to know what pads look like.
"""

import os
import time
import cv2
import numpy as np
from helios_compat import *

DIRECT_CAPTURE = False  # Cloud play uses C# screencast

# ═══════════════════════════════════════════════════════════════════
# CONFIG
# ═══════════════════════════════════════════════════════════════════

# YOLO model
MODEL_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pad_detector.pt")
YOLO_CONFIDENCE = 0.15      # lower = more sensitive (catch more, get more false positives)
YOLO_IMGSZ = 480

# Got Next prompt detector (UI element at bottom of screen)
PROMPT_LOWER = np.array([55, 110, 130])
PROMPT_UPPER = np.array([95, 255, 255])
MIN_PROMPT_PIXELS = 20
MAX_PROMPT_PIXELS = 800

# Movement (LS only — no camera control)
WALK_FORWARD_Y = -1.0       # full forward
LS_STEER_GAIN = 1.5         # how aggressively to steer toward target
LS_DEADZONE = 0.05          # ignore tiny offsets

# Target locking
TARGET_LOCK_FRAMES = 90     # ~3 seconds at 30fps
GHOST_LOCK_FRAMES = 30      # ~1 sec of "keep walking even after losing sight"
LOCK_DROP_DIST_PX = 250     # if target jumps more than this, drop the lock

# A button press
PRESS_COOLDOWN_SEC = 5.0

# ═══════════════════════════════════════════════════════════════════
# STATE
# ═══════════════════════════════════════════════════════════════════

_model           = None
_model_loaded    = False
_load_error      = None

_locked_target   = None     # (cx, cy, w, h, conf, source)
_lock_frames     = 0
_ghost_frames    = 0

_last_press_time = 0.0
_total_presses   = 0

_frame_count     = 0
_fps_start       = 0.0
_fps             = 0.0


# ═══════════════════════════════════════════════════════════════════
# LIFECYCLE
# ═══════════════════════════════════════════════════════════════════

def on_start(config: dict) -> None:
    global _model, _model_loaded, _load_error
    global _locked_target, _lock_frames, _ghost_frames
    global _last_press_time, _total_presses, _frame_count, _fps_start

    _model = None
    _model_loaded = False
    _load_error = None
    _locked_target = None
    _lock_frames = 0
    _ghost_frames = 0
    _last_press_time = 0.0
    _total_presses = 0
    _frame_count = 0
    _fps_start = time.monotonic()

    print(f"[boost] starting on session {config['session_id']}")

    # Try to load YOLO model — falls through to HSV-only if missing
    if os.path.isfile(MODEL_PATH):
        try:
            from ultralytics import YOLO
            _model = YOLO(MODEL_PATH)
            # Warm up so first inference isn't slow
            _ = _model.predict(np.zeros((YOLO_IMGSZ, YOLO_IMGSZ, 3), dtype=np.uint8),
                               imgsz=YOLO_IMGSZ, verbose=False)
            _model_loaded = True
            print(f"[boost] YOLO model loaded: {os.path.basename(MODEL_PATH)}")
        except Exception as e:
            _load_error = str(e)
            print(f"[boost] YOLO load failed: {e} — using HSV only")
    else:
        print(f"[boost] No YOLO model — using HSV only")


def on_stop() -> None:
    print(f"[boost] stopped. Total A presses: {_total_presses}")


# ═══════════════════════════════════════════════════════════════════
# DETECTION
# ═══════════════════════════════════════════════════════════════════

def detect_yolo(frame: np.ndarray):
    """Pure YOLO detection. Returns list of (x1, y1, x2, y2, conf)."""
    if _model is None:
        return []
    try:
        results = _model.predict(frame, imgsz=YOLO_IMGSZ, conf=YOLO_CONFIDENCE, verbose=False)
        if not results or results[0].boxes is None or len(results[0].boxes) == 0:
            return []
        boxes = results[0].boxes.xyxy.cpu().numpy()
        confs = results[0].boxes.conf.cpu().numpy()
        return [(int(x1), int(y1), int(x2), int(y2), float(c))
                for (x1, y1, x2, y2), c in zip(boxes, confs)]
    except Exception as e:
        print(f"[boost] YOLO error: {e}")
        return []


def detect_prompt(frame: np.ndarray):
    """Detect the 'Got Next' A-button prompt at bottom of screen."""
    h, w = frame.shape[:2]
    y1 = int(h * 0.85); y2 = int(h * 0.99)
    x1 = int(w * 0.30); x2 = int(w * 0.70)
    roi = frame[y1:y2, x1:x2]
    if roi.size == 0:
        return False, 0
    hsv = cv2.cvtColor(roi, cv2.COLOR_BGR2HSV)
    mask = cv2.inRange(hsv, PROMPT_LOWER, PROMPT_UPPER)
    count = int(cv2.countNonZero(mask))
    return MIN_PROMPT_PIXELS <= count <= MAX_PROMPT_PIXELS, count


# ═══════════════════════════════════════════════════════════════════
# TARGET SELECTION
# ═══════════════════════════════════════════════════════════════════

def score_detection(det, player_anchor):
    """Higher score = better target. Closer + bigger + more confident wins."""
    x1, y1, x2, y2, conf = det
    cx = (x1 + x2) / 2
    cy = (y1 + y2) / 2
    area = (x2 - x1) * (y2 - y1)
    dx = cx - player_anchor[0]
    dy = cy - player_anchor[1]
    dist = (dx * dx + dy * dy) ** 0.5
    return -dist * 1.0 + area * 0.3 + conf * 200.0


def select_target(detections, player_anchor):
    """Pick the highest-scoring detection."""
    if not detections:
        return None
    return max(detections, key=lambda d: score_detection(d, player_anchor))


def update_lock(detections, player_anchor):
    """
    Maintain or refresh the target lock.
    Returns (target_box_or_None, target_cx_or_None, target_cy_or_None, is_ghost)
    """
    global _locked_target, _lock_frames, _ghost_frames

    # Try to re-acquire the locked target in current detections
    if _locked_target is not None and _lock_frames > 0:
        lcx, lcy = _locked_target[0], _locked_target[1]
        best = None
        best_dist = LOCK_DROP_DIST_PX
        for det in detections:
            x1, y1, x2, y2, conf = det
            cx = (x1 + x2) / 2
            cy = (y1 + y2) / 2
            d = ((cx - lcx) ** 2 + (cy - lcy) ** 2) ** 0.5
            if d < best_dist:
                best_dist = d
                best = (cx, cy, x2 - x1, y2 - y1, conf, det)

        if best is not None:
            cx, cy, bw, bh, conf, det = best
            _locked_target = (cx, cy, bw, bh, conf)
            _lock_frames -= 1
            _ghost_frames = GHOST_LOCK_FRAMES
            return det, cx, cy, False

        # Lost sight — use ghost lock if we still have frames
        if _ghost_frames > 0:
            _ghost_frames -= 1
            return None, _locked_target[0], _locked_target[1], True

        _locked_target = None
        _lock_frames = 0
        _ghost_frames = 0

    # No active lock — pick a fresh target
    if detections:
        best = select_target(detections, player_anchor)
        x1, y1, x2, y2, conf = best
        cx = (x1 + x2) / 2
        cy = (y1 + y2) / 2
        _locked_target = (cx, cy, x2 - x1, y2 - y1, conf)
        _lock_frames = TARGET_LOCK_FRAMES
        _ghost_frames = GHOST_LOCK_FRAMES
        return best, cx, cy, False

    return None, None, None, False


# ═══════════════════════════════════════════════════════════════════
# MAIN LOOP
# ═══════════════════════════════════════════════════════════════════

def on_frame(frame: np.ndarray, session_id: int, emit) -> None:
    global _frame_count, _fps_start, _fps, _last_press_time, _total_presses

    h, w = frame.shape[:2]
    now = time.monotonic()
    player_anchor = (w // 2, int(h * 0.85))

    # ── DETECT (PURE YOLO) ──
    detections = detect_yolo(frame) if _model_loaded else []

    # ── TARGET LOCK ──
    target_box, target_cx, target_cy, is_ghost = update_lock(detections, player_anchor)

    # ── PROMPT CHECK ──
    prompt_found, prompt_px = detect_prompt(frame)

    # ── BUILD GAMEPAD STATE ──
    axes = [0.0, 0.0, 0.0, 0.0]
    buttons = [False] * 17

    if prompt_found and (now - _last_press_time) >= PRESS_COOLDOWN_SEC:
        # Press A to join
        buttons[0] = True
        _last_press_time = now
        _total_presses += 1
        print(f"[boost] PRESS A — pads={len(detections)} prompt={prompt_px}px")
    elif target_cx is not None:
        # Walk toward target — LS only
        offset = (target_cx - w / 2) / (w / 2)
        axes[1] = WALK_FORWARD_Y
        if abs(offset) > LS_DEADZONE:
            axes[0] = max(-1.0, min(1.0, offset * LS_STEER_GAIN))

    emit({"session_id": session_id, "axes": axes, "buttons": buttons})

    # ═══════════════════════════════════════════════════
    # OVERLAY (visual debug — what the bot sees)
    # ═══════════════════════════════════════════════════

    # Draw all YOLO detection boxes
    for det in detections:
        x1, y1, x2, y2, conf = det
        is_target = (target_box is not None and det is target_box)
        if is_target:
            color = (0, 255, 0)
            thickness = 4
            label = f"TARGET {conf:.2f}"
        else:
            color = (255, 200, 0)
            thickness = 2
            label = f"pad {conf:.2f}"
        cv2.rectangle(frame, (x1, y1), (x2, y2), color, thickness)
        (tw, th), _b = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.45, 1)
        cv2.rectangle(frame, (x1, y1 - th - 6), (x1 + tw + 6, y1), color, -1)
        cv2.putText(frame, label, (x1 + 3, y1 - 4),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 0, 0), 1)

    # Player anchor + arrow to target (the line that gets shorter as you approach)
    px, py = player_anchor
    cv2.circle(frame, (px, py), 7, (0, 255, 255), -1)
    cv2.circle(frame, (px, py), 9, (0, 0, 0), 1)

    if target_cx is not None:
        tx, ty = int(target_cx), int(target_cy)
        dist = int(((tx - px) ** 2 + (ty - py) ** 2) ** 0.5)
        arrow_color = (0, 255, 255) if is_ghost else (0, 255, 0)
        thickness = max(2, min(8, dist // 80))
        cv2.arrowedLine(frame, (px, py), (tx, ty), arrow_color, thickness, cv2.LINE_AA, tipLength=0.15)

        # Distance label at midpoint
        mx, my = (px + tx) // 2, (py + ty) // 2
        dist_label = f"{dist}px" + (" GHOST" if is_ghost else "")
        (tw, th), _b = cv2.getTextSize(dist_label, cv2.FONT_HERSHEY_SIMPLEX, 0.55, 2)
        cv2.rectangle(frame, (mx - 4, my - th - 6), (mx + tw + 4, my + 4), (0, 0, 0), -1)
        cv2.putText(frame, dist_label, (mx, my),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, arrow_color, 2, cv2.LINE_AA)

    # FPS counter
    _frame_count += 1
    elapsed = now - _fps_start
    if elapsed >= 1.0:
        _fps = _frame_count / elapsed
        _frame_count = 0
        _fps_start = now

    # HUD
    mode = "YOLO" if _model_loaded else "NO MODEL"
    mode_color = (0, 255, 0) if _model_loaded else (0, 0, 255)
    cv2.rectangle(frame, (5, 5), (450, 100), (0, 0, 0), -1)
    cv2.rectangle(frame, (5, 5), (450, 100), (60, 60, 60), 1)
    cv2.putText(frame, f"BoostCourt [{mode}] | {_fps:.0f} fps", (12, 28),
                cv2.FONT_HERSHEY_SIMPLEX, 0.55, mode_color, 2)
    cv2.putText(frame, f"Pads detected: {len(detections)}  conf threshold: {YOLO_CONFIDENCE}",
                (12, 50), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 255), 1)
    lock_state = ("LOCK" if not is_ghost else "GHOST") if target_cx is not None else "no target"
    cv2.putText(frame, f"State: {lock_state}  Lock: {_lock_frames}  Ghost: {_ghost_frames}",
                (12, 68), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (180, 220, 255), 1)
    cv2.putText(frame, f"Stick X={axes[0]:+.2f} Y={axes[1]:+.2f}  Presses: {_total_presses}",
                (12, 86), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (180, 220, 255), 1)

    preview = cv2.resize(frame, (960, 540)) if w > 1280 else frame
    cv2.imshow("TM Labs - Boost Court", preview)
    cv2.waitKey(1)
