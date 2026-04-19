"""
labs_gamepad_shm.py — Python writer for the Labs gamepad shared-memory block.

Mirrors the C# `LabsGamepadShmWriter` (see Labs.Engine.Core.Shm.LabsGamepadShm).
Replaces the per-frame ZMQ PUB path for gamepad events: the engine's
`LabsGamepadShmReader` polls the same block and forwards to the active
`IGamepadSink` (ViGEm virtual pad).

Layout (96 bytes = 32-byte header + 64-byte payload, little-endian):
  [ 0.. 3] magic "LABS"     (0x5342414C)
  [ 4.. 7] version          (uint32 = 1)
  [ 8..11] writer_pid       (uint32)
  [12..15] sequence         (uint32) — bumped AFTER every payload write
  [16..19] payload_size     (uint32 = 64)
  [20..31] reserved
  [32..47] axes[4]          (4 × float32)
  [48..64] buttons[17]      (17 × uint8)
  [65..67] padding
  [68..71] session_id       (int32)
  [72..95] reserved

Cross-process names:
  block:  Labs_<pid>_Gamepad
  event:  Global\\Labs_<pid>_Gamepad_Written
Where <pid> is the engine (reader) PID — `os.getppid()` for workers spawned
by the engine. Override with `writer_pid=` if running standalone.
"""

from __future__ import annotations

import ctypes
import mmap
import os
import struct
import sys

_MAGIC = 0x5342414C  # 'LABS' little-endian
_VERSION = 1
_HEADER_SIZE = 32
_PAYLOAD_SIZE = 64
_BLOCK_SIZE = _HEADER_SIZE + _PAYLOAD_SIZE

_OFF_MAGIC = 0
_OFF_VERSION = 4
_OFF_WRITER_PID = 8
_OFF_SEQUENCE = 12
_OFF_PAYLOAD_SIZE = 16
_OFF_AXES = _HEADER_SIZE + 0      # 4 × float32
_OFF_BUTTONS = _HEADER_SIZE + 16  # 17 × uint8
_OFF_SESSION_ID = _HEADER_SIZE + 36  # int32


def _block_name(pid: int) -> str:
    return f"Labs_{pid}_Gamepad"


def _event_name(pid: int) -> str:
    return f"Global\\Labs_{pid}_Gamepad_Written"


class LabsGamepadShmWriter:
    """
    Opens (or creates) the Labs gamepad block under the engine's PID and
    publishes gamepad frames. One writer per worker process.

    Use as:
        w = LabsGamepadShmWriter()           # writer_pid defaults to os.getppid()
        w.publish(session_id, axes=[...], buttons=[...])
        w.close()
    """

    def __init__(self, writer_pid: int | None = None):
        if sys.platform != "win32":
            raise RuntimeError("LabsGamepadShmWriter is Windows-only")

        self.writer_pid = writer_pid if writer_pid is not None else os.getppid()
        self._sequence = 0

        # CreateFileMapping + MapViewOfFile — mmap with tagname reuses the
        # existing Windows named section if the engine already created it.
        self._mmap = mmap.mmap(-1, _BLOCK_SIZE, tagname=_block_name(self.writer_pid))

        # If the block was freshly created by us (engine wasn't up yet),
        # initialize the header so the reader will accept frames once it opens.
        magic = struct.unpack_from("<I", self._mmap, _OFF_MAGIC)[0]
        if magic != _MAGIC:
            struct.pack_into("<I", self._mmap, _OFF_MAGIC, _MAGIC)
            struct.pack_into("<I", self._mmap, _OFF_VERSION, _VERSION)
            struct.pack_into("<I", self._mmap, _OFF_WRITER_PID, os.getpid() & 0xFFFFFFFF)
            struct.pack_into("<I", self._mmap, _OFF_SEQUENCE, 0)
            struct.pack_into("<I", self._mmap, _OFF_PAYLOAD_SIZE, _PAYLOAD_SIZE)

        # Named event for wake-up — must match the C# reader's WrittenEventName.
        # Global\\ namespace lets different sessions see the same event.
        kernel32 = ctypes.windll.kernel32
        EVENT_MODIFY_STATE = 0x0002
        SYNCHRONIZE = 0x00100000
        self._event = kernel32.OpenEventW(
            EVENT_MODIFY_STATE | SYNCHRONIZE, False, _event_name(self.writer_pid)
        )
        if not self._event:
            # Reader hasn't created it yet — create it ourselves (auto-reset,
            # initially not signalled) so the next OpenEventW in the reader
            # attaches to the same kernel object.
            EVENT_ALL_ACCESS = 0x001F0003
            self._event = kernel32.CreateEventW(
                None, False, False, _event_name(self.writer_pid)
            )

        self._set_event = kernel32.SetEvent
        self._close_handle = kernel32.CloseHandle

    def publish(self, session_id: int, axes, buttons) -> None:
        """Publish one gamepad frame. axes: iterable of 4 floats; buttons: iterable of 17 truthy/bools."""
        # Write payload first …
        for i in range(4):
            v = float(axes[i]) if i < len(axes) else 0.0
            struct.pack_into("<f", self._mmap, _OFF_AXES + i * 4, v)
        for i in range(17):
            b = 1 if (i < len(buttons) and buttons[i]) else 0
            struct.pack_into("<B", self._mmap, _OFF_BUTTONS + i, b)
        struct.pack_into("<i", self._mmap, _OFF_SESSION_ID, int(session_id))

        # … then bump the sequence so the reader snaps a consistent frame.
        self._sequence = (self._sequence + 1) & 0xFFFFFFFF
        struct.pack_into("<I", self._mmap, _OFF_SEQUENCE, self._sequence)

        # … and wake the reader.
        if self._event:
            self._set_event(self._event)

    def publish_gamepad_event(self, gamepad_event: dict) -> None:
        """Convenience: accept the same dict shape used by the ZMQ emit path."""
        self.publish(
            gamepad_event.get("session_id", 0),
            gamepad_event.get("axes", [0.0, 0.0, 0.0, 0.0]),
            gamepad_event.get("buttons", [False] * 17),
        )

    def close(self) -> None:
        try:
            if getattr(self, "_event", None):
                self._close_handle(self._event)
                self._event = None
        except Exception:
            pass
        try:
            if getattr(self, "_mmap", None):
                self._mmap.close()
                self._mmap = None
        except Exception:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
