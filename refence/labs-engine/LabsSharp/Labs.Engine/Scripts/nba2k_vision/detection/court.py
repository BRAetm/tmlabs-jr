"""
Court detection — ball tracking, court position, path detection.
Ported from KAI_MCAssistant: getBall2, getPathTime, getPathNum.
"""

import cv2
import numpy as np

# HSV color ranges from Helios
BALL_ORANGE_LOW = np.array([5, 100, 100])
BALL_ORANGE_HIGH = np.array([25, 255, 255])

PATH_RED_LOW = np.array([0, 100, 100])
PATH_RED_HIGH = np.array([10, 255, 255])
PATH_BLUE_LOW = np.array([100, 100, 100])
PATH_BLUE_HIGH = np.array([130, 255, 255])
PATH_BLACK_LOW = np.array([0, 0, 0])
PATH_BLACK_HIGH = np.array([180, 255, 50])
PATH_WHITE_LOW = np.array([0, 0, 200])
PATH_WHITE_HIGH = np.array([180, 30, 255])


class CourtDetector:
    """Detects court elements — ball position, court lines, path indicators."""

    def __init__(self, config=None):
        cfg = config or {}
        self.ball_min_area = cfg.get("ball_min_area", 50)
        self.ball_circularity = cfg.get("ball_circularity", 0.6)

        # State
        self.ball_found = False
        self.ball_position = None  # (cx, cy)
        self.path_position = None  # (x, y)

    def detect_ball(self, frame, roi=None):
        """
        Detect the basketball using orange HSV range and circularity.
        Returns: (found: bool, center_x: int, center_y: int)
        """
        crop = frame if roi is None else _crop(frame, roi)
        hsv = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV)
        mask = cv2.inRange(hsv, BALL_ORANGE_LOW, BALL_ORANGE_HIGH)
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        for c in sorted(contours, key=cv2.contourArea, reverse=True):
            area = cv2.contourArea(c)
            if area < self.ball_min_area:
                continue
            perimeter = cv2.arcLength(c, True)
            if perimeter == 0:
                continue
            circularity = 4 * np.pi * area / (perimeter * perimeter)
            if circularity > self.ball_circularity:
                M = cv2.moments(c)
                if M["m00"] > 0:
                    cx = int(M["m10"] / M["m00"])
                    cy = int(M["m01"] / M["m00"])
                    if roi is not None:
                        h, w = frame.shape[:2]
                        cx += int(roi[0] * w)
                        cy += int(roi[1] * h)
                    self.ball_found = True
                    self.ball_position = (cx, cy)
                    return True, cx, cy

        self.ball_found = False
        self.ball_position = None
        return False, 0, 0

    def detect_court_position(self, frame, roi=None):
        """
        Detect path/position indicators on the court using color segmentation.
        Looks for red, blue, and white path markers.

        Returns: (x, y) centroid of largest path indicator, or None
        """
        crop = frame if roi is None else _crop(frame, roi)
        hsv = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV)

        # Check each path color
        for low, high, name in [
            (PATH_RED_LOW, PATH_RED_HIGH, "red"),
            (PATH_BLUE_LOW, PATH_BLUE_HIGH, "blue"),
            (PATH_WHITE_LOW, PATH_WHITE_HIGH, "white"),
        ]:
            mask = cv2.inRange(hsv, low, high)
            contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
            if not contours:
                continue

            largest = max(contours, key=cv2.contourArea)
            area = cv2.contourArea(largest)
            if area < 100:
                continue

            x, y, w, h = cv2.boundingRect(largest)
            cx, cy = x + w // 2, y + h // 2
            if roi is not None:
                fh, fw = frame.shape[:2]
                cx += int(roi[0] * fw)
                cy += int(roi[1] * fh)
            self.path_position = (cx, cy)
            return cx, cy

        self.path_position = None
        return None

    def get_relative_position(self, frame):
        """
        Get ball position relative to frame center (normalized -1 to 1).
        Useful for walk-toward-ball logic.
        Returns: (dx, dy) or None if ball not found
        """
        if self.ball_position is None:
            return None
        h, w = frame.shape[:2]
        dx = (self.ball_position[0] / w) - 0.5
        dy = (self.ball_position[1] / h) - 0.5
        return (dx * 2, dy * 2)  # Normalized to -1..1


def _crop(frame, roi):
    """Crop frame using normalized ROI [x, y, w, h] in 0-1 range."""
    h, w = frame.shape[:2]
    x1 = max(0, int(roi[0] * w))
    y1 = max(0, int(roi[1] * h))
    x2 = min(w, int((roi[0] + roi[2]) * w))
    y2 = min(h, int((roi[1] + roi[3]) * h))
    return frame[y1:y2, x1:x2]
