"""
Labs Vision — nba2k_players.py

Detects and highlights NBA 2K players on the court using color-based
segmentation. Works with cloud gaming streams.

Uses DIRECT_CAPTURE for 30-60+ FPS (bypasses slow ZMQ JPEG pipeline).
Shows CV overlay in its own OpenCV window for zero-latency feedback.
"""

import cv2
import numpy as np
import time

# Uses standard ZMQ pipeline — C# screencast streams game frames at 30+ FPS
# No direct capture or shared memory needed

# ---------------------------------------------------------------------------
# Team color presets (HSV ranges)
# Adjust these to match the jerseys in your current game.
# Use the CV Builder or a color picker to find exact HSV values.
# ---------------------------------------------------------------------------

# Home team (white/light jerseys) — detect bright, low-saturation areas
HOME_LOWER = np.array([0, 0, 180])
HOME_UPPER = np.array([179, 60, 255])

# Away team (colored jerseys) — example: blue/dark jerseys
# Adjust hue range for the specific team color
AWAY_LOWER = np.array([100, 80, 80])
AWAY_UPPER = np.array([130, 255, 255])

# Court color (hardwood tan/brown) — used to isolate players from floor
COURT_LOWER = np.array([10, 40, 100])
COURT_UPPER = np.array([25, 180, 220])

# Minimum contour area to count as a player (filters noise)
MIN_PLAYER_AREA = 800
MAX_PLAYER_AREA = 25000

# Region of interest — focus on the court area (skip UI elements)
# Values are ratios of frame dimensions (0.0 to 1.0)
ROI_TOP = 0.08      # skip top scoreboard
ROI_BOTTOM = 0.92   # skip bottom UI
ROI_LEFT = 0.02
ROI_RIGHT = 0.98

# ---------------------------------------------------------------------------
# State
# ---------------------------------------------------------------------------

_frame_count = 0
_fps_start = 0.0
_fps = 0.0
_home_count = 0
_away_count = 0
_window_created = False


def on_start(config: dict) -> None:
    global _frame_count, _fps_start, _window_created
    _frame_count = 0
    _fps_start = time.monotonic()
    _window_created = False
    print(f"[nba2k_players] started on session {config['session_id']}")
    print(f"[nba2k_players] Home HSV: {HOME_LOWER} - {HOME_UPPER}")
    print(f"[nba2k_players] Away HSV: {AWAY_LOWER} - {AWAY_UPPER}")


def detect_players(frame: np.ndarray, lower: np.ndarray, upper: np.ndarray,
                   label: str, color: tuple) -> list:
    """Detect players by jersey color and return list of (x, y, w, h, label, color)."""
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    mask = cv2.inRange(hsv, lower, upper)

    # Remove court-colored pixels to avoid false positives
    court_mask = cv2.inRange(hsv, COURT_LOWER, COURT_UPPER)
    mask = cv2.bitwise_and(mask, cv2.bitwise_not(court_mask))

    # Morphological cleanup — remove noise, fill gaps
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel, iterations=2)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel, iterations=1)

    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    players = []
    for cnt in contours:
        area = cv2.contourArea(cnt)
        if MIN_PLAYER_AREA < area < MAX_PLAYER_AREA:
            x, y, w, h = cv2.boundingRect(cnt)
            # Players are taller than wide (aspect ratio filter)
            aspect = h / max(w, 1)
            if 0.8 < aspect < 4.0:
                players.append((x, y, w, h, label, color))

    return players


def on_frame(frame: np.ndarray, session_id: int, emit) -> None:
    global _frame_count, _fps_start, _fps, _home_count, _away_count

    h, w = frame.shape[:2]

    # Extract ROI (skip in-game UI elements like scoreboard)
    y1 = int(h * ROI_TOP)
    y2 = int(h * ROI_BOTTOM)
    x1 = int(w * ROI_LEFT)
    x2 = int(w * ROI_RIGHT)
    roi = frame[y1:y2, x1:x2]

    # Detect players for both teams
    home_players = detect_players(roi, HOME_LOWER, HOME_UPPER, "HOME", (255, 255, 255))
    away_players = detect_players(roi, AWAY_LOWER, AWAY_UPPER, "AWAY", (255, 100, 0))

    _home_count = len(home_players)
    _away_count = len(away_players)

    # Draw directly on frame (no .copy() — saves memory + time)
    # Draw ROI boundary
    cv2.rectangle(frame, (x1, y1), (x2, y2), (40, 40, 40), 1)

    # Draw player bounding boxes
    for (px, py, pw, ph, label, color) in home_players + away_players:
        fx, fy = px + x1, py + y1
        cv2.rectangle(frame, (fx, fy), (fx + pw, fy + ph), color, 2)
        cv2.rectangle(frame, (fx, fy - 18), (fx + len(label) * 10 + 8, fy), color, -1)
        cv2.putText(frame, label, (fx + 4, fy - 4),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 0, 0), 1)
        cx, cy = fx + pw // 2, fy + ph // 2
        cv2.circle(frame, (cx, cy), 3, color, -1)

    # FPS counter
    _frame_count += 1
    elapsed = time.monotonic() - _fps_start
    if elapsed >= 1.0:
        _fps = _frame_count / elapsed
        _frame_count = 0
        _fps_start = time.monotonic()

    # HUD
    cv2.putText(frame, f"NBA 2K Vision | {_fps:.0f} fps", (10, 25),
                cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)
    cv2.putText(frame, f"Home: {_home_count}  Away: {_away_count}  Total: {_home_count + _away_count}",
                (10, 50), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (200, 200, 200), 1)

    # Show in local OpenCV window (instant, no JPEG round-trip)
    global _window_created
    if not _window_created:
        cv2.namedWindow("TM Labs - 2K Vision", cv2.WINDOW_NORMAL)
        cv2.resizeWindow("TM Labs - 2K Vision", 960, 540)
        _window_created = True

    preview = cv2.resize(frame, (960, 540)) if w > 1280 else frame
    cv2.imshow("TM Labs - 2K Vision", preview)
    cv2.waitKey(1)


def on_stop() -> None:
    print(f"[nba2k_players] stopped. Last count: home={_home_count}, away={_away_count}")
