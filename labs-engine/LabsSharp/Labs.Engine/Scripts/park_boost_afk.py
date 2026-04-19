"""
park_boost_afk.py — In-game anti-AFK for 2v2 Park boosting.

Goal: keep the game registering input so you don't get kicked, without
      actively trying to help or hurt your team. Use this AFTER you've
      already joined a game (use boost_court_yolo.py to find/join pads).

Behavior:
  - Tiny stick wiggle every 2-3 seconds (looks like natural movement)
  - Occasional sprint tap (LT / button 6) so the game knows you're there
  - NO shooting, NO passing, NO stealing — pure idle presence
  - HUD shows time in game, total inputs sent

You can run this script once you're in a game. When the game ends and you
return to the park, switch back to boost_court_yolo.py to find another pad.
"""

import time
import random
import cv2
import numpy as np

DIRECT_CAPTURE = False  # Cloud play uses C# screencast

# ---------------------------------------------------------------------------
# Tunables
# ---------------------------------------------------------------------------

WIGGLE_INTERVAL_MIN = 2.0      # min seconds between stick wiggles
WIGGLE_INTERVAL_MAX = 3.5      # max seconds between stick wiggles
WIGGLE_DURATION = 0.3          # how long each wiggle holds
WIGGLE_MAGNITUDE = 0.4         # stick deflection (0.0-1.0)

SPRINT_INTERVAL = 8.0          # seconds between sprint taps
SPRINT_DURATION = 0.5          # hold sprint for this long

# ---------------------------------------------------------------------------
# State
# ---------------------------------------------------------------------------

_session_start = 0.0
_next_wiggle_at = 0.0
_wiggle_until = 0.0
_wiggle_dir = (0.0, 0.0)
_next_sprint_at = 0.0
_sprint_until = 0.0
_total_wiggles = 0
_total_sprints = 0
_frame_count = 0
_fps_start = 0.0
_fps = 0.0


def on_start(config: dict) -> None:
    global _session_start, _next_wiggle_at, _next_sprint_at
    global _total_wiggles, _total_sprints, _frame_count, _fps_start

    _session_start = time.monotonic()
    _next_wiggle_at = _session_start + 2.0
    _next_sprint_at = _session_start + 5.0
    _total_wiggles = 0
    _total_sprints = 0
    _frame_count = 0
    _fps_start = _session_start
    print(f"[park_boost_afk] started on session {config['session_id']}")
    print(f"[park_boost_afk] anti-AFK only — no shooting/stealing")


def _pick_wiggle_direction():
    """Random small stick deflection in any direction."""
    angle = random.uniform(0, 2 * 3.14159)
    return (
        WIGGLE_MAGNITUDE * np.cos(angle),
        WIGGLE_MAGNITUDE * np.sin(angle),
    )


def on_frame(frame, session_id: int, emit) -> None:
    global _next_wiggle_at, _wiggle_until, _wiggle_dir
    global _next_sprint_at, _sprint_until
    global _total_wiggles, _total_sprints
    global _frame_count, _fps_start, _fps

    now = time.monotonic()
    h, w = frame.shape[:2]

    axes = [0.0, 0.0, 0.0, 0.0]
    buttons = [False] * 17

    # ── Stick wiggle scheduling ──
    if now >= _next_wiggle_at and now >= _wiggle_until:
        _wiggle_dir = _pick_wiggle_direction()
        _wiggle_until = now + WIGGLE_DURATION
        _next_wiggle_at = now + random.uniform(WIGGLE_INTERVAL_MIN, WIGGLE_INTERVAL_MAX)
        _total_wiggles += 1

    if now < _wiggle_until:
        axes[0] = _wiggle_dir[0]   # LX
        axes[1] = _wiggle_dir[1]   # LY

    # ── Sprint tap scheduling ──
    if now >= _next_sprint_at and now >= _sprint_until:
        _sprint_until = now + SPRINT_DURATION
        _next_sprint_at = now + SPRINT_INTERVAL
        _total_sprints += 1

    if now < _sprint_until:
        buttons[6] = True   # LT (sprint in 2K)

    emit({"session_id": session_id, "axes": axes, "buttons": buttons})

    # ── HUD ──
    elapsed = now - _session_start
    mins = int(elapsed // 60)
    secs = int(elapsed % 60)

    _frame_count += 1
    fps_elapsed = now - _fps_start
    if fps_elapsed >= 1.0:
        _fps = _frame_count / fps_elapsed
        _frame_count = 0
        _fps_start = now

    cv2.rectangle(frame, (5, 5), (380, 110), (0, 0, 0), -1)
    cv2.rectangle(frame, (5, 5), (380, 110), (60, 60, 60), 1)
    cv2.putText(frame, f"Park Boost AFK | {_fps:.0f} fps", (12, 30),
                cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 200, 255), 2)
    cv2.putText(frame, f"Time in game: {mins:02d}:{secs:02d}", (12, 52),
                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
    cv2.putText(frame, f"Wiggles: {_total_wiggles}  Sprints: {_total_sprints}",
                (12, 72), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (180, 220, 255), 1)

    state = "WIGGLING" if now < _wiggle_until else ("SPRINTING" if now < _sprint_until else "idle")
    state_color = (0, 255, 0) if state != "idle" else (120, 120, 120)
    cv2.putText(frame, f"State: {state}  Stick=({axes[0]:+.2f},{axes[1]:+.2f})",
                (12, 92), cv2.FONT_HERSHEY_SIMPLEX, 0.45, state_color, 1)

    preview = cv2.resize(frame, (960, 540)) if w > 1280 else frame
    cv2.imshow("TM Labs - Park Boost AFK", preview)
    cv2.waitKey(1)


def on_stop() -> None:
    elapsed = time.monotonic() - _session_start
    print(f"[park_boost_afk] stopped after {elapsed:.0f}s")
    print(f"[park_boost_afk] sent {_total_wiggles} wiggles, {_total_sprints} sprints")
