"""
Clearing Multicast Client

Listens to the clearing multicast feed and tracks positions/PnL for all clients.
Ported from web_data/clearing_client.cpp.
"""

import socket
import struct
import threading
from collections import defaultdict
from typing import Dict, Tuple

from config import CLEARING_MCAST_IP, CLEARING_MCAST_PORT, MCAST_BIND_IP

# Clearing protocol constants (from matching_engine/clearing_protocol.H)
CLEARING_MAGIC_NUMBER = 0x12345678

# Message types
MSG_TYPE_HEARTBEAT = 0
MSG_TYPE_FILL = 1

# Side
SIDE_BUY = 1
SIDE_SELL = 2

# Struct formats (little-endian, packed)
# clearing_header: magic(u64) + length(u16) + seq_num(u32) + msg_type(u8) = 15 bytes
HEADER_FORMAT = "<QHIb"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)

# fill: header + client_id(u32) + symbol(u32) + quantity(u32) + price(i32) + side(u8)
FILL_FORMAT = "<QHIbIIIib"
FILL_SIZE = struct.calcsize(FILL_FORMAT)


class ClearingClient:
    """
    Listens to clearing multicast and tracks positions, PnL, and volume.
    Thread-safe for reading positions while processing incoming messages.
    """

    def __init__(self, mcast_ip: str = CLEARING_MCAST_IP, 
                 mcast_port: int = CLEARING_MCAST_PORT,
                 bind_ip: str = MCAST_BIND_IP):
        self.mcast_ip = mcast_ip
        self.mcast_port = mcast_port
        self.bind_ip = bind_ip
        
        # Position tracking: {client_id: {symbol: value}}
        self._lock = threading.Lock()
        self._positions: Dict[int, Dict[int, int]] = defaultdict(lambda: defaultdict(int))
        self._total_buy: Dict[int, Dict[int, int]] = defaultdict(lambda: defaultdict(int))
        self._total_sell: Dict[int, Dict[int, int]] = defaultdict(lambda: defaultdict(int))
        self._volume: Dict[int, Dict[int, int]] = defaultdict(lambda: defaultdict(int))
        self._raw_pnl: Dict[int, Dict[int, int]] = defaultdict(lambda: defaultdict(int))
        
        self._last_seq_num = 0
        self._socket = None
        self._running = False
        self._thread = None

    def start(self):
        """Start listening to clearing multicast in a background thread."""
        self._socket = self._create_socket()
        self._running = True
        self._thread = threading.Thread(target=self._listen_loop, daemon=True)
        self._thread.start()
        print(f"ClearingClient: Listening on {self.mcast_ip}:{self.mcast_port}")

    def stop(self):
        """Stop the listener thread."""
        self._running = False
        if self._socket:
            self._socket.close()
        if self._thread:
            self._thread.join(timeout=1.0)

    def _create_socket(self) -> socket.socket:
        """Create and configure the multicast socket."""
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except AttributeError:
            pass  # SO_REUSEPORT not available on all platforms
        
        # Bind to multicast address
        sock.bind((self.mcast_ip, self.mcast_port))
        
        # Join multicast group
        mreq = struct.pack("4s4s", 
                          socket.inet_aton(self.mcast_ip),
                          socket.inet_aton(self.bind_ip))
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
        
        # Set timeout for clean shutdown
        sock.settimeout(0.5)
        
        return sock

    def _listen_loop(self):
        """Main loop to receive and process clearing messages."""
        while self._running:
            try:
                data, _ = self._socket.recvfrom(4096)
                self._process_message(data)
            except socket.timeout:
                continue
            except Exception as e:
                if self._running:
                    print(f"ClearingClient error: {e}")

    def _process_message(self, data: bytes):
        """Parse and process a clearing message."""
        if len(data) < HEADER_SIZE:
            return
        
        # Parse header
        magic, length, seq_num, msg_type = struct.unpack_from(HEADER_FORMAT, data)
        
        if magic != CLEARING_MAGIC_NUMBER:
            print(f"ClearingClient: Invalid magic number {magic:#x}")
            return
        
        # Check sequence number
        if self._last_seq_num != 0 and seq_num != self._last_seq_num + 1:
            print(f"ClearingClient: Sequence gap - expected {self._last_seq_num + 1}, got {seq_num}")
        self._last_seq_num = seq_num
        
        if msg_type == MSG_TYPE_FILL:
            self._process_fill(data)
        elif msg_type == MSG_TYPE_HEARTBEAT:
            pass  # Heartbeat, nothing to do

    def _process_fill(self, data: bytes):
        """Process a fill message and update positions."""
        if len(data) < FILL_SIZE:
            return
        
        # Parse fill message
        (magic, length, seq_num, msg_type, 
         client_id, symbol, quantity, price, side) = struct.unpack_from(FILL_FORMAT, data)
        
        with self._lock:
            if side == SIDE_BUY:
                self._positions[client_id][symbol] += quantity
                self._total_buy[client_id][symbol] += quantity * price
            elif side == SIDE_SELL:
                self._positions[client_id][symbol] -= quantity
                self._total_sell[client_id][symbol] += quantity * price
            
            self._volume[client_id][symbol] += quantity
            self._raw_pnl[client_id][symbol] = (
                self._total_sell[client_id][symbol] - 
                self._total_buy[client_id][symbol]
            )

    def get_position(self, client_id: int, symbol: int) -> int:
        """Get position for a client/symbol pair."""
        with self._lock:
            return self._positions[client_id][symbol]

    def get_positions(self) -> Dict[int, Dict[int, int]]:
        """Get a copy of all positions."""
        with self._lock:
            return {cid: dict(syms) for cid, syms in self._positions.items()}

    def get_raw_pnl(self) -> Dict[int, Dict[int, int]]:
        """Get a copy of raw PnL data."""
        with self._lock:
            return {cid: dict(syms) for cid, syms in self._raw_pnl.items()}

    def get_volume(self) -> Dict[int, Dict[int, int]]:
        """Get a copy of volume data."""
        with self._lock:
            return {cid: dict(syms) for cid, syms in self._volume.items()}

    def get_all_client_ids(self) -> set:
        """Get set of all known client IDs."""
        with self._lock:
            return set(self._positions.keys())
