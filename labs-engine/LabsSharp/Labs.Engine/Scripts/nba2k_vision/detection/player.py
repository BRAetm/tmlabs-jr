"""
Player detection — presence, team identification.
Ported from KAI_MCAssistant: getPerson, checkMatchPerson.
"""

import cv2
import numpy as np

WHITE_UI_LOW = np.array([0, 0, 200])
WHITE_UI_HIGH = np.array([180, 30, 255])


class PlayerDetector:
    """Detects player presence and team assignment on screen."""

    def __init__(self, config=None):
        cfg = config or {}
        self.min_contour_area = cfg.get("player_min_area", 100)

        # State
        self.player_found = False
        self.match_person_found = False

    def detect_player_presence(self, frame, roi=None):
        """
        Detect if a player/character is visible via white contour analysis.
        Returns: (found: bool, contour_count: int)
        """
        crop = frame if roi is None else _crop(frame, roi)
        hsv = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV)
        mask = cv2.inRange(hsv, WHITE_UI_LOW, WHITE_UI_HIGH)
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        count = len([c for c in contours if cv2.contourArea(c) > 50])
        self.player_found = count > 0
        return self.player_found, count

    def detect_match_person(self, frame, roi=None, is_my_team=False):
        """
        Detect player in match selection screen.
        Uses contour area threshold > min_contour_area.
        Returns: bool
        """
        crop = frame if roi is None else _crop(frame, roi)
        hsv = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV)
        mask = cv2.inRange(hsv, WHITE_UI_LOW, WHITE_UI_HIGH)
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        for c in contours:
            if cv2.contourArea(c) > self.min_contour_area:
                self.match_person_found = True
                return True

        self.match_person_found = False
        return False

    def detect_got_next(self, frame, roi=None):
        """
        Detect the 'Got Next' spot on the court (green circle/diamond marker).
        Returns: (found: bool, center: tuple or None)
        """
        crop = frame if roi is None else _crop(frame, roi)
        hsv = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV)

        # Got Next spots are typically bright green/teal
        low = np.array([40, 120, 120])
        high = np.array([90, 255, 255])
        mask = cv2.inRange(hsv, low, high)

        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        for c in sorted(contours, key=cv2.contourArea, reverse=True):
            area = cv2.contourArea(c)
            if area < 200:
                continue
            M = cv2.moments(c)
            if M["m00"] > 0:
                cx = int(M["m10"] / M["m00"])
                cy = int(M["m01"] / M["m00"])
                if roi is not None:
                    h, w = frame.shape[:2]
                    cx += int(roi[0] * w)
                    cy += int(roi[1] * h)
                return True, (cx, cy)

        return False, None


def _crop(frame, roi):
    h, w = frame.shape[:2]
    x1 = max(0, int(roi[0] * w))
    y1 = max(0, int(roi[1] * h))
    x2 = min(w, int((roi[0] + roi[2]) * w))
    y2 = min(h, int((roi[1] + roi[3]) * h))
    return frame[y1:y2, x1:x2]
