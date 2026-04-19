"""
Labs Vision — test_session.py

Test script that runs a gamepad input sequence:
  1. Press A button 3 times
  2. Move left stick down 3 times
  3. Hold RT for 1 second
  4. Idle heartbeat loop

Uses the standard on_start/on_frame/on_stop API.
Gamepad events are sent via the emit() callback provided by the worker.
"""

import time
import numpy as np

# Web Gamepad API button indices
BTN_A = 0
BTN_B = 1
BTN_RT = 7

# Test sequence states
_state = {
    "phase": 0,       # 0=A presses, 1=stick, 2=RT hold, 3=idle
    "step": 0,
    "last_action": 0.0,
    "started": False,
}


def on_start(config: dict) -> None:
    """Called once when the worker starts."""
    _state["phase"] = 0
    _state["step"] = 0
    _state["last_action"] = 0.0
    _state["started"] = False
    print(f"[test_session] on_start — session {config['session_id']}")


def on_frame(frame: np.ndarray, session_id: int, emit) -> None:
    """Runs the test sequence across frames, emitting gamepad events."""
    now = time.monotonic()

    if not _state["started"]:
        _state["started"] = True
        _state["last_action"] = now
        print(f"[test_session] Starting test sequence")

    phase = _state["phase"]
    step = _state["step"]
    elapsed = now - _state["last_action"]

    # Phase 0: Press A 3 times (0.5s apart)
    if phase == 0:
        if step < 6:  # 3 press + 3 release = 6 steps
            if step % 2 == 0 and elapsed >= 0.5:
                # Press A
                buttons = [False] * 17
                buttons[BTN_A] = True
                emit({"session_id": session_id, "axes": [0, 0, 0, 0], "buttons": buttons})
                print(f"  A press {step // 2 + 1}/3")
                _state["step"] = step + 1
                _state["last_action"] = now
            elif step % 2 == 1 and elapsed >= 0.15:
                # Release A
                emit({"session_id": session_id, "axes": [0, 0, 0, 0], "buttons": [False] * 17})
                _state["step"] = step + 1
                _state["last_action"] = now
        else:
            print(f"[test_session] Phase 1: Left stick down 3x")
            _state["phase"] = 1
            _state["step"] = 0
            _state["last_action"] = now

    # Phase 1: Move left stick down 3 times
    elif phase == 1:
        if step < 6:
            if step % 2 == 0 and elapsed >= 0.4:
                # Stick down
                emit({"session_id": session_id, "axes": [0, 1.0, 0, 0], "buttons": [False] * 17})
                print(f"  Stick down {step // 2 + 1}/3")
                _state["step"] = step + 1
                _state["last_action"] = now
            elif step % 2 == 1 and elapsed >= 0.2:
                # Recenter
                emit({"session_id": session_id, "axes": [0, 0, 0, 0], "buttons": [False] * 17})
                _state["step"] = step + 1
                _state["last_action"] = now
        else:
            print(f"[test_session] Phase 2: Hold RT for 1s")
            _state["phase"] = 2
            _state["step"] = 0
            _state["last_action"] = now

    # Phase 2: Hold RT for 1 second
    elif phase == 2:
        if step == 0:
            buttons = [False] * 17
            buttons[BTN_RT] = True
            emit({"session_id": session_id, "axes": [0, 0, 0, 0], "buttons": buttons})
            _state["step"] = 1
            _state["last_action"] = now
        elif step == 1 and elapsed >= 1.0:
            emit({"session_id": session_id, "axes": [0, 0, 0, 0], "buttons": [False] * 17})
            print(f"  RT released")
            print(f"[test_session] Phase 3: Idle heartbeat")
            _state["phase"] = 3
            _state["step"] = 0
            _state["last_action"] = now

    # Phase 3: Idle — send neutral state every second
    elif phase == 3:
        if elapsed >= 1.0:
            emit({"session_id": session_id, "axes": [0, 0, 0, 0], "buttons": [False] * 17})
            _state["step"] += 1
            _state["last_action"] = now
            if _state["step"] % 10 == 0:
                print(f"  [test_session] alive — tick {_state['step']}")


def on_stop() -> None:
    """Called once when the worker shuts down."""
    print("[test_session] stopped")
