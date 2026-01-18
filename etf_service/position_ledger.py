"""
Position Ledger

Combines clearing data with ETF create/redeem adjustments.
This is the authoritative source for positions including ETF conversions.
"""

import threading
from collections import defaultdict
from typing import Dict, Tuple, List, Optional

from config import ETF_SYMBOL, UNDERLYING_SYMBOLS, SYMBOLS, get_ticker
from clearing_client import ClearingClient


class PositionLedger:
    """
    Tracks positions by combining:
    1. Real fills from clearing multicast (via ClearingClient)
    2. ETF create/redeem adjustments (tracked locally)
    
    Thread-safe for concurrent access.
    """

    def __init__(self, clearing_client: ClearingClient):
        self.clearing = clearing_client
        
        # ETF adjustments: {client_id: {symbol: adjustment}}
        # Positive = credited, Negative = debited
        self._lock = threading.Lock()
        self._etf_adjustments: Dict[int, Dict[int, int]] = defaultdict(lambda: defaultdict(int))
        
        # Track create/redeem history for auditing
        self._history: List[Dict] = []

    def get_position(self, client_id: int, symbol: int) -> int:
        """
        Get the effective position for a client/symbol pair.
        Returns clearing position + ETF adjustment.
        """
        clearing_pos = self.clearing.get_position(client_id, symbol)
        with self._lock:
            adjustment = self._etf_adjustments[client_id][symbol]
        return clearing_pos + adjustment

    def get_all_positions(self, client_id: int) -> Dict[int, int]:
        """Get all positions for a client."""
        result = {}
        for symbol in SYMBOLS.keys():
            pos = self.get_position(client_id, symbol)
            if pos != 0:
                result[symbol] = pos
        return result

    def create_etf(self, client_id: int, amount: int) -> Tuple[bool, str]:
        """
        Create ETF shares by exchanging underlying positions.
        
        Requires: `amount` shares of each underlying symbol.
        Result: Debits underlyings, credits UNDY.
        
        Returns: (success, message)
        """
        if amount <= 0:
            return False, "Amount must be positive"
        
        # Check if client has sufficient underlying positions
        insufficient = []
        for symbol in UNDERLYING_SYMBOLS:
            pos = self.get_position(client_id, symbol)
            if pos < amount:
                ticker = get_ticker(symbol)
                insufficient.append(f"{ticker}: have {pos}, need {amount}")
        
        if insufficient:
            return False, f"Insufficient positions: {', '.join(insufficient)}"
        
        # Apply adjustments atomically
        with self._lock:
            for symbol in UNDERLYING_SYMBOLS:
                self._etf_adjustments[client_id][symbol] -= amount
            self._etf_adjustments[client_id][ETF_SYMBOL] += amount
            
            self._history.append({
                "type": "create",
                "client_id": client_id,
                "amount": amount,
            })
        
        return True, f"Created {amount} UNDY from underlying positions"

    def redeem_etf(self, client_id: int, amount: int) -> Tuple[bool, str]:
        """
        Redeem ETF shares back to underlying positions.
        
        Requires: `amount` UNDY shares.
        Result: Debits UNDY, credits underlyings.
        
        Returns: (success, message)
        """
        if amount <= 0:
            return False, "Amount must be positive"
        
        # Check if client has sufficient UNDY
        undy_pos = self.get_position(client_id, ETF_SYMBOL)
        if undy_pos < amount:
            return False, f"Insufficient UNDY: have {undy_pos}, need {amount}"
        
        # Apply adjustments atomically
        with self._lock:
            self._etf_adjustments[client_id][ETF_SYMBOL] -= amount
            for symbol in UNDERLYING_SYMBOLS:
                self._etf_adjustments[client_id][symbol] += amount
            
            self._history.append({
                "type": "redeem",
                "client_id": client_id,
                "amount": amount,
            })
        
        return True, f"Redeemed {amount} UNDY to underlying positions"

    def get_etf_adjustments(self, client_id: int) -> Dict[int, int]:
        """Get the ETF adjustments for a client (for debugging)."""
        with self._lock:
            return dict(self._etf_adjustments[client_id])

    def get_all_clients_data(self, md_client=None) -> List[Dict]:
        """
        Get position data for all clients in the format expected by the dashboard.
        
        Returns list of dicts with: client_id, symbol, position, pnl, volume
        """
        result = []
        
        # Get all known client IDs from both clearing and adjustments
        client_ids = self.clearing.get_all_client_ids()
        with self._lock:
            client_ids = client_ids | set(self._etf_adjustments.keys())
        
        clearing_positions = self.clearing.get_positions()
        clearing_pnl = self.clearing.get_raw_pnl()
        clearing_volume = self.clearing.get_volume()
        
        for client_id in client_ids:
            for symbol in SYMBOLS.keys():
                # Get effective position (clearing + adjustment)
                clearing_pos = clearing_positions.get(client_id, {}).get(symbol, 0)
                with self._lock:
                    adjustment = self._etf_adjustments[client_id][symbol]
                position = clearing_pos + adjustment
                
                # Get PnL and volume from clearing (adjustments don't affect these directly)
                pnl = clearing_pnl.get(client_id, {}).get(symbol, 0)
                volume = clearing_volume.get(client_id, {}).get(symbol, 0)
                
                # Calculate mark-to-market PnL if we have market data
                if md_client and position != 0:
                    if position > 0:
                        bid_price, _ = md_client.get_best_bid(symbol)
                        if bid_price > 0:
                            pnl = pnl + (bid_price * position)
                    else:
                        ask_price, _ = md_client.get_best_ask(symbol)
                        if ask_price > 0:
                            pnl = pnl + (ask_price * position)
                    # Subtract fees (0.05 per share traded)
                    pnl -= volume * 0.05
                
                # Only include if there's any activity
                if position != 0 or pnl != 0 or volume != 0:
                    result.append({
                        "client_id": client_id,
                        "symbol": symbol,
                        "position": position,
                        "pnl": pnl,
                        "volume": volume,
                    })
        
        return result

    def get_history(self) -> List[Dict]:
        """Get create/redeem history."""
        with self._lock:
            return list(self._history)
