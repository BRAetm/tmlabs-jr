"""
Labs Vision — Helios-Style Demo Script

Demonstrates using the Helios GCVWorker API with Labs Vision.
Shows controller visualization (like Helios CVTest.py) and
button passthrough.

This script uses the same GCVWorker class pattern as Helios II,
so scripts written for Helios can be ported with minimal changes.
"""

import cv2
import numpy as np
from helios_compat import *


class GCVWorker:
    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.gcvdata = bytearray(32)
        self.frame_count = 0

    def __del__(self):
        del self.gcvdata

    def process(self, frame):
        self.gcvdata = bytearray(32)
        h, w = frame.shape[:2]

        # Draw controller overlay
        self.draw_face_buttons(frame, w, h)
        self.draw_dpad(frame, w, h)
        self.draw_sticks(frame, w, h)
        self.draw_triggers(frame, w, h)
        self.draw_info(frame, w, h)

        self.frame_count += 1
        return frame, self.gcvdata

    def draw_face_buttons(self, frame, w, h):
        """Draw A/B/X/Y face buttons in bottom-right."""
        cx, cy = int(w * 0.85), int(h * 0.65)
        r = 18
        buttons = [
            (XBOX_A, cx, cy + r + 5, (0, 255, 0)),     # A = green
            (XBOX_B, cx + r + 5, cy, (0, 0, 255)),     # B = red
            (XBOX_X, cx - r - 5, cy, (255, 180, 0)),   # X = blue
            (XBOX_Y, cx, cy - r - 5, (0, 255, 255)),   # Y = yellow
        ]
        for btn_id, bx, by, color in buttons:
            val = get_actual(btn_id)
            pressed = val > 50
            thickness = -1 if pressed else 2
            cv2.circle(frame, (bx, by), r, color, thickness, cv2.LINE_AA)
            if pressed:
                self.gcvdata[_BUTTON_TO_WEB_GAMEPAD.get(btn_id, 0) + 1] = 100

    def draw_dpad(self, frame, w, h):
        """Draw D-Pad in bottom-left."""
        cx, cy = int(w * 0.15), int(h * 0.65)
        size = 15
        dpad = [
            (DPAD_UP, cx, cy - size * 2),
            (DPAD_DOWN, cx, cy + size * 2),
            (DPAD_LEFT, cx - size * 2, cy),
            (DPAD_RIGHT, cx + size * 2, cy),
        ]
        for btn_id, bx, by in dpad:
            val = get_actual(btn_id)
            pressed = val > 50
            color = (255, 255, 255) if pressed else (100, 100, 100)
            cv2.rectangle(frame,
                          (bx - size, by - size),
                          (bx + size, by + size),
                          color, -1 if pressed else 2, cv2.LINE_AA)

    def draw_sticks(self, frame, w, h):
        """Draw left and right analog sticks."""
        for label, stick_x_id, stick_y_id, click_id, cx, cy in [
            ("L", STICK_2_X, STICK_2_Y, XBOX_LS, int(w * 0.3), int(h * 0.75)),
            ("R", STICK_1_X, STICK_1_Y, XBOX_RS, int(w * 0.7), int(h * 0.75)),
        ]:
            outer_r = 35
            inner_r = 20
            x_val = get_actual(stick_x_id) / 100.0
            y_val = get_actual(stick_y_id) / 100.0
            click = get_actual(click_id) > 50

            # Outer ring
            cv2.circle(frame, (cx, cy), outer_r, (80, 80, 80), 2, cv2.LINE_AA)

            # Inner stick position
            sx = int(cx + x_val * (outer_r - inner_r))
            sy = int(cy + y_val * (outer_r - inner_r))
            color = (200, 200, 200) if click else (150, 150, 150)
            cv2.circle(frame, (sx, sy), inner_r, color, -1, cv2.LINE_AA)

            # Label
            cv2.putText(frame, label, (cx - 5, cy + 5),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 0, 0), 1, cv2.LINE_AA)

    def draw_triggers(self, frame, w, h):
        """Draw LT/RT trigger bars at the top."""
        for label, btn_id, x_start in [
            ("LT", XBOX_LT, int(w * 0.1)),
            ("RT", XBOX_RT, int(w * 0.7)),
        ]:
            val = get_actual(btn_id)
            bar_w = int(w * 0.2)
            bar_h = 15
            y = int(h * 0.08)
            fill_w = int(bar_w * val / 100.0)

            cv2.rectangle(frame, (x_start, y), (x_start + bar_w, y + bar_h),
                          (80, 80, 80), 2, cv2.LINE_AA)
            if fill_w > 0:
                cv2.rectangle(frame, (x_start, y), (x_start + fill_w, y + bar_h),
                              (100, 200, 255), -1, cv2.LINE_AA)
            cv2.putText(frame, label, (x_start - 25, y + 12),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.4, (200, 200, 200), 1, cv2.LINE_AA)

    def draw_info(self, frame, w, h):
        """Draw frame counter and status info."""
        cv2.putText(frame, f"Labs Vision + Helios SDK | Frame {self.frame_count}",
                    (10, 20), cv2.FONT_HERSHEY_SIMPLEX, 0.45,
                    (200, 200, 200), 1, cv2.LINE_AA)
        cv2.putText(frame, "GCVWorker active",
                    (10, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.35,
                    (0, 200, 100), 1, cv2.LINE_AA)
