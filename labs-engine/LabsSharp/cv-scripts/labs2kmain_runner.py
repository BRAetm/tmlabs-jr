"""
Labs Engine — labs2kmain_runner.py
Core script runner: loads cv-scripts, feeds frames + controller I/O,
dispatches on_start / on_frame / on_tick / on_stop hooks.
"""

import sys
import os
import time
import threading
import importlib
import importlib.util
import traceback
from pathlib import Path

ROOT       = Path(__file__).resolve().parent.parent
SCRIPTS_DIR = Path(__file__).resolve().parent

sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "super source"))

# ── Session ───────────────────────────────────────────────────────────────────

class ScriptSession:
    def __init__(self, script_name: str, config: dict = None):
        self.script_name  = script_name
        self.session_id   = int(time.time() * 1000) & 0xFFFFFFFF
        self.config       = config or {}
        self._module      = None
        self._running     = False
        self._thread      = None
        self._lock        = threading.Lock()
        self._output_cb   = None   # called with each emit() payload
        self._log_cb      = None   # called with log strings

    # ── Load / unload ─────────────────────────────────────────────────────────

    def load(self):
        path = SCRIPTS_DIR / f"{self.script_name}.py"
        if not path.exists():
            raise FileNotFoundError(f"Script not found: {path}")
        spec = importlib.util.spec_from_file_location(self.script_name, path)
        mod  = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        self._module = mod
        self._log(f"Loaded script: {self.script_name}")

    def reload(self):
        self.stop()
        self.load()

    # ── Control ───────────────────────────────────────────────────────────────

    def start(self):
        if self._module is None:
            self.load()
        self._running = True
        if hasattr(self._module, "on_start"):
            try:
                self._module.on_start(self.config)
            except Exception:
                self._log(f"on_start error:\n{traceback.format_exc()}", error=True)
        self._log(f"Session {self.session_id} started")

    def stop(self):
        self._running = False
        if self._module and hasattr(self._module, "on_stop"):
            try:
                self._module.on_stop()
            except Exception:
                self._log(f"on_stop error:\n{traceback.format_exc()}", error=True)
        self._log(f"Session {self.session_id} stopped")

    @property
    def running(self):
        return self._running

    # ── Frame / tick dispatch ─────────────────────────────────────────────────

    def push_frame(self, frame):
        if not self._running or self._module is None:
            return
        if hasattr(self._module, "on_frame"):
            try:
                self._module.on_frame(frame, self.session_id, self._emit)
            except Exception:
                self._log(f"on_frame error:\n{traceback.format_exc()}", error=True)

    def tick(self):
        if not self._running or self._module is None:
            return
        if hasattr(self._module, "on_tick"):
            try:
                self._module.on_tick(self.session_id, self._emit)
            except Exception:
                self._log(f"on_tick error:\n{traceback.format_exc()}", error=True)

    # ── Emit / callbacks ──────────────────────────────────────────────────────

    def _emit(self, payload: dict):
        if self._output_cb:
            try:
                self._output_cb(payload)
            except Exception:
                pass

    def set_output_callback(self, cb):
        self._output_cb = cb

    def set_log_callback(self, cb):
        self._log_cb = cb

    def _log(self, msg, error=False):
        if self._log_cb:
            self._log_cb(msg, error)
        else:
            prefix = "[ERROR]" if error else "[INFO]"
            print(f"[labs2kmain] {prefix} {msg}")


# ── Runner ────────────────────────────────────────────────────────────────────

class Labs2KRunner:
    """
    Manages multiple ScriptSessions, feeds frames from capture,
    runs a tick loop for scripts that don't need frames.
    """

    def __init__(self):
        self._sessions: dict[str, ScriptSession] = {}
        self._lock      = threading.Lock()
        self._tick_thread = None
        self._running   = False
        self._log_cb    = None
        self._output_cb = None

    # ── Script management ─────────────────────────────────────────────────────

    def available_scripts(self) -> list[str]:
        return sorted(
            p.stem for p in SCRIPTS_DIR.glob("*.py")
            if not p.stem.startswith("_") and p.stem != "labs2kmain_runner"
        )

    def load_script(self, name: str, config: dict = None, autostart=True) -> ScriptSession:
        with self._lock:
            if name in self._sessions:
                self._sessions[name].stop()

            sess = ScriptSession(name, config)
            sess.set_log_callback(self._on_log)
            sess.set_output_callback(self._on_output)
            sess.load()

            if autostart:
                sess.start()

            self._sessions[name] = sess
            return sess

    def unload_script(self, name: str):
        with self._lock:
            sess = self._sessions.pop(name, None)
        if sess:
            sess.stop()

    def unload_all(self):
        with self._lock:
            names = list(self._sessions.keys())
        for name in names:
            self.unload_script(name)

    def active_scripts(self) -> list[str]:
        with self._lock:
            return [n for n, s in self._sessions.items() if s.running]

    # ── Frame feed ───────────────────────────────────────────────────────────

    def push_frame(self, frame):
        with self._lock:
            sessions = list(self._sessions.values())
        for sess in sessions:
            try:
                sess.push_frame(frame)
            except Exception:
                pass

    # ── Tick loop ─────────────────────────────────────────────────────────────

    def start_tick_loop(self, interval_ms=16):
        if self._running:
            return
        self._running = True
        self._tick_thread = threading.Thread(
            target=self._tick_loop,
            args=(interval_ms / 1000.0,),
            daemon=True,
            name="labs2k_tick"
        )
        self._tick_thread.start()

    def stop_tick_loop(self):
        self._running = False

    def _tick_loop(self, interval: float):
        while self._running:
            t0 = time.perf_counter()
            with self._lock:
                sessions = list(self._sessions.values())
            for sess in sessions:
                try:
                    sess.tick()
                except Exception:
                    pass
            elapsed = time.perf_counter() - t0
            sleep = interval - elapsed
            if sleep > 0:
                time.sleep(sleep)

    # ── Callbacks ─────────────────────────────────────────────────────────────

    def set_log_callback(self, cb):
        self._log_cb = cb

    def set_output_callback(self, cb):
        self._output_cb = cb

    def _on_log(self, msg, error=False):
        if self._log_cb:
            self._log_cb(msg, error)

    def _on_output(self, payload: dict):
        if self._output_cb:
            self._output_cb(payload)

    # ── Shutdown ──────────────────────────────────────────────────────────────

    def shutdown(self):
        self.stop_tick_loop()
        self.unload_all()


# ── Singleton ─────────────────────────────────────────────────────────────────

_runner = None

def get_runner() -> Labs2KRunner:
    global _runner
    if _runner is None:
        _runner = Labs2KRunner()
    return _runner
