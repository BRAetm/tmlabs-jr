# jajdivert.py — reconstructed from jajdivert.cp311-win_amd64.pyd
# Source: E:/PythonProjects/2kVision/nba2k/net_dev/jajdivert.c (Cython 3.0.12)
# WinDivert wrapper for NBA 2K packet capture/injection

import threading
import queue
import ctypes

# WinDivert constants
WINDIVERT_LAYER_NETWORK         = 0
WINDIVERT_LAYER_NETWORK_FORWARD = 1
MAX_PACKET_SIZE = 65535


class Divert:
    """
    WinDivert-based packet capture and injection.
    Intercepts NBA 2K UDP traffic at kernel level.
    Requires WinDivert64.sys driver loaded.
    """

    def __init__(self, filter_str, layer=WINDIVERT_LAYER_NETWORK):
        self.filter_str  = filter_str
        self.layer       = layer
        self.handle      = None
        self._running    = False
        self._send_queue = queue.Queue()
        self._recv_cb    = None
        self._send_thread  = None
        self._listen_thread = None

    def _open(self):
        # WinDivertOpen(filter, layer, priority=0, flags=0)
        # Returns handle or INVALID_HANDLE_VALUE
        pass  # ctypes call to WinDivert64 driver

    def listen_for_packets(self, inbound_cb, outbound_cb):
        """Start capture loop — calls inbound_cb / outbound_cb per packet."""
        self._running = True
        self._listen_thread = threading.Thread(target=self._capture_loop,
                                               args=(inbound_cb, outbound_cb),
                                               daemon=True)
        self._listen_thread.start()
        self._send_thread = threading.Thread(target=self.process_send_queue, daemon=True)
        self._send_thread.start()

    def _capture_loop(self, inbound_cb, outbound_cb):
        addr = {}   # WINDIVERT_ADDRESS struct
        while self._running:
            packet = self.recv(addr)
            if packet is None:
                continue
            if self.is_inbound(addr):
                inbound_cb(packet)
            else:
                outbound_cb(packet)
                if not self.is_second_hop(packet):
                    self.send(packet, addr)

    def recv(self, addr):
        """WinDivertRecv — capture next packet."""
        return None  # ctypes call

    def send(self, packet, addr):
        """WinDivertSend — reinject packet."""
        self._send_queue.put((packet, addr))

    def process_send_queue(self):
        """Drain send queue — WinDivertSend calls."""
        while self._running:
            try:
                packet, addr = self._send_queue.get(timeout=0.1)
                # ctypes WinDivertSend(handle, packet, len(packet), addr, byref(sent))
            except queue.Empty:
                continue

    def is_gameplay_packet(self, packet):
        """Filter: is this a 2K game traffic packet (not lobby/menu)?"""
        return packet is not None and len(packet) > 28

    def is_inbound(self, addr):
        """Check WINDIVERT_ADDRESS.Direction bit."""
        return addr.get('Direction', 0) == 1

    def is_second_hop(self, packet):
        """Detect relay/second-hop packets to avoid double-injection."""
        return False

    def recalculate_checksums(self, packet, addr):
        """WinDivertHelperCalcChecksums — fix IP/UDP/TCP checksums after modification."""
        pass  # ctypes call
