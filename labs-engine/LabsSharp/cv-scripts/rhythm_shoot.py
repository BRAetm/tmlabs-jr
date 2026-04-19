"""
Auto rhythm shooting — NBA 2K26 shot-stick rhythm release.

2K26 rhythm shooting is a TWO-phase stick motion, like a real jump shot:
  1. Pull right stick DOWN  — gather / load
  2. Push right stick UP    — release (timed on the rhythm beat)
  3. Let go                 — back to neutral

When the script starts, it pops its own little Tk window with sliders for
DOWN_MS / UP_MS / COOLDOWN_MS and a radio for SHOT_DIR. Drag a slider and
the next phase uses the new value — no restart needed. Close the window
or click Stop to end the script.
"""

import threading
import time
import tkinter as tk
from tkinter import ttk

DOWN_MS     = 480
UP_MS       = 140
COOLDOWN_MS = 1500
SHOT_DIR    = "down"

_AXIS_BY_DIR = {
    "right": (2,  1.0),
    "left":  (2, -1.0),
    "down":  (3,  1.0),
    "up":    (3, -1.0),
}

_phase = "cooldown"
_phase_start = 0.0
_shots = 0

_tk_thread = None
_tk_root = None
_tk_ready = threading.Event()
_status_var = None


def _run_tk():
    global _tk_root, _status_var

    root = tk.Tk()
    root.title("rhythm_shoot — NBA 2K26")
    root.geometry("360x260")
    root.attributes("-topmost", True)
    root.configure(bg="#1b1b1f")

    style = ttk.Style(root)
    try:
        style.theme_use("clam")
    except tk.TclError:
        pass
    style.configure("TLabel", background="#1b1b1f", foreground="#d6d6d6", font=("Segoe UI", 10))
    style.configure("Dim.TLabel", background="#1b1b1f", foreground="#7a7a80", font=("Consolas", 9))
    style.configure("TFrame", background="#1b1b1f")
    style.configure("TScale", background="#1b1b1f")
    style.configure("TRadiobutton", background="#1b1b1f", foreground="#d6d6d6")

    def _make_slider(parent, row, label, lo, hi, initial, setter, suffix="ms"):
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=(10,6), pady=(8,0))
        readout = ttk.Label(parent, text=f"{int(initial)} {suffix}", style="Dim.TLabel", width=8, anchor="e")
        readout.grid(row=row, column=2, sticky="e", padx=(6,10), pady=(8,0))
        var = tk.DoubleVar(value=initial)
        def on_move(_v):
            setter(int(var.get()))
            readout.configure(text=f"{int(var.get())} {suffix}")
        s = ttk.Scale(parent, from_=lo, to=hi, variable=var, orient="horizontal", command=on_move)
        s.grid(row=row, column=1, sticky="ew", padx=2, pady=(8,0))
        return var

    frm = ttk.Frame(root)
    frm.pack(fill="both", expand=True)
    frm.columnconfigure(1, weight=1)

    def _set_down(v):
        global DOWN_MS
        DOWN_MS = max(50, int(v))
    def _set_up(v):
        global UP_MS
        UP_MS = max(30, int(v))
    def _set_cd(v):
        global COOLDOWN_MS
        COOLDOWN_MS = max(200, int(v))

    _make_slider(frm, 0, "down (load)",  100, 1500, DOWN_MS,     _set_down)
    _make_slider(frm, 1, "up (release)",  40,  500, UP_MS,       _set_up)
    _make_slider(frm, 2, "cooldown",     300, 4000, COOLDOWN_MS, _set_cd)

    ttk.Label(frm, text="shot direction").grid(row=3, column=0, sticky="w", padx=(10,6), pady=(14,0))
    dir_frame = ttk.Frame(frm)
    dir_frame.grid(row=3, column=1, columnspan=2, sticky="w", pady=(14,0))
    dir_var = tk.StringVar(value=SHOT_DIR)
    def _set_dir():
        global SHOT_DIR
        SHOT_DIR = dir_var.get()
    for i, d in enumerate(("down","up","left","right")):
        ttk.Radiobutton(dir_frame, text=d, value=d, variable=dir_var, command=_set_dir).grid(row=0, column=i, padx=4)

    status = ttk.Label(frm, text="idle", style="Dim.TLabel", anchor="w")
    status.grid(row=4, column=0, columnspan=3, sticky="ew", padx=10, pady=(18,10))
    _status_var = status

    _tk_root = root
    _tk_ready.set()
    try:
        root.mainloop()
    except Exception as exc:
        print(f"[rhythm_shoot] tk mainloop error: {exc}")
    finally:
        _tk_root = None


def _ensure_ui():
    global _tk_thread
    if _tk_thread is not None and _tk_thread.is_alive():
        return
    _tk_ready.clear()
    _tk_thread = threading.Thread(target=_run_tk, name="rhythm_shoot_ui", daemon=True)
    _tk_thread.start()
    _tk_ready.wait(timeout=2.0)


def _update_status(text: str):
    if _tk_root is None or _status_var is None:
        return
    try:
        _tk_root.after(0, lambda: _status_var.configure(text=text))
    except Exception:
        pass


def on_start(config: dict) -> None:
    global _phase, _phase_start, _shots
    _phase = "cooldown"
    _phase_start = time.time()
    _shots = 0
    _ensure_ui()
    print(f"[rhythm_shoot] Started — UI open, down {DOWN_MS}ms + up {UP_MS}ms, cooldown {COOLDOWN_MS}ms")


def on_frame(frame, session_id: int, emit) -> None:
    global _phase, _phase_start, _shots

    now = time.time()
    elapsed_ms = (now - _phase_start) * 1000.0

    axes = [0.0, 0.0, 0.0, 0.0]
    buttons = [False] * 17

    if _phase == "cooldown":
        if elapsed_ms >= COOLDOWN_MS:
            _phase = "down"
            _phase_start = now
            _shots += 1
            print(f"[rhythm_shoot] shot #{_shots} — DOWN (load)")
    elif _phase == "down":
        idx, val = _AXIS_BY_DIR.get(SHOT_DIR, _AXIS_BY_DIR["down"])
        axes[idx] = val
        if elapsed_ms >= DOWN_MS:
            _phase = "up"
            _phase_start = now
            print(f"[rhythm_shoot] shot #{_shots} — UP (release, loaded {elapsed_ms:.0f}ms)")
    elif _phase == "up":
        idx, val = _AXIS_BY_DIR.get(SHOT_DIR, _AXIS_BY_DIR["down"])
        axes[idx] = -val
        if elapsed_ms >= UP_MS:
            _phase = "settle"
            _phase_start = now
    elif _phase == "settle":
        if elapsed_ms >= 60:
            _phase = "cooldown"
            _phase_start = now

    _update_status(f"phase={_phase}  shots={_shots}  dir={SHOT_DIR}  t={elapsed_ms:.0f}ms")

    emit({
        "session_id": session_id,
        "axes": axes,
        "buttons": buttons,
    })


def on_tick(session_id: int, emit) -> None:
    on_frame(None, session_id, emit)


def on_stop() -> None:
    global _tk_root
    print(f"[rhythm_shoot] Stopped — {_shots} shot(s) fired")
    r = _tk_root
    if r is not None:
        try:
            r.after(0, r.destroy)
        except Exception:
            pass
