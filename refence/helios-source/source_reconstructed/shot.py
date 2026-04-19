# shot.py — reconstructed from shot.cp311-win_amd64.pyd
# Source: E:/PythonProjects/2kVision/nba2k/shot.c (Cython 3.0.12)
# All meter types + trainer

import cv2
import numpy as np
import json
import os


class Meter:
    """Base class for all shot meters."""

    def __init__(self, config: dict):
        self.config = config
        self.search = config.get('search', {})
        self.timing = config.get('timing', {})
        self.contour = config.get('options', {}).get('Large/Side', {}).get('contour', {})
        self.color = config.get('options', {}).get('Large/Side', {}).get('color', {})
        self.confidence = config.get('options', {}).get('Large/Side', {}).get('confidence', {})
        self.bbox_pad = config.get('options', {}).get('Large/Side', {}).get('bbox', {})
        self.timing_value = self.timing.get('value', 50)
        self.selected_color = config.get('selected', {}).get('color', 'Purple')

    def _color_mask(self, frame):
        color_cfg = self.color.get(self.selected_color, {})
        low  = np.array(color_cfg.get('low',  [0, 0, 0]),   dtype=np.uint8)
        high = np.array(color_cfg.get('high', [255,255,255]), dtype=np.uint8)
        return cv2.inRange(frame, low, high)

    def _search_roi(self, frame):
        s = self.search
        return frame[s.get('top',0):s.get('bottom',1080),
                     s.get('left',0):s.get('right',1920)]

    def detect(self, frame):
        raise NotImplementedError

    def train(self, frame):
        raise NotImplementedError


class Trainer:
    """Training mode — records frames to tune meter detection."""

    def __init__(self, meter, threshold=120):
        self.meter = meter
        self.threshold = threshold
        self.samples = []

    def record(self, frame):
        self.samples.append(frame.copy())

    def analyze(self):
        pass


class Straight(Meter):
    """
    Straight bar meter.
    Contour: width 2-4px, height 43-217px
    """

    def find_meter_edges(self, frame):
        """Find left/right edges of the straight meter bar."""
        roi = self._search_roi(frame)
        mask = self._color_mask(roi)
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        valid = []
        for c in contours:
            x, y, w, h = cv2.boundingRect(c)
            wmin = self.contour.get('width',  {}).get('min', 2)
            wmax = self.contour.get('width',  {}).get('max', 4)
            hmin = self.contour.get('height', {}).get('min', 43)
            hmax = self.contour.get('height', {}).get('max', 217)
            if wmin <= w <= wmax and hmin <= h <= hmax:
                valid.append((x, y, w, h))
        return valid

    def detect(self, frame):
        edges = self.find_meter_edges(frame)
        if not edges:
            return None
        # Return fill percentage
        leftmost  = min(e[0] for e in edges)
        rightmost = max(e[0] + e[2] for e in edges)
        total = rightmost - leftmost
        if total == 0:
            return None
        filled = edges[-1][0] - leftmost if len(edges) > 1 else 0
        return filled / total

    def train(self, frame):
        return Trainer(self, self.config.get('training', {}).get('threshold', 120))


class Arrow(Meter):
    """Arrow meter — horizontal arrow indicator. Contour: 37-45w x 18-24h."""

    def detect(self, frame):
        roi = self._search_roi(frame)
        mask = self._color_mask(roi)
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        for c in contours:
            x, y, w, h = cv2.boundingRect(c)
            if 37 <= w <= 45 and 18 <= h <= 24:
                return (x, y, w, h)
        return None

    def train(self, frame):
        return Trainer(self, 150)


class Arrow2(Meter):
    """Arrow2 meter. Contour: 23-30w x 33-165h."""

    def detect(self, frame):
        roi = self._search_roi(frame)
        mask = self._color_mask(roi)
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        for c in contours:
            x, y, w, h = cv2.boundingRect(c)
            if 23 <= w <= 30 and 33 <= h <= 165:
                return (x, y, w, h)
        return None

    def train(self, frame):
        return Trainer(self)


class Pill(Meter):
    """Pill meter. Contour: 6-9w x 32-160h."""

    def detect(self, frame):
        roi = self._search_roi(frame)
        mask = self._color_mask(roi)
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        for c in contours:
            x, y, w, h = cv2.boundingRect(c)
            if 6 <= w <= 9 and 32 <= h <= 160:
                return (x, y, w, h)
        return None

    def train(self, frame):
        return Trainer(self)


class Sword(Meter):
    """Sword meter. Contour: 19-24w x 37-186h."""

    def detect(self, frame):
        roi = self._search_roi(frame)
        mask = self._color_mask(roi)
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        for c in contours:
            x, y, w, h = cv2.boundingRect(c)
            if 19 <= w <= 24 and 37 <= h <= 186:
                return (x, y, w, h)
        return None

    def train(self, frame):
        return Trainer(self)


class Dial(Meter):
    """Dial/circular meter. Contour: 13-53w x 13-26h."""

    def detect(self, frame):
        roi = self._search_roi(frame)
        mask = self._color_mask(roi)
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        for c in contours:
            x, y, w, h = cv2.boundingRect(c)
            if 13 <= w <= 53 and 13 <= h <= 26:
                return (x, y, w, h)
        return None

    def train(self, frame):
        return Trainer(self, 150)


class _2kOL2(Meter):
    """2K Online 2 meter. White only. Contour: 2-4w x 34-249h."""

    def __init__(self, config):
        super().__init__(config)
        self.selected_color = 'White'

    def detect(self, frame):
        roi = self._search_roi(frame)
        mask = self._color_mask(roi)
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        for c in contours:
            x, y, w, h = cv2.boundingRect(c)
            if 2 <= w <= 4 and 34 <= h <= 249:
                return (x, y, w, h)
        return None

    def train(self, frame):
        return Trainer(self, 100)


class Oop(Meter):
    """OOP (out-of-position) detection meter."""

    def __init__(self, config):
        super().__init__(config)

    def detect(self, frame):
        # NO TRAINING FOR THIS METER
        return None


METER_CLASSES = {
    'Straight': Straight,
    'Arrow':    Arrow,
    'Arrow2':   Arrow2,
    'Pill':     Pill,
    'Sword':    Sword,
    'Dial':     Dial,
    '_2kOL2':   _2kOL2,
}


def load_meter(name: str, config_dir: str) -> Meter:
    """Load a meter from its JSON config file."""
    path = os.path.join(config_dir, f'{name}.json')
    with open(path) as f:
        cfg = json.load(f)
    cls = METER_CLASSES.get(name)
    if cls is None:
        raise ValueError(f'Unknown meter: {name}')
    return cls(cfg)
