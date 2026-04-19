# decoderx.py — reconstructed from decoderx.cp311-win_amd64.pyd
# Source: E:/PythonProjects/netx/decoderx.c  (E:\PythonProjects\netx\decoderx.py)
# Extended decoder — NetX version with scalar sync extraction + matchup tracking

import time
from collections import deque


class Decoder:
    """
    NetX-extended packet decoder.
    Adds over jajdecoder:
      - extract_sync_scalar / recombine_sync_scalar
      - get_matchup_byte_location / get_matchup_position
      - frames_to_ms conversion
      - Controller integration
    """

    HEADER_OFFSET = 0

    def __init__(self, num_players=10):
        self.num_players     = num_players
        self.outbound_buffer = deque(maxlen=512)
        self.game_mode       = None
        self.synced          = False
        self._ticker_sent    = 0.0
        self._ticker_latency = 0.0
        self._game_ip        = None

    def set_num_players(self, n):
        self.num_players = n

    def get_ip(self):
        return self._game_ip

    def get_game_mode(self):
        return self.game_mode

    def is_synced(self):
        return self.synced

    def frames_to_ms(self, frames, fps=60):
        return (frames / fps) * 1000.0

    def is_5_second(self, packet):
        try:
            payload = packet[25 + self.HEADER_OFFSET:]
            return len(payload) > 4 and payload[0] == 0x05
        except Exception as e:
            print(f"Error checking 5 second packet: {e}")
            return False

    def add_outbound_byte_array(self, packet):
        """
        Adds the byte array starting at index 25+self.header_offset
        from the packet to the deque.
        """
        payload = packet[25 + self.HEADER_OFFSET:]
        self.outbound_buffer.append(payload)

    def extract_sync_scalar(self, packet):
        """
        Find and trim scalar bytes from 5-second packet.
        Returns: (trimmed_packet, scalar_value, start_position, scalar_length)
        """
        payload = packet[25 + self.HEADER_OFFSET:]
        # Scan for scalar marker bytes
        for i, byte in enumerate(payload):
            if byte == 0x05:
                scalar_val = int.from_bytes(payload[i+1:i+3], 'big')
                trimmed = payload[:i] + payload[i+3:]
                return trimmed, scalar_val, i, 3
        return payload, None, -1, 0

    def recombine_sync_scalar(self, trimmed, scalar_value, start_pos, scalar_length):
        """Recombine trimmed packet with scalar bytes at original position."""
        scalar_bytes = scalar_value.to_bytes(2, 'big')
        return trimmed[:start_pos] + bytes([0x05]) + scalar_bytes + trimmed[start_pos:]

    def find_matching_pattern_in_inbound(self, inbound_packet):
        """
        Searches for a matching pattern from the outbound byte arrays
        in the inbound packet.
        """
        payload = inbound_packet[25 + self.HEADER_OFFSET:]
        for outbound in self.outbound_buffer:
            if len(outbound) >= 4 and outbound[:4] in payload:
                return outbound
        return None

    def get_matchup_byte_location(self, packet):
        """Find byte offset of matchup data within packet."""
        return 25 + self.HEADER_OFFSET

    def get_matchup_position(self, packet):
        """Extract matchup player position data."""
        offset = self.get_matchup_byte_location(packet)
        return packet[offset:offset + self.num_players * 4]

    def get_player_location_bytes(self, packet):
        return packet[25 + self.HEADER_OFFSET:25 + self.HEADER_OFFSET + self.num_players * 4]

    def handle_outbound(self, packet):
        if not self.validate_inbound_packet(packet):
            return
        self.add_outbound_byte_array(packet)
        if self.is_5_second(packet):
            self._ticker_sent = time.monotonic()

    def handle_inbound(self, packet):
        if not self.validate_inbound_packet(packet):
            return
        match = self.find_matching_pattern_in_inbound(packet)
        if match:
            self.synced = True
            self.calculate_ticker_latency()

    def validate_inbound_packet(self, packet):
        return packet is not None and len(packet) > 25 + self.HEADER_OFFSET

    def record_ticker_sent_time(self):
        self._ticker_sent = time.monotonic()

    def calculate_ticker_latency(self):
        if self._ticker_sent:
            self._ticker_latency = (time.monotonic() - self._ticker_sent) * 1000

    def drop_game(self):
        self.outbound_buffer.clear()
        self.synced    = False
        self.game_mode = None
