# icon.py — reconstructed from icon.cp311-win_amd64.pyd
# Source: E:/PythonProjects/2kVision/nba2k/icon.c (Cython 3.0.12)
# Ability Icon / 3PT icon detection for X (icon) mode
#
# GUI info: "You NEED the selected Ability Icon on offense!
#  Your offense Ability Icon changing/disappearing is used to time your shot!
#  This is build dependent."

import cv2
import numpy as np


class X:
    """
    Icon-based shot timing.
    Watches for ability icon (3PT badge) appearing/disappearing on HUD.
    Icon types: '3pt', and others from CDN.
    Search region set per nba2k_settings.
    """

    def __init__(self, icon_type='3pt', search_region=None):
        self.icon_type     = icon_type
        self.search_region = search_region or {'left': 0, 'top': 0, 'right': 1920, 'bottom': 1080}
        self.template      = None       # loaded icon template image
        self.last_seen     = False
        self.disappeared   = False

    def load_template(self, template_path):
        self.template = cv2.imread(template_path, cv2.IMREAD_COLOR)

    def run(self, frame):
        """
        Detect ability icon in frame.
        Returns: 'appeared', 'disappeared', or None
        """
        if self.template is None:
            return None

        roi = self._get_roi(frame)
        result = cv2.matchTemplate(roi, self.template, cv2.TM_CCOEFF_NORMED)
        _, max_val, _, _ = cv2.minMaxLoc(result)

        currently_visible = max_val > 0.75

        if self.last_seen and not currently_visible:
            self.disappeared = True
            self.last_seen   = False
            return 'disappeared'

        if not self.last_seen and currently_visible:
            self.last_seen = True
            return 'appeared'

        return None

    def _get_roi(self, frame):
        s = self.search_region
        return frame[s['top']:s['bottom'], s['left']:s['right']]
