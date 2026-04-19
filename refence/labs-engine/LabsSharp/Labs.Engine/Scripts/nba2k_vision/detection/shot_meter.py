"""
Shot meter detection — green zone, shot readiness, meter tracking.
Ported from KAI_MCAssistant: getGreenLab, getGs, getCenterCirle.
"""

import cv2
import numpy as np

# HSV ranges from Helios
GREEN_ZONE_LOW = np.array([35, 100, 100])
GREEN_ZONE_HIGH = np.array([85, 255, 255])

WHITE_UI_LOW = np.array([0, 0, 200])
WHITE_UI_HIGH = np.array([180, 30, 255])

ORANGE_METER_LOW = np.array([10, 150, 150])
ORANGE_METER_HIGH = np.array([25, 255, 255])


class ShotMeterDetector:
    """Detects the shot meter, green zone, and shot readiness."""

    def __init__(self, config=None):
        cfg = config or {}
        self.green_min_area = cfg.get("green_zone_threshold", 50)
        self.circularity_threshold = cfg.get("circularity_threshold", 0.7)
        self.morph_kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (5, 5))

        # State tracking
        self.green_detected = False
        self.meter_position = None  # (x, y, w, h) of meter bar
        self.shot_ready = False
        self.center_circle = None  # (cx, cy, radius)

    def detect_green_zone(self, frame, roi=None):
        """
        Detect the green release window on the shot meter.
        HSV green [35,100,100]-[85,255,255], contour area > threshold.

        Returns: (found: bool, contours: list, largest_center: tuple or None)
        """
        crop = frame if roi is None else self._crop(frame, roi)
        hsv = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV)
        mask = cv2.inRange(hsv, GREEN_ZONE_LOW, GREEN_ZONE_HIGH)
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        valid = [c for c in contours if cv2.contourArea(c) > self.green_min_area]
        if not valid:
            self.green_detected = False
            return False, [], None

        self.green_detected = True
        largest = max(valid, key=cv2.contourArea)
        M = cv2.moments(largest)
        if M["m00"] > 0:
            cx = int(M["m10"] / M["m00"])
            cy = int(M["m01"] / M["m00"])
            if roi is not None:
                h, w = frame.shape[:2]
                cx += int(roi[0] * w)
                cy += int(roi[1] * h)
            return True, valid, (cx, cy)
        return True, valid, None

    def detect_shot_ready(self, frame, roi=None):
        """
        Detect if the shot is ready (white circle/gauge indicator).
        Uses circularity check (4*pi*area / perimeter^2 > 0.7).

        Returns: (ready: bool, shape: str 'circle'|'square'|None)
        """
        crop = frame if roi is None else self._crop(frame, roi)
        hsv = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV)
        mask = cv2.inRange(hsv, WHITE_UI_LOW, WHITE_UI_HIGH)
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        for c in contours:
            area = cv2.contourArea(c)
            if area < 100:
                continue
            perimeter = cv2.arcLength(c, True)
            if perimeter == 0:
                continue
            circularity = 4 * np.pi * area / (perimeter * perimeter)
            if circularity > self.circularity_threshold:
                self.shot_ready = True
                return True, "circle"

        # Check for square/rectangle
        for c in contours:
            if cv2.contourArea(c) > 100:
                self.shot_ready = True
                return True, "square"

        self.shot_ready = False
        return False, None

    def detect_center_circle(self, frame):
        """
        Detect the center court circle (largest white circular contour).
        Uses morphological open/close for noise reduction.

        Returns: (cx, cy, radius) or None
        """
        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
        mask = cv2.inRange(hsv, WHITE_UI_LOW, WHITE_UI_HIGH)

        # Morphological cleanup
        mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, self.morph_kernel)
        mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, self.morph_kernel)

        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        if not contours:
            self.center_circle = None
            return None

        # Find largest circular contour
        best = None
        best_area = 0
        for c in contours:
            area = cv2.contourArea(c)
            if area < 200:
                continue
            perimeter = cv2.arcLength(c, True)
            if perimeter == 0:
                continue
            circularity = 4 * np.pi * area / (perimeter * perimeter)
            if circularity > 0.5 and area > best_area:
                best = c
                best_area = area

        if best is not None:
            center, radius = cv2.minEnclosingCircle(best)
            self.center_circle = (int(center[0]), int(center[1]), int(radius))
            return self.center_circle
        self.center_circle = None
        return None

    def track_meter_position(self, frame, roi=None):
        """
        Track the shot meter bar position (orange/white bar).
        Returns: (x, y, w, h) of meter bounding box or None
        """
        crop = frame if roi is None else self._crop(frame, roi)
        hsv = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV)

        # Look for orange meter
        mask = cv2.inRange(hsv, ORANGE_METER_LOW, ORANGE_METER_HIGH)
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        if not contours:
            # Fallback: white meter bar
            mask = cv2.inRange(hsv, WHITE_UI_LOW, WHITE_UI_HIGH)
            contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        if contours:
            # Look for tall, narrow contour (meter bar shape)
            for c in sorted(contours, key=cv2.contourArea, reverse=True):
                x, y, w, h = cv2.boundingRect(c)
                aspect = h / max(w, 1)
                if aspect > 3 and h > 20:  # Tall and narrow = meter bar
                    if roi is not None:
                        fh, fw = frame.shape[:2]
                        x += int(roi[0] * fw)
                        y += int(roi[1] * fh)
                    self.meter_position = (x, y, w, h)
                    return self.meter_position

        self.meter_position = None
        return None

    @staticmethod
    def _crop(frame, roi):
        """Crop frame using normalized ROI [x, y, w, h] in 0-1 range."""
        h, w = frame.shape[:2]
        x1 = max(0, int(roi[0] * w))
        y1 = max(0, int(roi[1] * h))
        x2 = min(w, int((roi[0] + roi[2]) * w))
        y2 = min(h, int((roi[1] + roi[3]) * h))
        return frame[y1:y2, x1:x2]
