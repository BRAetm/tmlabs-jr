# controller.py — reconstructed from controller.cp311-win_amd64.pyd
# Source: E:/PythonProjects/netx/controller.c  (E:\PythonProjects\netx\controller.py)
# Decodes/encodes DS5 controller packets from 2K network stream + shot analysis

import math
from collections import deque


class Controller:
    """
    Decodes/encodes DualSense analog stick + button data from NBA 2K packets.
    Also tracks shooting state and matchup inputs for timing analysis.
    Encoding uses Q6 fixed-point format for analog sticks.
    """

    HISTORY_LEN = 60  # frames

    def __init__(self):
        self.tracking_history         = deque(maxlen=self.HISTORY_LEN)
        self.matchup_tracking_history = deque(maxlen=self.HISTORY_LEN)
        self.shooting_state           = False
        self.matchup_shooting_state   = False
        self._last_inputs             = {}

    # ── Analog stick encoding/decoding ────────────────────────────────────────

    @staticmethod
    def _sign_extend_6bit(val):
        if val & 0x20:
            return val - 64
        return val

    @staticmethod
    def _q6_to_percent(q6_val):
        """Q6 fixed-point → -100..100 percent."""
        return (q6_val / 32.0) * 100.0

    @staticmethod
    def _percent_to_q6(pct):
        """Percent -100..100 → Q6 fixed-point."""
        return int((pct / 100.0) * 32.0)

    @staticmethod
    def _q6_to_u6(q6_val):
        return q6_val & 0x3F

    @staticmethod
    def _clamp(val, lo, hi):
        return max(lo, min(hi, val))

    def decode_analog_sticks(self, packet_bytes):
        """
        Decode left/right stick from packet.
        Returns: {'lx': pct, 'ly': pct, 'rx': pct, 'ry': pct}
        """
        lx_raw = self._sign_extend_6bit(packet_bytes[0] & 0x3F)
        ly_raw = self._sign_extend_6bit((packet_bytes[0] >> 6) | ((packet_bytes[1] & 0x0F) << 2))
        rx_raw = self._sign_extend_6bit((packet_bytes[1] >> 4) | ((packet_bytes[2] & 0x03) << 4))
        ry_raw = self._sign_extend_6bit(packet_bytes[2] >> 2)
        return {
            'lx': self._q6_to_percent(lx_raw),
            'ly': self._q6_to_percent(ly_raw),
            'rx': self._q6_to_percent(rx_raw),
            'ry': self._q6_to_percent(ry_raw),
        }

    def encode_analog_sticks(self, lx_pct, ly_pct, rx_pct, ry_pct):
        """Encode stick percentages back to packet bytes."""
        lx = self._q6_to_u6(self._percent_to_q6(self._clamp(lx_pct, -100, 100)))
        ly = self._q6_to_u6(self._percent_to_q6(self._clamp(ly_pct, -100, 100)))
        rx = self._q6_to_u6(self._percent_to_q6(self._clamp(rx_pct, -100, 100)))
        ry = self._q6_to_u6(self._percent_to_q6(self._clamp(ry_pct, -100, 100)))
        b0 = lx | ((ly & 0x03) << 6)
        b1 = (ly >> 2) | ((rx & 0x0F) << 4)
        b2 = (rx >> 4) | (ry << 2)
        return bytes([b0, b1, b2])

    def decode_face_buttons(self, packet_bytes):
        """Decode face button states from packet."""
        return {
            'cross':    bool(packet_bytes[0] & 0x01),
            'circle':   bool(packet_bytes[0] & 0x02),
            'square':   bool(packet_bytes[0] & 0x04),
            'triangle': bool(packet_bytes[0] & 0x08),
        }

    def encode_face_buttons_selective(self, current_bytes, button_overrides: dict):
        """Encode only specific buttons, leaving others intact."""
        b = bytearray(current_bytes)
        btn_map = {'cross': 0x01, 'circle': 0x02, 'square': 0x04, 'triangle': 0x08}
        for btn, val in button_overrides.items():
            mask = btn_map.get(btn, 0)
            if val:
                b[0] |= mask
            else:
                b[0] &= ~mask
        return bytes(b)

    def decode_triggers(self, packet_bytes):
        """L2/R2 trigger values (0-255)."""
        return {'l2': packet_bytes[0], 'r2': packet_bytes[1]}

    def encode_triggers(self, l2, r2):
        l2 = self._clamp(int(l2), 0, 255)
        r2 = self._clamp(int(r2), 0, 255)
        return bytes([l2, r2])

    # ── Stick polar coordinates ────────────────────────────────────────────────

    def get_stick_polar_coords(self, lx_pct, ly_pct):
        """Convert left stick to (angle_deg, magnitude_pct)."""
        angle = math.degrees(math.atan2(ly_pct, lx_pct))
        mag   = math.sqrt(lx_pct**2 + ly_pct**2)
        return angle, min(mag, 100.0)

    def set_rhythm_stick(self, gcvdata, direction='Down', magnitude=100):
        """Apply rhythm stick input based on direction."""
        if direction == 'Down':
            gcvdata.set_stick_ly(magnitude)
        elif direction == 'Up':
            gcvdata.set_stick_ly(-magnitude)

    # ── Frame modification ─────────────────────────────────────────────────────

    def apply_frame_modifications(self, packet, quick_inputs=None, defense_copy=False):
        """
        Apply quick inputs and/or defense mode left stick copying
        based on current state.
        """
        data = bytearray(packet)
        if quick_inputs:
            for offset, val in quick_inputs:
                data[offset] = val
        return bytes(data)

    # ── Shot analysis ──────────────────────────────────────────────────────────

    def get_inputs(self, packet):
        """Extract all inputs from a packet."""
        sticks   = self.decode_analog_sticks(packet[0:3])
        buttons  = self.decode_face_buttons(packet[3:4])
        triggers = self.decode_triggers(packet[4:6])
        return {**sticks, **buttons, **triggers}

    def get_shot_analysis(self):
        """
        Get detailed shot analysis data for debugging/tuning.
        Returns timing windows, release frames, etc.
        """
        if not self.tracking_history:
            return {}
        return {
            'history_len': len(self.tracking_history),
            'shooting_state': self.shooting_state,
            'frames': list(self.tracking_history)[-10:],
        }

    def update_shooting_state(self, inputs):
        """Detect transition into/out of shooting state."""
        was_shooting = self.shooting_state
        self.shooting_state = inputs.get('square', False) or inputs.get('triangle', False)
        return not was_shooting and self.shooting_state  # True on rising edge

    def update_tracking_history(self, inputs):
        self.tracking_history.append(inputs)

    def store_matchup_inputs(self, inputs):
        self.matchup_tracking_history.append(inputs)

    def update_matchup_shooting_state(self, inputs):
        was = self.matchup_shooting_state
        self.matchup_shooting_state = inputs.get('square', False)
        return not was and self.matchup_shooting_state

    def update_matchup_tracking_history(self, inputs):
        self.matchup_tracking_history.append(inputs)
