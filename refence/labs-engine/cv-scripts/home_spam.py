"""
Test script — spams the Home/Guide button every 0.5 seconds.
Verifies controller input is getting through the pipeline.

Button layout (Web Gamepad API standard, 17 buttons):
  [0]  A          [1]  B          [2]  X          [3]  Y
  [4]  LB         [5]  RB         [6]  LT          [7]  RT
  [8]  Back/View  [9]  Start/Menu [10] L3          [11] R3
  [12] DPad Up    [13] DPad Down  [14] DPad Left   [15] DPad Right
  [16] Guide/Home
"""

import time

_last_press = 0.0
_pressed = False
INTERVAL = 0.5  # seconds between toggles


def on_start(config: dict) -> None:
    global _last_press, _pressed
    _last_press = time.time()
    _pressed = False
    print(f"[home_spam] Started — spamming Guide button every {INTERVAL}s")


def _tick(session_id, emit):
    global _last_press, _pressed

    now = time.time()
    if now - _last_press >= INTERVAL:
        _pressed = not _pressed
        _last_press = now
        state = "PRESS" if _pressed else "RELEASE"
        print(f"[home_spam] Guide {state}")

    buttons = [False] * 17
    buttons[16] = _pressed  # Guide/Home

    emit({
        "session_id": session_id,
        "axes": [0.0, 0.0, 0.0, 0.0],
        "buttons": buttons,
    })


def on_frame(frame, session_id: int, emit) -> None:
    _tick(session_id, emit)


def on_tick(session_id: int, emit) -> None:
    _tick(session_id, emit)


def on_stop() -> None:
    print("[home_spam] Stopped")
