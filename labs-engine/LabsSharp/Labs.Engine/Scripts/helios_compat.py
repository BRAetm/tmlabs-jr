"""
Labs Vision — Helios II Compatibility Layer

Provides the same button constants and API functions as Helios's gtuner.pyd,
so scripts written for Helios can run on Labs Vision with minimal changes.

Usage in scripts:
    from helios_compat import *

    class GCVWorker:
        def __init__(self, width, height):
            self.gcvdata = bytearray(32)

        def process(self, frame):
            if get_actual(BUTTON_14) > 50:  # A button pressed
                # do something
                pass
            return frame, self.gcvdata
"""

import threading

# ---------------------------------------------------------------------------
# Controller Constants (Gtuner IV / Helios compatible)
# ---------------------------------------------------------------------------

# Button identifiers (values 0.00 - 100.00 when read via get_actual)
BUTTON_1  = 0   # Xbox Guide
BUTTON_2  = 1   # Back / View
BUTTON_3  = 2   # Start / Menu
BUTTON_4  = 3   # Right Bumper (RB)
BUTTON_5  = 4   # Right Trigger (RT)
BUTTON_6  = 5   # Right Stick Click (RS)
BUTTON_7  = 6   # Left Bumper (LB)
BUTTON_8  = 7   # Left Trigger (LT)
BUTTON_9  = 8   # Left Stick Click (LS)
BUTTON_10 = 9   # D-Pad Up
BUTTON_11 = 10  # D-Pad Down
BUTTON_12 = 11  # D-Pad Left
BUTTON_13 = 12  # D-Pad Right
BUTTON_14 = 13  # A (Cross)
BUTTON_15 = 14  # B (Circle)
BUTTON_16 = 15  # X (Square)
BUTTON_17 = 16  # Y (Triangle)
BUTTON_18 = 17  # Share
BUTTON_19 = 18  # Touchpad Click
BUTTON_20 = 19  # Paddle 1
BUTTON_21 = 20  # Paddle 2

# Axis identifiers (values -100.00 to +100.00)
STICK_1_X  = 21  # Right Stick X
STICK_1_Y  = 22  # Right Stick Y
STICK_2_X  = 23  # Left Stick X
STICK_2_Y  = 24  # Left Stick Y
POINT_1_X  = 25  # Touch Point 1 X
POINT_1_Y  = 26  # Touch Point 1 Y
POINT_2_X  = 27  # Touch Point 2 X
POINT_2_Y  = 28  # Touch Point 2 Y
ACCEL_1_X  = 29  # Accelerometer X
ACCEL_1_Y  = 30  # Accelerometer Y
ACCEL_1_Z  = 31  # Accelerometer Z
GYRO_1_X   = 35  # Gyroscope X
GYRO_1_Y   = 36  # Gyroscope Y
GYRO_1_Z   = 37  # Gyroscope Z
PADDLE_1   = 38
PADDLE_2   = 39
PADDLE_3   = 40
PADDLE_4   = 41

# Friendly Xbox name aliases
XBOX_GUIDE = BUTTON_1
XBOX_VIEW  = BUTTON_2
XBOX_MENU  = BUTTON_3
XBOX_RB    = BUTTON_4
XBOX_RT    = BUTTON_5
XBOX_RS    = BUTTON_6
XBOX_LB    = BUTTON_7
XBOX_LT    = BUTTON_8
XBOX_LS    = BUTTON_9
DPAD_UP    = BUTTON_10
DPAD_DOWN  = BUTTON_11
DPAD_LEFT  = BUTTON_12
DPAD_RIGHT = BUTTON_13
XBOX_A     = BUTTON_14
XBOX_B     = BUTTON_15
XBOX_X     = BUTTON_16
XBOX_Y     = BUTTON_17

# Web Gamepad API button index mapping (for Labs Vision bridge)
_BUTTON_TO_WEB_GAMEPAD = {
    BUTTON_14: 0,   # A → buttons[0]
    BUTTON_15: 1,   # B → buttons[1]
    BUTTON_16: 2,   # X → buttons[2]
    BUTTON_17: 3,   # Y → buttons[3]
    BUTTON_7:  4,   # LB → buttons[4]
    BUTTON_4:  5,   # RB → buttons[5]
    BUTTON_8:  6,   # LT → buttons[6]
    BUTTON_5:  7,   # RT → buttons[7]
    BUTTON_2:  8,   # View → buttons[8]
    BUTTON_3:  9,   # Menu → buttons[9]
    BUTTON_9:  10,  # LS → buttons[10]
    BUTTON_6:  11,  # RS → buttons[11]
    BUTTON_10: 12,  # DPad Up → buttons[12]
    BUTTON_11: 13,  # DPad Down → buttons[13]
    BUTTON_12: 14,  # DPad Left → buttons[14]
    BUTTON_13: 15,  # DPad Right → buttons[15]
    BUTTON_1:  16,  # Guide → buttons[16]
}

# ---------------------------------------------------------------------------
# Runtime State (managed by worker.py)
# ---------------------------------------------------------------------------

_lock = threading.Lock()
_physical_input = {}   # button/axis ID → float value
_virtual_output = {}   # button/axis ID → float value
_gcvdata_callback = None  # set by worker to forward gcvdata to C#
_emit_callback = None     # set by worker to emit gamepad events


def _set_input_state(button_id: int, value: float):
    """Called by the worker to update physical input state."""
    with _lock:
        _physical_input[button_id] = value


def _set_callbacks(emit_fn=None, gcvdata_fn=None):
    """Called by the worker to wire up output callbacks."""
    global _emit_callback, _gcvdata_callback
    _emit_callback = emit_fn
    _gcvdata_callback = gcvdata_fn


# ---------------------------------------------------------------------------
# Public API (matches Helios gtuner.pyd interface)
# ---------------------------------------------------------------------------

def get_actual(button: int) -> float:
    """
    Returns the physical controller input value for the given button/axis.
    Buttons: 0.0 to 100.0 (0 = released, 100 = fully pressed)
    Axes: -100.0 to +100.0
    """
    with _lock:
        return _physical_input.get(button, 0.0)


def get_val(button: int) -> float:
    """
    Returns the virtual (output) controller value for the given button/axis.
    Same range as get_actual().
    """
    with _lock:
        return _virtual_output.get(button, 0.0)


def set_val(button: int, value: float):
    """
    Sets the virtual output value for a button/axis.
    This queues the value to be sent on the next frame.
    """
    with _lock:
        _virtual_output[button] = value


def send_gcvdata(data: bytes):
    """
    Sends raw GCV data bytes to the C# host (Helios wire format).
    This is the low-level output path — most scripts should use set_val() instead.
    """
    if _gcvdata_callback:
        _gcvdata_callback(data)


# ---------------------------------------------------------------------------
# GCV Data Helpers
# ---------------------------------------------------------------------------

def gcvdata_to_gamepad_state(data: bytes) -> dict:
    """
    Converts a Helios-format gcvdata bytearray into a Web Gamepad API dict
    compatible with Labs Vision's emit() format.

    Helios gcvdata layout (24-32+ bytes):
      [0]: frame counter / misc
      [1]: BUTTON_1 (Guide)
      [2]: BUTTON_2 (View)
      [3]: BUTTON_3 (Menu)
      [4]: BUTTON_4 (RB)
      [5:8]: BUTTON_5 (RT) - 4 bytes big-endian signed (-100*0x10000 to +100*0x10000)
      [9]: BUTTON_6 (RS)
      [10]: BUTTON_7 (LB)
      [11:14]: BUTTON_8 (LT) - 4 bytes big-endian signed
      [15]: BUTTON_9 (LS)
      [16]: BUTTON_10 (DPad Up)
      [17]: BUTTON_11 (DPad Down)
      [18]: BUTTON_12 (DPad Left)
      [19]: BUTTON_13 (DPad Right)
      [20]: BUTTON_14 (A)
      [21]: BUTTON_15 (B)
      [22]: BUTTON_16 (X)
      [23]: BUTTON_17 (Y)
      [24:27]: Left Stick X (4 bytes, big-endian signed, scale /0x10000)
      [28:31]: Left Stick Y
      [32:35]: Right Stick X
      [36:39]: Right Stick Y
    """
    buttons = [False] * 17
    axes = [0.0, 0.0, 0.0, 0.0]

    if len(data) < 24:
        return {"axes": axes, "buttons": buttons}

    # Simple button bytes (0-100 range, >50 = pressed)
    simple_buttons = {
        1: 16,   # BUTTON_1 → Guide (index 16)
        2: 8,    # BUTTON_2 → View (index 8)
        3: 9,    # BUTTON_3 → Menu (index 9)
        4: 5,    # BUTTON_4 → RB (index 5)
        9: 11,   # BUTTON_6 → RS (index 11)
        10: 4,   # BUTTON_7 → LB (index 4)
        15: 10,  # BUTTON_9 → LS (index 10)
        16: 12,  # BUTTON_10 → DPad Up (index 12)
        17: 13,  # BUTTON_11 → DPad Down (index 13)
        18: 14,  # BUTTON_12 → DPad Left (index 14)
        19: 15,  # BUTTON_13 → DPad Right (index 15)
        20: 0,   # BUTTON_14 → A (index 0)
        21: 1,   # BUTTON_15 → B (index 1)
        22: 2,   # BUTTON_16 → X (index 2)
        23: 3,   # BUTTON_17 → Y (index 3)
    }

    for byte_idx, gamepad_idx in simple_buttons.items():
        if byte_idx < len(data):
            buttons[gamepad_idx] = data[byte_idx] > 50

    # Triggers (4-byte big-endian signed, scale = value * 0x10000)
    if len(data) >= 9:
        rt_raw = int.from_bytes(data[5:9], 'big', signed=True)
        rt_normalized = max(0.0, min(1.0, rt_raw / (100.0 * 0x10000)))
        buttons[7] = rt_normalized > 0.5  # RT as button
        # RT value goes into button value, not axes

    if len(data) >= 15:
        lt_raw = int.from_bytes(data[11:15], 'big', signed=True)
        lt_normalized = max(0.0, min(1.0, lt_raw / (100.0 * 0x10000)))
        buttons[6] = lt_normalized > 0.5  # LT as button

    # Stick axes (4-byte big-endian signed each, scale = value * 0x10000)
    if len(data) >= 32:
        lx = int.from_bytes(data[24:28], 'big', signed=True) / (100.0 * 0x10000)
        ly = int.from_bytes(data[28:32], 'big', signed=True) / (100.0 * 0x10000)
        axes[0] = max(-1.0, min(1.0, lx))
        axes[1] = max(-1.0, min(1.0, ly))

    if len(data) >= 40:
        rx = int.from_bytes(data[32:36], 'big', signed=True) / (100.0 * 0x10000)
        ry = int.from_bytes(data[36:40], 'big', signed=True) / (100.0 * 0x10000)
        axes[2] = max(-1.0, min(1.0, rx))
        axes[3] = max(-1.0, min(1.0, ry))

    return {"axes": axes, "buttons": buttons}


def virtual_state_to_gamepad(output_state: dict) -> dict:
    """
    Converts the accumulated set_val() state into a Web Gamepad API dict.
    This is an alternative to gcvdata for scripts using set_val().
    """
    buttons = [False] * 17
    axes = [0.0, 0.0, 0.0, 0.0]

    for helios_id, web_idx in _BUTTON_TO_WEB_GAMEPAD.items():
        val = output_state.get(helios_id, 0.0)
        buttons[web_idx] = val > 50.0

    # Map axes: Helios -100..+100 → Web Gamepad -1..+1
    lx = output_state.get(STICK_2_X, 0.0) / 100.0
    ly = output_state.get(STICK_2_Y, 0.0) / 100.0
    rx = output_state.get(STICK_1_X, 0.0) / 100.0
    ry = output_state.get(STICK_1_Y, 0.0) / 100.0
    axes = [
        max(-1.0, min(1.0, lx)),
        max(-1.0, min(1.0, ly)),
        max(-1.0, min(1.0, rx)),
        max(-1.0, min(1.0, ry)),
    ]

    return {"axes": axes, "buttons": buttons}
