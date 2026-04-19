# nba2k_helper.py — reconstructed from nba2k_helper.cp311-win_amd64.pyd
# Source: E:/PythonProjects/2kVision/nba2k/nba2k_helper.c (Cython 3.0.12)

import os
import sys
import json
import time
import base64
import requests
import threading

CDN_BASE     = "https://2kv.inputsense.com/2k/"
CDN_METERS   = "https://2kv.inputsense.com/2k/meters/"
CDN_NET      = "https://2kv.inputsense.com/2k/net/"
CDN_NETX     = "https://2kv.inputsense.com/2k/netx/"
CDN_UI       = "https://2kv.inputsense.com/2k/ui/"
CDN_DRIBBLES = "https://dricon.lnputsense.com/dribbles/"
CDN_PUMA     = "https://puma.lnputsense.com/2KV/"
DISCORD_WEBHOOK = "https://discord.com/api/webhooks/1377534539030593576/<TOKEN>"

SETTINGS_DIR = "./userdata/settings"
METERS_DIR   = "./userdata/settings/meters"


def stringToRGB(s: str) -> tuple:
    if s.startswith('#'):
        return (int(s[1:3],16), int(s[3:5],16), int(s[5:7],16))
    return tuple(int(x) for x in s.split(','))


class Helper:
    """
    Main CV bot class. Instantiated by GCVWorker.
    Dispatches to meter/skele/icon/oop/img_send sub-systems.
    """

    def __init__(self):
        os.chdir(os.path.dirname(__file__) if '__file__' in dir() else '.')
        self._verify_session()
        self._check_for_updates()
        self._download_files()
        self._load_submodules()
        self._load_settings()
        self._load_meters()
        self._init_jajnet()

        # Shot engine
        self.skele_shooter  = None
        self.skele_releaser = None
        self.skele_parser   = None
        self.bot_name = "2kVision"
        self.frame_count = 0

    def _verify_session(self):
        """Raises RuntimeError('Verify Failed (Helper)') if session invalid."""
        pass  # calls inference_core session validation

    def _check_for_updates(self):
        """Download update manifest. Error: 'NY Download Failed. Server unreachable.'"""
        pass

    def _download_files(self):
        """Pull missing .pyd files from CDN."""
        pass

    def _load_submodules(self):
        self._shot   = None
        self._skele  = None
        self._icon   = None
        self._bot    = None
        self._extra  = None
        self._gym    = None
        try:
            import shot
            self._shot = shot
        except ImportError: pass
        try:
            import skele
            self._skele = skele
        except ImportError: pass
        try:
            import icon
            self._icon = icon
        except ImportError: pass
        try:
            import bot
            self._bot = bot
        except ImportError: pass
        try:
            import extra
            self._extra = extra
        except ImportError: pass

    def _load_settings(self):
        path = os.path.join(SETTINGS_DIR, "nba2k_settings.current")
        try:
            with open(path) as f:
                self.settings = json.load(f)
            self._settings_mtime = os.path.getmtime(path)
        except Exception:
            self.settings = {}
            self._settings_mtime = 0.0
        self._settings_path = path
        self.shot_cue = float(open(os.path.join(SETTINGS_DIR, "2kVision.txt")).read().strip()) \
            if os.path.exists(os.path.join(SETTINGS_DIR, "2kVision.txt")) else 3.85

    def _reload_if_changed(self):
        """Cheap mtime check; reload settings if labs2kmain wrote new values."""
        try:
            m = os.path.getmtime(self._settings_path)
        except OSError:
            return
        if m == self._settings_mtime:
            return
        try:
            with open(self._settings_path) as f:
                self.settings = json.load(f)
            self._settings_mtime = m
        except Exception:
            pass

    def _load_meters(self):
        """Load meter configs from ./userdata/settings/meters/."""
        self.meters = {}
        if not os.path.isdir(METERS_DIR):
            return
        for fname in os.listdir(METERS_DIR):
            if fname.endswith('.json'):
                name = fname[:-5]
                with open(os.path.join(METERS_DIR, fname)) as f:
                    self.meters[name] = json.load(f)

    def _init_jajnet(self):
        pass

    # ── Main entry points ─────────────────────────────────────────────────────

    def run(self, frame):
        """Primary dispatcher called by GCVWorker.process()."""
        self.frame_count += 1
        if self.frame_count % 30 == 0:
            self._reload_if_changed()
        mode = self.settings.get('mode', 'Online')
        meter_cfg = self.settings.get('meter', {})
        skele_cfg = self.settings.get('skele', {})
        x_cfg     = self.settings.get('x', {})

        result = frame.copy() if hasattr(frame, 'copy') else frame

        if meter_cfg.get('enabled'):
            result = self.meter_run(result)
        if skele_cfg.get('enabled'):
            result = self.skele_run(result)
        if x_cfg.get('enabled'):
            result = self.icon_run(result)

        return result

    def meter_run(self, frame):
        """Shot meter detection + release timing."""
        if self._shot is None or not self.meters:
            return frame
        shot_cfg = self.settings.get('meter', {}).get('types', {}).get('Shot', {})
        active_type = shot_cfg.get('enabled', 'Arrow2')
        meter_config = self.meters.get(active_type)
        if meter_config is None:
            return frame
        cls = getattr(self._shot, active_type, None)
        if cls is None:
            return frame
        meter = cls(meter_config)
        pct = meter.detect(frame)
        if pct is not None:
            timing = shot_cfg.get(active_type, {}).get('timing', {}).get('value', 50)
            threshold = timing / 100.0
            if pct >= threshold:
                pass  # trigger release via gcvdata
        return frame

    def skele_run(self, frame):
        """
        Skeleton pose detection + release.
        Status: UPGRADED SKELE WILL RETURN SOON
        """
        if self._skele is None:
            return frame
        # skele.Shooter + skele.Parser via inference_core
        return frame

    def icon_run(self, frame):
        """Ability icon detection (X mode)."""
        if self._icon is None:
            return frame
        x = self._icon.X(icon_type=self.settings.get('x', {}).get('icon', '3pt'))
        result = x.run(frame)
        if result == 'disappeared':
            pass  # trigger shot release
        return frame

    def oop_run(self, frame):
        """Out-of-position detection via jajnet."""
        return frame

    def img_send_run(self, frame, img_name=None):
        """Send recap frame to Discord webhook for telemetry."""
        try:
            import cv2
            _, buf = cv2.imencode('.png', frame)
            files   = {'file': (img_name or 'recap.png', buf.tobytes(), 'image/png')}
            payload = {'username': self.bot_name}
            requests.post(DISCORD_WEBHOOK,
                          data={'payload_json': json.dumps(payload)},
                          files=files, timeout=5)
        except Exception:
            pass

    def vs(self):
        """Generator — versus mode frame comparison."""
        while True:
            yield None
