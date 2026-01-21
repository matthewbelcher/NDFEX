#!/usr/bin/env python3
"""
ETF Service - Main Application

Provides:
1. REST API for ETF create/redeem operations
2. WebSocket server for dashboard (replaces web_data.cpp)
3. Position tracking via clearing multicast + ETF adjustments

Usage:
    python app.py <md_mcast_ip> <clearing_mcast_ip> <mcast_bind_ip> [--rest-port PORT] [--ws-port PORT]
"""

import argparse
import asyncio
import json
import signal
import sys
import threading
import time
from functools import wraps

from flask import Flask, jsonify, request, render_template

# Import with proper handling for running as main
try:
    from config import (
        SYMBOLS, ETF_SYMBOL, UNDERLYING_SYMBOLS,
        MD_MCAST_PORT, CLEARING_MCAST_PORT, WEBSOCKET_PORT, REST_PORT,
        get_ticker
    )
    from clearing_client import ClearingClient
    from md_client import MDClient
    from position_ledger import PositionLedger
except ImportError:
    # Running from parent directory
    from etf_service.config import (
        SYMBOLS, ETF_SYMBOL, UNDERLYING_SYMBOLS,
        MD_MCAST_PORT, CLEARING_MCAST_PORT, WEBSOCKET_PORT, REST_PORT,
        get_ticker
    )
    from etf_service.clearing_client import ClearingClient
    from etf_service.md_client import MDClient
    from etf_service.position_ledger import PositionLedger

# Try to import websockets, provide helpful error if missing
try:
    import websockets
except ImportError:
    print("ERROR: websockets package not installed. Run: pip install websockets")
    sys.exit(1)


# Global instances (initialized in main)
clearing_client: ClearingClient = None
md_client: MDClient = None
ledger: PositionLedger = None

# Flask app
app = Flask(__name__)


# ============================================================================
# REST API Endpoints
# ============================================================================

@app.route('/health')
def health():
    """Health check endpoint."""
    return jsonify({"status": "ok", "service": "etf_service"})


@app.route('/symbols')
def get_symbols():
    """Get all symbol definitions."""
    return jsonify({
        "symbols": [
            {"id": sym_id, **info}
            for sym_id, info in SYMBOLS.items()
        ],
        "etf_symbol": ETF_SYMBOL,
        "underlying_symbols": UNDERLYING_SYMBOLS,
    })


@app.route('/positions/<int:client_id>')
def get_positions(client_id: int):
    """Get all positions for a client."""
    positions = ledger.get_all_positions(client_id)
    return jsonify({
        "client_id": client_id,
        "positions": {
            get_ticker(sym): qty
            for sym, qty in positions.items()
        }
    })


@app.route('/positions/<int:client_id>/<int:symbol>')
def get_position(client_id: int, symbol: int):
    """Get position for a specific client/symbol."""
    position = ledger.get_position(client_id, symbol)
    return jsonify({
        "client_id": client_id,
        "symbol": symbol,
        "ticker": get_ticker(symbol),
        "position": position,
    })


@app.route('/create', methods=['POST'])
def create_etf():
    """
    Create UNDY ETF shares from underlying positions.
    
    Request body:
        {"client_id": int, "amount": int}
    
    Response:
        {"success": bool, "message": str, "undy_balance": int}
    """
    data = request.get_json()
    if not data:
        return jsonify({"success": False, "message": "Missing JSON body"}), 400
    
    client_id = data.get("client_id")
    amount = data.get("amount")
    
    if client_id is None or amount is None:
        return jsonify({"success": False, "message": "Missing client_id or amount"}), 400
    
    try:
        client_id = int(client_id)
        amount = int(amount)
    except (ValueError, TypeError):
        return jsonify({"success": False, "message": "Invalid client_id or amount"}), 400
    
    success, message = ledger.create_etf(client_id, amount)
    undy_balance = ledger.get_position(client_id, ETF_SYMBOL)
    
    status_code = 200 if success else 400
    return jsonify({
        "success": success,
        "message": message,
        "undy_balance": undy_balance,
    }), status_code


@app.route('/redeem', methods=['POST'])
def redeem_etf():
    """
    Redeem UNDY ETF shares back to underlying positions.
    
    Request body:
        {"client_id": int, "amount": int}
    
    Response:
        {"success": bool, "message": str, "undy_balance": int}
    """
    data = request.get_json()
    if not data:
        return jsonify({"success": False, "message": "Missing JSON body"}), 400
    
    client_id = data.get("client_id")
    amount = data.get("amount")
    
    if client_id is None or amount is None:
        return jsonify({"success": False, "message": "Missing client_id or amount"}), 400
    
    try:
        client_id = int(client_id)
        amount = int(amount)
    except (ValueError, TypeError):
        return jsonify({"success": False, "message": "Invalid client_id or amount"}), 400
    
    success, message = ledger.redeem_etf(client_id, amount)
    undy_balance = ledger.get_position(client_id, ETF_SYMBOL)
    
    status_code = 200 if success else 400
    return jsonify({
        "success": success,
        "message": message,
        "undy_balance": undy_balance,
    }), status_code


@app.route('/history')
def get_history():
    """Get ETF create/redeem history."""
    return jsonify({"history": ledger.get_history()})


@app.route('/')
def index():
    """Simple web UI for ETF operations."""
    return render_template('etf.html', symbols=SYMBOLS, etf_symbol=ETF_SYMBOL)


# ============================================================================
# WebSocket Server (replaces web_data.cpp)
# ============================================================================

async def websocket_handler(websocket, path):
    """
    Handle WebSocket connections for the dashboard.
    Sends periodic snapshots in the same format as web_data.cpp.
    """
    print(f"WebSocket: New connection from {websocket.remote_address}")
    try:
        while True:
            # Build snapshot in the format expected by clearing-web-app
            snapshot = build_dashboard_snapshot()
            await websocket.send(json.dumps(snapshot))
            await asyncio.sleep(0.1)  # 100ms interval
    except websockets.exceptions.ConnectionClosed:
        print(f"WebSocket: Connection closed from {websocket.remote_address}")
    except Exception as e:
        print(f"WebSocket error: {e}")


def build_dashboard_snapshot() -> dict:
    """
    Build the snapshot JSON in the format expected by clearing-web-app.
    
    Format:
    {
        "timestamp": int (nanoseconds),
        "snapshot": [{"symbol": int, "best_bid": int, "best_ask": int}, ...],
        "positions": [{"client_id": int, "symbol": int, "position": int, "pnl": float, "volume": int}, ...]
    }
    """
    timestamp = time.time_ns()
    
    # Market data snapshot
    snapshot = []
    for symbol in SYMBOLS.keys():
        bid_price, _ = md_client.get_best_bid(symbol)
        ask_price, _ = md_client.get_best_ask(symbol)
        snapshot.append({
            "symbol": symbol,
            "best_bid": bid_price,
            "best_ask": ask_price,
        })
    
    # Position data (includes ETF adjustments)
    positions = ledger.get_all_clients_data(md_client)
    
    return {
        "timestamp": timestamp,
        "snapshot": snapshot,
        "positions": positions,
    }


def run_websocket_server(port: int):
    """Run the WebSocket server in its own event loop."""
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    
    start_server = websockets.serve(websocket_handler, "0.0.0.0", port)
    print(f"WebSocket server starting on port {port}")
    
    loop.run_until_complete(start_server)
    loop.run_forever()


# ============================================================================
# Main
# ============================================================================

def main():
    global clearing_client, md_client, ledger
    
    parser = argparse.ArgumentParser(description="NDFEX ETF Service")
    parser.add_argument("md_mcast_ip", help="Market data multicast IP")
    parser.add_argument("clearing_mcast_ip", help="Clearing multicast IP")
    parser.add_argument("mcast_bind_ip", help="Local interface IP for multicast")
    parser.add_argument("--rest-port", type=int, default=REST_PORT, help="REST API port")
    parser.add_argument("--ws-port", type=int, default=WEBSOCKET_PORT, help="WebSocket port")
    
    args = parser.parse_args()
    
    print("=" * 60)
    print("NDFEX ETF Service")
    print("=" * 60)
    print(f"Market Data:  {args.md_mcast_ip}:{MD_MCAST_PORT}")
    print(f"Clearing:     {args.clearing_mcast_ip}:{CLEARING_MCAST_PORT}")
    print(f"Bind IP:      {args.mcast_bind_ip}")
    print(f"REST API:     http://0.0.0.0:{args.rest_port}")
    print(f"WebSocket:    ws://0.0.0.0:{args.ws_port}")
    print("=" * 60)
    
    # Initialize clients
    clearing_client = ClearingClient(
        mcast_ip=args.clearing_mcast_ip,
        mcast_port=CLEARING_MCAST_PORT,
        bind_ip=args.mcast_bind_ip
    )
    
    md_client = MDClient(
        mcast_ip=args.md_mcast_ip,
        mcast_port=MD_MCAST_PORT,
        bind_ip=args.mcast_bind_ip
    )
    
    ledger = PositionLedger(clearing_client)
    
    # Start multicast listeners
    clearing_client.start()
    md_client.start()
    
    # Start WebSocket server in background thread
    ws_thread = threading.Thread(
        target=run_websocket_server,
        args=(args.ws_port,),
        daemon=True
    )
    ws_thread.start()
    
    # Handle shutdown gracefully
    def shutdown(signum, frame):
        print("\nShutting down...")
        clearing_client.stop()
        md_client.stop()
        sys.exit(0)
    
    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)
    
    # Run Flask (blocking)
    print(f"\nStarting REST API on port {args.rest_port}...")
    app.run(host="0.0.0.0", port=args.rest_port, threaded=True)


if __name__ == "__main__":
    main()
