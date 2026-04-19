"""
2K Vision Pro — Full CV script with settings GUI

Loads inside Labs Vision via the GCVWorker pattern.
When started, spawns a PyQt6 settings window for configuring
shot algorithm, meter type, controller mods, etc.

Usage: Select this script in Labs Vision and press Run.
"""

import os
import sys
import json
import time
import threading
import numpy as np
import cv2
from helios_compat import *

# ---------------------------------------------------------------------------
# DIRECT CAPTURE — bypasses C# JPEG pipeline for 3-6x better FPS
# Uses dxcam (GPU) or mss (CPU) to grab the screen directly.
# Worker.py reads this flag and switches to the fast capture loop.
# ---------------------------------------------------------------------------
DIRECT_CAPTURE = True

# ---------------------------------------------------------------------------
# Settings
# ---------------------------------------------------------------------------

SETTINGS_DIR = os.path.join(os.path.dirname(__file__), "2k_vision_pro_data")
SETTINGS_FILE = os.path.join(SETTINGS_DIR, "settings.json")

DEFAULT_SETTINGS = {
    "meter_type": "auto",
    "shot_algo": "ztrig",
    "skele_timing": 0.0,
    "skele_enabled": False,
    "skele_mode": "Premium_Dribbles",
    "x_enabled": False,
    "builder_enabled": False,
    "algo": "distance",
    "timing_offset_ms": 0,
    "overlay_enabled": True,
    "no_meter_lock_enabled": True,
    "star_pixel_enable": True,
    "star_pixel_x_norm": 0.50,
    "star_pixel_y_norm": 0.85,
    "star_pixel_radius": 6,
    "pixel_v_thresh": 210,
    "pixel_confirm_frames": 1,
    "stamina_enabled": True,
    "stamina_scale": 70,
    "stamina_deadzone": 18.0,
    "shooting_enabled": False,
    "shooting_trigger": "L1",
    "defense_enabled": True,
    "defense_profile": "LOCKDOWN",
}


def load_settings():
    os.makedirs(SETTINGS_DIR, exist_ok=True)
    if os.path.isfile(SETTINGS_FILE):
        try:
            with open(SETTINGS_FILE, "r") as f:
                s = json.load(f)
                merged = dict(DEFAULT_SETTINGS)
                merged.update(s)
                return merged
        except Exception:
            pass
    return dict(DEFAULT_SETTINGS)


def save_settings(settings):
    os.makedirs(SETTINGS_DIR, exist_ok=True)
    with open(SETTINGS_FILE, "w") as f:
        json.dump(settings, f, indent=2)


# ---------------------------------------------------------------------------
# Meter configs (from Helios nba2k_settings)
# ---------------------------------------------------------------------------

METERS = {
    "straight": {
        "name": "Straight", "width_range": (2, 4), "height_range": (43, 217),
        "colors": {"purple": {"low": [190, 0, 190], "high": [255, 60, 255]},
                   "white": {"low": [235, 235, 235], "high": [255, 255, 255]}},
        "timing_ms": 50, "confidence_init": 1.0, "confidence_min": 0.95, "confidence_decay": 0.01,
    },
    "arrow2": {
        "name": "Arrow2", "width_range": (23, 30), "height_range": (33, 165),
        "colors": {"purple": {"low": [216, 46, 234], "high": [222, 52, 240]},
                   "white": {"low": [240, 240, 240], "high": [255, 255, 255]}},
        "timing_ms": 48, "confidence_init": 1.0, "confidence_min": 0.95, "confidence_decay": 0.01,
    },
    "pill": {
        "name": "Pill", "width_range": (6, 9), "height_range": (32, 160),
        "colors": {"purple": {"low": [190, 0, 190], "high": [255, 60, 255]}},
        "timing_ms": 50, "confidence_init": 1.0, "confidence_min": 0.95, "confidence_decay": 0.01,
    },
    "sword": {
        "name": "Sword", "width_range": (19, 24), "height_range": (37, 186),
        "colors": {"purple": {"low": [220, 0, 220], "high": [255, 60, 255]}},
        "timing_ms": 50, "confidence_init": 1.0, "confidence_min": 0.95, "confidence_decay": 0.01,
    },
    "dial": {
        "name": "Dial", "width_range": (13, 53), "height_range": (13, 26),
        "colors": {"purple": {"low": [190, 0, 190], "high": [255, 60, 255]},
                   "yellow": {"low": [0, 190, 190], "high": [60, 255, 255]}},
        "timing_ms": 40, "confidence_init": 1.0, "confidence_min": 0.70, "confidence_decay": 0.1,
    },
    "arrow": {
        "name": "Arrow", "width_range": (37, 45), "height_range": (18, 24),
        "colors": {"purple": {"low": [190, 0, 190], "high": [255, 60, 255]}},
        "timing_ms": 40, "confidence_init": 1.0, "confidence_min": 0.80, "confidence_decay": 0.1,
    },
    "2kol2": {
        "name": "2K OL2", "width_range": (2, 4), "height_range": (34, 249),
        "colors": {"white": {"low": [225, 225, 225], "high": [255, 255, 255]}},
        "timing_ms": 40, "confidence_init": 1.0, "confidence_min": 0.75, "confidence_decay": 0.03,
    },
}

SEARCH_REGION = {"left": 5, "top": 250, "right": 1915, "bottom": 770}


# ---------------------------------------------------------------------------
# Meter Detector
# ---------------------------------------------------------------------------

class MeterDetector:
    def __init__(self, config):
        self.config = config
        self.name = config["name"]
        self.confidence = 0.0
        self.last_pos = None
        self.tracking = False
        self.detect_time = 0.0
        self.positions = []

    def detect(self, frame, roi):
        w_min, w_max = self.config["width_range"]
        h_min, h_max = self.config["height_range"]
        best_match = None
        best_area = 0

        for color_name, color_range in self.config["colors"].items():
            low = np.array(color_range["low"], dtype=np.uint8)
            high = np.array(color_range["high"], dtype=np.uint8)
            mask = cv2.inRange(roi, low, high)
            contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

            for cnt in contours:
                x, y, w, h = cv2.boundingRect(cnt)
                if w_min <= w <= w_max and h_min <= h <= h_max:
                    area = w * h
                    if area > best_area:
                        best_area = area
                        best_match = (x, y, w, h)

        if best_match:
            if not self.tracking:
                self.tracking = True
                self.detect_time = time.monotonic()
                self.confidence = self.config["confidence_init"]
                self.positions = []
            self.last_pos = best_match
            self.positions.append((time.monotonic(), best_match))
            if len(self.positions) > 30:
                self.positions = self.positions[-30:]
            return True, best_match, self.confidence
        else:
            if self.tracking:
                self.confidence -= self.config["confidence_decay"]
                if self.confidence < self.config["confidence_min"]:
                    self.tracking = False
                    self.confidence = 0.0
            return False, None, self.confidence

    def get_velocity(self):
        if len(self.positions) < 2:
            return 0.0
        t0, p0 = self.positions[0]
        t1, p1 = self.positions[-1]
        dt = t1 - t0
        if dt <= 0:
            return 0.0
        dy = abs(p1[1] - p0[1])
        return dy / dt


# ---------------------------------------------------------------------------
# Settings GUI (PyQt6) — spawned in a thread
# ---------------------------------------------------------------------------

SHOT_ALGORITHMS = {
    "LB/L1": "Uses the left-bumper timing window.",
    "Just Play": "Automatic release at the optimal frame.",
    "Stick Only": "Release determined by right-stick position.",
    "Button": "Standard button (square/X) release.",
}

ALGO_OPTIONS = [
    ("speed", "Speed — release when remaining meter distance <= timing value"),
    ("distance", "Distance — release when bar travel distance <= timing value"),
    ("ztrig", "Z Trigger — speed + distance + bar stall detection (recommended)"),
    ("skeleton", "Skeleton — pose-based release detecting ball leaving wrist"),
]

SKELE_MODES = {
    "Premium_Dribbles": "Dribble-move detection using the premium model.",
    "Post Game": "Post-move detection model for back-to-basket plays.",
}

CLAMP_PROFILES = {
    "LOCKDOWN": {"HOLD_THRESHOLD": 14, "LERP_RATE": 0.30, "Y_DAMPEN": 0.45, "RS_COOLDOWN_TIME": 40},
    "BALANCED": {"HOLD_THRESHOLD": 20, "LERP_RATE": 0.22, "Y_DAMPEN": 0.60, "RS_COOLDOWN_TIME": 30},
    "LOOSE":    {"HOLD_THRESHOLD": 28, "LERP_RATE": 0.15, "Y_DAMPEN": 0.80, "RS_COOLDOWN_TIME": 20},
}


def _run_gui(settings):
    """Spawn a tkinter settings window. Runs in its own thread."""
    try:
        import tkinter as tk
        from tkinter import ttk, messagebox

        root = tk.Tk()
        root.title("2K Vision Pro \u2014 Settings")
        root.geometry("580x520")
        root.configure(bg="#2e2f30")
        root.attributes("-topmost", True)
        root.after(500, lambda: root.attributes("-topmost", False))

        style = ttk.Style()
        style.theme_use("clam")
        style.configure(".", background="#2e2f30", foreground="#e0e0e0", font=("Segoe UI", 9))
        style.configure("TNotebook", background="#2e2f30")
        style.configure("TNotebook.Tab", background="#38393a", foreground="#a0a0a0",
                         padding=[10, 5], font=("Segoe UI", 9, "bold"))
        style.map("TNotebook.Tab", background=[("selected", "#4a4b4c")],
                  foreground=[("selected", "#e0e0e0")])
        style.configure("TFrame", background="#38393a")
        style.configure("TLabel", background="#38393a", foreground="#e0e0e0")
        style.configure("TLabelframe", background="#38393a", foreground="#e0e0e0")
        style.configure("TLabelframe.Label", background="#4a4b4c", foreground="#e0e0e0",
                         font=("Segoe UI", 9, "bold"))
        style.configure("TButton", background="#55aaff", foreground="#ffffff",
                         font=("Segoe UI", 9, "bold"), padding=[12, 5])
        style.map("TButton", background=[("active", "#66bbff")])
        style.configure("TCheckbutton", background="#38393a", foreground="#e0e0e0")
        style.configure("TCombobox", fieldbackground="#38393a", foreground="#e0e0e0")
        style.configure("TSpinbox", fieldbackground="#38393a", foreground="#e0e0e0")

        nb = ttk.Notebook(root)
        nb.pack(fill="both", expand=True, padx=4, pady=4)

        # ── Shooting tab ──
        f1 = ttk.Frame(nb)
        nb.add(f1, text="Shooting")
        meter_lf = ttk.LabelFrame(f1, text="Meter")
        meter_lf.pack(fill="x", padx=8, pady=4)
        for lbl in ["Distance", "Speed", "Timing"]:
            ttk.Label(meter_lf, text=lbl).pack(anchor="w", padx=8, pady=2)
        algo_lf = ttk.LabelFrame(f1, text="Release Algorithm")
        algo_lf.pack(fill="x", padx=8, pady=4)
        algo_text = tk.Text(algo_lf, height=3, bg="#38393a", fg="#e0e0e0", wrap="word",
                            font=("Segoe UI", 9), relief="flat", state="disabled")
        algo_text.pack(fill="x", padx=8, pady=4)
        rhythm_lf = ttk.LabelFrame(f1, text="Rhythm")
        rhythm_lf.pack(fill="x", padx=8, pady=4)
        ttk.Combobox(rhythm_lf, values=["Normal", "Slow", "Fast"]).pack(padx=8, pady=2)

        # ── Algorithm tab ──
        f2 = ttk.Frame(nb)
        nb.add(f2, text="Algorithm")
        algo_lf2 = ttk.LabelFrame(f2, text="Shot Algorithm")
        algo_lf2.pack(fill="x", padx=8, pady=8)
        ttk.Label(algo_lf2, text="Algorithm:").pack(anchor="w", padx=8, pady=2)
        algo_var = tk.StringVar(value=settings.get("algo", "distance").capitalize())
        algo_cb = ttk.Combobox(algo_lf2, textvariable=algo_var,
                                values=[k.capitalize() for k, _ in ALGO_OPTIONS], state="readonly")
        algo_cb.pack(fill="x", padx=8, pady=2)
        algo_desc_var = tk.StringVar()
        ttk.Label(algo_lf2, textvariable=algo_desc_var, wraplength=500).pack(padx=8, pady=4)
        def on_algo_sel(_=None):
            idx = algo_cb.current()
            if 0 <= idx < len(ALGO_OPTIONS):
                algo_desc_var.set(ALGO_OPTIONS[idx][1])
        algo_cb.bind("<<ComboboxSelected>>", on_algo_sel)
        on_algo_sel()
        def save_algo():
            idx = algo_cb.current()
            settings["algo"] = ALGO_OPTIONS[idx][0] if 0 <= idx < len(ALGO_OPTIONS) else "distance"
            save_settings(settings)
            messagebox.showinfo("Saved", "Algorithm saved.", parent=root)
        ttk.Button(f2, text="Save Algorithm", command=save_algo).pack(padx=8, pady=8)

        # ── No-Meter tab ──
        f3 = ttk.Frame(nb)
        nb.add(f3, text="No-Meter")
        nm_lf = ttk.LabelFrame(f3, text="No-Meter Star Lock")
        nm_lf.pack(fill="x", padx=8, pady=4)
        nm_en = tk.BooleanVar(value=settings.get("no_meter_lock_enabled", True))
        star_en = tk.BooleanVar(value=settings.get("star_pixel_enable", True))
        ttk.Checkbutton(nm_lf, text="Enable No-Meter Lock", variable=nm_en).pack(anchor="w", padx=8, pady=2)
        ttk.Checkbutton(nm_lf, text="Enable Star Pixel Detection", variable=star_en).pack(anchor="w", padx=8, pady=2)
        det_lf = ttk.LabelFrame(f3, text="Detection Settings")
        det_lf.pack(fill="x", padx=8, pady=4)
        vt_var = tk.IntVar(value=settings.get("pixel_v_thresh", 210))
        cf_var = tk.IntVar(value=settings.get("pixel_confirm_frames", 1))
        fr = ttk.Frame(det_lf)
        fr.pack(fill="x", padx=8, pady=2)
        ttk.Label(fr, text="Brightness (V):").pack(side="left")
        ttk.Spinbox(fr, from_=100, to=255, textvariable=vt_var, width=6).pack(side="left", padx=4)
        ttk.Label(fr, text="  Confirm Frames:").pack(side="left")
        ttk.Spinbox(fr, from_=1, to=10, textvariable=cf_var, width=4).pack(side="left", padx=4)
        def save_nm():
            settings["no_meter_lock_enabled"] = nm_en.get()
            settings["star_pixel_enable"] = star_en.get()
            settings["pixel_v_thresh"] = vt_var.get()
            settings["pixel_confirm_frames"] = cf_var.get()
            save_settings(settings)
            messagebox.showinfo("Saved", "No-Meter config saved.", parent=root)
        ttk.Button(f3, text="Save No-Meter Config", command=save_nm).pack(padx=8, pady=8)

        # ── Mods tab ──
        f4 = ttk.Frame(nb)
        nb.add(f4, text="Mods")
        stam_lf = ttk.LabelFrame(f4, text="Stamina Smoother")
        stam_lf.pack(fill="x", padx=8, pady=4)
        stam_en = tk.BooleanVar(value=settings.get("stamina_enabled", True))
        ttk.Checkbutton(stam_lf, text="Enable Unlimited Stamina", variable=stam_en).pack(anchor="w", padx=8, pady=2)
        shoot_lf = ttk.LabelFrame(f4, text="Shooting Switch")
        shoot_lf.pack(fill="x", padx=8, pady=4)
        shoot_en = tk.BooleanVar(value=settings.get("shooting_enabled", False))
        ttk.Checkbutton(shoot_lf, text="Enable Shooting Switch", variable=shoot_en).pack(anchor="w", padx=8, pady=2)
        def_lf = ttk.LabelFrame(f4, text="Defense Mirror (Clamp2K)")
        def_lf.pack(fill="x", padx=8, pady=4)
        def_en = tk.BooleanVar(value=settings.get("defense_enabled", True))
        ttk.Checkbutton(def_lf, text="Enable Defense Mirror", variable=def_en).pack(anchor="w", padx=8, pady=2)
        prof_var = tk.StringVar(value=settings.get("defense_profile", "LOCKDOWN"))
        prof_fr = ttk.Frame(def_lf)
        prof_fr.pack(fill="x", padx=8, pady=2)
        ttk.Label(prof_fr, text="Profile:").pack(side="left")
        ttk.Combobox(prof_fr, textvariable=prof_var, values=list(CLAMP_PROFILES.keys()),
                      state="readonly", width=12).pack(side="left", padx=4)
        def save_mods():
            settings["stamina_enabled"] = stam_en.get()
            settings["shooting_enabled"] = shoot_en.get()
            settings["defense_enabled"] = def_en.get()
            settings["defense_profile"] = prof_var.get()
            save_settings(settings)
            messagebox.showinfo("Saved", "Controller mods saved.", parent=root)
        ttk.Button(f4, text="Save Controller Mods", command=save_mods).pack(padx=8, pady=8)

        print("[2K Vision Pro] Settings window opened.")
        sys.stdout.flush()
        root.mainloop()
        print("[2K Vision Pro] Settings window closed.")
        sys.stdout.flush()

    except Exception as e:
        print(f"[2K Vision Pro] GUI error: {e}")
        import traceback
        traceback.print_exc()
        sys.stdout.flush()


# ---------------------------------------------------------------------------
# GCVWorker — loaded by Labs Vision's worker.py
# ---------------------------------------------------------------------------

class GCVWorker:
    """NBA 2K shot meter detection + settings GUI."""

    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.gcvdata = bytearray(32)
        self.frame_count = 0
        self.settings = load_settings()

        # Meter detectors
        self.detectors = {name: MeterDetector(cfg) for name, cfg in METERS.items()}
        self.active_meter = None
        self.active_name = self.settings.get("meter_type", "auto")

        # Shot state
        self.timing_offset_ms = self.settings.get("timing_offset_ms", 0)
        self.shot_fired = False
        self.shot_cooldown = 0.0
        self.shots_taken = 0

        # Spawn settings GUI in background thread
        self._gui_thread = threading.Thread(target=_run_gui, args=(self.settings,), daemon=True)
        self._gui_thread.start()

        print(f"[2K Vision Pro] Initialized ({width}x{height}). Settings GUI launching...")
        sys.stdout.flush()

    def process(self, frame):
        self.gcvdata = bytearray(32)
        h, w = frame.shape[:2]
        self.frame_count += 1

        # Reload settings periodically (every 60 frames) to pick up GUI changes
        if self.frame_count % 60 == 0:
            try:
                self.settings = load_settings()
                self.active_name = self.settings.get("meter_type", "auto")
                self.timing_offset_ms = self.settings.get("timing_offset_ms", 0)
            except Exception:
                pass

        # Search region
        sx = max(0, min(SEARCH_REGION["left"], w))
        sy = max(0, min(SEARCH_REGION["top"], h))
        ex = max(0, min(SEARCH_REGION["right"], w))
        ey = max(0, min(SEARCH_REGION["bottom"], h))
        roi = frame[sy:ey, sx:ex] if ex > sx and ey > sy else frame

        # Detect meters
        active_detection = None
        active_detector = None

        if self.active_name == "auto":
            for name, detector in self.detectors.items():
                detected, bbox, conf = detector.detect(frame, roi)
                if detected and conf > 0:
                    if active_detection is None or conf > active_detection[2]:
                        active_detection = (detected, bbox, conf)
                        active_detector = detector
                        self.active_meter = name
        else:
            detector = self.detectors.get(self.active_name)
            if detector:
                detected, bbox, conf = detector.detect(frame, roi)
                if detected:
                    active_detection = (detected, bbox, conf)
                    active_detector = detector

        # Shot timing
        now = time.monotonic()

        if active_detection and active_detector and active_detector.tracking:
            detected, bbox, conf = active_detection
            if bbox:
                abs_x = sx + bbox[0]
                abs_y = sy + bbox[1]
                cv2.rectangle(frame, (abs_x, abs_y), (abs_x + bbox[2], abs_y + bbox[3]), (0, 255, 0), 2)
                bar_w = int(conf * 100)
                cv2.rectangle(frame, (abs_x, abs_y - 15), (abs_x + bar_w, abs_y - 5), (0, 255, 0), -1)
                cv2.putText(frame, f"{active_detector.name} ({conf:.2f})",
                            (abs_x, abs_y - 20), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)

            timing_ms = active_detector.config["timing_ms"] + self.timing_offset_ms
            elapsed_ms = (now - active_detector.detect_time) * 1000

            if not self.shot_fired and elapsed_ms >= timing_ms and now > self.shot_cooldown:
                self.shot_fired = True
                self.shot_cooldown = now + 0.5
                self.shots_taken += 1
                self.gcvdata[BUTTON_16] = 100  # X button
                cv2.putText(frame, "SHOT!", (w // 2 - 40, h // 2),
                            cv2.FONT_HERSHEY_SIMPLEX, 1.5, (0, 0, 255), 3)
        else:
            self.shot_fired = False

        # HUD
        if self.settings.get("overlay_enabled", True):
            self._draw_hud(frame, w, h, active_detector)

        return frame, self.gcvdata

    def _draw_hud(self, frame, w, h, active_detector):
        cv2.rectangle(frame, (5, 5), (320, 95), (0, 0, 0), -1)
        cv2.rectangle(frame, (5, 5), (320, 95), (80, 80, 80), 1)
        y = 22
        cv2.putText(frame, f"2K Vision Pro | Frame {self.frame_count}",
                    (12, y), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (200, 200, 200), 1)
        y += 18
        meter_name = self.active_meter or "scanning..."
        cv2.putText(frame, f"Meter: {meter_name}",
                    (12, y), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (100, 255, 100), 1)
        y += 18
        if active_detector and active_detector.tracking:
            vel = active_detector.get_velocity()
            cv2.putText(frame, f"Speed: {vel:.0f}px/s  Conf: {active_detector.confidence:.2f}",
                        (12, y), cv2.FONT_HERSHEY_SIMPLEX, 0.35, (200, 200, 100), 1)
        else:
            cv2.putText(frame, "Speed: -- Conf: --",
                        (12, y), cv2.FONT_HERSHEY_SIMPLEX, 0.35, (120, 120, 120), 1)
        y += 18
        timing = (active_detector.config["timing_ms"] + self.timing_offset_ms) if active_detector else 0
        cv2.putText(frame, f"Timing: {timing}ms  Shots: {self.shots_taken}  Algo: {self.settings.get('algo', '?')}",
                    (12, y), cv2.FONT_HERSHEY_SIMPLEX, 0.35, (180, 180, 220), 1)

    def __del__(self):
        print("[2K Vision Pro] Shutting down.")
