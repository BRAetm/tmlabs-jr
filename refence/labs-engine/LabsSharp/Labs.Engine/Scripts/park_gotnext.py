"""
Labs Vision — park_gotnext.py

Detects blue "Got Next" spots on 2K Park/City courts and
auto-walks the player toward the nearest one.

How it works:
  1. Scans for bright blue/cyan glowing spots (the lit-up dots)
  2. Finds the largest/nearest blue spot
  3. Calculates direction from player (assumed center of screen) to spot
  4. Pushes left stick toward the spot
  5. Stops when close enough or no spot found

Tuning:
  - Adjust SPOT_LOWER/SPOT_UPPER if the spots aren't being detected
  - Adjust ARRIVAL_THRESHOLD to control how close before stopping
  - The player is assumed to be at screen center — adjust PLAYER_Y_RATIO if needed
"""

import cv2
import numpy as np
import time

# ---------------------------------------------------------------------------
# Got Next spot detection (HSV range for the teal/cyan diamond markers)
# Measured from actual 2K screenshots:
#   H: 50-90 (teal/cyan-green range)
#   S: 60-210 (moderately to highly saturated)
#   V: 120-255 (bright glowing)
# ---------------------------------------------------------------------------
SPOT_LOWER = np.array([45, 55, 120])
SPOT_UPPER = np.array([95, 255, 255])

# Minimum area for a spot to count (filters small noise)
MIN_SPOT_AREA = 150
MAX_SPOT_AREA = 20000

# Player position — assumed to be roughly center-bottom of screen
PLAYER_X_RATIO = 0.5    # horizontal center
PLAYER_Y_RATIO = 0.55   # slightly below center (player feet)

# How close (in pixels, normalized) before we stop walking
ARRIVAL_THRESHOLD = 0.05  # 5% of screen width

# Stick sensitivity — how hard to push the stick (0.0 to 1.0)
STICK_STRENGTH = 0.8

# Dead zone — ignore spots too close to screen edges (UI elements)
EDGE_MARGIN = 0.08  # 8% from each edge

# ---------------------------------------------------------------------------
# State
# ---------------------------------------------------------------------------
_frame_count = 0
_fps_start = 0.0
_fps = 0.0
_state = "SCANNING"  # SCANNING, WALKING, ARRIVED
_target = None        # (x, y) normalized coords of target spot
_walk_start = 0.0
_max_walk_time = 8.0  # seconds before giving up on current target


def on_start(config: dict) -> None:
    global _frame_count, _fps_start, _state, _target
    _frame_count = 0
    _fps_start = time.monotonic()
    _state = "SCANNING"
    _target = None
    print(f"[park_gotnext] started on session {config['session_id']}")
    print(f"[park_gotnext] Spot HSV: {SPOT_LOWER} - {SPOT_UPPER}")
    print(f"[park_gotnext] Looking for teal Got Next diamond spots...")


def find_blue_spots(frame: np.ndarray) -> list:
    """Find blue glowing spots and return list of (cx, cy, area) in pixel coords."""
    h, w = frame.shape[:2]

    # Crop out edges (skip UI)
    margin_x = int(w * EDGE_MARGIN)
    margin_y = int(h * EDGE_MARGIN)
    roi = frame[margin_y:h - margin_y, margin_x:w - margin_x]

    hsv = cv2.cvtColor(roi, cv2.COLOR_BGR2HSV)
    mask = cv2.inRange(hsv, SPOT_LOWER, SPOT_UPPER)

    # The spots glow brighter than the court — filter out dim green court pixels
    # by requiring high brightness (V channel)
    bright_mask = hsv[:, :, 2] > 140
    mask = cv2.bitwise_and(mask, mask, mask=bright_mask.astype(np.uint8) * 255)

    # Clean up noise
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7))
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel, iterations=2)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel, iterations=1)

    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    spots = []
    for cnt in contours:
        area = cv2.contourArea(cnt)
        if MIN_SPOT_AREA < area < MAX_SPOT_AREA:
            M = cv2.moments(cnt)
            if M["m00"] > 0:
                cx = int(M["m10"] / M["m00"]) + margin_x
                cy = int(M["m01"] / M["m00"]) + margin_y
                spots.append((cx, cy, area))

    # Sort by area descending (biggest/closest first)
    spots.sort(key=lambda s: s[2], reverse=True)
    return spots


def on_frame(frame: np.ndarray, session_id: int, emit) -> None:
    global _frame_count, _fps_start, _fps, _state, _target, _walk_start

    h, w = frame.shape[:2]
    _frame_count += 1

    # FPS
    elapsed = time.monotonic() - _fps_start
    if elapsed >= 1.0:
        _fps = _frame_count / elapsed
        _frame_count = 0
        _fps_start = time.monotonic()

    # Player position (screen center)
    player_x = w * PLAYER_X_RATIO
    player_y = h * PLAYER_Y_RATIO

    # Find blue spots
    spots = find_blue_spots(frame)

    # Build overlay
    overlay = frame.copy()

    # Draw player position crosshair
    px, py = int(player_x), int(player_y)
    cv2.drawMarker(overlay, (px, py), (0, 255, 0), cv2.MARKER_CROSS, 20, 2)
    cv2.putText(overlay, "YOU", (px + 12, py - 5),
                cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 255, 0), 1)

    # Draw detected spots
    for i, (sx, sy, area) in enumerate(spots):
        color = (255, 255, 0) if i == 0 else (100, 100, 255)
        cv2.circle(overlay, (sx, sy), int(np.sqrt(area / np.pi)), color, 2)
        cv2.putText(overlay, f"SPOT {i+1}", (sx + 8, sy - 5),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.4, color, 1)

    # State machine
    axes = [0.0, 0.0, 0.0, 0.0]
    buttons = [False] * 17

    if _state == "SCANNING":
        if spots:
            # Lock onto the biggest/closest spot
            sx, sy, _ = spots[0]
            _target = (sx / w, sy / h)  # normalize
            _state = "WALKING"
            _walk_start = time.monotonic()
            print(f"[park_gotnext] Found spot at ({_target[0]:.2f}, {_target[1]:.2f}), walking...")

    elif _state == "WALKING":
        if _target is None:
            _state = "SCANNING"
        else:
            # Check timeout
            if time.monotonic() - _walk_start > _max_walk_time:
                print(f"[park_gotnext] Walk timeout, re-scanning...")
                _state = "SCANNING"
                _target = None
            else:
                # Update target if we still see spots (track the moving target)
                if spots:
                    sx, sy, _ = spots[0]
                    _target = (sx / w, sy / h)

                # Calculate direction from player to target
                dx = _target[0] - PLAYER_X_RATIO
                dy = _target[1] - PLAYER_Y_RATIO

                distance = np.sqrt(dx * dx + dy * dy)

                if distance < ARRIVAL_THRESHOLD:
                    _state = "ARRIVED"
                    print(f"[park_gotnext] Arrived at spot!")
                else:
                    # Normalize and apply stick
                    if distance > 0:
                        stick_x = (dx / distance) * STICK_STRENGTH
                        stick_y = (dy / distance) * STICK_STRENGTH
                        axes[0] = float(np.clip(stick_x, -1, 1))
                        axes[1] = float(np.clip(stick_y, -1, 1))

                    # Draw line from player to target
                    tx, ty = int(_target[0] * w), int(_target[1] * h)
                    cv2.arrowedLine(overlay, (px, py), (tx, ty), (0, 255, 255), 2, tipLength=0.15)
                    cv2.putText(overlay, f"DIST: {distance:.3f}", (px + 12, py + 20),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 255, 255), 1)

    elif _state == "ARRIVED":
        # We're on the spot — stop moving, re-scan after a bit
        if not spots:
            # Spot disappeared (maybe game started or we moved off)
            _state = "SCANNING"
            _target = None
            print(f"[park_gotnext] Spot gone, re-scanning...")

    # HUD
    state_colors = {"SCANNING": (100, 100, 255), "WALKING": (0, 255, 255), "ARRIVED": (0, 255, 0)}
    cv2.putText(overlay, f"Park Bot | {_fps:.0f} fps | {_state}", (10, 25),
                cv2.FONT_HERSHEY_SIMPLEX, 0.6, state_colors.get(_state, (200, 200, 200)), 2)
    cv2.putText(overlay, f"Spots: {len(spots)} | Stick: ({axes[0]:.2f}, {axes[1]:.2f})",
                (10, 50), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (180, 180, 180), 1)

    # Emit gamepad state
    emit({
        "session_id": session_id,
        "axes": axes,
        "buttons": buttons,
    })

    # Send annotated frame to CV preview
    if hasattr(emit, 'cv_frame'):
        emit.cv_frame(overlay)


def on_stop() -> None:
    print(f"[park_gotnext] stopped. Last state: {_state}")
