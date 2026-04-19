"""
Dribble combo engine — playback, recording, ground control.
Ported from Helios dribbles_main.py: ComboPlayer, DribbleRecorder.
"""

import time

DEFAULT_GROUND_CONTROL = 100
DEADZONE = 20


class ComboPlayer:
    """
    Plays back a pre-recorded dribble combo sequence step by step.
    Action types: Wait, Hold, Release, Set.
    """

    def __init__(self):
        self.sequence = []       # List of action dicts
        self.step = 0            # Current action index
        self.is_playing = False
        self.active_combo = None  # Name of current combo
        self.loop = False
        self.hold_actions = {}   # {control_name: bool}
        self._wait_remaining = 0.0

    def start(self, combo_name, combo_data, loop=False):
        """Start playing a combo sequence."""
        self.sequence = [dict(a) for a in combo_data]  # Deep copy
        self.step = 0
        self.is_playing = True
        self.active_combo = combo_name
        self.loop = loop
        self.hold_actions = {}
        self._wait_remaining = 0.0

        # Pre-scan to identify all hold controls
        for action in self.sequence:
            if action.get("type") in ("Hold", "Release"):
                ctrl = action.get("control", "")
                self.hold_actions[ctrl] = False

    def stop(self):
        """Stop playback."""
        self.is_playing = False
        self.active_combo = None
        self.step = 0
        self.hold_actions = {}
        self._wait_remaining = 0.0

    def update(self, dt_ms):
        """
        Advance the combo by dt_ms milliseconds.
        Returns: dict of control values to apply {control_name: value}
        """
        outputs = {}

        if not self.is_playing or not self.sequence:
            return outputs

        # Keep all held controls active
        for ctrl, held in self.hold_actions.items():
            if held:
                outputs[ctrl] = self._get_held_value(ctrl)

        # Process actions
        while self.step < len(self.sequence):
            action = self.sequence[self.step]
            atype = action.get("type", "")

            if atype == "Wait":
                wait_ms = action.get("wait", 0)
                if self._wait_remaining <= 0:
                    self._wait_remaining = wait_ms

                self._wait_remaining -= dt_ms
                if self._wait_remaining > 0:
                    return outputs  # Still waiting
                self._wait_remaining = 0
                self.step += 1

            elif atype == "Hold":
                ctrl = action.get("control", "")
                value = action.get("value", 0)
                outputs[ctrl] = value / 100.0  # Normalize to -1..1
                self.hold_actions[ctrl] = True
                self.step += 1

            elif atype == "Release":
                ctrl = action.get("control", "")
                outputs[ctrl] = 0.0
                self.hold_actions[ctrl] = False
                self.step += 1

            elif atype == "Set":
                ctrl = action.get("control", "")
                value = action.get("value", 0)
                outputs[ctrl] = value / 100.0
                self.step += 1

            else:
                self.step += 1

        # Sequence complete
        if self.step >= len(self.sequence):
            if self.loop and self._any_held():
                self.step = 0  # Loop
            else:
                self.stop()

        return outputs

    def _any_held(self):
        return any(self.hold_actions.values())

    def _get_held_value(self, ctrl):
        """Find the last hold value for this control in the sequence."""
        for action in reversed(self.sequence[:self.step]):
            if action.get("type") == "Hold" and action.get("control") == ctrl:
                return action.get("value", 0) / 100.0
        return 0.0

    def get_next_action_info(self):
        """Look-ahead: find next Hold/Release and total wait time to reach it."""
        total_wait = 0.0
        for i in range(self.step, len(self.sequence)):
            action = self.sequence[i]
            if action["type"] == "Wait":
                total_wait += action.get("wait", 0)
            elif action["type"] in ("Hold", "Release"):
                return i, total_wait
        return -1, total_wait


class DribbleRecorder:
    """
    Records live controller inputs as a replayable combo sequence.
    """

    def __init__(self):
        self.recording = False
        self.sequence = []
        self._last_state = {}
        self._wait_accum = 0.0

    def start(self):
        """Start recording."""
        self.recording = True
        self.sequence = []
        self._last_state = {}
        self._wait_accum = 0.0

    def stop(self):
        """Stop recording and return the recorded sequence."""
        self.recording = False
        return list(self.sequence)

    def sample(self, dt_ms, controls):
        """
        Sample current control state.
        controls: dict of {name: value} where value is -100 to 100
        """
        if not self.recording:
            return

        # Find changes
        changes = []
        for name, value in controls.items():
            old = self._last_state.get(name, 0)
            if abs(value) > DEADZONE and abs(old) <= DEADZONE:
                changes.append(("Hold", name, value))
            elif abs(value) <= DEADZONE and abs(old) > DEADZONE:
                changes.append(("Release", name, 0))
            elif abs(value) > DEADZONE and abs(value - old) > 10:
                changes.append(("Set", name, value))

        if changes:
            # Insert accumulated wait time
            if self._wait_accum > 0:
                self.sequence.append({"type": "Wait", "wait": self._wait_accum})
                self._wait_accum = 0

            for atype, name, value in changes:
                if atype == "Release":
                    self.sequence.append({"type": "Release", "control": name})
                else:
                    self.sequence.append({"type": atype, "control": name, "value": value})
        else:
            self._wait_accum += dt_ms

        self._last_state = dict(controls)


def apply_ground_control(axes, ground_control=DEFAULT_GROUND_CONTROL):
    """
    Apply Jake Ramirez style ground-control scaling to left stick.
    Modifies axes in-place.
    """
    if ground_control == DEFAULT_GROUND_CONTROL:
        return

    lx = axes[0] * 100  # Convert from -1..1 to -100..100
    ly = axes[1] * 100

    if abs(lx) > DEADZONE or abs(ly) > DEADZONE:
        lx = (lx * ground_control) / 100
        ly = (ly * ground_control) / 100

    if abs(lx) < DEADZONE and abs(ly) < DEADZONE:
        lx = 0
        ly = 0

    axes[0] = max(-1.0, min(1.0, lx / 100))
    axes[1] = max(-1.0, min(1.0, ly / 100))
