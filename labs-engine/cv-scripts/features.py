"""
PSControllerBridge — controller-output features reverse-engineered from ui.dll.

Constants sourced from Nuitka bytecode (0x28D5xxx region), hex-verified:
  offset 0x28D5EA9  stamina_scale         = 0.70
  offset 0x28D5F7C  _defense_boost_amount = 1.30  (a_defense_boost_amount)
  offset 0x28D5F9C  _defense_lateral_scale= 1.20  (a_defense_lateral_scale)
  offset 0x28D5FBD  _defense_r2_cap       = 0.40  (a_defense_r2_cap)
  offset 0x28D71B5  _defense_flick_dur    = 0.080 (80ms contest RS-up)
  offset 0x28D5DEC  _poll_rate            = 500   (Hz passthrough)
  offset 0x28D5E30  tempo_ms              = 39
  offset 0x28D5E83  stick_tempo_enabled   (attr name)
  offset 0x028D5EC1 quickstop_enabled     (attr name)
"""
import time
import threading

try:
    import vgamepad as vg
    VG_OK = True
except ImportError:
    VG_OK = False

# ── confirmed constants from bytecode ─────────────────────────────────────────
_STAMINA_SCALE   = 0.70    # stamina_scale — R2 x 0.70 to prevent drain
_DEFENSE_BOOST   = 1.30    # _defense_boost_amount — sensitivity x1.30 in defense
_LATERAL_SCALE   = 1.20    # _defense_lateral_scale — lateral RS x1.20 in defense
_R2_CAP          = 0.40    # _defense_r2_cap — R2 max 0.40 in defense mode
_CONTEST_DUR     = 0.080   # contest flick RS-up hold (seconds)
_CONTEST_COOL    = 0.600   # refractory after a contest flick
_POLL_RATE       = 500     # Hz passthrough rate

# XInput wButtons -> XUSB_BUTTON mapping (bit values are identical)
_XI_BTNS = []
if VG_OK:
    _XI_BTNS = [
        (0x0001, vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_UP),
        (0x0002, vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_DOWN),
        (0x0004, vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_LEFT),
        (0x0008, vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_RIGHT),
        (0x0010, vg.XUSB_BUTTON.XUSB_GAMEPAD_START),
        (0x0020, vg.XUSB_BUTTON.XUSB_GAMEPAD_BACK),
        (0x0040, vg.XUSB_BUTTON.XUSB_GAMEPAD_LEFT_THUMB),
        (0x0080, vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_THUMB),
        (0x0100, vg.XUSB_BUTTON.XUSB_GAMEPAD_LEFT_SHOULDER),
        (0x0200, vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_SHOULDER),
        (0x1000, vg.XUSB_BUTTON.XUSB_GAMEPAD_A),
        (0x2000, vg.XUSB_BUTTON.XUSB_GAMEPAD_B),
        (0x4000, vg.XUSB_BUTTON.XUSB_GAMEPAD_X),
        (0x8000, vg.XUSB_BUTTON.XUSB_GAMEPAD_Y),
    ]


class PSControllerBridge:
    """
    Wraps a VDS4Gamepad + VX360Gamepad and applies defense AI / stamina transforms
    before each update, mirroring PSControllerBridge._apply_defense_ai from ui.dll.
    VX360 runs alongside DS4 so Xbox Remote Play receives XInput shots.
    """

    def __init__(self):
        self.defense_enabled    = False
        self.infinite_stamina   = False

        # sub-feature toggles (exact attribute names from bytecode)
        self.anti_blowby        = True   # defense_anti_blowby
        self.auto_hands_up      = True   # defense_auto_hands_up
        self.contest_assist     = True   # defense_contest_assist
        self.lateral_boost      = True   # defense_lateral_boost
        self.sensitivity_boost  = True   # defense_sensitivity_boost

        # stick tempo: threaded RS-down flick before R2 fire
        self.stick_tempo_enabled = False
        self._tempo_active       = False
        self._tempo_rs_override  = None  # (rx, ry) or None
        self._tempo_lock         = threading.Lock()
        self._tempo_cool_end     = 0.0   # 1000ms cooldown after each stick tempo fire

        # quickstop: RS pull-down + brief pause before R2 fire
        self.quickstop_enabled   = False
        self._qs_square_held     = False
        self._qs_flicked         = False

        # virtual gamepads
        self.ds4  = None
        self.x360 = None
        if VG_OK:
            self.ds4  = vg.VDS4Gamepad()
            self.x360 = vg.VX360Gamepad()
            self.ds4.reset();  self.ds4.update()
            self.x360.reset(); self.x360.update()
            # OPTIONS pulse so Chiaki/SDL detects DS4
            self.ds4.press_button(vg.DS4_BUTTONS.DS4_BUTTON_OPTIONS); self.ds4.update()
            time.sleep(0.05)
            self.ds4.release_button(vg.DS4_BUTTONS.DS4_BUTTON_OPTIONS); self.ds4.update()

        self._contest_end  = 0.0
        self._contesting   = False
        self._contest_lock = threading.Lock()

        # DPAD_UP quickstop in-game toggle
        self._dpad_up_prev        = False
        self._qs_toggle_requested = False

    # ── public API ─────────────────────────────────────────────────────────────

    def passthrough(self, gp):
        """Apply features and push state to both DS4 and VX360.  gp = XInput GAMEPAD."""
        if not self.ds4 or not gp:
            return

        lx = gp.sThumbLX / 32768.0
        ly = gp.sThumbLY / 32768.0
        rx = gp.sThumbRX / 32768.0
        ry = gp.sThumbRY / 32768.0
        lt = gp.bLeftTrigger  / 255.0
        rt = gp.bRightTrigger / 255.0

        if self.defense_enabled:
            rx, ry, lt, rt = self._apply_defense_ai(rx, ry, lt, rt)

        if self.infinite_stamina:
            rt = self._apply_stamina(rt)

        # contest flick overrides RS-up
        with self._contest_lock:
            if self._contesting and time.perf_counter() < self._contest_end:
                ry = 1.0
            else:
                self._contesting = False

        # stick_tempo / quickstop RS override
        with self._tempo_lock:
            if self._tempo_rs_override is not None:
                rx, ry = self._tempo_rs_override

        # push to DS4 (PS Remote Play)
        self.ds4.left_joystick_float(lx, ly)
        self.ds4.right_joystick_float(rx, ry)
        self.ds4.left_trigger_float(lt)
        self.ds4.right_trigger_float(rt)
        self.ds4.update()

        # push to VX360 (Xbox Remote Play) — full mirror including buttons
        if self.x360:
            self.x360.left_joystick_float(lx, ly)
            self.x360.right_joystick_float(rx, ry)
            self.x360.left_trigger_float(lt)
            self.x360.right_trigger_float(rt)
            for xi_bit, xusb_btn in _XI_BTNS:
                if gp.wButtons & xi_bit:
                    self.x360.press_button(xusb_btn)
                else:
                    self.x360.release_button(xusb_btn)
            self.x360.update()

        # DPAD_UP rising edge → signal quickstop toggle (labs2kmain relays to engine)
        dpad_up = bool(gp.wButtons == 0x0001)  # DPAD_UP alone, no other buttons
        if dpad_up and not self._dpad_up_prev:
            self._qs_toggle_requested = True
        self._dpad_up_prev = dpad_up

    def fire_shot(self, l2=False, tempo=False, tempo_ms=39):
        """Fire R2 for a shot release. Routes through stick_tempo or quickstop if enabled."""
        if not self.ds4:
            return
        if self.stick_tempo_enabled and not l2:
            if time.perf_counter() < self._tempo_cool_end:
                return
            threading.Thread(target=self._stick_tempo_fire,
                             args=(tempo_ms,), daemon=True).start()
            return
        if self.quickstop_enabled and not l2:
            threading.Thread(target=self._quickstop_fire, daemon=True).start()
            return
        if tempo and not l2:
            time.sleep(tempo_ms / 1000.0)
        self._fire_rt()

    def contest_flick(self):
        """
        defense_contest_assist: RS-up flick (80ms) when shot meter fires while defending.
        After the flick, _CONTEST_COOL refractory prevents spam.
        """
        if not self.contest_assist:
            return
        now = time.perf_counter()
        if now < self._contest_end + _CONTEST_COOL:
            return
        with self._contest_lock:
            self._contesting  = True
            self._contest_end = now + _CONTEST_DUR

    # ── private ────────────────────────────────────────────────────────────────

    def _fire_rt(self):
        """Send a 30ms R2 pulse on both DS4 and VX360 (_release_duration = 0.030 from bytecode)."""
        self.ds4.right_trigger_float(1.0); self.ds4.update()
        if self.x360:
            self.x360.right_trigger_float(1.0); self.x360.update()
        time.sleep(0.030)
        self.ds4.right_trigger_float(0.0); self.ds4.update()
        if self.x360:
            self.x360.right_trigger_float(0.0); self.x360.update()

    def _stick_tempo_fire(self, tempo_ms: int):
        """
        stick_tempo sequence from bytecode TEMPO log:
          release Square -> RS DOWN flick for tempo_ms -> fire R2 -> 1000ms cooldown.
        Runs in a side thread; passthrough picks up RS override each tick.
        """
        # release Square (PS4 Square = game shot button in some setups)
        if VG_OK:
            self.ds4.release_button(vg.DS4_BUTTONS.DS4_BUTTON_SQUARE)
            self.ds4.update()
        with self._tempo_lock:
            self._tempo_rs_override = (0.0, -1.0)
            self._tempo_active = True
        time.sleep(tempo_ms / 1000.0)
        with self._tempo_lock:
            self._tempo_rs_override = None
            self._tempo_active = False
        self._fire_rt()
        self._tempo_cool_end = time.perf_counter() + 1.0  # 1000ms cooldown

    def _quickstop_fire(self):
        """
        quickstop sequence: brief RS pull-down (20ms) → neutral (15ms) → R2.
        Mimics the RS flick that triggers quickstop footwork in 2K.
        """
        with self._tempo_lock:
            self._tempo_rs_override = (0.0, -1.0)
            self._qs_flicked = True
        time.sleep(0.020)
        with self._tempo_lock:
            self._tempo_rs_override = (0.0, 0.0)
        time.sleep(0.015)
        with self._tempo_lock:
            self._tempo_rs_override = None
            self._qs_flicked = False
        self._fire_rt()

    def _apply_defense_ai(self, rx, ry, lt, rt):
        """
        Mirrors PSControllerBridge._apply_defense_ai from ui.dll.

        lateral_boost     -> _defense_lateral_scale (1.20) on RX only
        sensitivity_boost -> _defense_boost_amount  (1.30) on RY only
        anti_blowby       -> _defense_r2_cap        (0.40) caps RT
        """
        if self.lateral_boost and abs(rx) > 0.05:
            rx = max(-1.0, min(1.0, rx * _LATERAL_SCALE))
        if self.sensitivity_boost and abs(ry) > 0.05:
            ry = max(-1.0, min(1.0, ry * _DEFENSE_BOOST))
        if self.anti_blowby:
            rt = min(rt, _R2_CAP)
        return rx, ry, lt, rt

    def _apply_stamina(self, rt):
        """stamina_scale = 0.70: R2 x 0.70 so game never drains stamina fully."""
        return rt * _STAMINA_SCALE
