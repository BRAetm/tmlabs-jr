# nba2k_helper — reconstructed source
# Decompiled from: nba2k_helper.cp311-win_amd64.pyd
# Original: E:/PythonProjects/2kVision/nba2k/nba2k_helper.c  (Cython 3.0.12)
# Method: static string extraction + pefile + Frida runtime analysis

import os
import sys
import json
import time
import base64
import requests
import threading

# ── Module-level dependencies (downloaded at runtime) ─────────────────────────
# bot.cp311       → bot automation routines
# extra.cp311     → helper utilities
# gym.cp311       → training/dataset utilities
# icon.cp311      → icon detection
# shot.cp311      → shot meter CV
# skele.cp311     → skeleton/pose detection
# nba2k_gui.cp311 → UI overlay
# jajdecoder.cp   → stream decoder ("jaj" internal codename)
# jajdivert.cp    → network divert
# jajnet_gui.cp   → jajnet UI
# netx_gui.cp     → netX UI
# Premium_Dribbles.cp → premium dribble detection (Puma)
# Puma_Tools.cp   → Puma toolset
# decoderx.cp     → secondary decoder
# divertx.cp      → secondary divert
# controller.cp   → controller I/O

# CDN endpoints
CDN_BASE         = "https://2kv.inputsense.com/2k/"
CDN_METERS       = "https://2kv.inputsense.com/2k/meters/"
CDN_NET          = "https://2kv.inputsense.com/2k/net/"
CDN_NETX         = "https://2kv.inputsense.com/2k/netx/"
CDN_UI           = "https://2kv.inputsense.com/2k/ui/"
CDN_DRIBBLES     = "https://dricon.lnputsense.com/dribbles/"  # homoglyph: lnputsense
CDN_PUMA         = "https://puma.lnputsense.com/2KV/"
AUTH_ENDPOINT    = "https://auth.<REDACTED>"                  # partial - not fully recovered

DISCORD_WEBHOOK  = "https://discord.com/api/webhooks/1377534539030593576/<TOKEN>"
# NOTE: full token recovered from .pyd strings - not reproduced here


class Helper:
    """
    Main CV bot class for NBA 2K.
    Instantiated by GCVWorker in nba2k_main.py.
    Calls inference_core for ONNX/TensorRT detections.
    """

    def __init__(self):
        # Verify auth/session before allowing initialization
        # Error string: "Verify Failed (Helper)"
        self._verify_session()

        # Check + pull updates
        self._check_for_updates()

        # Download runtime module files from CDN
        self._download_files()

        # Load sub-modules
        self._load_submodules()

        # Initialize shot meter handler
        self.meter_handler = None  # from: meter_handler string
        self._load_meters()        # reads ./userdata/settings/meters/

        # Skeleton sub-system
        # Strings: skele_shooter, skele_releaser, skele_parser
        # Status: "UPGRADED SKELE WILL RETURN SOON"
        self.skele_shooter  = None
        self.skele_releaser = None
        self.skele_parser   = None

        # Bot name (shown in UI)
        self.bot_name = "2kVision"   # from: bot_name string

        # Networking (jajnet)
        self._init_jajnet()

    def _verify_session(self):
        """Check auth token. Raises RuntimeError('Verify Failed (Helper)') if invalid."""
        # Calls into inference_core's infcore_set_session / infcore_init
        # Uses session.dat AES-GCM decrypted token
        ...

    def _check_for_updates(self):
        """
        Downloads update manifest from CDN.
        String: check_for_updates
        Falls back to lnputsense.com (homoglyph) if primary fails.
        Error: "NY Download Failed. Server unreachable."
        """
        ...

    def _download_files(self):
        """
        Downloads missing runtime .cp311-win_amd64.pyd files from CDN.
        String: download_files, netx_files, meter_files
        Modules pulled: bot, extra, gym, icon, shot, skele, nba2k_gui,
                        ui_netx, netx_gui, jajnet_gui, jajdecoder, jajdivert
        Icons pulled from: CDN_BASE (2knet_icon.ico, 2kv_icon.ico, netx_icon.ico)
        Banners: nba2k_banner.png, netx_banner.png, 2knet_banner.png
        """
        ...

    def _load_submodules(self):
        """Dynamic import of downloaded .pyd files."""
        ...

    def _load_meters(self):
        """
        Loads shot meter configs from ./userdata/settings/meters/.
        String: meter_found, meter_files
        Each meter file maps visual region → timing window.
        """
        ...

    def _init_jajnet(self):
        """
        Initialises jajnet networking layer.
        jajnet.py + jajnet.dll + jajnet_gui.cp
        Used for real-time shot data relay / OOP detection.
        """
        ...

    # ── Main entry points ──────────────────────────────────────────────────────

    def run(self, frame):
        """
        Primary frame processing pipeline called by GCVWorker.process().
        Dispatches to meter_run, skele_run, icon_run, oop_run based on mode.
        Returns processed frame with overlays.
        """
        ...

    def meter_run(self, frame):
        """
        Shot meter detection.
        Uses: shot.cp311, meter_handler, userdata/settings/meters/ configs
        Reads meter region → detects fill level → triggers release timing.
        Skele sub: skele_shooter (determines shot window), skele_releaser (triggers).
        String: "Running version <X>"
        CDN: CDN_METERS for meter asset updates
        """
        ...

    def skele_run(self, frame):
        """
        Skeleton / player pose detection.
        Uses: skele.cp311 + inference_core (ONNX keypoint model)
        Components: skele_parser, skele_shooter, skele_releaser
        Status: "UPGRADED SKELE WILL RETURN SOON" (feature temporarily disabled)
        Player referenced: Ronnie2k (likely used as benchmark/calibration identity)
        """
        ...

    def icon_run(self, frame):
        """
        Icon detection — badges, boosts, animations on HUD.
        Uses: icon.cp311
        Assets: downloaded from CDN_BASE, CDN_DRIBBLES
        2knet_icon.ico, netx_icon.ico
        """
        ...

    def oop_run(self, frame):
        """
        Out-of-position detection.
        Uses: jajnet for real-time positioning data relay.
        Likely compares detected player positions vs expected defensive assignments.
        """
        ...

    def img_send_run(self, frame, img_name=None):
        """
        Sends a recap/screenshot to the Discord webhook for telemetry.
        String: img_name, send_recap_image
        Target: DISCORD_WEBHOOK (hardcoded in .pyd)
        Encodes frame → base64 PNG → multipart POST to webhook.
        The embedded base64 PNG (~1400 bytes) in the .pyd is a 90x75 icon.
        """
        # Approximate reconstruction:
        _, img_encoded = cv2.imencode('.png', frame)
        img_b64 = base64.b64encode(img_encoded.tobytes()).decode()
        payload = {
            "username": "2kVision",
            "embeds": [{
                "image": {"url": f"attachment://{img_name or 'recap.png'}"},
            }]
        }
        files = {"file": (img_name or "recap.png", img_encoded.tobytes(), "image/png")}
        requests.post(DISCORD_WEBHOOK, data={"payload_json": json.dumps(payload)}, files=files)

    # ── Generator (vs / vs.genexpr) ───────────────────────────────────────────
    def vs(self):
        """
        Generator — likely 'versus' mode streaming comparison.
        Strings: nba2k_helper.vs, nba2k_helper.vs.genexpr
        Yields per-frame comparison results.
        """
        ...


# ── stringToRGB utility (seen in .pyd strings) ────────────────────────────────
def stringToRGB(s: str) -> tuple:
    """Convert color string '#RRGGBB' or 'R,G,B' to tuple."""
    if s.startswith('#'):
        r = int(s[1:3], 16)
        g = int(s[3:5], 16)
        b = int(s[5:7], 16)
        return (r, g, b)
    return tuple(int(x) for x in s.split(','))
