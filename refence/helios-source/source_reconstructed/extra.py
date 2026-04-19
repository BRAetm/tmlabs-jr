# extra.py — reconstructed from extra.cp311-win_amd64.pyd
# Source: E:/PythonProjects/2kVision/nba2k/extra.c (Cython 3.0.12)

import time

BUTTON_5  = 5   # R1
BUTTON_7  = 7   # L1
BUTTON_17 = 17  # Triangle


class Releaser:
    """Handles shot button release timing."""

    def __init__(self, timing_value=50, modifier=0.0):
        self.timing_value = timing_value   # 0-100 scale
        self.modifier     = modifier

    def run(self, fill_pct, gcvdata):
        """
        Fire release when fill_pct reaches timing threshold.
        timing_value mapped to 0.0-1.0 fill range.
        """
        threshold = self.timing_value / 100.0 + self.modifier
        if fill_pct is not None and fill_pct >= threshold:
            gcvdata.release_shot()
            return True
        return False


class Rhythm:
    """Rhythm timing engine — tempo-based shot release."""

    def __init__(self, tempo=29, direction='Down', enabled=False):
        self.tempo     = tempo       # BPM-style tempo value
        self.direction = direction   # 'Down' or 'Up'
        self.enabled   = enabled
        self._beat_ms  = 60000 / max(tempo, 1)
        self._last_beat = 0.0

    def calculate_ending_position(self, start_time, frame_rate=60):
        """Calculate where meter will be at beat end given start time."""
        elapsed = (time.monotonic() - start_time) * 1000
        beats   = elapsed / self._beat_ms
        position = beats % 1.0
        if self.direction == 'Down':
            return 1.0 - position
        return position

    def is_on_beat(self):
        now = time.monotonic() * 1000
        return (now - self._last_beat) >= self._beat_ms


class Skeleaser:
    """
    Combined skeleton + releaser — skeleton-driven release timing.
    Bridges skele.Shooter output → Releaser trigger.
    """

    def __init__(self, shooter, releaser):
        self.shooter  = shooter
        self.releaser = releaser

    def run(self, frame, player_detection, gcvdata):
        result = self.shooter.run(frame, player_detection)
        if result == 'release':
            self.releaser.run(1.0, gcvdata)
        elif result == 'at_peak':
            # Use rhythm timing if enabled
            pass
        return result


class Dunk:
    """Contact dunk shortcut handler."""

    def __init__(self, buttons=None):
        self.buttons = buttons or []

    def check_shortcuts(self, gcvdata):
        """
        Check if dunk shortcut buttons are held.
        Assign contact dunk shortcut to buttons of your choice.
        """
        for btn, val in self.buttons:
            if gcvdata.get_button(btn) < val:
                return False
        return len(self.buttons) > 0
