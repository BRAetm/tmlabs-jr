"""
Labs Vision — NBA 2K Shot Meter Detection (2KVision)

Pure OpenCV implementation of Helios-style shot meter detection.
Supports all 7 meter types from Helios's nba2k_settings configuration:
  - Straight, Arrow, Arrow2, Pill, Sword, Dial, 2KOL2

Uses BGR color range filtering + contour detection with confidence decay
to track the moving meter and fire the shot button at the right moment.

Usage: Select this script in Labs Vision and start it on your Xbox Cloud session.
"""

import time
import cv2
import numpy as np
from helios_compat import *

# ---------------------------------------------------------------------------
# Meter type configurations (extracted from Helios nba2k_settings)
# ---------------------------------------------------------------------------

METERS = {
    "straight": {
        "name": "Straight",
        "width_range": (2, 4),
        "height_range": (43, 217),
        "colors": {
            "purple": {"low": [190, 0, 190], "high": [255, 60, 255]},
            "white": {"low": [235, 235, 235], "high": [255, 255, 255]},
        },
        "timing_ms": 50,
        "confidence_init": 1.0,
        "confidence_min": 0.95,
        "confidence_decay": 0.01,
        "threshold": 0,
    },
    "arrow2": {
        "name": "Arrow2",
        "width_range": (23, 30),
        "height_range": (33, 165),
        "colors": {
            "purple": {"low": [216, 46, 234], "high": [222, 52, 240]},
            "white": {"low": [240, 240, 240], "high": [255, 255, 255]},
        },
        "timing_ms": 48,
        "confidence_init": 1.0,
        "confidence_min": 0.95,
        "confidence_decay": 0.01,
        "threshold": 0,
    },
    "pill": {
        "name": "Pill",
        "width_range": (6, 9),
        "height_range": (32, 160),
        "colors": {
            "purple": {"low": [190, 0, 190], "high": [255, 60, 255]},
        },
        "timing_ms": 50,
        "confidence_init": 1.0,
        "confidence_min": 0.95,
        "confidence_decay": 0.01,
        "threshold": 0,
    },
    "sword": {
        "name": "Sword",
        "width_range": (19, 24),
        "height_range": (37, 186),
        "colors": {
            "purple": {"low": [220, 0, 220], "high": [255, 60, 255]},
        },
        "timing_ms": 50,
        "confidence_init": 1.0,
        "confidence_min": 0.95,
        "confidence_decay": 0.01,
        "threshold": 0,
    },
    "dial": {
        "name": "Dial",
        "width_range": (13, 53),
        "height_range": (13, 26),
        "colors": {
            "purple": {"low": [190, 0, 190], "high": [255, 60, 255]},
            "yellow": {"low": [0, 190, 190], "high": [60, 255, 255]},
        },
        "timing_ms": 40,
        "confidence_init": 1.0,
        "confidence_min": 0.70,
        "confidence_decay": 0.1,
        "threshold": 150,
    },
    "arrow": {
        "name": "Arrow",
        "width_range": (37, 45),
        "height_range": (18, 24),
        "colors": {
            "purple": {"low": [190, 0, 190], "high": [255, 60, 255]},
        },
        "timing_ms": 40,
        "confidence_init": 1.0,
        "confidence_min": 0.80,
        "confidence_decay": 0.1,
        "threshold": 0,
    },
    "2kol2": {
        "name": "2K OL2",
        "width_range": (2, 4),
        "height_range": (34, 249),
        "colors": {
            "white": {"low": [225, 225, 225], "high": [255, 255, 255]},
        },
        "timing_ms": 40,
        "confidence_init": 1.0,
        "confidence_min": 0.75,
        "confidence_decay": 0.03,
        "threshold": 100,
    },
}

# Search region within the frame (Helios default: game viewport center)
SEARCH_REGION = {
    "left": 5,
    "top": 250,
    "right": 1915,
    "bottom": 770,
}


# ---------------------------------------------------------------------------
# Shot Meter Detector
# ---------------------------------------------------------------------------

class MeterDetector:
    """Detects and tracks a single shot meter type using color-based contour detection."""

    def __init__(self, config: dict):
        self.config = config
        self.name = config["name"]
        self.confidence = 0.0
        self.last_pos = None       # (x, y, w, h) of last detection
        self.tracking = False
        self.detect_time = 0.0     # When meter was first detected
        self.positions = []        # Position history for velocity calc

    def detect(self, frame: np.ndarray, roi: np.ndarray) -> tuple:
        """
        Scans the ROI for this meter type.
        Returns (detected: bool, bbox: tuple or None, confidence: float)
        """
        w_min, w_max = self.config["width_range"]
        h_min, h_max = self.config["height_range"]

        best_match = None
        best_area = 0

        for color_name, color_range in self.config["colors"].items():
            low = np.array(color_range["low"], dtype=np.uint8)
            high = np.array(color_range["high"], dtype=np.uint8)

            # BGR color range mask
            mask = cv2.inRange(roi, low, high)

            # Apply threshold if configured
            thresh = self.config.get("threshold", 0)
            if thresh > 0:
                mask = cv2.threshold(mask, thresh, 255, cv2.THRESH_BINARY)[1]

            # Morphological cleanup
            kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (3, 3))
            mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)

            # Find contours
            contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

            for cnt in contours:
                x, y, w, h = cv2.boundingRect(cnt)

                # Check dimension constraints
                if w_min <= w <= w_max and h_min <= h <= h_max:
                    area = w * h
                    if area > best_area:
                        best_area = area
                        best_match = (x, y, w, h)

        if best_match is not None:
            if not self.tracking:
                self.tracking = True
                self.confidence = self.config["confidence_init"]
                self.detect_time = time.monotonic()
                self.positions = []

            self.last_pos = best_match
            self.positions.append((time.monotonic(), best_match))
            # Keep only last 10 positions for velocity
            self.positions = self.positions[-10:]
            return True, best_match, self.confidence

        # Not detected — decay confidence
        if self.tracking:
            self.confidence -= self.config["confidence_decay"]
            if self.confidence < self.config["confidence_min"]:
                self.tracking = False
                self.confidence = 0.0
                self.positions = []

        return False, self.last_pos, self.confidence

    def get_velocity(self) -> float:
        """Returns pixels/second velocity of the meter (vertical movement)."""
        if len(self.positions) < 2:
            return 0.0
        t1, p1 = self.positions[0]
        t2, p2 = self.positions[-1]
        dt = t2 - t1
        if dt < 0.001:
            return 0.0
        dy = abs(p2[1] - p1[1])
        return dy / dt

    def reset(self):
        self.confidence = 0.0
        self.last_pos = None
        self.tracking = False
        self.detect_time = 0.0
        self.positions = []


# ---------------------------------------------------------------------------
# GCVWorker — Main entry point (Helios API)
# ---------------------------------------------------------------------------

class GCVWorker:
    """NBA 2K shot meter detection and auto-release."""

    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.gcvdata = bytearray(32)
        self.frame_count = 0

        # Initialize all meter detectors
        self.detectors = {name: MeterDetector(cfg) for name, cfg in METERS.items()}

        # Active meter (auto-detected or user-selected)
        self.active_meter = None
        self.active_name = "auto"

        # Timing adjustment
        self.timing_offset_ms = 0
        self.shot_fired = False
        self.shot_cooldown = 0.0

        # Stats
        self.shots_taken = 0
        self.shots_hit = 0

    def process(self, frame):
        self.gcvdata = bytearray(32)
        h, w = frame.shape[:2]
        self.frame_count += 1

        # Clamp search region to frame size
        sx = max(0, min(SEARCH_REGION["left"], w))
        sy = max(0, min(SEARCH_REGION["top"], h))
        ex = max(0, min(SEARCH_REGION["right"], w))
        ey = max(0, min(SEARCH_REGION["bottom"], h))

        if ex <= sx or ey <= sy:
            # Frame too small for search region, use full frame
            roi = frame
            sx, sy = 0, 0
        else:
            roi = frame[sy:ey, sx:ex]

        # Detect meters
        active_detection = None
        active_detector = None

        if self.active_name == "auto":
            # Auto-detect: try all meter types
            for name, detector in self.detectors.items():
                detected, bbox, conf = detector.detect(frame, roi)
                if detected and conf > 0:
                    if active_detection is None or conf > active_detection[2]:
                        active_detection = (detected, bbox, conf)
                        active_detector = detector
                        self.active_meter = name
        else:
            # Single meter mode
            detector = self.detectors.get(self.active_name)
            if detector:
                detected, bbox, conf = detector.detect(frame, roi)
                if detected:
                    active_detection = (detected, bbox, conf)
                    active_detector = detector

        # Process shot timing
        now = time.monotonic()

        if active_detection and active_detector and active_detector.tracking:
            detected, bbox, conf = active_detection

            if bbox:
                # Draw detection overlay
                abs_x = sx + bbox[0]
                abs_y = sy + bbox[1]
                cv2.rectangle(frame,
                              (abs_x, abs_y),
                              (abs_x + bbox[2], abs_y + bbox[3]),
                              (0, 255, 0), 2)

                # Confidence bar
                bar_w = int(conf * 100)
                cv2.rectangle(frame, (abs_x, abs_y - 15),
                              (abs_x + bar_w, abs_y - 5),
                              (0, 255, 0), -1)

                # Meter name
                cv2.putText(frame, f"{active_detector.name} ({conf:.2f})",
                            (abs_x, abs_y - 20),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)

            # Check if we should fire the shot
            timing_ms = active_detector.config["timing_ms"] + self.timing_offset_ms
            elapsed_ms = (now - active_detector.detect_time) * 1000

            if not self.shot_fired and elapsed_ms >= timing_ms and now > self.shot_cooldown:
                # FIRE! Press X button (shot release in 2K)
                self.shot_fired = True
                self.shot_cooldown = now + 0.5  # 500ms cooldown
                self.shots_taken += 1
                # Set button in gcvdata (X button = BUTTON_16 = index 22 in gcvdata)
                self.gcvdata[22] = 100
                cv2.putText(frame, "SHOT!", (w // 2 - 40, h // 2),
                            cv2.FONT_HERSHEY_SIMPLEX, 1.5, (0, 0, 255), 3)

        else:
            self.shot_fired = False

        # Draw HUD
        self._draw_hud(frame, w, h, active_detector)

        return frame, self.gcvdata

    def _draw_hud(self, frame, w, h, active_detector):
        """Draw heads-up display overlay."""
        # Background panel
        cv2.rectangle(frame, (5, 5), (320, 95), (0, 0, 0), -1)
        cv2.rectangle(frame, (5, 5), (320, 95), (80, 80, 80), 1)

        y = 22
        cv2.putText(frame, f"2KVision | Frame {self.frame_count}",
                    (12, y), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (200, 200, 200), 1)
        y += 18

        meter_name = self.active_meter or "scanning..."
        cv2.putText(frame, f"Meter: {meter_name}",
                    (12, y), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (100, 255, 100), 1)
        y += 18

        if active_detector and active_detector.tracking:
            vel = active_detector.get_velocity()
            cv2.putText(frame, f"Speed: {vel:.0f}px/s  Conf: {active_detector.confidence:.2f}",
                        (12, y), cv2.FONT_HERSHEY_SIMPLEX, 0.35, (200, 200, 100), 1)
        else:
            cv2.putText(frame, "Speed: -- Conf: --",
                        (12, y), cv2.FONT_HERSHEY_SIMPLEX, 0.35, (120, 120, 120), 1)
        y += 18

        timing = (active_detector.config["timing_ms"] + self.timing_offset_ms) if active_detector else 0
        cv2.putText(frame, f"Timing: {timing}ms  Offset: {self.timing_offset_ms:+d}ms  Shots: {self.shots_taken}",
                    (12, y), cv2.FONT_HERSHEY_SIMPLEX, 0.35, (180, 180, 220), 1)
