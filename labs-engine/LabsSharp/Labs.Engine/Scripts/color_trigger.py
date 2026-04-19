"""
Labs Vision — Color Trigger Script

Detects a specific color on screen and presses a button when found.
Configure the color range and button below, or use the CV Builder
to generate a customized version.

Good for: menu navigation, prompt detection, UI element triggers.
"""

import cv2
from cv_utils import ColorDetector, FrameDrawer

# --- Configuration ---
# Detect bright green (common for "ready" / "go" indicators)
LOWER_BGR = (0, 180, 0)
UPPER_BGR = (80, 255, 80)
MIN_AREA = 200
BUTTON_INDEX = 0  # A button (Web Gamepad index)
# ---------------------

detector = ColorDetector(LOWER_BGR, UPPER_BGR, min_area=MIN_AREA)
drawer = FrameDrawer()


def on_start(config):
    print(f"[color_trigger] Started on session {config['session_id']}")
    print(f"[color_trigger] Detecting BGR [{LOWER_BGR}]-[{UPPER_BGR}], pressing button {BUTTON_INDEX}")


def on_frame(frame, session_id, emit):
    detections = detector.detect(frame)
    buttons = [False] * 17
    axes = [0.0, 0.0, 0.0, 0.0]

    if detections:
        # Draw detection overlay
        for x, y, w, h, area in detections[:3]:
            drawer.draw_bbox(frame, x, y, w, h, (0, 255, 0), 2, f"area={area}")

        # Press button when color is detected
        buttons[BUTTON_INDEX] = True

    # Draw HUD
    status = f"Detected: {len(detections)}" if detections else "Scanning..."
    color = (0, 255, 0) if detections else (100, 100, 100)
    drawer.draw_hud_panel(frame, 5, 5, [
        ("Color Trigger", (200, 200, 200)),
        (status, color),
        (f"Button: {'PRESSED' if buttons[BUTTON_INDEX] else 'idle'}", (200, 200, 100)),
    ])

    emit({"axes": axes, "buttons": buttons})


def on_stop():
    print("[color_trigger] Stopped")
