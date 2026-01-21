"""
Market Data Multicast Client

Listens to the market data feed and tracks best bid/offer for all symbols.
Used to provide BBO data to the dashboard.
"""

import socket
import struct
import threading
from collections import defaultdict
from typing import Dict, Tuple, Optional

from config import MD_MCAST_IP, MD_MCAST_PORT, MCAST_BIND_IP, SYMBOLS

# Market data protocol constants (from market_data/md_protocol.H)
# "GOIRISH!" as little-endian u64
MD_MAGIC_NUMBER = int.from_bytes(b"GOIRISH!", "little")

# Message types
MSG_TYPE_HEARTBEAT = 0
MSG_TYPE_NEW_ORDER = 1
MSG_TYPE_DELETE_ORDER = 2
MSG_TYPE_MODIFY_ORDER = 3
MSG_TYPE_TRADE = 4
MSG_TYPE_TRADE_SUMMARY = 5
MSG_TYPE_SNAPSHOT_INFO = 6

# Side
SIDE_BUY = 1
SIDE_SELL = 2

# Struct formats (little-endian, packed)
# md_header: magic(u64) + length(u16) + seq_num(u32) + timestamp(u64) + msg_type(u8) = 23 bytes
HEADER_FORMAT = "<QHIQb"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)

# new_order: header + order_id(u64) + symbol(u32) + side(u8) + quantity(u32) + price(i32) + flags(u8)
NEW_ORDER_FORMAT = "<QHIQbQIbIib"
NEW_ORDER_SIZE = struct.calcsize(NEW_ORDER_FORMAT)

# delete_order: header + order_id(u64)
DELETE_ORDER_FORMAT = "<QHIQbQ"
DELETE_ORDER_SIZE = struct.calcsize(DELETE_ORDER_FORMAT)

# modify_order: header + order_id(u64) + side(u8) + quantity(u32) + price(i32)
MODIFY_ORDER_FORMAT = "<QHIQbQbIi"
MODIFY_ORDER_SIZE = struct.calcsize(MODIFY_ORDER_FORMAT)


class Order:
    """Represents a resting order in the book."""
    __slots__ = ['order_id', 'symbol', 'side', 'quantity', 'price']
    
    def __init__(self, order_id: int, symbol: int, side: int, quantity: int, price: int):
        self.order_id = order_id
        self.symbol = symbol
        self.side = side
        self.quantity = quantity
        self.price = price


class MDClient:
    """
    Listens to market data multicast and maintains order book state.
    Provides best bid/offer for each symbol.
    """

    def __init__(self, mcast_ip: str = MD_MCAST_IP,
                 mcast_port: int = MD_MCAST_PORT,
                 bind_ip: str = MCAST_BIND_IP):
        self.mcast_ip = mcast_ip
        self.mcast_port = mcast_port
        self.bind_ip = bind_ip
        
        self._lock = threading.Lock()
        # order_id -> Order
        self._orders: Dict[int, Order] = {}
        # symbol -> {price -> total_quantity} for bids (descending) and asks (ascending)
        self._bids: Dict[int, Dict[int, int]] = defaultdict(lambda: defaultdict(int))
        self._asks: Dict[int, Dict[int, int]] = defaultdict(lambda: defaultdict(int))
        
        self._last_seq_num = 0
        self._socket = None
        self._running = False
        self._thread = None

    def start(self):
        """Start listening to market data in a background thread."""
        self._socket = self._create_socket()
        self._running = True
        self._thread = threading.Thread(target=self._listen_loop, daemon=True)
        self._thread.start()
        print(f"MDClient: Listening on {self.mcast_ip}:{self.mcast_port}")

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
            pass
        
        sock.bind((self.mcast_ip, self.mcast_port))
        
        mreq = struct.pack("4s4s",
                          socket.inet_aton(self.mcast_ip),
                          socket.inet_aton(self.bind_ip))
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
        sock.settimeout(0.5)
        
        return sock

    def _listen_loop(self):
        """Main loop to receive and process market data messages."""
        while self._running:
            try:
                data, _ = self._socket.recvfrom(4096)
                self._process_message(data)
            except socket.timeout:
                continue
            except Exception as e:
                if self._running:
                    print(f"MDClient error: {e}")

    def _process_message(self, data: bytes):
        """Parse and process a market data message."""
        if len(data) < HEADER_SIZE:
            return
        
        magic, length, seq_num, timestamp, msg_type = struct.unpack_from(HEADER_FORMAT, data)
        
        if magic != MD_MAGIC_NUMBER:
            return
        
        self._last_seq_num = seq_num
        
        if msg_type == MSG_TYPE_NEW_ORDER:
            self._process_new_order(data)
        elif msg_type == MSG_TYPE_DELETE_ORDER:
            self._process_delete_order(data)
        elif msg_type == MSG_TYPE_MODIFY_ORDER:
            self._process_modify_order(data)
        # Ignore trades and heartbeats for BBO purposes

    def _process_new_order(self, data: bytes):
        """Process a new order message."""
        if len(data) < NEW_ORDER_SIZE:
            return
        
        (magic, length, seq_num, timestamp, msg_type,
         order_id, symbol, side, quantity, price, flags) = struct.unpack_from(NEW_ORDER_FORMAT, data)
        
        with self._lock:
            order = Order(order_id, symbol, side, quantity, price)
            self._orders[order_id] = order
            
            if side == SIDE_BUY:
                self._bids[symbol][price] += quantity
            else:
                self._asks[symbol][price] += quantity

    def _process_delete_order(self, data: bytes):
        """Process a delete order message."""
        if len(data) < DELETE_ORDER_SIZE:
            return
        
        (magic, length, seq_num, timestamp, msg_type, order_id) = struct.unpack_from(DELETE_ORDER_FORMAT, data)
        
        with self._lock:
            order = self._orders.pop(order_id, None)
            if order:
                if order.side == SIDE_BUY:
                    self._bids[order.symbol][order.price] -= order.quantity
                    if self._bids[order.symbol][order.price] <= 0:
                        del self._bids[order.symbol][order.price]
                else:
                    self._asks[order.symbol][order.price] -= order.quantity
                    if self._asks[order.symbol][order.price] <= 0:
                        del self._asks[order.symbol][order.price]

    def _process_modify_order(self, data: bytes):
        """Process a modify order message."""
        if len(data) < MODIFY_ORDER_SIZE:
            return
        
        (magic, length, seq_num, timestamp, msg_type,
         order_id, side, quantity, price) = struct.unpack_from(MODIFY_ORDER_FORMAT, data)
        
        with self._lock:
            order = self._orders.get(order_id)
            if order:
                # Remove old quantity from book
                if order.side == SIDE_BUY:
                    self._bids[order.symbol][order.price] -= order.quantity
                    if self._bids[order.symbol][order.price] <= 0:
                        del self._bids[order.symbol][order.price]
                else:
                    self._asks[order.symbol][order.price] -= order.quantity
                    if self._asks[order.symbol][order.price] <= 0:
                        del self._asks[order.symbol][order.price]
                
                # Update order and add new quantity
                order.side = side
                order.quantity = quantity
                order.price = price
                
                if side == SIDE_BUY:
                    self._bids[order.symbol][price] += quantity
                else:
                    self._asks[order.symbol][price] += quantity

    def get_best_bid(self, symbol: int) -> Tuple[int, int]:
        """Get best bid price and quantity for a symbol. Returns (price, qty) or (0, 0)."""
        with self._lock:
            bids = self._bids.get(symbol, {})
            if bids:
                price = max(bids.keys())
                return (price, bids[price])
            return (0, 0)

    def get_best_ask(self, symbol: int) -> Tuple[int, int]:
        """Get best ask price and quantity for a symbol. Returns (price, qty) or (0, 0)."""
        with self._lock:
            asks = self._asks.get(symbol, {})
            if asks:
                price = min(asks.keys())
                return (price, asks[price])
            return (0, 0)

    def get_bbo(self, symbol: int) -> Dict:
        """Get best bid and offer for a symbol."""
        bid_price, bid_qty = self.get_best_bid(symbol)
        ask_price, ask_qty = self.get_best_ask(symbol)
        return {
            "symbol": symbol,
            "best_bid": bid_price,
            "bid_qty": bid_qty,
            "best_ask": ask_price,
            "ask_qty": ask_qty,
        }
