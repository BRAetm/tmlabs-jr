"""
Shot type controllers — all 6 shot types as state machines.
Ported from Puma Tools main.py: handle_button_press, handle_spin_shot,
handle_rhythm_shot, handle_self_timing, handle_self_lob, run_active_quick_shot.

Each shot controller produces stick/button outputs via update().
"""

import math
import time


# Button indices (Web Gamepad API)
BTN_A = 0
BTN_B = 1
BTN_X = 2
BTN_Y = 3
BTN_LB = 4
BTN_RB = 5
BTN_LT = 6
BTN_RT = 7
BTN_VIEW = 8
BTN_MENU = 9
BTN_LS = 10
BTN_RS = 11
BTN_DPAD_UP = 12
BTN_DPAD_DOWN = 13
BTN_DPAD_LEFT = 14
BTN_DPAD_RIGHT = 15
BTN_GUIDE = 16

# Axis indices
AXIS_LX = 0
AXIS_LY = 1
AXIS_RX = 2
AXIS_RY = 3


def _polar_to_stick(radius, angle_deg):
    """Convert polar coordinates to stick X/Y values (-1.0 to 1.0)."""
    angle_rad = math.radians(angle_deg)
    x = radius * math.cos(angle_rad)
    y = radius * math.sin(angle_rad)
    return max(-1.0, min(1.0, x)), max(-1.0, min(1.0, y))


class ButtonPressShot:
    """
    Hold RS_DOWN for a timed duration.
    States: releasing → holding → releasing
    """

    def __init__(self, hold_time=200, no_dip_modifier=0,
                 left_fade_modifier=0, right_fade_modifier=0):
        self.hold_time = hold_time  # ms
        self.no_dip_modifier = no_dip_modifier
        self.left_fade_modifier = left_fade_modifier
        self.right_fade_modifier = right_fade_modifier
        self.state = "releasing"
        self._start_time = 0.0

    def update(self, elapsed_ms, trigger_pressed, modifier_active=False,
               fade_direction=None):
        """Returns: (rx, ry) stick values"""
        if self.state == "releasing":
            if trigger_pressed:
                self.state = "holding"
                self._start_time = elapsed_ms
            return 0.0, 0.0

        elif self.state == "holding":
            dt = elapsed_ms - self._start_time

            # Determine hold value based on modifiers
            if modifier_active:
                hold_val = self.no_dip_modifier / 100.0
            elif fade_direction == "left":
                hold_val = self.left_fade_modifier / 100.0
            elif fade_direction == "right":
                hold_val = self.right_fade_modifier / 100.0
            else:
                hold_val = -1.0  # Full RS_DOWN

            if dt >= self.hold_time or not trigger_pressed:
                self.state = "releasing"
                return 0.0, 0.0

            return 0.0, hold_val

        return 0.0, 0.0


class SpinShot:
    """
    Push down then continuously rotate RS in a circular motion.
    States: releasing → pushing → spinning → releasing
    """

    def __init__(self, push_time=150, spin_speed=400, handedness="right",
                 no_dip_push_modifier=0, no_dip_spin_modifier=0,
                 left_fade_push_modifier=0, left_fade_spin_modifier=0,
                 right_fade_push_modifier=0, right_fade_spin_modifier=0):
        self.push_time = push_time  # ms
        self.spin_speed = spin_speed  # ms for full 360°
        self.handedness = handedness  # "right" or "left"
        self.no_dip_push_modifier = no_dip_push_modifier
        self.no_dip_spin_modifier = no_dip_spin_modifier
        self.left_fade_push_modifier = left_fade_push_modifier
        self.left_fade_spin_modifier = left_fade_spin_modifier
        self.right_fade_push_modifier = right_fade_push_modifier
        self.right_fade_spin_modifier = right_fade_spin_modifier

        self.state = "releasing"
        self._start_time = 0.0
        self._spin_start_time = 0.0

    def update(self, elapsed_ms, trigger_pressed, modifier_active=False,
               fade_direction=None):
        """Returns: (rx, ry) stick values"""
        if self.state == "releasing":
            if trigger_pressed:
                self.state = "pushing"
                self._start_time = elapsed_ms
            return 0.0, 0.0

        elif self.state == "pushing":
            dt = elapsed_ms - self._start_time
            push_val = self._get_push_modifier(modifier_active, fade_direction)
            if dt >= self.push_time:
                self.state = "spinning"
                self._spin_start_time = elapsed_ms
            return 0.0, push_val

        elif self.state == "spinning":
            if not trigger_pressed:
                self.state = "releasing"
                return 0.0, 0.0

            spin_mod = self._get_spin_modifier(modifier_active, fade_direction)
            if spin_mod == 0:
                spin_mod = self.spin_speed

            spin_elapsed = elapsed_ms - self._spin_start_time
            direction = -1 if self.handedness == "right" else 1
            angle = 270 + direction * (spin_elapsed * 360 / max(spin_mod, 1))
            rx, ry = _polar_to_stick(1.0, angle)
            return rx, ry

        return 0.0, 0.0

    def _get_push_modifier(self, modifier_active, fade_direction):
        if modifier_active and self.no_dip_push_modifier:
            return -(self.no_dip_push_modifier / 100.0)
        if fade_direction == "left" and self.left_fade_push_modifier:
            return -(self.left_fade_push_modifier / 100.0)
        if fade_direction == "right" and self.right_fade_push_modifier:
            return -(self.right_fade_push_modifier / 100.0)
        return -1.0

    def _get_spin_modifier(self, modifier_active, fade_direction):
        if modifier_active:
            return self.no_dip_spin_modifier
        if fade_direction == "left":
            return self.left_fade_spin_modifier
        if fade_direction == "right":
            return self.right_fade_spin_modifier
        return 0


class RhythmShot:
    """
    4-phase tempo shot: push(-1) → hold → finish(+1) → release.
    States: releasing → pushing → holding → finishing → releasing
    """

    def __init__(self, tempo=180, hold_time=120, pull_down=False,
                 no_dip_tempo_modifier=0, no_dip_hold_modifier=0):
        self.tempo = tempo  # ms push duration
        self.hold_time = hold_time  # ms hold duration
        self.pull_down = pull_down
        self.no_dip_tempo_modifier = no_dip_tempo_modifier
        self.no_dip_hold_modifier = no_dip_hold_modifier

        self.state = "releasing"
        self._start_time = 0.0
        self._hold_start = 0.0

    def update(self, elapsed_ms, trigger_pressed, modifier_active=False):
        """Returns: (rx, ry) stick values"""
        final_tempo = self.no_dip_tempo_modifier if modifier_active and self.no_dip_tempo_modifier else self.tempo
        final_hold = self.no_dip_hold_modifier if modifier_active and self.no_dip_hold_modifier else self.hold_time

        if self.state == "releasing":
            if trigger_pressed:
                self.state = "pushing"
                self._start_time = elapsed_ms
            return 0.0, 0.0

        elif self.state == "pushing":
            dt = elapsed_ms - self._start_time
            if dt >= final_tempo:
                self.state = "holding"
                self._hold_start = elapsed_ms
            return 0.0, -1.0  # RS_DOWN full

        elif self.state == "holding":
            dt = elapsed_ms - self._hold_start
            hold_val = -(final_hold / max(self.hold_time, 1))
            if dt >= final_hold:
                self.state = "finishing"
                self._start_time = elapsed_ms
            return 0.0, max(-1.0, min(1.0, hold_val))

        elif self.state == "finishing":
            dt = elapsed_ms - self._start_time
            if dt >= final_tempo or not trigger_pressed:
                self.state = "releasing"
                return 0.0, 0.0
            return 0.0, 1.0  # RS_UP (release)

        return 0.0, 0.0


class SelfTimingShot:
    """
    Timing shot with rotational thumbstick roll.
    States: releasing → pushing → holding(rotating) → finishing → releasing
    """

    def __init__(self, speed=350, handedness="right",
                 no_dip_tempo_modifier=0):
        self.speed = speed  # ms for rotation
        self.handedness = handedness
        self.no_dip_tempo_modifier = no_dip_tempo_modifier

        self.state = "releasing"
        self._start_time = 0.0
        self._hold_start = 0.0

    def update(self, elapsed_ms, trigger_pressed, modifier_active=False):
        """Returns: (rx, ry) stick values"""
        final_speed = self.no_dip_tempo_modifier if modifier_active and self.no_dip_tempo_modifier else self.speed

        if self.state == "releasing":
            if trigger_pressed:
                self.state = "pushing"
                self._start_time = elapsed_ms
            return 0.0, 0.0

        elif self.state == "pushing":
            dt = elapsed_ms - self._start_time
            if dt >= final_speed:
                self.state = "holding"
                self._hold_start = elapsed_ms
            return 0.0, -1.0  # RS_DOWN

        elif self.state == "holding":
            if not trigger_pressed:
                self.state = "finishing"
                self._start_time = elapsed_ms
                return 0.0, 0.0

            hold_elapsed = elapsed_ms - self._hold_start
            direction = -1 if self.handedness == "right" else 1
            angle = 270 + direction * (hold_elapsed * 360 / max(final_speed, 1))
            rx, ry = _polar_to_stick(1.0, angle)
            return rx, ry

        elif self.state == "finishing":
            dt = elapsed_ms - self._start_time
            if dt >= final_speed:
                self.state = "releasing"
                return 0.0, 0.0
            return 0.0, 1.0  # RS_UP (release)

        return 0.0, 0.0


class SelfLob:
    """
    Simultaneous DPAD_UP + SELECT press.
    Simple trigger → press → release.
    """

    def __init__(self, tap_speed=100):
        self.tap_speed = tap_speed  # ms
        self.state = "releasing"
        self._start_time = 0.0

    def update(self, elapsed_ms, trigger_pressed):
        """Returns: dict of button overrides {index: bool}"""
        if self.state == "releasing":
            if trigger_pressed:
                self.state = "holding"
                self._start_time = elapsed_ms
            return {}

        elif self.state == "holding":
            dt = elapsed_ms - self._start_time
            if dt >= self.tap_speed:
                self.state = "releasing"
                return {}
            return {BTN_DPAD_UP: True, BTN_VIEW: True}

        return {}


class QuickShot:
    """
    One-touch RS pull/release (quick tap shot).
    """

    def __init__(self, tap_speed=100):
        self.tap_speed = tap_speed  # ms per phase
        self.state = "idle"
        self._start_time = 0.0

    def fire(self, elapsed_ms):
        """Trigger a quick shot."""
        self.state = "pulling"
        self._start_time = elapsed_ms

    def update(self, elapsed_ms):
        """Returns: (rx, ry) stick values"""
        if self.state == "idle":
            return 0.0, 0.0

        dt = elapsed_ms - self._start_time

        if self.state == "pulling":
            if dt >= self.tap_speed:
                self.state = "releasing"
                self._start_time = elapsed_ms
            return 0.0, -1.0  # Pull down

        elif self.state == "releasing":
            if dt >= self.tap_speed:
                self.state = "idle"
            return 0.0, 1.0  # Release up

        return 0.0, 0.0


class ShotController:
    """Manages all shot types and produces combined gamepad output."""

    def __init__(self, config=None):
        cfg = config or {}
        shot_cfg = cfg.get("shot_timing", {})

        self.enabled = shot_cfg.get("enabled", True)

        self.button_press = ButtonPressShot(
            hold_time=shot_cfg.get("button_press_hold", 200))
        self.spin = SpinShot(
            push_time=shot_cfg.get("spin_push", 150),
            spin_speed=shot_cfg.get("spin_speed", 400))
        self.rhythm = RhythmShot(
            tempo=shot_cfg.get("rhythm_tempo", 180),
            hold_time=shot_cfg.get("rhythm_hold", 120))
        self.self_timing = SelfTimingShot(
            speed=shot_cfg.get("self_timing_speed", 350))
        self.self_lob = SelfLob(
            tap_speed=shot_cfg.get("self_lob_speed", 100))
        self.quick_shot = QuickShot(
            tap_speed=shot_cfg.get("quick_shot_speed", 100))

        # Active shot type (only one at a time)
        self.active_type = None  # "button_press", "spin", "rhythm", etc.

    def get_active_shot(self):
        """Get the currently active shot controller."""
        if self.active_type == "button_press":
            return self.button_press
        elif self.active_type == "spin":
            return self.spin
        elif self.active_type == "rhythm":
            return self.rhythm
        elif self.active_type == "self_timing":
            return self.self_timing
        return None

    def update(self, elapsed_ms, axes, buttons):
        """
        Update all shot controllers and apply RS output.
        Modifies axes and buttons in-place.
        """
        if not self.enabled:
            return

        # Quick shot runs independently
        qrx, qry = self.quick_shot.update(elapsed_ms)
        if self.quick_shot.state != "idle":
            axes[AXIS_RX] = qrx
            axes[AXIS_RY] = qry
            return

        # Self lob runs independently
        lob_buttons = self.self_lob.update(elapsed_ms, buttons[BTN_VIEW] if len(buttons) > BTN_VIEW else False)
        for idx, val in lob_buttons.items():
            if idx < len(buttons):
                buttons[idx] = val
