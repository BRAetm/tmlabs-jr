"""
Labs Vision — cv_test_overlay.py

Visual-only test script. Shows an FPS counter, frame dimensions,
color histogram, and edge detection overlay to confirm the CV
pipeline is working. No controller input needed.
"""

import cv2
import numpy as np
import time

_frame_count = 0
_fps_start = 0.0
_fps = 0.0
_total_frames = 0


def on_start(config: dict) -> None:
    global _frame_count, _fps_start, _total_frames
    _frame_count = 0
    _total_frames = 0
    _fps_start = time.monotonic()
    print(f"[cv_test] started on session {config['session_id']}")


def on_frame(frame: np.ndarray, session_id: int, emit) -> None:
    global _frame_count, _fps_start, _fps, _total_frames

    _total_frames += 1
    _frame_count += 1
    h, w = frame.shape[:2]

    # Calculate FPS
    elapsed = time.monotonic() - _fps_start
    if elapsed >= 1.0:
        _fps = _frame_count / elapsed
        _frame_count = 0
        _fps_start = time.monotonic()

    # Create overlay
    overlay = frame.copy()

    # Semi-transparent dark bar at top for HUD
    bar = overlay[0:80, :].copy()
    cv2.rectangle(overlay, (0, 0), (w, 80), (0, 0, 0), -1)
    cv2.addWeighted(bar, 0.3, overlay[0:80, :], 0.7, 0, overlay[0:80, :])

    # FPS counter (large, green)
    cv2.putText(overlay, f"{_fps:.1f} FPS", (15, 35),
                cv2.FONT_HERSHEY_SIMPLEX, 1.0, (0, 255, 0), 2)

    # Frame info
    cv2.putText(overlay, f"{w}x{h} | Frame #{_total_frames}", (15, 65),
                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (180, 180, 180), 1)

    # CV ACTIVE badge (top right)
    badge_text = "CV ACTIVE"
    cv2.rectangle(overlay, (w - 140, 8), (w - 10, 38), (0, 180, 0), -1)
    cv2.putText(overlay, badge_text, (w - 135, 30),
                cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)

    # Edge detection on a center region to show CV is processing
    cx, cy = w // 2, h // 2
    roi_size = min(w, h) // 3
    rx1 = cx - roi_size
    ry1 = cy - roi_size
    rx2 = cx + roi_size
    ry2 = cy + roi_size

    roi = frame[ry1:ry2, rx1:rx2]
    gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
    edges = cv2.Canny(gray, 50, 150)

    # Draw edges in green on the overlay
    edges_bgr = np.zeros_like(roi)
    edges_bgr[:, :, 1] = edges  # green channel
    overlay[ry1:ry2, rx1:rx2] = cv2.addWeighted(
        overlay[ry1:ry2, rx1:rx2], 0.7, edges_bgr, 0.5, 0)

    # Draw ROI box
    cv2.rectangle(overlay, (rx1, ry1), (rx2, ry2), (0, 255, 255), 2)
    cv2.putText(overlay, "Edge Detection ROI", (rx1 + 5, ry1 + 20),
                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1)

    # Color histogram bars (bottom left)
    hist_h = 60
    hist_w = 150
    hist_x = 10
    hist_y = h - hist_h - 10
    cv2.rectangle(overlay, (hist_x - 2, hist_y - 2),
                  (hist_x + hist_w + 2, hist_y + hist_h + 2), (40, 40, 40), -1)

    for i, (color, label) in enumerate([(2, "R"), (1, "G"), (0, "B")]):
        hist = cv2.calcHist([frame], [color], None, [32], [0, 256])
        hist = hist.flatten()
        max_val = hist.max() if hist.max() > 0 else 1
        hist = (hist / max_val * hist_h).astype(int)
        bar_w = hist_w // 32
        draw_color = [(0, 0, 200), (0, 200, 0), (200, 0, 0)][i]
        for j, val in enumerate(hist):
            x1 = hist_x + j * bar_w
            y1 = hist_y + hist_h - val
            y2 = hist_y + hist_h
            cv2.rectangle(overlay, (x1, y1), (x1 + bar_w - 1, y2), draw_color, -1)

    cv2.putText(overlay, "RGB Histogram", (hist_x, hist_y - 5),
                cv2.FONT_HERSHEY_SIMPLEX, 0.4, (150, 150, 150), 1)

    # Timestamp (bottom right)
    ts = time.strftime("%H:%M:%S")
    cv2.putText(overlay, ts, (w - 100, h - 15),
                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (100, 100, 100), 1)

    # Send annotated frame back to UI
    if hasattr(emit, 'cv_frame'):
        emit.cv_frame(overlay)


def on_stop() -> None:
    print(f"[cv_test] stopped after {_total_frames} total frames")
