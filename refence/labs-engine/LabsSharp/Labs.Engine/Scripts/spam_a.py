"""
Labs Vision — spam_a.py

Spams the A button by alternating press/release every frame.
Web Gamepad API button index 0 = A button.

Sends annotated CV frames back to the UI showing what it sees.
"""

import cv2
import numpy as np

_frame_counter = 0


def on_start(config: dict) -> None:
    """Called once when the worker starts."""
    global _frame_counter
    _frame_counter = 0
    print(f"[spam_a] started on session {config['session_id']}")


def on_frame(frame: np.ndarray, session_id: int, emit) -> None:
    """Alternate A button press/release every frame, with CV overlay."""
    global _frame_counter

    pressed = _frame_counter % 2 == 0
    buttons = [False] * 17
    if pressed:
        buttons[0] = True  # A button pressed on even frames

    emit({
        "session_id": session_id,
        "axes": [0.0, 0.0, 0.0, 0.0],
        "buttons": buttons,
    })

    # Draw CV overlay on the frame to show what the script sees
    overlay = frame.copy()
    h, w = overlay.shape[:2]

    # Draw frame counter
    cv2.putText(overlay, f"Frame #{_frame_counter}", (10, 30),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

    # Show button state
    color = (0, 255, 0) if pressed else (0, 0, 255)
    label = "A: PRESSED" if pressed else "A: released"
    cv2.putText(overlay, label, (10, 60),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, color, 2)

    # Draw a detection region box (center of screen)
    cx, cy = w // 2, h // 2
    box_size = min(w, h) // 4
    cv2.rectangle(overlay, (cx - box_size, cy - box_size),
                  (cx + box_size, cy + box_size), (0, 255, 255), 2)
    cv2.putText(overlay, "ROI", (cx - box_size + 4, cy - box_size + 20),
                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1)

    # Send annotated frame back to UI (every other frame to save bandwidth)
    if _frame_counter % 2 == 0 and hasattr(emit, 'cv_frame'):
        emit.cv_frame(overlay)

    _frame_counter += 1


def on_stop() -> None:
    """Called once when the worker shuts down."""
    print("[spam_a] stopped")
