"""
ETF Service Configuration

Symbol definitions and network settings for the NDFEX ETF service.
"""

# Network configuration (defaults, can be overridden via command line)
CLEARING_MCAST_IP = "239.0.0.2"
CLEARING_MCAST_PORT = 12346
MD_MCAST_IP = "239.0.0.1"
MD_MCAST_PORT = 12345
SNAPSHOT_MCAST_IP = "239.0.0.3"
MCAST_BIND_IP = "127.0.0.1"

WEBSOCKET_PORT = 9002
REST_PORT = 5000

# Symbol definitions
# Format: {symbol_id: {"ticker": str, "name": str, "tick_size": int}}
SYMBOLS = {
    1: {"ticker": "GOLD", "name": "Gold", "tick_size": 10},
    2: {"ticker": "BLUE", "name": "Blue", "tick_size": 5},
    # UNDY ETF underlying components (Notre Dame dorms)
    # Men's dorms
    3: {"ticker": "KNAN", "name": "Keenan Hall", "tick_size": 5},
    4: {"ticker": "STED", "name": "St. Edward's Hall", "tick_size": 5},
    5: {"ticker": "FISH", "name": "Fisher Hall", "tick_size": 5},
    6: {"ticker": "DILN", "name": "Dillon Hall", "tick_size": 5},
    7: {"ticker": "SORN", "name": "Sorin Hall", "tick_size": 5},
    # Women's dorms
    8: {"ticker": "RYAN", "name": "Ryan Hall", "tick_size": 5},
    9: {"ticker": "LYON", "name": "Lyons Hall", "tick_size": 5},
    10: {"ticker": "WLSH", "name": "Walsh Hall", "tick_size": 5},
    11: {"ticker": "LEWI", "name": "Lewis Hall", "tick_size": 5},
    12: {"ticker": "BDIN", "name": "Badin Hall", "tick_size": 5},
    # ETF
    13: {"ticker": "UNDY", "name": "Notre Dame Dorm ETF", "tick_size": 10},
}

# ETF configuration
ETF_SYMBOL = 13  # UNDY
UNDERLYING_SYMBOLS = [3, 4, 5, 6, 7, 8, 9, 10, 11, 12]  # All dorm symbols

# Helper functions
def get_ticker(symbol_id: int) -> str:
    """Get ticker string for a symbol ID."""
    return SYMBOLS.get(symbol_id, {}).get("ticker", f"SYM{symbol_id}")

def get_symbol_id(ticker: str) -> int:
    """Get symbol ID for a ticker string."""
    for sym_id, info in SYMBOLS.items():
        if info["ticker"] == ticker:
            return sym_id
    return -1
