"""
Game state detection — menus, end screen, team select, MyCareer mode.
Ported from KAI_MCAssistant: getEndLogo, getStar, getMc, getTeamSelect.
"""

import os
import cv2
import numpy as np

WHITE_UI_LOW = np.array([0, 0, 200])
WHITE_UI_HIGH = np.array([180, 30, 255])


class GameStateDetector:
    """Detects game flow states — menus, end screen, in-game, team selection."""

    def __init__(self, config=None, templates_dir=None):
        cfg = config or {}
        self.confidence = cfg.get("confidence", 0.7)
        self.templates_dir = templates_dir

        # Load template images if available
        self.end_logo_template = None
        self.star_template = None
        if templates_dir and os.path.isdir(templates_dir):
            self._load_templates(templates_dir)

        # State
        self.is_end_screen = False
        self.is_menu = False
        self.is_in_game = True
        self.mycareer_pixels = 0

    def _load_templates(self, path):
        """Load PNG template images from the templates directory."""
        end_path = os.path.join(path, "end_logo.png")
        star_path = os.path.join(path, "star.png")
        if os.path.exists(end_path):
            self.end_logo_template = cv2.imread(end_path)
        if os.path.exists(star_path):
            self.star_template = cv2.imread(star_path)

    def detect_end_screen(self, frame):
        """
        Detect the end-of-game screen via template matching.
        Returns: bool
        """
        if self.end_logo_template is None:
            # Fallback: detect by high white pixel density in top portion
            h, w = frame.shape[:2]
            top = frame[0:h // 4, :]
            hsv = cv2.cvtColor(top, cv2.COLOR_BGR2HSV)
            mask = cv2.inRange(hsv, WHITE_UI_LOW, WHITE_UI_HIGH)
            white_pct = cv2.countNonZero(mask) / (mask.shape[0] * mask.shape[1])
            self.is_end_screen = white_pct > 0.3
            return self.is_end_screen

        result = cv2.matchTemplate(frame, self.end_logo_template, cv2.TM_CCOEFF_NORMED)
        _, max_val, _, _ = cv2.minMaxLoc(result)
        self.is_end_screen = max_val > self.confidence
        return self.is_end_screen

    def detect_star_rating(self, frame):
        """
        Detect star rating graphic via template matching.
        Returns: (found: bool, confidence: float, location: tuple or None)
        """
        if self.star_template is None:
            return False, 0.0, None

        result = cv2.matchTemplate(frame, self.star_template, cv2.TM_CCOEFF_NORMED)
        _, max_val, _, max_loc = cv2.minMaxLoc(result)
        if max_val > self.confidence:
            th, tw = self.star_template.shape[:2]
            cx = max_loc[0] + tw // 2
            cy = max_loc[1] + th // 2
            return True, max_val, (cx, cy)
        return False, max_val, None

    def detect_mycareer_state(self, frame, roi=None):
        """
        Detect MyCareer state via white pixel density analysis.
        High density = menu/UI overlay. Low density = in-game.
        Returns: white_pixel_count
        """
        crop = frame if roi is None else self._crop(frame, roi)
        hsv = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV)
        mask = cv2.inRange(hsv, WHITE_UI_LOW, WHITE_UI_HIGH)
        self.mycareer_pixels = cv2.countNonZero(mask)
        return self.mycareer_pixels

    def detect_team_select(self, frame, roi=None):
        """
        Detect team selection screen using contour analysis on white elements.
        Returns: bool
        """
        crop = frame if roi is None else self._crop(frame, roi)
        hsv = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV)
        mask = cv2.inRange(hsv, WHITE_UI_LOW, WHITE_UI_HIGH)
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        # Team select has many rectangular white elements
        rect_count = 0
        for c in contours:
            area = cv2.contourArea(c)
            if area < 500:
                continue
            x, y, w, h = cv2.boundingRect(c)
            aspect = w / max(h, 1)
            if 0.3 < aspect < 3.0:  # Roughly rectangular
                rect_count += 1

        self.is_menu = rect_count >= 3
        return self.is_menu

    def is_in_game_check(self, frame):
        """
        Composite check: are we in actual gameplay (not menu, not end screen)?
        Returns: bool
        """
        end = self.detect_end_screen(frame)
        if end:
            self.is_in_game = False
            return False

        # Check white density — menus have high white pixel counts
        h, w = frame.shape[:2]
        mc_pixels = self.detect_mycareer_state(frame)
        total_pixels = h * w
        white_ratio = mc_pixels / total_pixels
        if white_ratio > 0.15:  # More than 15% white = likely menu
            self.is_in_game = False
            return False

        self.is_in_game = True
        return True

    @staticmethod
    def _crop(frame, roi):
        h, w = frame.shape[:2]
        x1 = max(0, int(roi[0] * w))
        y1 = max(0, int(roi[1] * h))
        x2 = min(w, int((roi[0] + roi[2]) * w))
        y2 = min(h, int((roi[1] + roi[3]) * h))
        return frame[y1:y2, x1:x2]
