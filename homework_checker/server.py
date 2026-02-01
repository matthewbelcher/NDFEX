#!/usr/bin/env python3
"""
NDFEX Homework Checker Server

Validates student submissions of order book state against the live market data feed.
Students must correctly report:
- Sequence number
- Symbol (1=GOLD, 2=BLUE)
- Best bid price and quantity
- Best ask price and quantity
- Trade summaries they observe

The server connects to the web_data WebSocket to get the "correct" answers,
indexed by sequence number for accurate validation.
"""

import asyncio
import json
import sqlite3
import time
from collections import OrderedDict
from dataclasses import dataclass, asdict
from typing import Dict, Optional
from aiohttp import web, ClientSession, WSMsgType
import argparse
import logging

logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)


@dataclass
class BBOState:
    """Best Bid/Offer state for a symbol at a specific sequence number"""
    seq_num: int
    symbol: int
    best_bid_price: int
    best_bid_qty: int
    best_ask_price: int
    best_ask_qty: int


@dataclass
class TradeSummary:
    """A trade summary observation"""
    seq_num: int
    symbol: int
    aggressor_side: int  # 1=BUY, 2=SELL
    quantity: int
    price: int


class LRUCache(OrderedDict):
    """Simple LRU cache using OrderedDict"""
    def __init__(self, maxsize=10000):
        super().__init__()
        self.maxsize = maxsize

    def __setitem__(self, key, value):
        if key in self:
            self.move_to_end(key)
        super().__setitem__(key, value)
        if len(self) > self.maxsize:
            oldest = next(iter(self))
            del self[oldest]


class StateTracker:
    """Tracks market state indexed by sequence number for validation"""

    def __init__(self, history_size: int = 10000):
        # BBO states indexed by (seq_num, symbol) -> BBOState
        self.bbo_by_seq: LRUCache = LRUCache(maxsize=history_size)
        # Trade summaries indexed by (seq_num, symbol, side, qty, price) for dedup
        self.trades_seen: LRUCache = LRUCache(maxsize=history_size)
        # Set of trade keys we've already counted for volume (dedup against repeated sends)
        self.volume_counted_trades: set = set()
        self.volume_counted_trades_maxsize = history_size * 2
        # Cumulative volume per symbol: symbol -> [(seq_num, cumulative_volume), ...]
        # Kept sorted by seq_num for binary search
        self.volume_history: Dict[int, list] = {1: [], 2: []}  # GOLD=1, BLUE=2
        self.volume_history_maxsize = history_size
        # Current state for debugging
        self.current_seq_num: int = 0
        self.current_bbo: Dict[int, BBOState] = {}
        self.last_update = 0

    def update_from_snapshot(self, seq_num: int, timestamp: int, snapshots: list, trades: list):
        """Update state from a web_data WebSocket message"""
        self.current_seq_num = seq_num
        self.last_update = timestamp

        # Store BBO state for each symbol at this seq_num
        for snap in snapshots:
            symbol = snap['symbol']
            state = BBOState(
                seq_num=seq_num,
                symbol=symbol,
                best_bid_price=snap.get('best_bid', 0),
                best_bid_qty=snap.get('best_bid_qty', 0),
                best_ask_price=snap.get('best_ask', 0),
                best_ask_qty=snap.get('best_ask_qty', 0)
            )
            self.bbo_by_seq[(seq_num, symbol)] = state
            self.current_bbo[symbol] = state

        # Store trades and accumulate volume using per-trade seq_num
        # web_data sends its full trade buffer every ~100ms, so we must deduplicate
        for trade in trades:
            trade_seq = trade.get('seq_num', seq_num)  # Use trade's seq_num, fallback to snapshot seq_num
            key = (trade['symbol'], trade['aggressor_side'],
                   trade['quantity'], trade['price'])
            self.trades_seen[key] = TradeSummary(
                seq_num=trade_seq,
                symbol=trade['symbol'],
                aggressor_side=trade['aggressor_side'],
                quantity=trade['quantity'],
                price=trade['price']
            )

            # Deduplicate trades for volume counting using a unique key per trade
            volume_key = (
                trade_seq,
                trade['symbol'],
                trade['aggressor_side'],
                trade['quantity'],
                trade['price'],
                trade.get('timestamp', 0),
            )
            if volume_key in self.volume_counted_trades:
                continue  # Already counted this trade

            self.volume_counted_trades.add(volume_key)
            # Evict old entries if the set grows too large
            if len(self.volume_counted_trades) > self.volume_counted_trades_maxsize:
                self.volume_counted_trades.clear()

            # Update cumulative volume history for this trade's symbol at its seq_num
            symbol = trade['symbol']
            history = self.volume_history[symbol]

            # Get previous cumulative volume
            prev_cumulative = history[-1][1] if history else 0
            new_cumulative = prev_cumulative + trade['quantity']

            # Only add entry if seq_num advanced (avoid duplicates)
            if not history or history[-1][0] < trade_seq:
                history.append((trade_seq, new_cumulative))

                # Trim history if too large
                if len(history) > self.volume_history_maxsize:
                    self.volume_history[symbol] = history[-self.volume_history_maxsize:]
            elif history[-1][0] == trade_seq:
                # Same seq_num, update cumulative volume
                history[-1] = (trade_seq, new_cumulative)

    def validate_bbo(self, seq_num: int, symbol: int, bid_price: int, bid_qty: int,
                     ask_price: int, ask_qty: int) -> tuple[bool, str]:
        """
        Validate a student's BBO submission against state at the given seq_num.
        Returns (is_valid, message)
        """
        key = (seq_num, symbol)

        if key not in self.bbo_by_seq:
            # Check if seq_num is too old or too new
            if self.current_seq_num == 0:
                return False, "No market data available yet"
            elif seq_num > self.current_seq_num:
                return False, f"Sequence number {seq_num} is in the future (current: {self.current_seq_num})"
            elif seq_num < self.current_seq_num - 10000:
                return False, f"Sequence number {seq_num} is too old (current: {self.current_seq_num})"
            else:
                return False, f"Sequence number {seq_num} not found in history"

        state = self.bbo_by_seq[key]

        if (state.best_bid_price == bid_price and
            state.best_bid_qty == bid_qty and
            state.best_ask_price == ask_price and
            state.best_ask_qty == ask_qty):
            return True, "Correct"

        return False, (f"Incorrect for seq_num={seq_num}. "
                      f"Expected bid={state.best_bid_qty}@{state.best_bid_price}, "
                      f"ask={state.best_ask_qty}@{state.best_ask_price}. "
                      f"Got bid={bid_qty}@{bid_price}, ask={ask_qty}@{ask_price}")

    def validate_trade(self, symbol: int, aggressor_side: int, quantity: int,
                       price: int) -> tuple[bool, str]:
        """
        Validate a student's trade observation.
        Returns (is_valid, message)
        """
        key = (symbol, aggressor_side, quantity, price)
        if key in self.trades_seen:
            return True, "Correct"
        return False, "Trade not found in recent history"

    def _find_cumulative_volume_at_seq(self, symbol: int, seq_num: int) -> Optional[int]:
        """Find cumulative volume at or before given seq_num using binary search"""
        import bisect
        history = self.volume_history.get(symbol, [])
        if not history:
            return None

        # Binary search for the rightmost entry <= seq_num
        # history is list of (seq_num, cumulative_volume) tuples
        idx = bisect.bisect_right(history, (seq_num, float('inf'))) - 1
        if idx < 0:
            return 0  # Before first recorded trade
        return history[idx][1]

    def get_volume_range(self, symbol: int, start_seq: int, end_seq: int) -> Optional[tuple[int, str]]:
        """
        Get total traded volume for symbol between start_seq and end_seq (inclusive).
        Returns (volume, message) or (None, error_message)
        """
        if symbol not in self.volume_history:
            return None, f"Unknown symbol {symbol}"

        history = self.volume_history[symbol]
        if not history:
            return None, "No trade history available yet"

        min_seq = history[0][0]
        max_seq = history[-1][0]

        if end_seq < min_seq:
            return None, f"Range {start_seq}-{end_seq} is before recorded history (starts at {min_seq})"
        if start_seq > max_seq:
            return None, f"Range {start_seq}-{end_seq} is after recorded history (ends at {max_seq})"

        # Get cumulative volume at end and at (start - 1)
        vol_at_end = self._find_cumulative_volume_at_seq(symbol, end_seq)
        vol_before_start = self._find_cumulative_volume_at_seq(symbol, start_seq - 1)

        if vol_at_end is None:
            return None, "Could not find volume at end of range"

        vol_before_start = vol_before_start or 0
        volume = vol_at_end - vol_before_start

        return volume, "OK"

    def get_volume_range_bounds(self, symbol: int) -> Optional[tuple[int, int]]:
        """Get the (min_seq, max_seq) for which we have volume data"""
        history = self.volume_history.get(symbol, [])
        if not history or len(history) < 2:
            return None
        return (history[0][0], history[-1][0])


class ScoreTracker:
    """Tracks student scores in SQLite"""

    def __init__(self, db_path: str = "scores.db"):
        self.db_path = db_path
        self._init_db()

    def _init_db(self):
        conn = sqlite3.connect(self.db_path)
        c = conn.cursor()
        c.execute('''
            CREATE TABLE IF NOT EXISTS scores (
                student_id TEXT PRIMARY KEY,
                bbo_correct INTEGER DEFAULT 0,
                bbo_incorrect INTEGER DEFAULT 0,
                trades_correct INTEGER DEFAULT 0,
                trades_incorrect INTEGER DEFAULT 0,
                last_submission TEXT,
                created_at TEXT DEFAULT CURRENT_TIMESTAMP
            )
        ''')
        c.execute('''
            CREATE TABLE IF NOT EXISTS submissions (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                student_id TEXT,
                submission_type TEXT,
                is_correct INTEGER,
                details TEXT,
                timestamp TEXT DEFAULT CURRENT_TIMESTAMP
            )
        ''')
        conn.commit()
        conn.close()

    def record_bbo_submission(self, student_id: str, is_correct: bool, details: str = ""):
        conn = sqlite3.connect(self.db_path)
        c = conn.cursor()

        # Ensure student exists
        c.execute('INSERT OR IGNORE INTO scores (student_id) VALUES (?)', (student_id,))

        # Update score
        if is_correct:
            c.execute('UPDATE scores SET bbo_correct = bbo_correct + 1, last_submission = CURRENT_TIMESTAMP WHERE student_id = ?',
                     (student_id,))
        else:
            c.execute('UPDATE scores SET bbo_incorrect = bbo_incorrect + 1, last_submission = CURRENT_TIMESTAMP WHERE student_id = ?',
                     (student_id,))

        # Log submission (but don't log every single one to avoid huge DB)
        # Only log incorrect or every 100th correct
        c.execute('SELECT bbo_correct FROM scores WHERE student_id = ?', (student_id,))
        count = c.fetchone()[0]
        if not is_correct or count % 100 == 0:
            c.execute('INSERT INTO submissions (student_id, submission_type, is_correct, details) VALUES (?, ?, ?, ?)',
                     (student_id, 'bbo', 1 if is_correct else 0, details[:500]))

        conn.commit()
        conn.close()

    def record_trade_submission(self, student_id: str, is_correct: bool, details: str = ""):
        conn = sqlite3.connect(self.db_path)
        c = conn.cursor()

        c.execute('INSERT OR IGNORE INTO scores (student_id) VALUES (?)', (student_id,))

        if is_correct:
            c.execute('UPDATE scores SET trades_correct = trades_correct + 1, last_submission = CURRENT_TIMESTAMP WHERE student_id = ?',
                     (student_id,))
        else:
            c.execute('UPDATE scores SET trades_incorrect = trades_incorrect + 1, last_submission = CURRENT_TIMESTAMP WHERE student_id = ?',
                     (student_id,))

        conn.commit()
        conn.close()

    def get_score(self, student_id: str) -> dict:
        conn = sqlite3.connect(self.db_path)
        c = conn.cursor()
        c.execute('SELECT bbo_correct, bbo_incorrect, trades_correct, trades_incorrect FROM scores WHERE student_id = ?',
                 (student_id,))
        row = c.fetchone()
        conn.close()

        if row:
            return {
                'student_id': student_id,
                'bbo_correct': row[0],
                'bbo_incorrect': row[1],
                'trades_correct': row[2],
                'trades_incorrect': row[3],
                'bbo_total': row[0] + row[1],
                'trades_total': row[2] + row[3]
            }
        return {
            'student_id': student_id,
            'bbo_correct': 0, 'bbo_incorrect': 0,
            'trades_correct': 0, 'trades_incorrect': 0,
            'bbo_total': 0, 'trades_total': 0
        }

    def get_all_scores(self) -> list:
        conn = sqlite3.connect(self.db_path)
        c = conn.cursor()
        c.execute('SELECT student_id, bbo_correct, bbo_incorrect, trades_correct, trades_incorrect FROM scores ORDER BY bbo_correct DESC')
        rows = c.fetchall()
        conn.close()

        return [{
            'student_id': row[0],
            'bbo_correct': row[1],
            'bbo_incorrect': row[2],
            'trades_correct': row[3],
            'trades_incorrect': row[4]
        } for row in rows]


class HomeworkChecker:
    """Main application class"""

    def __init__(self, ws_url: str, required_correct: int = 1000):
        self.ws_url = ws_url
        self.required_correct = required_correct
        self.state = StateTracker()
        self.scores = ScoreTracker()
        self.ws_connected = False

    async def connect_websocket(self):
        """Connect to web_data WebSocket and track state"""
        while True:
            try:
                async with ClientSession() as session:
                    async with session.ws_connect(self.ws_url) as ws:
                        logger.info(f"Connected to WebSocket at {self.ws_url}")
                        self.ws_connected = True

                        async for msg in ws:
                            if msg.type == WSMsgType.TEXT:
                                try:
                                    data = json.loads(msg.data)
                                    self._process_ws_message(data)
                                except json.JSONDecodeError:
                                    logger.warning(f"Invalid JSON from WebSocket: {msg.data[:100]}")
                            elif msg.type == WSMsgType.ERROR:
                                logger.error(f"WebSocket error: {ws.exception()}")
                                break

            except Exception as e:
                logger.error(f"WebSocket connection failed: {e}")
                self.ws_connected = False

            logger.info("WebSocket disconnected, reconnecting in 5 seconds...")
            await asyncio.sleep(5)

    def _process_ws_message(self, data: dict):
        """Process a message from web_data WebSocket"""
        seq_num = data.get('seq_num', 0)
        timestamp = data.get('timestamp', int(time.time() * 1e9))
        snapshots = data.get('snapshot', [])
        trades = data.get('trades', [])

        self.state.update_from_snapshot(seq_num, timestamp, snapshots, trades)

    # HTTP Handlers
    async def handle_submit_bbo(self, request: web.Request) -> web.Response:
        """Handle BBO submission from student"""
        try:
            data = await request.json()
        except json.JSONDecodeError:
            return web.json_response({'error': 'Invalid JSON'}, status=400)

        required = ['student_id', 'seq_num', 'symbol', 'best_bid_price', 'best_bid_qty',
                    'best_ask_price', 'best_ask_qty']
        missing = [f for f in required if f not in data]
        if missing:
            return web.json_response({'error': f'Missing fields: {missing}'}, status=400)

        if not self.ws_connected:
            return web.json_response({'error': 'Market data feed not connected'}, status=503)

        is_correct, message = self.state.validate_bbo(
            seq_num=data['seq_num'],
            symbol=data['symbol'],
            bid_price=data['best_bid_price'],
            bid_qty=data['best_bid_qty'],
            ask_price=data['best_ask_price'],
            ask_qty=data['best_ask_qty']
        )

        self.scores.record_bbo_submission(data['student_id'], is_correct, message)
        score = self.scores.get_score(data['student_id'])

        return web.json_response({
            'correct': is_correct,
            'message': message,
            'score': score,
            'passing': score['bbo_correct'] >= self.required_correct
        })

    async def handle_submit_trade(self, request: web.Request) -> web.Response:
        """Handle trade submission from student"""
        try:
            data = await request.json()
        except json.JSONDecodeError:
            return web.json_response({'error': 'Invalid JSON'}, status=400)

        required = ['student_id', 'symbol', 'aggressor_side', 'quantity', 'price']
        missing = [f for f in required if f not in data]
        if missing:
            return web.json_response({'error': f'Missing fields: {missing}'}, status=400)

        if not self.ws_connected:
            return web.json_response({'error': 'Market data feed not connected'}, status=503)

        is_correct, message = self.state.validate_trade(
            symbol=data['symbol'],
            aggressor_side=data['aggressor_side'],
            quantity=data['quantity'],
            price=data['price']
        )

        self.scores.record_trade_submission(data['student_id'], is_correct, message)
        score = self.scores.get_score(data['student_id'])

        return web.json_response({
            'correct': is_correct,
            'message': message,
            'score': score
        })

    async def handle_get_status(self, request: web.Request) -> web.Response:
        """Get student's current score"""
        student_id = request.match_info.get('student_id')
        if not student_id:
            return web.json_response({'error': 'Missing student_id'}, status=400)

        score = self.scores.get_score(student_id)
        score['passing'] = score['bbo_correct'] >= self.required_correct
        score['required_correct'] = self.required_correct

        return web.json_response(score)

    async def handle_get_current(self, request: web.Request) -> web.Response:
        """Get current market state (for debugging/testing)"""
        if not self.ws_connected:
            return web.json_response({'error': 'Market data feed not connected'}, status=503)

        return web.json_response({
            'connected': self.ws_connected,
            'current_seq_num': self.state.current_seq_num,
            'symbols': {
                symbol: asdict(state) if state else None
                for symbol, state in self.state.current_bbo.items()
            }
        })

    async def handle_get_volume(self, request: web.Request) -> web.Response:
        """Get traded volume for a symbol between two sequence numbers"""
        try:
            symbol = int(request.query.get('symbol', 0))
            start_seq = int(request.query.get('start_seq', 0))
            end_seq = int(request.query.get('end_seq', 0))
        except ValueError:
            return web.json_response({'error': 'Invalid parameters'}, status=400)

        if not symbol or not start_seq or not end_seq:
            return web.json_response({
                'error': 'Missing required parameters: symbol, start_seq, end_seq'
            }, status=400)

        if start_seq > end_seq:
            return web.json_response({'error': 'start_seq must be <= end_seq'}, status=400)

        if not self.ws_connected:
            return web.json_response({'error': 'Market data feed not connected'}, status=503)

        result = self.state.get_volume_range(symbol, start_seq, end_seq)
        if result[0] is None:
            return web.json_response({'error': result[1]}, status=400)

        return web.json_response({
            'symbol': symbol,
            'start_seq': start_seq,
            'end_seq': end_seq,
            'volume': result[0]
        })

    async def handle_get_volume_bounds(self, request: web.Request) -> web.Response:
        """Get the sequence number range for which we have volume data"""
        if not self.ws_connected:
            return web.json_response({'error': 'Market data feed not connected'}, status=503)

        bounds = {}
        for symbol in [1, 2]:
            result = self.state.get_volume_range_bounds(symbol)
            if result:
                bounds[symbol] = {'min_seq': result[0], 'max_seq': result[1]}

        return web.json_response({
            'current_seq_num': self.state.current_seq_num,
            'volume_bounds': bounds
        })

    async def handle_leaderboard(self, request: web.Request) -> web.Response:
        """Get all student scores"""
        scores = self.scores.get_all_scores()
        return web.json_response({
            'required_correct': self.required_correct,
            'students': scores
        })

    async def handle_health(self, request: web.Request) -> web.Response:
        """Health check endpoint"""
        return web.json_response({
            'status': 'ok',
            'ws_connected': self.ws_connected,
            'current_seq_num': self.state.current_seq_num,
            'last_update': self.state.last_update
        })

    def create_app(self) -> web.Application:
        """Create the aiohttp application"""
        app = web.Application()
        app.router.add_post('/submit/bbo', self.handle_submit_bbo)
        app.router.add_post('/submit/trade', self.handle_submit_trade)
        app.router.add_get('/status/{student_id}', self.handle_get_status)
        app.router.add_get('/current', self.handle_get_current)
        app.router.add_get('/volume', self.handle_get_volume)
        app.router.add_get('/volume/bounds', self.handle_get_volume_bounds)
        app.router.add_get('/leaderboard', self.handle_leaderboard)
        app.router.add_get('/health', self.handle_health)
        return app

    async def run(self, host: str = '0.0.0.0', port: int = 8080):
        """Run the server"""
        app = self.create_app()

        # Start WebSocket connection in background
        asyncio.create_task(self.connect_websocket())

        runner = web.AppRunner(app)
        await runner.setup()
        site = web.TCPSite(runner, host, port)
        await site.start()

        logger.info(f"Homework checker running on http://{host}:{port}")
        logger.info(f"Required correct submissions: {self.required_correct}")

        # Keep running
        while True:
            await asyncio.sleep(3600)


def main():
    parser = argparse.ArgumentParser(description='NDFEX Homework Checker Server')
    parser.add_argument('--ws-host', default='localhost', help='WebSocket host (default: localhost)')
    parser.add_argument('--ws-port', type=int, default=9002, help='WebSocket port (default: 9002)')
    parser.add_argument('--port', type=int, default=8080, help='HTTP server port (default: 8080)')
    parser.add_argument('--required', type=int, default=1000,
                        help='Required correct submissions for passing (default: 1000)')
    args = parser.parse_args()

    ws_url = f"ws://{args.ws_host}:{args.ws_port}"
    checker = HomeworkChecker(ws_url=ws_url, required_correct=args.required)

    asyncio.run(checker.run(port=args.port))


if __name__ == '__main__':
    main()
