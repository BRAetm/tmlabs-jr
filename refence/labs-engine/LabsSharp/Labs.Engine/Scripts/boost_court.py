"""
boost_court.py — NBA 2K Park Got Next finder

Real CV demo style — every detection is highlighted and labeled so you can
SEE exactly what the script thinks each thing is and why.

Color legend:
  GREEN  box  = "GOT NEXT PAD" — accepted, targeted
  CYAN   box  = "PAD CANDIDATE" — accepted as a pad (smaller, not target)
  RED    box  = "NAMEPLATE"  — rejected (wide-thin shape)
  ORANGE box  = "OWN PLAYER" — rejected (in self-exclusion zone)
  PURPLE box  = "TOO SMALL"  — rejected (below area threshold)
"""

import os
import cv2
import numpy as np
import time

DIRECT_CAPTURE = False  # Cloud play uses C# screencast

# ---------------------------------------------------------------------------
# YOLO model (optional — falls back to HSV if missing)
# ---------------------------------------------------------------------------
MODEL_PATH      = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pad_detector.pt")
YOLO_CONFIDENCE = 0.15
YOLO_IMGSZ      = 480
YOLO_PREFERRED  = True   # when True and model loads, YOLO is the targeting source

# ---------------------------------------------------------------------------
# HSV — Got Next pads' bright cyan-green color
# Tuned from close-up of actual pad: bright glowing cyan with white core
# ---------------------------------------------------------------------------

PAD_LOWER = np.array([70, 80, 180])    # cyan-green hue, high saturation, bright
PAD_UPPER = np.array([95, 255, 255])

PROMPT_LOWER = np.array([55, 110, 130])
PROMPT_UPPER = np.array([95, 255, 255])
MIN_PROMPT_PIXELS = 20
MAX_PROMPT_PIXELS = 800

# ---------------------------------------------------------------------------
# Detection thresholds
# ---------------------------------------------------------------------------

MIN_PAD_AREA   = 60       # distant pads are TINY
MAX_PAD_AREA   = 12000    # close-up pads can be big

# Pads are TRIANGULAR/TRAPEZOID 3D shapes — taller than wide, OR roughly square
# Width / height — pads are typically 0.6-1.4 (slightly taller than wide)
# Nameplates: WIDE text strips, aspect 2.5-6.0
PAD_ASPECT_MIN = 0.4
PAD_ASPECT_MAX = 1.8      # KILLS wide nameplates (which are 2.0+)

# Solidity = contour_area / convex_hull_area
# Pads are solid filled blobs ~0.80+
# Nameplates are text with gaps between letters ~0.3-0.5
MIN_SOLIDITY = 0.70

# Fill ratio = contour_area / bounding_box_area
# Pads fill ~0.55-0.80 of their bounding box
# Nameplate text fills only ~0.25-0.45 of its box
MIN_FILL_RATIO = 0.45

# CIRCULARITY DISABLED — pads are pyramids, not circles
# (this was rejecting real pads — the original main bug)
MIN_CIRCULARITY = 0.0     # disabled

# Bonus check: pads have a BRIGHT WHITE-ISH CORE
# Look for very bright pixels inside the contour
PAD_HAS_BRIGHT_CORE = True
BRIGHT_CORE_V_MIN = 230   # core pixels must be >= this brightness
MIN_BRIGHT_CORE_PIXELS = 3

# Self exclusion column DISABLED — relying on shape filters instead
# (was rejecting real pads in the center of the frame)
SELF_LEFT  = -1.0
SELF_RIGHT = -1.0

# Own nameplate is right above your player at this Y range — only exclude here
OWN_NAMEPLATE_Y_MIN = 0.45
OWN_NAMEPLATE_Y_MAX = 0.65
OWN_NAMEPLATE_X_MIN = 0.42
OWN_NAMEPLATE_X_MAX = 0.58

# Search ROI — EXCLUDE top half (billboards, scoreboards, leaderboards)
# Pads are on the ground in the lower portion of the frame
ROI_TOP    = 0.50    # pads are NEVER in the top half
ROI_BOTTOM = 0.95
ROI_LEFT   = 0.05
ROI_RIGHT  = 0.95

# Walking
WALK_FORWARD_Y = -0.95
STEER_GAIN     = 1.6
STEER_DEADZONE = 0.06
PRESS_COOLDOWN_SEC = 5.0

# Axis inversion fixes — flip if your build sends LS in the wrong direction
INVERT_LS_X = True   # set False if steering goes the right way naturally
INVERT_LS_Y = False  # set True if "forward" walks backward

# ---------------------------------------------------------------------------
# Target locking — stops oscillation between pads
# ---------------------------------------------------------------------------
TARGET_LOCK_FRAMES = 90      # ~3 sec at 30fps
GHOST_LOCK_FRAMES  = 30      # ~1 sec persistence after losing sight
LOCK_DROP_DIST_PX  = 250     # if pad jumps more than this, drop the lock

# ---------------------------------------------------------------------------
# Orientation phase — fix the "going wrong way at start" problem
# ---------------------------------------------------------------------------
# When a new pad is locked, we don't know which direction the player is currently
# facing. Pushing LS forward might walk them whatever direction they were facing,
# which is often the WRONG way. So for the first N frames after locking a new pad,
# push LS purely TOWARD the pad direction (rotation only, no forward) — this
# orients the player to face the pad. After orientation, switch to forward+steer.

ORIENT_FRAMES        = 20    # ~0.7s at 30fps — frames to spend just rotating
ORIENT_LS_GAIN       = 1.0   # how hard to push LS during orientation
START_GRACE_FRAMES   = 30    # ~1s at startup — don't move at all, just observe

# ---------------------------------------------------------------------------
# Right-stick tracking nudge
# ---------------------------------------------------------------------------
# DISABLED by default — if RS makes the player walk in circles, leave this off.
# When locked + walking forward, a constant rightward RS turns the camera right
# which means LS forward now walks the player a different direction every frame.
# Use ADAPTIVE_RS instead (turns toward the pad to keep it centered).
RS_TRACK_WHEN_LOCKED = False
RS_TRACK_X           = 0.0   # set to >0 to enable constant rightward turn
RS_RAMP_FRAMES       = 15

# Adaptive RS — turn camera TOWARD the pad to keep it centered in view.
# Only fires when the pad drifts past the screen edge bands. Set to True to try.
ADAPTIVE_RS_ENABLED  = False
ADAPTIVE_RS_GAIN     = 0.25  # how hard to turn camera per frame
ADAPTIVE_RS_EDGE     = 0.30  # only nudge when pad is in outer 30% of screen

# ---------------------------------------------------------------------------
# Color legend (BGR) for the labels and boxes
# ---------------------------------------------------------------------------

CLR_PAD_TARGET    = (0, 255, 0)      # bright green — the chosen target
CLR_PAD_CANDIDATE = (255, 200, 0)    # cyan — other pad candidates
CLR_NAMEPLATE     = (0, 0, 255)      # red — rejected nameplate
CLR_OWN_PLAYER    = (0, 165, 255)    # orange — rejected (self zone)
CLR_TOO_SMALL     = (200, 0, 200)    # purple — rejected (too small)
CLR_HUD           = (255, 255, 255)
CLR_BLACK         = (0, 0, 0)

# ---------------------------------------------------------------------------
# State
# ---------------------------------------------------------------------------

_frame_count     = 0
_fps_start       = 0.0
_fps             = 0.0
_last_press_time = 0.0
_total_presses   = 0

# YOLO model state
_model         = None
_model_loaded  = False
_load_error    = None

# Target lock state
_locked_target = None     # detection dict or None
_lock_frames   = 0
_ghost_frames  = 0
_rs_ramp       = 0        # frames since lock acquired (for RS ramp-in)
_orient_frames = 0        # frames remaining in orientation phase (rotate-only)
_session_frames = 0       # total frames since on_start (for startup grace period)


def on_start(config: dict) -> None:
    global _frame_count, _fps_start, _last_press_time, _total_presses
    global _model, _model_loaded, _load_error
    global _locked_target, _lock_frames, _ghost_frames, _rs_ramp
    global _orient_frames, _session_frames

    _frame_count = 0
    _fps_start = time.monotonic()
    _last_press_time = 0.0
    _total_presses = 0
    _locked_target = None
    _lock_frames = 0
    _ghost_frames = 0
    _rs_ramp = 0
    _orient_frames = 0
    _session_frames = 0
    _model = None
    _model_loaded = False
    _load_error = None

    print(f"[boost_court] started on session {config['session_id']}")

    # Load template library (from smart_label.py click sessions)
    n_templates = load_pad_templates()
    if n_templates > 0:
        print(f"[boost_court] template matching enabled ({n_templates} templates)")

    if os.path.isfile(MODEL_PATH):
        try:
            from ultralytics import YOLO
            _model = YOLO(MODEL_PATH)
            # Warm up so first real inference isn't slow
            _ = _model.predict(
                np.zeros((YOLO_IMGSZ, YOLO_IMGSZ, 3), dtype=np.uint8),
                imgsz=YOLO_IMGSZ, verbose=False,
            )
            _model_loaded = True
            print(f"[boost_court] YOLO loaded: {os.path.basename(MODEL_PATH)}")
        except Exception as e:
            _load_error = str(e)
            print(f"[boost_court] YOLO load failed: {e} — HSV only")
    else:
        print(f"[boost_court] No YOLO model — HSV only")


# ---------------------------------------------------------------------------
# Detection — returns CLASSIFIED detections (each with a label + reason)
# ---------------------------------------------------------------------------

def classify_detections(frame: np.ndarray) -> list:
    """
    Run color detection over the search ROI, then classify EVERY contour
    we find into one of these categories:
        "pad"          — accepted as a Got Next pad
        "nameplate"    — rejected (wrong shape — too wide, too thin)
        "own_player"   — rejected (inside self-exclusion column)
        "too_small"    — rejected (below MIN_PAD_AREA)
    Returns a list of dicts: {x, y, w, h, area, label, color, accepted}
    """
    h, w = frame.shape[:2]

    # ROI rect (full-frame coords)
    rx1 = int(w * ROI_LEFT);  ry1 = int(h * ROI_TOP)
    rx2 = int(w * ROI_RIGHT); ry2 = int(h * ROI_BOTTOM)
    roi = frame[ry1:ry2, rx1:rx2]
    if roi.size == 0:
        return []

    hsv = cv2.cvtColor(roi, cv2.COLOR_BGR2HSV)
    mask = cv2.inRange(hsv, PAD_LOWER, PAD_UPPER)

    # Light morph cleanup so noise doesn't fragment a pad into 5 contours
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel, iterations=1)

    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    # Own-nameplate exclusion box (small box where your own player's name appears)
    onx1 = int(w * OWN_NAMEPLATE_X_MIN)
    onx2 = int(w * OWN_NAMEPLATE_X_MAX)
    ony1 = int(h * OWN_NAMEPLATE_Y_MIN)
    ony2 = int(h * OWN_NAMEPLATE_Y_MAX)

    detections = []
    for cnt in contours:
        area = cv2.contourArea(cnt)
        x, y, bw, bh = cv2.boundingRect(cnt)
        # Convert to full-frame coords
        fx = x + rx1
        fy = y + ry1

        # Skip near-zero junk so we don't draw thousands of dots
        if bw < 4 or bh < 4 or area < 30:
            continue

        # Default classification
        label    = ""
        color    = CLR_TOO_SMALL
        accepted = False

        # CHECK 1: OWN PLAYER exclusion DISABLED (was rejecting real pads)
        contour_cx = fx + bw // 2
        contour_cy = fy + bh // 2

        # CHECK 2: Too small?
        if area < MIN_PAD_AREA:
            label = "TOO SMALL"
            color = CLR_TOO_SMALL
            detections.append({
                "x": fx, "y": fy, "w": bw, "h": bh, "area": int(area),
                "label": label, "color": color, "accepted": False,
            })
            continue

        # CHECK 3: Aspect ratio — too wide/tall?
        aspect = bw / max(bh, 1)
        if aspect > PAD_ASPECT_MAX or aspect < PAD_ASPECT_MIN:
            label = "NAMEPLATE"
            color = CLR_NAMEPLATE
            detections.append({
                "x": fx, "y": fy, "w": bw, "h": bh, "area": int(area),
                "label": label, "color": color, "accepted": False,
            })
            continue

        # CHECK 4: Solidity — pads are solid blobs, nameplates have gaps between letters
        hull = cv2.convexHull(cnt)
        hull_area = cv2.contourArea(hull)
        solidity = area / max(hull_area, 1.0)
        if solidity < MIN_SOLIDITY:
            label = f"NAMEPLATE sol={solidity:.2f}"
            color = CLR_NAMEPLATE
            detections.append({
                "x": fx, "y": fy, "w": bw, "h": bh, "area": int(area),
                "label": label, "color": color, "accepted": False,
            })
            continue

        # CHECK 5: Fill ratio — how much of the bbox the contour covers
        # Pads fill ~70%+, text fills ~30-45% (lots of empty space between letters)
        fill_ratio = area / max(bw * bh, 1)
        if fill_ratio < MIN_FILL_RATIO:
            label = f"NAMEPLATE fill={fill_ratio:.2f}"
            color = CLR_NAMEPLATE
            detections.append({
                "x": fx, "y": fy, "w": bw, "h": bh, "area": int(area),
                "label": label, "color": color, "accepted": False,
            })
            continue

        # CHECK 6: Bright white core — pads have a glowing white center,
        # text/nameplates don't have super-bright pixels
        if PAD_HAS_BRIGHT_CORE:
            # Crop the bounding box from the original frame and check brightness
            crop = frame[fy:fy + bh, fx:fx + bw]
            if crop.size > 0:
                gray = cv2.cvtColor(crop, cv2.COLOR_BGR2GRAY)
                bright_count = int(np.sum(gray >= BRIGHT_CORE_V_MIN))
                if bright_count < MIN_BRIGHT_CORE_PIXELS:
                    label = f"NAMEPLATE no-glow"
                    color = CLR_NAMEPLATE
                    detections.append({
                        "x": fx, "y": fy, "w": bw, "h": bh, "area": int(area),
                        "label": label, "color": color, "accepted": False,
                    })
                    continue

        # PASSED ALL CHECKS — it's a pad
        label    = f"PAD a={int(area)} ar={aspect:.2f}"
        color    = CLR_PAD_CANDIDATE
        accepted = True
        detections.append({
            "x": fx, "y": fy, "w": bw, "h": bh, "area": int(area),
            "label": label, "color": color, "accepted": True,
        })

    return detections


def detect_prompt(frame: np.ndarray) -> tuple:
    """Got Next 'A' prompt at bottom-center."""
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


# ---------------------------------------------------------------------------
# Template matching detection — uses the smart_label.py template library
# ---------------------------------------------------------------------------
# Templates live in: <Scripts>/boost_court_templates/_templates/pad/*.png
# Each template was extracted from a clicked pad in the labeler.
# At runtime, we slide every template across the current frame and find matches.
# Way faster than YOLO and trained from your real gameplay clicks.

TEMPLATE_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "boost_court_templates", "_templates", "pad"
)
TEMPLATE_MATCH_THRESHOLD = 0.65
TEMPLATE_NMS_IOU = 0.30

_pad_templates = []   # list of loaded template images
_templates_loaded = False


def load_pad_templates():
    """Read all PNG templates from the templates folder into memory."""
    global _pad_templates, _templates_loaded
    _pad_templates = []
    if os.path.isdir(TEMPLATE_DIR):
        for f in sorted(os.listdir(TEMPLATE_DIR)):
            if f.lower().endswith((".png", ".jpg", ".jpeg")):
                img = cv2.imread(os.path.join(TEMPLATE_DIR, f))
                if img is not None and img.size > 0:
                    _pad_templates.append(img)
    _templates_loaded = True
    if _pad_templates:
        print(f"[boost_court] loaded {len(_pad_templates)} pad templates from {TEMPLATE_DIR}")
    return len(_pad_templates)


def detect_templates(frame: np.ndarray) -> list:
    """Run all pad templates against the frame, dedupe with NMS.
    Returns dicts in the same format as classify_detections()."""
    if not _pad_templates:
        return []

    h, w = frame.shape[:2]
    raw_matches = []  # (x1, y1, x2, y2, score)

    for tpl in _pad_templates:
        th, tw = tpl.shape[:2]
        if h < th or w < tw:
            continue
        try:
            result = cv2.matchTemplate(frame, tpl, cv2.TM_CCOEFF_NORMED)
        except Exception:
            continue
        ys, xs = np.where(result >= TEMPLATE_MATCH_THRESHOLD)
        for x, y in zip(xs, ys):
            raw_matches.append((int(x), int(y), int(x + tw), int(y + th),
                                float(result[y, x])))

    if not raw_matches:
        return []

    # NMS — drop overlapping boxes, keep highest-score one
    raw_matches.sort(key=lambda b: -b[4])  # by score desc
    kept = []
    for m in raw_matches:
        x1, y1, x2, y2, score = m
        is_dup = False
        for k in kept:
            kx1, ky1, kx2, ky2, _ = k
            ix1 = max(x1, kx1); iy1 = max(y1, ky1)
            ix2 = min(x2, kx2); iy2 = min(y2, ky2)
            iw = max(0, ix2 - ix1); ih = max(0, iy2 - iy1)
            inter = iw * ih
            if inter > 0:
                a1 = (x2 - x1) * (y2 - y1)
                a2 = (kx2 - kx1) * (ky2 - ky1)
                iou = inter / (a1 + a2 - inter)
                if iou > TEMPLATE_NMS_IOU:
                    is_dup = True
                    break
        if not is_dup:
            kept.append(m)

    out = []
    for (x1, y1, x2, y2, score) in kept:
        bw = x2 - x1
        bh = y2 - y1
        out.append({
            "x": x1, "y": y1, "w": bw, "h": bh,
            "area": int(bw * bh),
            "label": f"TPL {score:.2f}",
            "color": CLR_PAD_CANDIDATE,
            "accepted": True,
        })
    return out


# ---------------------------------------------------------------------------
# YOLO detection — returns dicts in the same shape as classify_detections()
# ---------------------------------------------------------------------------

def detect_yolo(frame: np.ndarray) -> list:
    """Run YOLO and return ONLY pad detections (class 0). Multi-class models
    may also detect nameplates/walls/etc — those are training negatives only,
    not targets. Returns dicts in classify_detections() format."""
    if _model is None:
        return []
    try:
        results = _model.predict(frame, imgsz=YOLO_IMGSZ, conf=YOLO_CONFIDENCE, verbose=False)
        if not results or results[0].boxes is None or len(results[0].boxes) == 0:
            return []
        boxes = results[0].boxes.xyxy.cpu().numpy()
        confs = results[0].boxes.conf.cpu().numpy()
        cls_ids = results[0].boxes.cls.cpu().numpy() if results[0].boxes.cls is not None else None
    except Exception as e:
        print(f"[boost_court] YOLO error: {e}")
        return []

    out = []
    for i, ((x1, y1, x2, y2), c) in enumerate(zip(boxes, confs)):
        # Filter to class 0 (pad) only — single-class models have no cls,
        # multi-class models do
        if cls_ids is not None and int(cls_ids[i]) != 0:
            continue
        x, y = int(x1), int(y1)
        bw, bh = int(x2 - x1), int(y2 - y1)
        if bw <= 0 or bh <= 0:
            continue
        out.append({
            "x": x, "y": y, "w": bw, "h": bh,
            "area": int(bw * bh),
            "label": f"YOLO pad {c:.2f}",
            "color": CLR_PAD_CANDIDATE,
            "accepted": True,
        })
    return out


# ---------------------------------------------------------------------------
# Target lock — keeps the same pad selected across frames (no oscillation)
# ---------------------------------------------------------------------------

def _det_center(d):
    return (d["x"] + d["w"] / 2.0, d["y"] + d["h"] / 2.0)


def update_lock(accepted_dets: list):
    """
    Maintain or refresh target lock across frames.
    Returns (target_dict_or_None, cx_or_None, cy_or_None, is_ghost).
    """
    global _locked_target, _lock_frames, _ghost_frames, _rs_ramp

    # Try to re-acquire the existing lock in this frame's detections
    if _locked_target is not None and _lock_frames > 0:
        lcx, lcy = _det_center(_locked_target)
        best = None
        best_dist = LOCK_DROP_DIST_PX
        for d in accepted_dets:
            cx, cy = _det_center(d)
            dist = ((cx - lcx) ** 2 + (cy - lcy) ** 2) ** 0.5
            if dist < best_dist:
                best_dist = dist
                best = d
        if best is not None:
            _locked_target = best
            _lock_frames -= 1
            _ghost_frames = GHOST_LOCK_FRAMES
            cx, cy = _det_center(best)
            return best, cx, cy, False

        # Lost sight — ghost lock if we still have frames left
        if _ghost_frames > 0:
            _ghost_frames -= 1
            lcx, lcy = _det_center(_locked_target)
            return None, lcx, lcy, True

        _locked_target = None
        _lock_frames = 0
        _ghost_frames = 0
        _rs_ramp = 0

    # No active lock — pick a fresh target (biggest accepted, matches old behavior)
    if accepted_dets:
        global _orient_frames
        fresh = max(accepted_dets, key=lambda d: d["area"])
        _locked_target = fresh
        _lock_frames = TARGET_LOCK_FRAMES
        _ghost_frames = GHOST_LOCK_FRAMES
        _rs_ramp = 0
        _orient_frames = ORIENT_FRAMES   # start orientation phase
        cx, cy = _det_center(fresh)
        return fresh, cx, cy, False

    return None, None, None, False


# ---------------------------------------------------------------------------
# Drawing helpers
# ---------------------------------------------------------------------------

def _draw_label(frame, text, x, y, color):
    """Draw a colored box with the label name above the detection."""
    (tw, th), _ = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, 0.45, 1)
    pad = 3
    bx1 = x
    by1 = max(0, y - th - pad * 2)
    bx2 = x + tw + pad * 2
    by2 = by1 + th + pad * 2
    cv2.rectangle(frame, (bx1, by1), (bx2, by2), color, -1)
    cv2.putText(frame, text, (bx1 + pad, by2 - pad - 1),
                cv2.FONT_HERSHEY_SIMPLEX, 0.45, CLR_BLACK, 1)


def draw_detections(frame, detections, target_idx):
    """Draw all detections with their labels — like nba2k_players.py style."""
    for i, det in enumerate(detections):
        x, y, w, h = det["x"], det["y"], det["w"], det["h"]
        color = det["color"]
        label = det["label"]
        thickness = 2

        # Highlight the target with extra thick green box
        if i == target_idx:
            color = CLR_PAD_TARGET
            thickness = 4
            label = "TARGET"

        cv2.rectangle(frame, (x, y), (x + w, y + h), color, thickness)
        # Label with size info
        text = f"{label} {det['area']}px"
        _draw_label(frame, text, x, y, color)


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

def on_frame(frame: np.ndarray, session_id: int, emit) -> None:
    global _frame_count, _fps_start, _fps, _last_press_time, _total_presses

    h, w = frame.shape[:2]
    now = time.monotonic()

    # ── Run detections (HSV always; YOLO + templates if available) ──
    hsv_dets = classify_detections(frame)
    yolo_dets = detect_yolo(frame) if _model_loaded else []
    tpl_dets = detect_templates(frame) if _pad_templates else []

    # Priority order for targeting:
    #   1. Templates (if any loaded — your real clicked pads, most accurate)
    #   2. YOLO (if model loaded)
    #   3. HSV fallback
    if tpl_dets:
        targeting_pool = [d for d in tpl_dets if d["accepted"]]
    elif _model_loaded and YOLO_PREFERRED:
        targeting_pool = [d for d in yolo_dets if d["accepted"]]
    else:
        targeting_pool = [d for d in hsv_dets if d["accepted"]]

    all_dets_for_draw = hsv_dets + yolo_dets + tpl_dets

    prompt_found, prompt_px = detect_prompt(frame)

    # ── DEAD SIMPLE: pick biggest accepted pad each frame ──
    target_x = None
    target_y = None
    target_idx = -1
    is_ghost = False
    if targeting_pool:
        biggest = max(targeting_pool, key=lambda d: d["area"])
        target_x = biggest["x"] + biggest["w"] // 2
        target_y = biggest["y"] + biggest["h"] // 2
        try:
            target_idx = all_dets_for_draw.index(biggest)
        except ValueError:
            target_idx = -1

    # ── Build gamepad output ──
    # The cloud gaming pipeline (WebView2 → JS gamepad → 2K) ignores weak axis
    # values due to input deadzones. We MUST send strong values (>= 0.5) for
    # the game to register movement. So: full forward whenever we have a
    # target, and use X for steering at full strength.
    axes = [0.0, 0.0, 0.0, 0.0]
    buttons = [False] * 17
    cooldown_ok = (now - _last_press_time) >= PRESS_COOLDOWN_SEC

    if prompt_found and cooldown_ok:
        buttons[0] = True
        _last_press_time = now
        _total_presses += 1
        print(f"[boost_court] PRESS A — pads={len(targeting_pool)} prompt={prompt_px}px")
    elif target_x is not None:
        # Always push LS at FULL strength when we have a target.
        # Forward is strong (-1.0). X steering is strong based on offset.
        target_norm = (target_x - w / 2) / (w / 2)

        axes[1] = -1.0   # FULL forward, no half measures
        # Strong X push — at least 0.6 magnitude when target is off-center
        if abs(target_norm) > 0.05:
            sign = 1.0 if target_norm > 0 else -1.0
            magnitude = max(0.6, min(1.0, abs(target_norm) * 2.5))
            axes[0] = sign * magnitude

    # Apply axis inversion if your build flips LS
    if INVERT_LS_X: axes[0] = -axes[0]
    if INVERT_LS_Y: axes[1] = -axes[1]

    emit({"session_id": session_id, "axes": axes, "buttons": buttons})

    # ── Overlay ──
    # Faint ROI guide
    rx1 = int(w * ROI_LEFT);  ry1 = int(h * ROI_TOP)
    rx2 = int(w * ROI_RIGHT); ry2 = int(h * ROI_BOTTOM)
    cv2.rectangle(frame, (rx1, ry1), (rx2, ry2), (40, 80, 40), 1)

    # Draw all classified detections with labels (HSV + YOLO merged)
    draw_detections(frame, all_dets_for_draw, target_idx)

    # Big target crosshair
    if target_x is not None:
        cv2.circle(frame, (target_x, target_y), 32, CLR_PAD_TARGET, 3)
        cv2.line(frame, (target_x - 50, target_y), (target_x + 50, target_y), CLR_PAD_TARGET, 2)
        cv2.line(frame, (target_x, target_y - 50), (target_x, target_y + 50), CLR_PAD_TARGET, 2)
        cv2.line(frame, (w // 2, int(h * 0.88)), (target_x, target_y), (0, 200, 255), 2)

    # FPS
    _frame_count += 1
    elapsed = now - _fps_start
    if elapsed >= 1.0:
        _fps = _frame_count / elapsed
        _frame_count = 0
        _fps_start = now

    # Counts per category — total accepted from BOTH detection sources
    n_pads = sum(1 for d in all_dets_for_draw if d["accepted"])
    # Rejection counts from HSV only (YOLO doesn't produce reject categories)
    n_nameplates = sum(1 for d in hsv_dets if d.get("label", "").startswith("NAMEPLATE"))
    n_own = sum(1 for d in hsv_dets if d.get("label", "") == "OWN PLAYER")
    n_small = sum(1 for d in hsv_dets if d.get("label", "") == "TOO SMALL")

    # ── Legend / HUD ──
    cv2.rectangle(frame, (5, 5), (430, 195), (0, 0, 0), -1)
    cv2.rectangle(frame, (5, 5), (430, 195), (60, 60, 60), 1)
    if _pad_templates:
        mode = f"TEMPLATES({len(_pad_templates)})"
        mode_color = (0, 255, 0)
    elif _model_loaded:
        mode = "YOLO+HSV"
        mode_color = (0, 255, 0)
    else:
        mode = "HSV"
        mode_color = (0, 200, 255)
    cv2.putText(frame, f"BoostCourt [{mode}] | {_fps:.0f} fps", (12, 28),
                cv2.FONT_HERSHEY_SIMPLEX, 0.6, mode_color, 2)
    cv2.putText(frame, f"Pads: {n_pads}  Prompt: {prompt_px}px {'YES' if prompt_found else 'no'}",
                (12, 50), cv2.FONT_HERSHEY_SIMPLEX, 0.5, CLR_HUD, 1)
    cv2.putText(frame, f"Stick X={axes[0]:+.2f} Y={axes[1]:+.2f}  Presses: {_total_presses}",
                (12, 68), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (180, 220, 255), 1)
    lock_state = ("LOCK" if not is_ghost else "GHOST") if target_x is not None else "no target"
    cv2.putText(frame, f"Lock: {lock_state}  L:{_lock_frames}  G:{_ghost_frames}  RS:{axes[2]:+.2f}",
                (12, 86), cv2.FONT_HERSHEY_SIMPLEX, 0.42, (180, 220, 255), 1)

    # Color legend
    cv2.putText(frame, "LEGEND:", (12, 108),
                cv2.FONT_HERSHEY_SIMPLEX, 0.45, CLR_HUD, 1)
    legend_items = [
        (f"GOT NEXT PAD ({n_pads})",  CLR_PAD_TARGET, 124),
        (f"NAMEPLATE ({n_nameplates})", CLR_NAMEPLATE,    140),
        (f"OWN PLAYER ({n_own})",     CLR_OWN_PLAYER,   156),
        (f"TOO SMALL ({n_small})",    CLR_TOO_SMALL,    172),
    ]
    for txt, col, ypos in legend_items:
        cv2.rectangle(frame, (12, ypos - 10), (24, ypos), col, -1)
        cv2.putText(frame, txt, (30, ypos),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.42, col, 1)

    preview = cv2.resize(frame, (960, 540)) if w > 1280 else frame
    cv2.imshow("TM Labs - Boost Court", preview)
    cv2.waitKey(1)


def on_stop() -> None:
    print(f"[boost_court] stopped. Total presses: {_total_presses}")
