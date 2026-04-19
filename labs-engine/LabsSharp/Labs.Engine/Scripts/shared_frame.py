"""
Shared memory frame reader — reads raw BGR frames written by C# SharedFrameBridge.
Zero JPEG encoding/decoding. Achieves 60+ FPS from the game capture.

Usage in scripts:
    from shared_frame import SharedFrameReader
    reader = SharedFrameReader()
    frame = reader.read()  # returns numpy BGR array or None
"""

import mmap
import struct
import numpy as np

MAP_NAME = "TMLabs_FrameBridge"
HEADER_SIZE = 16
MAX_SIZE = 1920 * 1080 * 3 + HEADER_SIZE


class SharedFrameReader:
    """Reads raw BGR frames from the C# SharedFrameBridge shared memory."""

    def __init__(self):
        self._mmap = None
        self._last_counter = -1

    def open(self):
        """Open the shared memory-mapped file."""
        try:
            self._mmap = mmap.mmap(-1, MAX_SIZE, tagname=MAP_NAME, access=mmap.ACCESS_READ)
            return True
        except Exception as e:
            print(f"[SharedFrameReader] Could not open shared memory '{MAP_NAME}': {e}")
            return False

    def read(self):
        """
        Read the latest frame. Returns (numpy BGR array, changed: bool) or (None, False).
        'changed' is True if this is a new frame since the last read.
        """
        if self._mmap is None:
            if not self.open():
                return None, False

        try:
            self._mmap.seek(0)
            header = self._mmap.read(HEADER_SIZE)

            # Check magic
            if header[:4] != b'FRMX':
                return None, False

            w = struct.unpack_from('<i', header, 4)[0]
            h = struct.unpack_from('<i', header, 8)[0]
            counter = struct.unpack_from('<i', header, 12)[0]

            if w <= 0 or h <= 0 or w > 1920 or h > 1080:
                return None, False

            changed = counter != self._last_counter
            self._last_counter = counter

            if not changed:
                return None, False

            # Read pixel data
            data_size = w * h * 3
            pixels = self._mmap.read(data_size)

            if len(pixels) < data_size:
                return None, False

            frame = np.frombuffer(pixels, dtype=np.uint8).reshape((h, w, 3))
            return frame, True

        except Exception:
            return None, False

    def close(self):
        if self._mmap is not None:
            self._mmap.close()
            self._mmap = None
