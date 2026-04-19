# jajdecoder.py — reconstructed from jajdecoder.cp311-win_amd64.pyd
# Source: E:/PythonProjects/2kVision/nba2k/net_dev/jajdecoder.c (Cython 3.0.12)
# Network packet decoder for NBA 2K game traffic

import time
from collections import deque


class Decoder:
    """
    Decodes NBA 2K UDP game packets captured via WinDivert.
    Matches outbound packets to inbound responses to extract game state.
    Tracks ticker latency for shot timing synchronization.
    """

    HEADER_OFFSET = 0   # adjusted per game version

    def __init__(self, num_players=10):
        self.num_players     = num_players
        self.outbound_buffer = deque(maxlen=512)   # circular buffer of sent packets
        self.game_mode       = None
        self.synced          = False
        self._ticker_sent_time = 0.0
        self._ticker_latency   = 0.0
        self._game_ip          = None

    def set_num_players(self, n):
        self.num_players = n

    def get_ip(self):
        """Get game server IP from captured packets."""
        return self._game_ip

    def get_game_mode(self):
        """Detect game mode (Online, MyTeam, etc.) from packet signatures."""
        return self.game_mode

    def is_synced(self):
        return self.synced

    def is_5_second(self, packet):
        """
        Detect the 5-second shot clock packet.
        Used for timing calibration.
        """
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

    def handle_outbound(self, packet):
        if not self.validate_inbound_packet(packet):
            return
        self.add_outbound_byte_array(packet)
        if self.is_5_second(packet):
            self.record_ticker_sent_time()

    def handle_inbound(self, packet):
        if not self.validate_inbound_packet(packet):
            return
        match = self.find_matching_pattern_in_inbound(packet)
        if match:
            self.synced = True
            self.calculate_ticker_latency()

    def validate_inbound_packet(self, packet):
        """Basic packet structure validation."""
        return packet is not None and len(packet) > 25 + self.HEADER_OFFSET

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

    def get_player_location_bytes(self, packet):
        """Extract player XY position bytes from packet."""
        return packet[25 + self.HEADER_OFFSET:25 + self.HEADER_OFFSET + self.num_players * 4]

    def record_ticker_sent_time(self):
        self._ticker_sent_time = time.monotonic()

    def calculate_ticker_latency(self):
        if self._ticker_sent_time:
            self._ticker_latency = (time.monotonic() - self._ticker_sent_time) * 1000

    def drop_game(self):
        """Reset all state when game disconnects."""
        self.outbound_buffer.clear()
        self.synced    = False
        self.game_mode = None
