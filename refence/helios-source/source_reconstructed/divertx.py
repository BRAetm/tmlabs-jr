# divertx.py — reconstructed from divertx.cp311-win_amd64.pyd
# Source: E:/PythonProjects/netx/divertx.c  (E:\PythonProjects\netx\divertx.py)
# Extended WinDivert — NetX version with stop() and improved checksums

import threading
import queue

WINDIVERT_LAYER_NETWORK         = 0
WINDIVERT_LAYER_NETWORK_FORWARD = 1
MAX_PACKET_SIZE = 65535


class Divert:
    """
    NetX variant of WinDivert wrapper.
    Adds: stop(), WinDivertClose, improved send queue.
    Used by netx instead of jajdivert.
    """

    def __init__(self, filter_str, layer=WINDIVERT_LAYER_NETWORK):
        self.filter_str  = filter_str
        self.layer       = layer
        self.handle      = None
        self._running    = False
        self._send_queue = queue.Queue()

    def listen_for_packets(self, inbound_cb, outbound_cb):
        self._running = True
        threading.Thread(target=self._loop, args=(inbound_cb, outbound_cb), daemon=True).start()
        threading.Thread(target=self.process_send_queue, daemon=True).start()

    def _loop(self, inbound_cb, outbound_cb):
        addr = {}
        while self._running:
            pkt = self.recv(addr)
            if pkt is None:
                continue
            if self.is_inbound(addr):
                inbound_cb(pkt)
            else:
                outbound_cb(pkt)
                if not self.is_second_hop(pkt):
                    self.send(pkt, addr)

    def recv(self, addr):
        return None

    def send(self, packet, addr):
        self._send_queue.put((packet, addr))

    def process_send_queue(self):
        while self._running:
            try:
                pkt, addr = self._send_queue.get(timeout=0.1)
                self.recalculate_checksums(pkt, addr)
                # WinDivertSend(...)
            except queue.Empty:
                continue

    def stop(self):
        """WinDivertClose — shut down capture."""
        self._running = False
        # WinDivertClose(self.handle)

    def is_gameplay_packet(self, packet):
        return packet is not None and len(packet) > 28

    def is_inbound(self, addr):
        return addr.get('Direction', 0) == 1

    def is_second_hop(self, packet):
        return False

    def recalculate_checksums(self, packet, addr):
        pass  # WinDivertHelperCalcChecksums
