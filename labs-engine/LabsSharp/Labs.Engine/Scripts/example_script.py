"""
Labs Vision — Example CV script.

Demonstrates the required three-method API.
Receives frames from C# via ZMQ (decoded as BGR numpy arrays).
Emits a neutral GamepadState on every frame as a no-op passthrough.
"""

import numpy as np


def on_start(config: dict) -> None:
    """Called once when the worker starts. Receive session config and initialize state."""
    print(f"[example_script] on_start — session {config['session_id']}")


def on_frame(frame: np.ndarray, session_id: int, emit) -> None:
    """Called once per received frame. Analyze frame and call emit() with a gamepad state dict."""
    # No-op: emit neutral controller state every frame
    emit({
        "session_id": session_id,
        "axes": [0.0, 0.0, 0.0, 0.0],
        "buttons": [False] * 17,
    })


def on_stop() -> None:
    """Called once when the worker shuts down. Release any resources held by this script."""
    print("[example_script] on_stop")
