"""
controller_ps.py - DualShock 4 / PS virtual controller
Reconstructed from decompiled controller_ps module in ui.dll
"""
import vgamepad as vg


class PSController:
    def __init__(self):
        self._pad = vg.VDS4Gamepad()
        self._pad.reset()
        self._pad.update()

    def move_right_stick(self, dx: float, dy: float):
        x = max(-1.0, min(1.0, dx))
        y = max(-1.0, min(1.0, dy))
        self._pad.right_joystick_float(x_value_float=x, y_value_float=-y)
        self._pad.update()

    def press_right_trigger(self, value: float = 1.0):
        self._pad.right_trigger_float(value_float=value)
        self._pad.update()

    def release_right_trigger(self):
        self._pad.right_trigger_float(value_float=0.0)
        self._pad.update()

    def reset(self):
        self._pad.reset()
        self._pad.update()
