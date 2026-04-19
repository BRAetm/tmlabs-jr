"""
titan_bridge.py - Titan One / Titan Two hardware device bridge
Reconstructed from decompiled titan_bridge module in ui.dll
Titan devices allow modifying controller input at hardware level.
Requires: Titan One/Two device + ConsoleTuner GPC SDK DLL
"""
import ctypes
import os


class TitanBridge:
    """
    Communicates with a Titan One or Titan Two device via the
    ConsoleTuner GPC/GCAPI SDK (gcdapi.dll).
    """

    GCAPI_DLL = "gcdapi.dll"  # Must be in PATH or same directory

    # GCAPI output slot constants
    SLOT_RX   = 4   # Right stick X
    SLOT_RY   = 5   # Right stick Y
    SLOT_R2   = 9   # R2 / Right trigger

    def __init__(self):
        self._lib = None
        self._load_sdk()

    def _load_sdk(self):
        try:
            dll_path = os.path.join(os.path.dirname(__file__), self.GCAPI_DLL)
            if not os.path.exists(dll_path):
                dll_path = self.GCAPI_DLL  # Try system PATH
            self._lib = ctypes.CDLL(dll_path)
            self._lib.gcapi_Connect()
        except Exception as e:
            print(f"[!] TitanBridge: failed to load SDK — {e}")
            self._lib = None

    def move_right_stick(self, dx: float, dy: float):
        """dx, dy: -1.0 to 1.0 mapped to Titan -100 to 100."""
        if not self._lib:
            return
        x = int(max(-100, min(100, dx * 100)))
        y = int(max(-100, min(100, dy * 100)))
        try:
            self._lib.gcapi_SetOutput(self.SLOT_RX, x)
            self._lib.gcapi_SetOutput(self.SLOT_RY, y)
        except Exception:
            pass

    def press_right_trigger(self, value: float = 1.0):
        if not self._lib:
            return
        try:
            self._lib.gcapi_SetOutput(self.SLOT_R2, int(value * 100))
        except Exception:
            pass

    def release_right_trigger(self):
        self.press_right_trigger(0.0)

    def reset(self):
        if self._lib:
            try:
                self._lib.gcapi_Disconnect()
            except Exception:
                pass
