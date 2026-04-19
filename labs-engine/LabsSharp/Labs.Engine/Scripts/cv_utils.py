"""
Labs Vision — CV Utility Library

Reusable computer vision helpers for building detection scripts.
Import these into your on_frame() or GCVWorker.process() scripts.

Usage:
    from cv_utils import ColorDetector, TemplateMatcher, RegionCapture
"""

import cv2
import numpy as np
from typing import Optional


class ColorDetector:
    """Detects objects by BGR or HSV color range within a region of interest."""

    def __init__(self, lower: tuple, upper: tuple, color_space: str = "bgr",
                 min_area: int = 50, max_area: int = 100000):
        """
        Args:
            lower: Lower bound (B,G,R) or (H,S,V)
            upper: Upper bound (B,G,R) or (H,S,V)
            color_space: "bgr" or "hsv"
            min_area: Minimum contour area to accept
            max_area: Maximum contour area to accept
        """
        self.lower = np.array(lower, dtype=np.uint8)
        self.upper = np.array(upper, dtype=np.uint8)
        self.color_space = color_space
        self.min_area = min_area
        self.max_area = max_area

    def detect(self, frame: np.ndarray, roi: tuple = None) -> list:
        """
        Detects colored regions in the frame.

        Args:
            frame: BGR image
            roi: Optional (x, y, w, h) region of interest

        Returns:
            List of (x, y, w, h, area) bounding boxes, sorted by area descending
        """
        if roi:
            x, y, w, h = roi
            crop = frame[y:y+h, x:x+w]
            offset_x, offset_y = x, y
        else:
            crop = frame
            offset_x, offset_y = 0, 0

        if self.color_space == "hsv":
            converted = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV)
        else:
            converted = crop

        mask = cv2.inRange(converted, self.lower, self.upper)

        # Morphological cleanup
        kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (3, 3))
        mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)
        mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel)

        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        results = []
        for cnt in contours:
            area = cv2.contourArea(cnt)
            if self.min_area <= area <= self.max_area:
                bx, by, bw, bh = cv2.boundingRect(cnt)
                results.append((bx + offset_x, by + offset_y, bw, bh, area))

        results.sort(key=lambda r: r[4], reverse=True)
        return results

    def is_present(self, frame: np.ndarray, roi: tuple = None) -> bool:
        """Returns True if any matching color region is detected."""
        return len(self.detect(frame, roi)) > 0

    def get_center(self, frame: np.ndarray, roi: tuple = None) -> Optional[tuple]:
        """Returns (cx, cy) center of the largest detection, or None."""
        detections = self.detect(frame, roi)
        if not detections:
            return None
        x, y, w, h, _ = detections[0]
        return (x + w // 2, y + h // 2)


class TemplateMatcher:
    """Matches a template image against a frame using OpenCV template matching."""

    def __init__(self, template_path: str, threshold: float = 0.8,
                 method: int = cv2.TM_CCOEFF_NORMED):
        """
        Args:
            template_path: Path to template image file
            threshold: Minimum match confidence (0-1)
            method: OpenCV template matching method
        """
        self.template = cv2.imread(template_path, cv2.IMREAD_COLOR)
        if self.template is None:
            raise FileNotFoundError(f"Template not found: {template_path}")
        self.threshold = threshold
        self.method = method
        self.template_h, self.template_w = self.template.shape[:2]

    def detect(self, frame: np.ndarray, roi: tuple = None) -> list:
        """
        Finds template matches in the frame.

        Returns:
            List of (x, y, w, h, confidence) matches above threshold
        """
        if roi:
            x, y, w, h = roi
            crop = frame[y:y+h, x:x+w]
            offset_x, offset_y = x, y
        else:
            crop = frame
            offset_x, offset_y = 0, 0

        if crop.shape[0] < self.template_h or crop.shape[1] < self.template_w:
            return []

        result = cv2.matchTemplate(crop, self.template, self.method)
        locations = np.where(result >= self.threshold)

        matches = []
        for pt in zip(*locations[::-1]):
            conf = result[pt[1], pt[0]]
            matches.append((
                pt[0] + offset_x,
                pt[1] + offset_y,
                self.template_w,
                self.template_h,
                float(conf)
            ))

        # Non-maximum suppression (simple distance-based)
        matches.sort(key=lambda m: m[4], reverse=True)
        filtered = []
        for match in matches:
            too_close = False
            for existing in filtered:
                dx = abs(match[0] - existing[0])
                dy = abs(match[1] - existing[1])
                if dx < self.template_w // 2 and dy < self.template_h // 2:
                    too_close = True
                    break
            if not too_close:
                filtered.append(match)

        return filtered

    def is_present(self, frame: np.ndarray, roi: tuple = None) -> bool:
        """Returns True if the template is found."""
        return len(self.detect(frame, roi)) > 0

    def get_center(self, frame: np.ndarray, roi: tuple = None) -> Optional[tuple]:
        """Returns (cx, cy) center of best match, or None."""
        matches = self.detect(frame, roi)
        if not matches:
            return None
        x, y, w, h, _ = matches[0]
        return (x + w // 2, y + h // 2)


class RegionCapture:
    """Captures and analyzes specific regions of the frame."""

    @staticmethod
    def crop(frame: np.ndarray, x: int, y: int, w: int, h: int) -> np.ndarray:
        """Safely crops a region from the frame."""
        fh, fw = frame.shape[:2]
        x = max(0, min(x, fw))
        y = max(0, min(y, fh))
        x2 = max(0, min(x + w, fw))
        y2 = max(0, min(y + h, fh))
        return frame[y:y2, x:x2]

    @staticmethod
    def crop_normalized(frame: np.ndarray, nx: float, ny: float,
                        nw: float, nh: float) -> np.ndarray:
        """Crops using normalized 0-1 coordinates."""
        fh, fw = frame.shape[:2]
        return RegionCapture.crop(frame,
                                  int(nx * fw), int(ny * fh),
                                  int(nw * fw), int(nh * fh))

    @staticmethod
    def average_color(frame: np.ndarray, roi: tuple = None) -> tuple:
        """Returns average (B, G, R) color of the region."""
        if roi:
            x, y, w, h = roi
            crop = frame[y:y+h, x:x+w]
        else:
            crop = frame
        avg = cv2.mean(crop)[:3]
        return (int(avg[0]), int(avg[1]), int(avg[2]))

    @staticmethod
    def pixel_count(frame: np.ndarray, lower: tuple, upper: tuple,
                    roi: tuple = None) -> int:
        """Counts pixels within the given BGR range."""
        if roi:
            x, y, w, h = roi
            crop = frame[y:y+h, x:x+w]
        else:
            crop = frame
        low = np.array(lower, dtype=np.uint8)
        high = np.array(upper, dtype=np.uint8)
        mask = cv2.inRange(crop, low, high)
        return int(cv2.countNonZero(mask))


class FrameDrawer:
    """Helper for drawing detection overlays on frames."""

    @staticmethod
    def draw_bbox(frame: np.ndarray, x: int, y: int, w: int, h: int,
                  color: tuple = (0, 255, 0), thickness: int = 2,
                  label: str = None):
        """Draws a bounding box with optional label."""
        cv2.rectangle(frame, (x, y), (x + w, y + h), color, thickness)
        if label:
            cv2.putText(frame, label, (x, y - 5),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.4, color, 1, cv2.LINE_AA)

    @staticmethod
    def draw_crosshair(frame: np.ndarray, cx: int, cy: int,
                       size: int = 20, color: tuple = (0, 255, 255)):
        """Draws a crosshair at the given center point."""
        cv2.line(frame, (cx - size, cy), (cx + size, cy), color, 1)
        cv2.line(frame, (cx, cy - size), (cx, cy + size), color, 1)

    @staticmethod
    def draw_confidence_bar(frame: np.ndarray, x: int, y: int,
                            confidence: float, width: int = 100,
                            color: tuple = (0, 255, 0)):
        """Draws a horizontal confidence bar."""
        fill = int(width * max(0, min(1, confidence)))
        cv2.rectangle(frame, (x, y), (x + width, y + 8), (40, 40, 40), -1)
        if fill > 0:
            cv2.rectangle(frame, (x, y), (x + fill, y + 8), color, -1)

    @staticmethod
    def draw_hud_panel(frame: np.ndarray, x: int, y: int,
                       lines: list, width: int = 300):
        """Draws a semi-transparent HUD panel with text lines."""
        line_h = 16
        panel_h = len(lines) * line_h + 10
        cv2.rectangle(frame, (x, y), (x + width, y + panel_h), (0, 0, 0), -1)
        cv2.rectangle(frame, (x, y), (x + width, y + panel_h), (60, 60, 60), 1)
        for i, (text, color) in enumerate(lines):
            cv2.putText(frame, text, (x + 8, y + 14 + i * line_h),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.38, color, 1, cv2.LINE_AA)
