"""
Labs Vision — Enhanced Python CV Worker (v2.0)

Supports both Labs Vision API (on_start/on_frame/on_stop) and
Helios II API (GCVWorker class with process() method).

Usage:
    python worker.py <session_id> <zmq_port> <script_path> [--fps 30]

Performance optimizations ported from Helios GCVLauncher.py:
  - Process priority set to ABOVE_NORMAL
  - Power throttling disabled
  - Thread priority boosted
"""

import ctypes
import ctypes.wintypes as wintypes
import importlib.util
import json
import os
import signal
import sys
import time
import types

import cv2
import numpy as np
import zmq

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

FRAME_PUB_PORT = 5580      # Must match FramePublisher.PubPort in C#
DEFAULT_FPS = 60           # Default frame rate cap
MIN_FPS = 1
MAX_FPS = 120

# Error codes (Helios-style)
ERR_SCRIPT_LOAD    = "SCRIPT_001"
ERR_SCRIPT_MISSING = "SCRIPT_002"
ERR_SCRIPT_API     = "SCRIPT_003"
ERR_SCRIPT_RUNTIME = "SCRIPT_004"
ERR_ZMQ_CONNECT    = "IPC_001"
ERR_FRAME_DECODE   = "IPC_002"
ERR_SYSTEM         = "SYS_999"


# ---------------------------------------------------------------------------
# Performance optimizations (ported from Helios GCVLauncher.py)
# ---------------------------------------------------------------------------

def set_performance_optimizations():
    """Boost process and thread priority, disable power throttling."""
    if sys.platform != "win32":
        return

    try:
        kernel32 = ctypes.windll.kernel32
        ABOVE_NORMAL_PRIORITY_CLASS = 0x00008000
        THREAD_PRIORITY_ABOVE_NORMAL = 1

        handle = kernel32.GetCurrentProcess()
        kernel32.SetPriorityClass(handle, ABOVE_NORMAL_PRIORITY_CLASS)

        # Disable power throttling
        PROCESS_POWER_THROTTLING_CURRENT_VERSION = 1
        PROCESS_POWER_THROTTLING_EXECUTION_SPEED = 0x1
        ProcessPowerThrottling = 4

        class PROCESS_POWER_THROTTLING_STATE(ctypes.Structure):
            _fields_ = [
                ("Version", wintypes.ULONG),
                ("ControlMask", wintypes.ULONG),
                ("StateMask", wintypes.ULONG),
            ]

        kernel32.SetProcessInformation.argtypes = [
            wintypes.HANDLE, wintypes.ULONG, ctypes.c_void_p, wintypes.DWORD
        ]
        kernel32.SetProcessInformation.restype = wintypes.BOOL

        throttling = PROCESS_POWER_THROTTLING_STATE()
        throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION
        throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED
        throttling.StateMask = 0  # 0 = disable throttling
        kernel32.SetProcessInformation(
            handle, ProcessPowerThrottling,
            ctypes.byref(throttling), ctypes.sizeof(throttling)
        )

        # Boost thread priority
        thread_handle = kernel32.GetCurrentThread()
        kernel32.SetThreadPriority(thread_handle, THREAD_PRIORITY_ABOVE_NORMAL)

        print(f"[worker] Performance optimized: ABOVE_NORMAL priority, power throttling disabled")
    except Exception as e:
        print(f"[worker] Performance optimization failed (non-fatal): {e}")


# ---------------------------------------------------------------------------
# Script loader (dual API: Labs Vision + Helios)
# ---------------------------------------------------------------------------

SCRIPT_API_CVCLOUD = "cvcloud"    # on_start / on_frame / on_stop
SCRIPT_API_HELIOS  = "helios"     # GCVWorker class with process()


def detect_script_api(module: types.ModuleType) -> str:
    """Detects whether a script uses Labs Vision or Helios API."""
    if hasattr(module, "GCVWorker"):
        return SCRIPT_API_HELIOS
    if callable(getattr(module, "on_frame", None)):
        return SCRIPT_API_CVCLOUD
    return SCRIPT_API_CVCLOUD  # fallback


def load_script(script_path: str) -> tuple[types.ModuleType, str]:
    """Loads a CV script and detects its API type. Returns (module, api_type)."""
    if not os.path.isfile(script_path):
        print(f"Error Code: {ERR_SCRIPT_MISSING} - Script not found: {script_path}")
        sys.exit(1)

    try:
        # Add script directory to path so relative imports work
        script_dir = os.path.dirname(os.path.abspath(script_path))
        if script_dir not in sys.path:
            sys.path.insert(0, script_dir)

        # Also add the Scripts folder itself (for helios_compat imports)
        scripts_dir = os.path.dirname(os.path.abspath(__file__))
        if scripts_dir not in sys.path:
            sys.path.insert(0, scripts_dir)

        spec = importlib.util.spec_from_file_location("cv_script", script_path)
        if spec is None or spec.loader is None:
            raise ImportError(f"Cannot create module spec for: {script_path}")

        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    except Exception as e:
        print(f"Error Code: {ERR_SCRIPT_LOAD} - Failed to load script: {e}")
        sys.exit(1)

    api_type = detect_script_api(module)

    if api_type == SCRIPT_API_CVCLOUD:
        for required in ("on_start", "on_frame", "on_stop"):
            if not callable(getattr(module, required, None)):
                print(f"Error Code: {ERR_SCRIPT_API} - Script must implement '{required}()' or GCVWorker class")
                sys.exit(1)

    elif api_type == SCRIPT_API_HELIOS:
        worker_cls = getattr(module, "GCVWorker")
        if not callable(worker_cls):
            print(f"Error Code: {ERR_SCRIPT_API} - GCVWorker must be a class")
            sys.exit(1)

    print(f"[worker] Script loaded: {os.path.basename(script_path)} (API: {api_type})")
    return module, api_type


# ---------------------------------------------------------------------------
# ZMQ emit helper
# ---------------------------------------------------------------------------

def make_emit(socket: zmq.Socket, session_id: int):
    """Returns an emit() callable that serializes and PUB-sends a GamepadEvent dict."""
    _count = [0]
    topic = f"gamepad_{session_id}"
    cv_topic = f"cv_frame_{session_id}"

    # Zero-copy gamepad path — writes directly into the Labs shared-memory block
    # that LabsGamepadShmReader polls on the engine side. Falls back silently
    # (ZMQ still carries the event) if SHM can't be opened.
    shm_writer = None
    try:
        from labs_gamepad_shm import LabsGamepadShmWriter
        shm_writer = LabsGamepadShmWriter()
        print(f"[worker:{session_id}] gamepad SHM writer ready (pid={shm_writer.writer_pid})")
        sys.stdout.flush()
    except Exception as e:
        print(f"[worker:{session_id}] gamepad SHM unavailable ({e}); using ZMQ only")
        sys.stdout.flush()

    def emit(gamepad_event: dict) -> None:
        gamepad_event["session_id"] = session_id
        if shm_writer is not None:
            try:
                shm_writer.publish_gamepad_event(gamepad_event)
            except Exception as ex:
                # Don't kill the hot path over a transient SHM error.
                if _count[0] < 3:
                    print(f"[worker:{session_id}] SHM publish error: {ex}", file=sys.stderr)
        payload = json.dumps(gamepad_event)
        socket.send_string(topic, flags=zmq.SNDMORE | zmq.NOBLOCK)
        socket.send_string(payload, flags=zmq.NOBLOCK)
        _count[0] += 1
        if _count[0] <= 3 or _count[0] % 100 == 0:
            print(f"[worker:{session_id}] emitted gamepad event #{_count[0]}")
            sys.stdout.flush()

    def emit_cv_frame(frame: np.ndarray, quality: int = 60) -> None:
        """Send an annotated CV frame back to C# for display overlay."""
        try:
            _, jpeg = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, quality])
            socket.send_string(cv_topic, flags=zmq.SNDMORE | zmq.NOBLOCK)
            socket.send(jpeg.tobytes(), flags=zmq.NOBLOCK)
        except zmq.ZMQError:
            pass  # drop frame if send would block

    emit.cv_frame = emit_cv_frame
    return emit


# ---------------------------------------------------------------------------
# Helios GCVWorker adapter
# ---------------------------------------------------------------------------

class HeliosWorkerAdapter:
    """Wraps a Helios GCVWorker instance to work with Labs Vision's pipeline."""

    def __init__(self, worker_cls, width: int = 640, height: int = 480):
        self.worker = worker_cls(width, height)

    def on_start(self, config: dict):
        pass  # GCVWorker doesn't have on_start

    def on_frame(self, frame, session_id: int, emit):
        try:
            result = self.worker.process(frame)
            if result is None:
                return

            out_frame, gcvdata = result

            # Convert gcvdata to Web Gamepad API format
            try:
                from helios_compat import gcvdata_to_gamepad_state
                gamepad = gcvdata_to_gamepad_state(bytes(gcvdata))
                emit(gamepad)
            except ImportError:
                # Fallback: emit raw gcvdata as base64 for C# to decode
                import base64
                emit({
                    "axes": [0, 0, 0, 0],
                    "buttons": [False] * 17,
                    "gcvdata_b64": base64.b64encode(bytes(gcvdata)).decode()
                })
        except Exception as e:
            print(f"Error Code: {ERR_SCRIPT_RUNTIME} - GCVWorker.process() error: {e}", file=sys.stderr)

    def on_stop(self):
        try:
            del self.worker
        except Exception:
            pass


# ---------------------------------------------------------------------------
# Frame receive loop
# ---------------------------------------------------------------------------

def shm_frame_loop(session_id: int, script, emit, reader,
                   fps: int = DEFAULT_FPS) -> None:
    """
    Raw-BGRA SHM frame loop — replaces the JPEG-over-ZMQ path.
    Blocks on the named event, snapshots a zero-copy numpy view, converts
    BGRA→BGR, and hands it to script.on_frame().
    """
    frame_interval = 1.0 / fps
    print(f"[worker:{session_id}] SHM frame loop starting at {fps}fps")
    sys.stdout.flush()

    frame_count = 0
    poll_miss = 0
    error_count = 0
    MAX_CONSECUTIVE_ERRORS = 10

    fps_counter = 0
    fps_timer = time.monotonic()

    while True:
        frame_start = time.monotonic()

        got_frame = False
        if reader.wait(timeout_ms=50):
            frame_bgra = reader.snapshot()
            if frame_bgra is not None:
                # BGRA -> BGR (drop alpha). cv2.cvtColor is fine but numpy slice is zero-ish cost.
                frame = frame_bgra[:, :, :3]
                frame_count += 1
                poll_miss = 0
                error_count = 0
                fps_counter += 1
                got_frame = True

                if frame_count <= 5 or frame_count % 200 == 0:
                    h, w = frame.shape[:2]
                    print(f"[worker:{session_id}] shm frame #{frame_count} ({w}x{h})")
                    sys.stdout.flush()

                try:
                    script.on_frame(frame, session_id, emit)
                except Exception as exc:
                    error_count += 1
                    print(f"Error Code: {ERR_SCRIPT_RUNTIME} - on_frame error: {exc}", file=sys.stderr)
                    sys.stderr.flush()
                    if error_count >= MAX_CONSECUTIVE_ERRORS:
                        print(f"[worker:{session_id}] Too many consecutive errors ({error_count}), stopping.", file=sys.stderr)
                        break

        if not got_frame:
            poll_miss += 1
            if poll_miss == 40 or poll_miss % 400 == 0:
                print(f"[worker:{session_id}] no shm frames ({poll_miss} waits missed)", file=sys.stderr)
                sys.stderr.flush()
            tick = getattr(script, "on_tick", None)
            if callable(tick):
                try:
                    tick(session_id, emit)
                except Exception as exc:
                    error_count += 1
                    print(f"Error Code: {ERR_SCRIPT_RUNTIME} - on_tick error: {exc}", file=sys.stderr)
                    sys.stderr.flush()

        now = time.monotonic()
        if now - fps_timer >= 3.0:
            actual_fps = fps_counter / (now - fps_timer)
            print(f"[worker:{session_id}] actual FPS: {actual_fps:.1f}")
            sys.stdout.flush()
            fps_counter = 0
            fps_timer = now

        elapsed = time.monotonic() - frame_start
        sleep_time = frame_interval - elapsed
        if sleep_time > 0.001:
            time.sleep(sleep_time)


def frame_loop(session_id: int, script, emit, sub_socket: zmq.Socket,
               fps: int = DEFAULT_FPS) -> None:
    """
    Main loop — receives JPEG frames from C# via ZMQ SUB, decodes to numpy,
    and passes them to the script's on_frame().
    """
    frame_interval = 1.0 / fps
    print(f"[worker:{session_id}] Frame loop starting at {fps}fps ({frame_interval*1000:.1f}ms/frame)")
    sys.stdout.flush()

    frame_count = 0
    poll_miss = 0
    error_count = 0
    MAX_CONSECUTIVE_ERRORS = 10

    fps_counter = 0
    fps_timer = time.monotonic()

    while True:
        frame_start = time.monotonic()

        try:
            # Drain all pending frames, only process the LATEST one (drop stale)
            latest_jpeg = None
            drained = 0
            while True:
                events = sub_socket.poll(0)  # non-blocking check
                if not events:
                    break
                # Use recv() not recv_string() — topic frames can be binary on some ZMQ builds
                try:
                    topic_bytes = sub_socket.recv(zmq.NOBLOCK)
                    jpeg_bytes = sub_socket.recv(zmq.NOBLOCK)
                    latest_jpeg = jpeg_bytes
                    drained += 1
                except zmq.ZMQError:
                    break

            # If nothing was ready, do a short blocking poll
            if latest_jpeg is None:
                events = sub_socket.poll(50)  # 50ms timeout (was 100)
                if events:
                    try:
                        topic_bytes = sub_socket.recv()
                        latest_jpeg = sub_socket.recv()
                    except zmq.ZMQError:
                        pass

            if latest_jpeg is not None:
                frame_count += 1
                poll_miss = 0
                error_count = 0
                fps_counter += 1

                if frame_count <= 5 or frame_count % 200 == 0:
                    print(f"[worker:{session_id}] frame #{frame_count} ({len(latest_jpeg)} bytes, drained {drained})")
                    sys.stdout.flush()

                arr = np.frombuffer(latest_jpeg, dtype=np.uint8)
                frame = cv2.imdecode(arr, cv2.IMREAD_COLOR)

                if frame is not None:
                    try:
                        script.on_frame(frame, session_id, emit)
                    except Exception as exc:
                        error_count += 1
                        print(f"Error Code: {ERR_SCRIPT_RUNTIME} - on_frame error: {exc}", file=sys.stderr)
                        sys.stderr.flush()
                        if error_count >= MAX_CONSECUTIVE_ERRORS:
                            print(f"[worker:{session_id}] Too many consecutive errors ({error_count}), stopping.", file=sys.stderr)
                            break
                else:
                    print(f"Error Code: {ERR_FRAME_DECODE} - decode failed ({len(latest_jpeg)} bytes)", file=sys.stderr)
            else:
                poll_miss += 1
                if poll_miss == 40 or poll_miss % 400 == 0:
                    print(f"[worker:{session_id}] no frames ({poll_miss} polls missed)", file=sys.stderr)
                    sys.stderr.flush()
                # Tick fallback — scripts that don't need CV (pure input automation)
                # define on_tick so they keep running on sessions without a frame stream.
                tick = getattr(script, "on_tick", None)
                if callable(tick):
                    try:
                        tick(session_id, emit)
                    except Exception as exc:
                        error_count += 1
                        print(f"Error Code: {ERR_SCRIPT_RUNTIME} - on_tick error: {exc}", file=sys.stderr)
                        sys.stderr.flush()

            # Print actual FPS every 3 seconds
            now = time.monotonic()
            if now - fps_timer >= 3.0:
                actual_fps = fps_counter / (now - fps_timer)
                print(f"[worker:{session_id}] actual FPS: {actual_fps:.1f}")
                sys.stdout.flush()
                fps_counter = 0
                fps_timer = now

        except zmq.ZMQError as e:
            if e.errno != zmq.EAGAIN:
                print(f"Error Code: {ERR_ZMQ_CONNECT} - ZMQ error: {e}", file=sys.stderr)

        # Minimal sleep to avoid busy-spinning, but don't cap FPS artificially
        elapsed = time.monotonic() - frame_start
        sleep_time = frame_interval - elapsed
        if sleep_time > 0.001:
            time.sleep(sleep_time)


# ---------------------------------------------------------------------------
# Direct screen capture loop (bypasses C# JPEG pipeline entirely)
# ---------------------------------------------------------------------------

def _find_window_rect(title_substring: str):
    """Find a window by title substring and return its (left, top, right, bottom) rect."""
    import ctypes
    import ctypes.wintypes as wintypes

    user32 = ctypes.windll.user32
    EnumWindows = user32.EnumWindows
    GetWindowTextW = user32.GetWindowTextW
    GetWindowTextLengthW = user32.GetWindowTextLengthW
    IsWindowVisible = user32.IsWindowVisible
    GetWindowRect = user32.GetWindowRect
    GetClientRect = user32.GetClientRect
    ClientToScreen = user32.ClientToScreen

    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int))

    result = [None]
    search = title_substring.lower()

    def callback(hwnd, lparam):
        if not IsWindowVisible(hwnd):
            return True
        length = GetWindowTextLengthW(hwnd)
        if length == 0:
            return True
        buf = ctypes.create_unicode_buffer(length + 1)
        GetWindowTextW(hwnd, buf, length + 1)
        title = buf.value
        if search in title.lower():
            # Get client area position (excludes title bar/borders)
            rect = wintypes.RECT()
            GetClientRect(hwnd, ctypes.byref(rect))
            pt = wintypes.POINT(0, 0)
            ClientToScreen(hwnd, ctypes.byref(pt))
            left = pt.x
            top = pt.y
            right = left + rect.right
            bottom = top + rect.bottom
            if right > left and bottom > top:
                result[0] = {"left": left, "top": top, "right": right, "bottom": bottom,
                             "width": right - left, "height": bottom - top, "title": title}
                return False  # stop enumeration
        return True

    EnumWindows(WNDENUMPROC(callback), 0)
    return result[0]


def direct_capture_loop(session_id: int, script, emit, fps: int = 60,
                        region: dict = None) -> None:
    """
    Captures frames directly from the screen using dxcam (GPU) or mss (fallback).
    No JPEG encode/decode — frames stay as numpy arrays the whole time.
    If the script sets CAPTURE_WINDOW, only that window's region is captured.
    """
    frame_interval = 1.0 / fps
    frame_count = 0
    fps_counter = 0
    fps_timer = time.monotonic()
    camera = None
    sct = None
    window_region = None
    last_window_check = 0

    # Check if script wants a specific window
    capture_window = getattr(sys.modules.get("cv_script"), "CAPTURE_WINDOW", None)
    if capture_window is None:
        # Check the loaded module directly
        for mod in sys.modules.values():
            cw = getattr(mod, "CAPTURE_WINDOW", None)
            if cw and isinstance(cw, str):
                capture_window = cw
                break

    if capture_window:
        print(f"[worker:{session_id}] Looking for window: '{capture_window}'...")
        sys.stdout.flush()
        # Wait up to 10s for the window to appear
        for _ in range(20):
            window_region = _find_window_rect(capture_window)
            if window_region:
                break
            time.sleep(0.5)
        if window_region:
            region = window_region
            print(f"[worker:{session_id}] Found window: '{window_region['title']}' "
                  f"at ({region['left']},{region['top']}) {region['width']}x{region['height']}")
        else:
            print(f"[worker:{session_id}] WARNING: Window '{capture_window}' not found, capturing full screen")
        sys.stdout.flush()

    # Try dxcam first (GPU-accelerated, lowest latency)
    try:
        import dxcam
        camera = dxcam.create(output_color="BGR")
        if region:
            capture_region = (region["left"], region["top"], region["right"], region["bottom"])
        else:
            capture_region = None
        camera.start(target_fps=fps, region=capture_region)
        print(f"[worker:{session_id}] Direct capture: dxcam (GPU) at {fps}fps")
        sys.stdout.flush()
    except Exception as e:
        print(f"[worker:{session_id}] dxcam unavailable ({e}), falling back to mss")
        camera = None

    # Fallback to mss
    if camera is None:
        try:
            from mss import mss
            sct = mss()
            print(f"[worker:{session_id}] Direct capture: mss (CPU) at {fps}fps")
            sys.stdout.flush()
        except ImportError:
            print(f"[worker:{session_id}] ERROR: No capture library. pip install dxcam mss")
            return

    print(f"[worker:{session_id}] Direct capture loop starting...")
    sys.stdout.flush()

    while True:
        frame_start = time.monotonic()

        try:
            # Re-check window position every 2 seconds (in case it moved/resized)
            if capture_window and sct is not None and frame_start - last_window_check > 2.0:
                new_rect = _find_window_rect(capture_window)
                if new_rect:
                    region = new_rect
                last_window_check = frame_start

            frame = None

            if camera is not None:
                frame = camera.get_latest_frame()
            elif sct is not None:
                if region:
                    monitor = {"left": region["left"], "top": region["top"],
                               "width": region["width"], "height": region["height"]}
                else:
                    monitor = sct.monitors[1]
                img = sct.grab(monitor)
                frame = np.array(img)[:, :, :3]  # BGRA -> BGR

            if frame is not None:
                frame_count += 1
                fps_counter += 1

                if frame_count <= 3 or frame_count % 300 == 0:
                    h, w = frame.shape[:2]
                    print(f"[worker:{session_id}] direct frame #{frame_count} ({w}x{h})")
                    sys.stdout.flush()

                try:
                    script.on_frame(frame, session_id, emit)
                except Exception as exc:
                    print(f"Error Code: {ERR_SCRIPT_RUNTIME} - on_frame error: {exc}", file=sys.stderr)

            # Print actual FPS every 3 seconds
            now = time.monotonic()
            if now - fps_timer >= 3.0:
                actual_fps = fps_counter / (now - fps_timer)
                print(f"[worker:{session_id}] direct capture FPS: {actual_fps:.1f}")
                sys.stdout.flush()
                fps_counter = 0
                fps_timer = now

        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"[worker:{session_id}] capture error: {e}", file=sys.stderr)
            time.sleep(0.1)

        elapsed = time.monotonic() - frame_start
        sleep_time = frame_interval - elapsed
        if sleep_time > 0.001:
            time.sleep(sleep_time)

    # Cleanup
    if camera is not None:
        try:
            camera.stop()
        except Exception:
            pass
    cv2.destroyAllWindows()
    print(f"[worker:{session_id}] Direct capture ended ({frame_count} frames)")


# ---------------------------------------------------------------------------
# Shared memory frame loop (reads raw BGR from C# — no JPEG, 60+ FPS)
# ---------------------------------------------------------------------------

def shared_memory_loop(session_id: int, script, emit, fps: int = 60) -> None:
    """
    Reads raw BGR frames from C# via shared memory (SharedFrameBridge).
    No JPEG encode/decode at all — pure game frames at capture speed.
    """
    try:
        from shared_frame import SharedFrameReader
    except ImportError:
        print(f"[worker:{session_id}] ERROR: shared_frame.py not found")
        return

    reader = SharedFrameReader()
    if not reader.open():
        print(f"[worker:{session_id}] Shared memory not available, falling back to ZMQ")
        return

    frame_interval = 1.0 / fps
    frame_count = 0
    fps_counter = 0
    fps_timer = time.monotonic()

    print(f"[worker:{session_id}] Shared memory frame loop at {fps}fps (zero-copy, no JPEG)")
    sys.stdout.flush()

    while True:
        frame_start = time.monotonic()

        try:
            frame, changed = reader.read()

            if frame is not None and changed:
                # Make a writable copy (shared memory is read-only)
                frame = frame.copy()
                frame_count += 1
                fps_counter += 1

                if frame_count <= 3 or frame_count % 300 == 0:
                    h, w = frame.shape[:2]
                    print(f"[worker:{session_id}] shared mem frame #{frame_count} ({w}x{h})")
                    sys.stdout.flush()

                try:
                    script.on_frame(frame, session_id, emit)
                except Exception as exc:
                    print(f"Error Code: {ERR_SCRIPT_RUNTIME} - on_frame error: {exc}", file=sys.stderr)

            # FPS tracking
            now = time.monotonic()
            if now - fps_timer >= 3.0:
                actual_fps = fps_counter / (now - fps_timer)
                print(f"[worker:{session_id}] shared memory FPS: {actual_fps:.1f}")
                sys.stdout.flush()
                fps_counter = 0
                fps_timer = now

        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"[worker:{session_id}] shared mem error: {e}", file=sys.stderr)
            time.sleep(0.1)

        elapsed = time.monotonic() - frame_start
        sleep_time = frame_interval - elapsed
        if sleep_time > 0.001:
            time.sleep(sleep_time)

    reader.close()
    cv2.destroyAllWindows()
    print(f"[worker:{session_id}] Shared memory loop ended ({frame_count} frames)")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    if len(sys.argv) < 4:
        print("Usage: worker.py <session_id> <zmq_port> <script_path> [--fps N]", file=sys.stderr)
        sys.exit(1)

    session_id  = int(sys.argv[1])
    zmq_port    = int(sys.argv[2])
    script_path = sys.argv[3]

    # Parse optional --fps argument
    fps = DEFAULT_FPS
    if "--fps" in sys.argv:
        fps_idx = sys.argv.index("--fps")
        if fps_idx + 1 < len(sys.argv):
            try:
                fps = max(MIN_FPS, min(MAX_FPS, int(sys.argv[fps_idx + 1])))
            except ValueError:
                pass

    # Apply Helios-style performance optimizations
    set_performance_optimizations()

    config = {
        "session_id":  session_id,
        "zmq_port":    zmq_port,
        "script_path": script_path,
        "fps":         fps,
    }

    # Load script (auto-detects Labs Vision vs Helios API)
    module, api_type = load_script(script_path)

    # Create appropriate script adapter
    if api_type == SCRIPT_API_HELIOS:
        script = HeliosWorkerAdapter(module.GCVWorker)
        print(f"[worker:{session_id}] Using Helios GCVWorker adapter")
    else:
        script = module

    # ZMQ context
    context = zmq.Context()

    # PUB socket for gamepad events
    pub_socket = context.socket(zmq.PUB)
    pub_socket.connect(f"tcp://127.0.0.1:{zmq_port}")

    # SUB socket for receiving frames from C# FramePublisher
    sub_socket = context.socket(zmq.SUB)
    sub_socket.setsockopt(zmq.RCVHWM, 2)  # Keep only 2 frames buffered — always process latest
    sub_socket.connect(f"tcp://127.0.0.1:{FRAME_PUB_PORT}")
    sub_socket.setsockopt_string(zmq.SUBSCRIBE, f"frame_{session_id}")

    emit = make_emit(pub_socket, session_id)

    # Wire up helios_compat callbacks if available
    try:
        import helios_compat
        helios_compat._set_callbacks(
            emit_fn=emit,
            gcvdata_fn=lambda data: emit(helios_compat.gcvdata_to_gamepad_state(data))
        )
    except ImportError:
        pass

    # Graceful shutdown on SIGTERM
    def handle_sigterm(_sig, _frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, handle_sigterm)

    print(f"[worker:{session_id}] started (port={zmq_port}, fps={fps}, api={api_type}, script={os.path.basename(script_path)})")
    sys.stdout.flush()

    # Give SUB socket time to connect (ZMQ slow-joiner problem)
    time.sleep(0.3)

    # Check capture mode:
    # 1. SHARED_MEMORY = True -> read raw BGR from C# shared memory (fastest, pure game frames)
    # 2. DIRECT_CAPTURE = True -> dxcam/mss screen grab (fast but captures screen, not game)
    # 3. Default -> ZMQ JPEG pipeline from C# (slowest but always works)
    use_shared = getattr(module, "SHARED_MEMORY", False) or "--shared" in sys.argv
    use_direct = getattr(module, "DIRECT_CAPTURE", False) or "--direct" in sys.argv

    try:
        script.on_start(config)
        print(f"[worker:{session_id}] on_start() complete")
        sys.stdout.flush()

        if use_shared:
            print(f"[worker:{session_id}] Using SHARED MEMORY (zero-copy raw BGR from C# capture)")
            sys.stdout.flush()
            shared_memory_loop(session_id, script, emit, fps=fps)
        elif use_direct:
            print(f"[worker:{session_id}] Using DIRECT screen capture (dxcam/mss)")
            sys.stdout.flush()
            direct_capture_loop(session_id, script, emit, fps=fps, region=getattr(module, "CAPTURE_REGION", None))
        else:
            # Prefer the raw-BGRA SHM bus if the engine is publishing frames for
            # this session. Falls through to the ZMQ JPEG path if not.
            shm_reader = None
            try:
                from labs_frame_shm import LabsFrameShmReader
                candidate = LabsFrameShmReader(session_id)
                # Give the engine a beat to produce the first frame.
                deadline = time.monotonic() + 1.5
                while time.monotonic() < deadline and not candidate.is_ready():
                    time.sleep(0.05)
                if candidate.is_ready():
                    shm_reader = candidate
                else:
                    candidate.close()
            except Exception as exc:
                print(f"[worker:{session_id}] SHM reader unavailable ({exc}); using ZMQ.")

            if shm_reader is not None:
                print(f"[worker:{session_id}] Using SHM frame bus (zero-copy BGRA, no JPEG round-trip)")
                sys.stdout.flush()
                shm_frame_loop(session_id, script, emit, shm_reader, fps=fps)
            else:
                print(f"[worker:{session_id}] Using ZMQ frame pipeline from C#")
                sys.stdout.flush()
                frame_loop(session_id, script, emit, sub_socket, fps=fps)

    except KeyboardInterrupt:
        print(f"[worker:{session_id}] shutting down (signal).")

    finally:
        try:
            script.on_stop()
        except Exception as exc:
            print(f"[worker:{session_id}] on_stop error: {exc}", file=sys.stderr)

        pub_socket.close()
        sub_socket.close()
        context.term()
        print(f"[worker:{session_id}] stopped.")


if __name__ == "__main__":
    main()
