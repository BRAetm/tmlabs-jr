"""
Defense automation — LT tapping, stationary LT hold, smart toggle.
Ported from Puma Tools main.py: handle_defense, handle_stationary_lt_hold.
"""

# Button indices
BTN_LT = 6
BTN_RT = 7
BTN_DPAD_UP = 12
BTN_DPAD_DOWN = 13
BTN_DPAD_LEFT = 14
BTN_DPAD_RIGHT = 15
BTN_VIEW = 8

STICK_DEADZONE = 0.15


class DefenseHandler:
    """Auto-taps LT at a configurable interval for tight defense."""

    def __init__(self, tap_speed=100):
        self.tap_speed = tap_speed  # ms between taps
        self.enabled = False
        self.state = "releasing"  # "releasing" or "holding"
        self._last_tap_time = 0.0

    def update(self, elapsed_ms, trigger_pressed, buttons):
        """
        Update defense state. Modifies buttons in-place.
        trigger_pressed: whether the defense trigger is held by user
        """
        if not self.enabled or not trigger_pressed:
            self.state = "releasing"
            return

        if self.state == "releasing":
            self.state = "holding"
            self._last_tap_time = elapsed_ms

        elif self.state == "holding":
            dt = elapsed_ms - self._last_tap_time
            if dt < self.tap_speed:
                buttons[BTN_LT] = True
            else:
                self.state = "releasing"


class StationaryLTHold:
    """
    Auto-holds LT when the player is stationary.
    Cancels on stick movement, RT press, or DPAD press.
    """

    def __init__(self):
        self.enabled = False
        self.is_holding = False

    def update(self, axes, buttons):
        """
        Check if player is stationary and apply LT hold.
        Modifies buttons in-place.
        """
        if not self.enabled:
            self.is_holding = False
            return

        # Cancel conditions
        ls_moving = abs(axes[0]) > STICK_DEADZONE or abs(axes[1]) > STICK_DEADZONE
        rs_moving = abs(axes[2]) > STICK_DEADZONE or abs(axes[3]) > STICK_DEADZONE
        rt_pressed = buttons[BTN_RT]
        dpad_pressed = any(buttons[i] for i in [BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT])
        select_pressed = buttons[BTN_VIEW]

        if ls_moving or rs_moving or rt_pressed or dpad_pressed or select_pressed:
            self.is_holding = False
            return

        # Stationary — hold LT
        self.is_holding = True
        buttons[BTN_LT] = True


class SmartToggle:
    """
    Toggle features on/off via a button press.
    Tracks press/release edge detection.
    """

    def __init__(self, button_index=None):
        self.enabled = False
        self.button_index = button_index
        self.active = False
        self._pressed_last_frame = False

    def update(self, buttons):
        """
        Check for toggle button press. Returns current active state.
        """
        if not self.enabled or self.button_index is None:
            return self.active

        pressed = buttons[self.button_index] if self.button_index < len(buttons) else False

        # Edge detection — toggle on press, not hold
        if pressed and not self._pressed_last_frame:
            self.active = not self.active

        self._pressed_last_frame = pressed
        return self.active


class DefenseController:
    """Manages all defense features."""

    def __init__(self, config=None):
        cfg = config or {}
        def_cfg = cfg.get("defense", {})

        self.defense = DefenseHandler(
            tap_speed=def_cfg.get("tap_speed", 100))
        self.defense.enabled = def_cfg.get("enabled", True)

        self.lt_hold = StationaryLTHold()
        self.lt_hold.enabled = def_cfg.get("lt_hold_auto", True)

        self.smart_toggle = SmartToggle()

    def update(self, elapsed_ms, axes, buttons):
        """Update all defense features. Modifies axes/buttons in-place."""
        # Smart toggle check
        self.smart_toggle.update(buttons)

        # Defense tapping
        trigger = buttons[BTN_LT] if len(buttons) > BTN_LT else False
        self.defense.update(elapsed_ms, trigger, buttons)

        # Stationary LT
        self.lt_hold.update(axes, buttons)
