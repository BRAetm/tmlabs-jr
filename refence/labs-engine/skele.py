# skele.py — reconstructed from skele.cp311-win_amd64.pyd
# Source: E:/PythonProjects/2kVision/nba2k/skele.c (Cython 3.0.12)

import numpy as np


class Parser:
    """Parses raw keypoint/skeleton results from InferenceCore."""

    def __init__(self, keypoint_map=None):
        # keypoint_map: dict of keypoint index -> body part name
        self.keypoint_map = keypoint_map or {
            0: 'nose', 1: 'left_eye', 2: 'right_eye',
            3: 'left_ear', 4: 'right_ear',
            5: 'left_shoulder', 6: 'right_shoulder',
            7: 'left_elbow', 8: 'right_elbow',
            9: 'left_wrist', 10: 'right_wrist',
            11: 'left_hip', 12: 'right_hip',
            13: 'left_knee', 14: 'right_knee',
            15: 'left_ankle', 16: 'right_ankle',
        }

    def calculate_roi(self, player_box):
        """Compute tight ROI from player bounding box for skeleton inference."""
        x, y, w, h = player_box
        pad_x = int(w * 0.1)
        pad_y = int(h * 0.05)
        return (
            max(0, x - pad_x),
            max(0, y - pad_y),
            w + 2 * pad_x,
            h + 2 * pad_y,
        )

    def parse_results(self, inference_output):
        """
        Process keypoint output from InferenceCore.
        Returns dict: { body_part: (x, y, confidence) }
        """
        keypoints = {}
        if inference_output is None:
            return keypoints
        for idx, (x, y, conf) in enumerate(inference_output):
            name = self.keypoint_map.get(idx, f'kp_{idx}')
            keypoints[name] = (x, y, conf)
        return keypoints


class Shooter:
    """
    Shot timing engine using skeleton keypoints.
    Detects shooting pose → ball release → triggers input.
    Status: "UPGRADED SKELE WILL RETURN SOON"
    """

    def __init__(self, inference_engine, loaded_model):
        self.inference_engine = inference_engine
        self.loaded_model     = loaded_model
        self.parser           = Parser()

        self.player       = None   # current tracked player detection
        self.player_box   = None   # (x, y, w, h)
        self.player_height = 0
        self.player_top   = 0
        self.in_shooting_pose = False
        self.frame        = None

    def run(self, frame, player_detection):
        """
        Main per-frame shot detection.
        1. Get player bounding box
        2. Compute skeleton ROI
        3. Run inference
        4. Parse keypoints
        5. Check shooting pose
        6. Detect ball release
        """
        self.frame  = frame
        self.player = player_detection

        if player_detection is None:
            self.in_shooting_pose = False
            return None

        self.player_box    = player_detection['bbox']
        self.player_height = self.player_box[3]
        self.player_top    = self.player_box[1]

        roi = self.parser.calculate_roi(self.player_box)
        crop = frame[roi[1]:roi[1]+roi[3], roi[0]:roi[0]+roi[2]]

        # Run inference on cropped ROI
        raw = self.inference_engine.run(crop)
        keypoints = self.parser.parse_results(raw)

        self.in_shooting_pose = self._check_shooting_pose(keypoints)

        if self.in_shooting_pose:
            if self.ball_released(keypoints):
                return 'release'

        if self.ball_intersects_player_top(keypoints):
            return 'at_peak'

        return None

    def _check_shooting_pose(self, keypoints):
        """Determine if player is in a shooting stance from keypoints."""
        rw = keypoints.get('right_wrist')
        rs = keypoints.get('right_shoulder')
        if rw is None or rs is None:
            return False
        # Wrist above shoulder = shooting pose
        return rw[1] < rs[1] and rw[2] > 0.5 and rs[2] > 0.5

    def ball_released(self, keypoints):
        """
        Detect ball release — wrist fully extended above head.
        Returns True when release point detected.
        """
        rw = keypoints.get('right_wrist')
        nose = keypoints.get('nose')
        if rw is None or nose is None:
            return False
        return rw[1] < nose[1] and rw[2] > 0.6

    def ball_intersects_player_top(self, keypoints):
        """
        Check ball crossing above player_top.
        Used for peak detection timing.
        """
        if self.player_top == 0:
            return False
        rw = keypoints.get('right_wrist')
        if rw is None:
            return False
        return rw[1] <= self.player_top
