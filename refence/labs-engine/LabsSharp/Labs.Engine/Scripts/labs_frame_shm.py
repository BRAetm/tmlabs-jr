"""
labs_frame_shm.py — Python reader for the Labs raw-BGRA frame bus.

Mirrors the C# `LabsFrameShmWriter` (see Labs.Engine.Core.Shm.LabsFrameShm).
Replaces the JPEG-over-ZMQ frame path for CV scripts: the engine writes
decoded BGRA into a per-session named memory-mapped block, raises a named
event, and Python wraps the payload as a zero-copy numpy array.

Layout (header 64 bytes + BGRA payload, little-endian):
  [ 0.. 3] magic 'FRML'  (0x4D52464C)
  [ 4.. 7] version       (=1)
  [ 8..11] writer_pid
  [12..15] sequence      (bumped AFTER payload write)
  [16..19] payload_size  (w * h * bpp)
  [20..23] width
  [24..27] height
  [28..31] stride
  [32..35] format        (0=BGRA, 1=BGR, 2=NV12)
  [36..39] session_id
  [40..47] timestamp_ms  (int64)

Names:
  block:  Labs_<engine_pid>_Frame_<session_id>
  event:  Global\\Labs_<engine_pid>_Frame_<session_id>_Written
"""

from __future__ import annotations

import ctypes
import mmap
import os
import struct
import sys

import numpy as np

_MAGIC = 0x4D52464C
_VERSION = 1
_HEADER_SIZE = 64
_MAX_PAYLOAD = 1920 * 1080 * 4
_BLOCK_SIZE = _HEADER_SIZE + _MAX_PAYLOAD

_OFF_MAGIC        = 0
_OFF_VERSION      = 4
_OFF_WRITER_PID   = 8
_OFF_SEQUENCE     = 12
_OFF_PAYLOAD_SIZE = 16
_OFF_WIDTH        = 20
_OFF_HEIGHT       = 24
_OFF_STRIDE       = 28
_OFF_FORMAT       = 32
_OFF_SESSION_ID   = 36
_OFF_TIMESTAMP_MS = 40

FORMAT_BGRA = 0
FORMAT_BGR  = 1
FORMAT_NV12 = 2


def _block_name(pid: int, sid: int) -> str:
    return f"Labs_{pid}_Frame_{sid}"


def _event_name(pid: int, sid: int) -> str:
    return f"Global\\Labs_{pid}_Frame_{sid}_Written"


class LabsFrameShmReader:
    """
    Opens (or attaches to) the per-session frame block written by the engine.
    Blocks on the named event and returns the latest coherent BGRA frame as a
    numpy array view (zero-copy — do not mutate; copy if you need to keep it).

    Typical use:
        r = LabsFrameShmReader(session_id=2)          # writer_pid defaults to parent
        if r.wait(timeout_ms=50):
            frame = r.snapshot()     # H x W x 4 uint8 BGRA view
            if frame is not None:
                ...
    """

    def __init__(self, session_id: int, writer_pid: int | None = None):
        if sys.platform != "win32":
            raise RuntimeError("LabsFrameShmReader is Windows-only")

        self.session_id = session_id
        self.writer_pid = writer_pid if writer_pid is not None else os.getppid()
        self._last_sequence = 0

        self._mmap = mmap.mmap(-1, _BLOCK_SIZE, tagname=_block_name(self.writer_pid, session_id))

        kernel32 = ctypes.windll.kernel32
        SYNCHRONIZE = 0x00100000
        EVENT_MODIFY_STATE = 0x0002
        self._event = kernel32.OpenEventW(
            SYNCHRONIZE | EVENT_MODIFY_STATE, False, _event_name(self.writer_pid, session_id)
        )
        if not self._event:
            self._event = kernel32.CreateEventW(
                None, False, False, _event_name(self.writer_pid, session_id)
            )

        self._wait_for_single = kernel32.WaitForSingleObject
        self._close_handle = kernel32.CloseHandle

    def is_ready(self) -> bool:
        """True once the writer has produced at least one frame with the right magic."""
        magic = struct.unpack_from("<I", self._mmap, _OFF_MAGIC)[0]
        seq = struct.unpack_from("<I", self._mmap, _OFF_SEQUENCE)[0]
        return magic == _MAGIC and seq > 0

    def wait(self, timeout_ms: int = 50) -> bool:
        """Blocks up to timeout_ms for a new-frame event. Returns True if signalled."""
        if not self._event:
            return False
        return self._wait_for_single(self._event, int(timeout_ms)) == 0

    def snapshot(self) -> np.ndarray | None:
        """
        Returns an H x W x 4 uint8 BGRA numpy view of the latest frame, or None
        if no new frame has arrived since the last call. The view aliases the
        mmap — copy with .copy() if you need to mutate or outlive the next call.
        """
        magic = struct.unpack_from("<I", self._mmap, _OFF_MAGIC)[0]
        if magic != _MAGIC:
            return None

        seq = struct.unpack_from("<I", self._mmap, _OFF_SEQUENCE)[0]
        if seq == self._last_sequence:
            return None
        self._last_sequence = seq

        width  = struct.unpack_from("<I", self._mmap, _OFF_WIDTH)[0]
        height = struct.unpack_from("<I", self._mmap, _OFF_HEIGHT)[0]
        stride = struct.unpack_from("<I", self._mmap, _OFF_STRIDE)[0]
        fmt    = struct.unpack_from("<I", self._mmap, _OFF_FORMAT)[0]
        size   = struct.unpack_from("<I", self._mmap, _OFF_PAYLOAD_SIZE)[0]

        if width == 0 or height == 0 or size == 0 or size > _MAX_PAYLOAD:
            return None
        if fmt != FORMAT_BGRA:
            return None  # Extend to BGR/NV12 when needed.

        bpp = 4
        row = width * bpp
        # If stride matches the packed row, wrap directly. Otherwise slice each row.
        buf = np.frombuffer(self._mmap, dtype=np.uint8, count=size, offset=_HEADER_SIZE)
        if stride == row:
            return buf.reshape(height, width, bpp)
        # Strided: build a contiguous view by slicing the first `row` bytes per stride.
        strided = np.lib.stride_tricks.as_strided(
            buf, shape=(height, row), strides=(stride, 1)
        )
        return strided.reshape(height, width, bpp)

    def close(self) -> None:
        try:
            if self._event:
                self._close_handle(self._event)
                self._event = None
        except Exception:
            pass
        try:
            if self._mmap:
                self._mmap.close()
                self._mmap = None
        except Exception:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
