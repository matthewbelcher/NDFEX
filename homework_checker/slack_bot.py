#!/usr/bin/env python3
"""
NDFEX Homework Checker Slack Bot

Challenge/response system where:
1. Student DMs the bot saying they're ready (with their team name)
2. Bot issues a challenge: "What was the BBO for GOLD at seq_num 12345?"
3. Student responds with: bid_qty bid_price ask_qty ask_price
4. Bot validates and tracks correct answers
5. After X correct answers, team passes and bot announces to class channel

Usage:
    export SLACK_BOT_TOKEN=xoxb-...
    export SLACK_APP_TOKEN=xapp-...
    export SLACK_CHANNEL_ID=C...  # Channel for announcements
    uv run slack_bot.py
"""

import asyncio
import csv
import json
import logging
import os
import random
import re
import sqlite3
import time
import urllib.request
import urllib.error
from dataclasses import dataclass, field
from datetime import date, datetime
from enum import Enum
from typing import Optional, Dict, Tuple
from collections import OrderedDict


class ChallengeType(Enum):
    BBO = "bbo"
    VOLUME = "volume"

from slack_bolt.async_app import AsyncApp
from slack_bolt.adapter.socket_mode.async_handler import AsyncSocketModeHandler

logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

# Configuration
REQUIRED_CORRECT = int(os.environ.get("REQUIRED_CORRECT", "10"))
HOMEWORK_CHECKER_URL = os.environ.get("HOMEWORK_CHECKER_URL", "http://localhost:8080")
SLACK_CHANNEL_ID = os.environ.get("SLACK_CHANNEL_ID", "")  # For announcements
INSTRUCTOR_USER_ID = os.environ.get("INSTRUCTOR_USER_ID", "")  # DM instructor on completion
TEAMS_FILE = os.environ.get("TEAMS_FILE", "teams.csv")
ME_LOG_DIR = os.environ.get("ME_LOG_DIR", os.path.join(os.path.dirname(__file__), "..", "matching_engine", "logs"))
USERS_FILE = os.environ.get("USERS_FILE", os.path.join(os.path.dirname(__file__), "..", "matching_engine", "users.txt"))
TICK_SIZES = {1: 10, 2: 5}  # GOLD tick=10, BLUE tick=5

SYMBOL_NAMES = {1: "GOLD", 2: "BLUE"}


@dataclass
class Challenge:
    """An active challenge for a user"""
    team_name: str
    challenge_type: ChallengeType
    symbol: int
    issued_at: float = field(default_factory=time.time)
    # BBO challenge fields
    seq_num: Optional[int] = None
    expected_bid_price: Optional[int] = None
    expected_bid_qty: Optional[int] = None
    expected_ask_price: Optional[int] = None
    expected_ask_qty: Optional[int] = None
    # Volume challenge fields
    start_seq: Optional[int] = None
    end_seq: Optional[int] = None
    expected_volume: Optional[int] = None


@dataclass
class HW2Session:
    """An active HW2 session for a team"""
    team_name: str
    client_id: int
    username: str
    started_at: str                     # "YYYY-MM-DD HH:MM:SS.mmm"
    started_by: str                     # slack_user_id
    dm_channel: str                     # Slack DM channel for sending updates
    passed: bool = False
    disconnected: bool = False
    steps: Optional[list] = None        # List of (result_key, instruction_text) tuples
    results: Dict[str, bool] = field(default_factory=lambda: {
        "login": False, "buy_gold": False, "sell_gold": False,
        "buy_blue": False, "sell_blue": False,
        "reject_bad_tick": False, "reject_dup_oid": False, "reject_zero_qty": False,
        "fill": False,
        "cancel": False, "modify_2": False, "modify_fill": False,
    })


class LRUCache(OrderedDict):
    """Simple LRU cache"""
    def __init__(self, maxsize=5000):
        super().__init__()
        self.maxsize = maxsize

    def __setitem__(self, key, value):
        if key in self:
            self.move_to_end(key)
        super().__setitem__(key, value)
        if len(self) > self.maxsize:
            oldest = next(iter(self))
            del self[oldest]


class HomeworkBot:
    # Compiled regexes for ME log parsing
    RE_LOGIN = re.compile(r'Client (\d+) logged in successfully')
    RE_NEW_ORDER = re.compile(r'Received new order from client (\d+): order id: (\d+) symbol: (\d+) side: (\d+)')
    RE_FILL = re.compile(r'Sending fill to client (\d+) seq \d+ order id (\d+) quantity (\d+) price (\d+) flags (\d+)')
    RE_MODIFY = re.compile(r'Received modify order from client (\d+), order id: (\d+)')
    RE_CLOSE = re.compile(r'Sending close to client (\d+) seq \d+ order id (\d+)')
    RE_CANCEL_ALL = re.compile(r'Cancelling all orders for client (\d+)')
    RE_REJECT = re.compile(r'Sending reject to client (\d+) seq \d+ order id (\d+) reason (\d+)')

    def __init__(self):
        self.app = AsyncApp(token=os.environ.get("SLACK_BOT_TOKEN"))

        # State history from homework checker - (seq_num, symbol) -> BBOState
        self.state_history: LRUCache = LRUCache(maxsize=1000)
        self.current_seq_num = 0

        # Volume bounds from homework checker - symbol -> (min_seq, max_seq)
        self.volume_bounds: Dict[int, Tuple[int, int]] = {}

        # Active challenges per user - slack_user_id -> Challenge
        self.active_challenges: Dict[str, Challenge] = {}

        # Team scores - team_name -> {"correct": int, "incorrect": int, "passed": bool}
        self.team_scores: Dict[str, dict] = {}

        # Teams that have already been announced (HW1)
        self.announced_teams: set = set()

        # Team membership: slack_name (lowercase) -> team_name
        self.team_membership: Dict[str, str] = {}

        # Cache: slack_user_id -> team_name (to avoid repeated API lookups)
        self.user_team_cache: Dict[str, Optional[str]] = {}

        # HW2 state
        self.team_client_ids: Dict[str, int] = {}      # username -> client_id
        self.team_passwords: Dict[str, str] = {}        # username -> password
        self.hw2_sessions: Dict[str, HW2Session] = {}   # team_name -> session
        self.hw2_announced_teams: set = set()
        self.hw2_monitor_tasks: Dict[str, asyncio.Task] = {}  # team_name -> background task

        # Load team configuration
        self._load_teams()
        self._load_users()

        # Load persisted scores
        self._init_db()
        self._load_scores()
        self._load_hw2_sessions()

        # Register handlers
        self._register_handlers()

    def _load_teams(self):
        """Load team membership from CSV file"""
        if not os.path.exists(TEAMS_FILE):
            logger.warning(f"Teams file not found: {TEAMS_FILE}")
            return

        try:
            with open(TEAMS_FILE, 'r') as f:
                reader = csv.DictReader(f)
                for row in reader:
                    slack_name = row.get('slack_name', '').strip().lower()
                    team = row.get('team', '').strip()
                    if slack_name and team:
                        self.team_membership[slack_name] = team
            logger.info(f"Loaded {len(self.team_membership)} team memberships from {TEAMS_FILE}")
        except Exception as e:
            logger.error(f"Failed to load teams file: {e}")

    def _load_users(self):
        """Load username -> client_id and username -> password from users.txt"""
        if not os.path.exists(USERS_FILE):
            logger.warning(f"Users file not found: {USERS_FILE}")
            return

        try:
            with open(USERS_FILE, 'r') as f:
                for line in f:
                    parts = line.strip().split()
                    if len(parts) >= 3:
                        client_id = int(parts[0])
                        username = parts[1]
                        password = parts[2]
                        self.team_client_ids[username] = client_id
                        self.team_passwords[username] = password
            logger.info(f"Loaded {len(self.team_client_ids)} users from {USERS_FILE}")
        except Exception as e:
            logger.error(f"Failed to load users file: {e}")

    async def _get_user_team(self, client, user_id: str) -> Optional[str]:
        """Look up user's team from their Slack profile"""
        # Check cache first
        if user_id in self.user_team_cache:
            return self.user_team_cache[user_id]

        try:
            # Get user info from Slack
            result = await client.users_info(user=user_id)
            user = result.get("user", {})

            # Try matching by real_name, display_name, or email
            names_to_try = [
                user.get("real_name", ""),
                user.get("profile", {}).get("display_name", ""),
                user.get("profile", {}).get("real_name", ""),
                user.get("name", ""),  # username
            ]

            # Also try email prefix (before @)
            email = user.get("profile", {}).get("email", "")
            if email:
                names_to_try.append(email.split("@")[0])

            for name in names_to_try:
                name_lower = name.strip().lower()
                if name_lower in self.team_membership:
                    team = self.team_membership[name_lower]
                    self.user_team_cache[user_id] = team
                    logger.info(f"Matched user {user_id} ({name}) to team {team}")
                    return team

            # No match found
            self.user_team_cache[user_id] = None
            logger.info(f"No team found for user {user_id} (tried: {names_to_try})")
            return None

        except Exception as e:
            logger.error(f"Failed to look up user {user_id}: {e}")
            return None

    def _init_db(self):
        """Initialize SQLite database for persistence"""
        self.db_path = "slack_scores.db"
        conn = sqlite3.connect(self.db_path)
        c = conn.cursor()
        c.execute('''
            CREATE TABLE IF NOT EXISTS team_scores (
                team_name TEXT PRIMARY KEY,
                correct INTEGER DEFAULT 0,
                incorrect INTEGER DEFAULT 0,
                passed INTEGER DEFAULT 0,
                last_activity TEXT DEFAULT CURRENT_TIMESTAMP
            )
        ''')
        c.execute('''
            CREATE TABLE IF NOT EXISTS challenge_log (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                team_name TEXT,
                slack_user_id TEXT,
                seq_num INTEGER,
                symbol INTEGER,
                is_correct INTEGER,
                response TEXT,
                timestamp TEXT DEFAULT CURRENT_TIMESTAMP
            )
        ''')
        c.execute('''
            CREATE TABLE IF NOT EXISTS hw2_sessions (
                team_name TEXT PRIMARY KEY,
                client_id INTEGER,
                username TEXT,
                started_at TEXT,
                started_by TEXT,
                dm_channel TEXT,
                passed INTEGER DEFAULT 0,
                results TEXT DEFAULT '{}'
            )
        ''')
        c.execute('''
            CREATE TABLE IF NOT EXISTS hw2_check_log (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                team_name TEXT,
                slack_user_id TEXT,
                results TEXT,
                all_passed INTEGER,
                timestamp TEXT DEFAULT CURRENT_TIMESTAMP
            )
        ''')
        conn.commit()
        conn.close()

    def _load_scores(self):
        """Load scores from database"""
        conn = sqlite3.connect(self.db_path)
        c = conn.cursor()
        c.execute('SELECT team_name, correct, incorrect, passed FROM team_scores')
        for row in c.fetchall():
            self.team_scores[row[0]] = {
                "correct": row[1],
                "incorrect": row[2],
                "passed": bool(row[3])
            }
            if row[3]:
                self.announced_teams.add(row[0])
        conn.close()
        logger.info(f"Loaded {len(self.team_scores)} team scores from database")

    def _save_score(self, team_name: str):
        """Save team score to database"""
        score = self.team_scores.get(team_name, {"correct": 0, "incorrect": 0, "passed": False})
        conn = sqlite3.connect(self.db_path)
        c = conn.cursor()
        c.execute('''
            INSERT OR REPLACE INTO team_scores (team_name, correct, incorrect, passed, last_activity)
            VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP)
        ''', (team_name, score["correct"], score["incorrect"], 1 if score["passed"] else 0))
        conn.commit()
        conn.close()

    def _log_challenge(self, team_name: str, user_id: str, seq_num: int, symbol: int,
                       is_correct: bool, response: str):
        """Log challenge attempt"""
        conn = sqlite3.connect(self.db_path)
        c = conn.cursor()
        c.execute('''
            INSERT INTO challenge_log (team_name, slack_user_id, seq_num, symbol, is_correct, response)
            VALUES (?, ?, ?, ?, ?, ?)
        ''', (team_name, user_id, seq_num, symbol, 1 if is_correct else 0, response[:500]))
        conn.commit()
        conn.close()

    def _save_hw2_session(self, session: HW2Session):
        """Save HW2 session to database"""
        conn = sqlite3.connect(self.db_path)
        c = conn.cursor()
        c.execute('''
            INSERT OR REPLACE INTO hw2_sessions (team_name, client_id, username, started_at, started_by, dm_channel, passed, results)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ''', (session.team_name, session.client_id, session.username, session.started_at,
              session.started_by, session.dm_channel, 1 if session.passed else 0,
              json.dumps(session.results)))
        conn.commit()
        conn.close()

    def _load_hw2_sessions(self):
        """Load HW2 sessions from database"""
        conn = sqlite3.connect(self.db_path)
        c = conn.cursor()
        c.execute('SELECT team_name, client_id, username, started_at, started_by, dm_channel, passed, results FROM hw2_sessions')
        for row in c.fetchall():
            session = HW2Session(
                team_name=row[0],
                client_id=row[1],
                username=row[2],
                started_at=row[3],
                started_by=row[4],
                dm_channel=row[5],
                passed=bool(row[6]),
                results=json.loads(row[7]) if row[7] else {},
            )
            self.hw2_sessions[row[0]] = session
            if session.passed:
                self.hw2_announced_teams.add(row[0])
        conn.close()
        logger.info(f"Loaded {len(self.hw2_sessions)} HW2 sessions from database")

    def _log_hw2_check(self, team_name: str, user_id: str, results: dict, all_passed: bool):
        """Log an HW2 check attempt"""
        conn = sqlite3.connect(self.db_path)
        c = conn.cursor()
        c.execute('''
            INSERT INTO hw2_check_log (team_name, slack_user_id, results, all_passed)
            VALUES (?, ?, ?, ?)
        ''', (team_name, user_id, json.dumps(results), 1 if all_passed else 0))
        conn.commit()
        conn.close()

    async def sync_state_from_checker(self):
        """Periodically sync state history from homework checker"""
        while True:
            try:
                # Sync BBO state
                url = f"{HOMEWORK_CHECKER_URL}/current"
                req = urllib.request.Request(url)
                with urllib.request.urlopen(req, timeout=5) as response:
                    data = json.loads(response.read().decode())

                seq_num = data.get("current_seq_num", 0)
                if seq_num > self.current_seq_num:
                    self.current_seq_num = seq_num

                    # Store state for each symbol
                    for symbol_id, symbol_data in data.get("symbols", {}).items():
                        key = (seq_num, int(symbol_id))
                        self.state_history[key] = {
                            "best_bid_price": symbol_data["best_bid_price"],
                            "best_bid_qty": symbol_data["best_bid_qty"],
                            "best_ask_price": symbol_data["best_ask_price"],
                            "best_ask_qty": symbol_data["best_ask_qty"]
                        }

                # Sync volume bounds (less frequently - every 5 seconds)
                if int(time.time()) % 10 == 0:
                    vol_url = f"{HOMEWORK_CHECKER_URL}/volume/bounds"
                    vol_req = urllib.request.Request(vol_url)
                    with urllib.request.urlopen(vol_req, timeout=5) as vol_response:
                        vol_data = json.loads(vol_response.read().decode())

                    for symbol_str, bounds in vol_data.get("volume_bounds", {}).items():
                        symbol = int(symbol_str)
                        self.volume_bounds[symbol] = (bounds["min_seq"], bounds["max_seq"])

            except Exception as e:
                logger.error(f"Failed to sync state: {e}")

            await asyncio.sleep(0.5)  # Sync every 500ms

    def _get_random_bbo_challenge(self) -> Optional[Tuple[int, int, dict]]:
        """Get a random (seq_num, symbol, state) from history for BBO challenge"""
        if len(self.state_history) < 10:
            return None

        # Pick from older entries (not the most recent ones)
        keys = list(self.state_history.keys())
        # Use entries from 10-90% of history to avoid too recent or too old
        start_idx = len(keys) // 10
        end_idx = int(len(keys) * 0.9)
        if end_idx <= start_idx:
            end_idx = len(keys) - 1
            start_idx = 0

        key = random.choice(keys[start_idx:end_idx])
        seq_num, symbol = key
        state = self.state_history[key]
        return seq_num, symbol, state

    def _get_random_volume_challenge(self) -> Optional[Tuple[int, int, int, int]]:
        """Get a random volume challenge (symbol, start_seq, end_seq, expected_volume)"""
        # Need volume bounds for at least one symbol
        if not self.volume_bounds:
            return None

        # Clamp volume bounds to roughly match BBO cache depth
        oldest_seq = min((k[0] for k in self.state_history), default=0) if self.state_history else 0

        # Pick a random symbol that has volume data
        available_symbols = [s for s in self.volume_bounds if self.volume_bounds[s][1] - max(self.volume_bounds[s][0], oldest_seq) > 100]
        if not available_symbols:
            return None

        symbol = random.choice(available_symbols)
        min_seq, max_seq = self.volume_bounds[symbol]
        min_seq = max(min_seq, oldest_seq)

        # Pick a range within the available bounds (not too recent)
        # Use 10-80% of range to avoid edge cases
        range_size = max_seq - min_seq
        usable_start = min_seq + int(range_size * 0.1)
        usable_end = min_seq + int(range_size * 0.8)

        if usable_end - usable_start < 50:
            return None

        # Retry up to 5 times to find a range with nonzero volume
        for _ in range(5):
            # Pick a random range of 20-100 sequence numbers
            range_len = random.randint(20, min(100, usable_end - usable_start))
            start_seq = random.randint(usable_start, usable_end - range_len)
            end_seq = start_seq + range_len

            # Fetch the expected volume from the server
            try:
                url = f"{HOMEWORK_CHECKER_URL}/volume?symbol={symbol}&start_seq={start_seq}&end_seq={end_seq}"
                req = urllib.request.Request(url)
                with urllib.request.urlopen(req, timeout=5) as response:
                    data = json.loads(response.read().decode())
                expected_volume = data.get("volume", 0)
                if expected_volume > 0:
                    return symbol, start_seq, end_seq, expected_volume
            except Exception as e:
                logger.error(f"Failed to fetch volume for challenge: {e}")
                return None

        logger.warning("Could not find volume challenge with nonzero volume after 5 attempts")
        return None

    def _get_random_challenge(self) -> Optional[Challenge]:
        """Get a random challenge (BBO or volume)"""
        # 50/50 chance of BBO vs volume challenge, but fall back if one isn't available
        challenge_types = [ChallengeType.BBO, ChallengeType.VOLUME]
        random.shuffle(challenge_types)

        for ctype in challenge_types:
            if ctype == ChallengeType.BBO:
                result = self._get_random_bbo_challenge()
                if result:
                    seq_num, symbol, state = result
                    return Challenge(
                        team_name="",  # Will be set by caller
                        challenge_type=ChallengeType.BBO,
                        symbol=symbol,
                        seq_num=seq_num,
                        expected_bid_price=state["best_bid_price"],
                        expected_bid_qty=state["best_bid_qty"],
                        expected_ask_price=state["best_ask_price"],
                        expected_ask_qty=state["best_ask_qty"]
                    )
            else:  # VOLUME
                result = self._get_random_volume_challenge()
                if result:
                    symbol, start_seq, end_seq, expected_volume = result
                    return Challenge(
                        team_name="",  # Will be set by caller
                        challenge_type=ChallengeType.VOLUME,
                        symbol=symbol,
                        start_seq=start_seq,
                        end_seq=end_seq,
                        expected_volume=expected_volume
                    )

        return None

    def _register_handlers(self):
        """Register Slack event handlers"""

        @self.app.event("message")
        async def handle_message(event, say, client):
            """Handle DM messages"""
            # Only handle DMs (im channel type)
            channel_type = event.get("channel_type", "")
            if channel_type != "im":
                return

            user_id = event.get("user", "")
            text = event.get("text", "").strip().lower()

            # Ignore bot messages
            if event.get("bot_id"):
                return

            # HW2 commands (check before HW1)
            if text.startswith("start hw2"):
                await self._handle_hw2_ready(event, say, client)
            elif text.startswith("restart hw2"):
                await self._handle_hw2_restart(event, say, client)
            elif text.startswith("check hw2"):
                await self._handle_hw2_check(event, say, client)

            # HW1: Check for ready/start command
            elif any(word in text for word in ["ready", "start", "begin", "challenge", "quiz"]):
                await self._handle_ready(event, say, client)

            # Check for status command
            elif any(word in text for word in ["status", "score", "progress"]):
                await self._handle_status(event, say, client)

            # Check for help command
            elif any(word in text for word in ["help", "how", "instructions"]):
                await self._handle_help(event, say)

            # Check if user has active challenge - might be an answer
            elif user_id in self.active_challenges:
                await self._handle_answer(event, say, client)

            # Unknown command
            else:
                await say(
                    "I didn't understand that. Try:\n"
                    "• `ready` - Start an HW1 challenge\n"
                    "• `start hw2` - Start HW2 (exchange connectivity)\n"
                    "• `status` - Check your team's progress\n"
                    "• `help` - Show instructions"
                )

        @self.app.command("/homework")
        async def handle_slash_command(ack, command, say):
            """Handle /homework slash command"""
            await ack()
            text = command.get("text", "").strip()

            if text.startswith("leaderboard"):
                await self._handle_leaderboard(say)
            elif text.startswith("status"):
                parts = text.split()
                if len(parts) >= 2:
                    team_name = parts[1]
                    score = self.team_scores.get(team_name, {"correct": 0, "incorrect": 0, "passed": False})
                    hw1_status = "PASSED" if score["passed"] else f"{score['correct']}/{REQUIRED_CORRECT}"
                    msg = f"Team *{team_name}*\nHW1: {hw1_status} ({score['correct']} correct, {score['incorrect']} incorrect)"
                    hw2 = self.hw2_sessions.get(team_name)
                    if hw2:
                        done = sum(1 for v in hw2.results.values() if v)
                        total = len(hw2.results)
                        hw2_status = "PASSED" if hw2.passed else f"{done}/{total}"
                        msg += f"\nHW2: {hw2_status}"
                    await say(msg)
                else:
                    await say("Usage: `/homework status <team_name>`")
            else:
                await say(
                    "*Homework Bot Commands:*\n"
                    "• `/homework leaderboard` - Show all team scores\n"
                    "• `/homework status <team_name>` - Check a team's progress"
                )

    async def _handle_ready(self, event, say, client):
        """Handle ready command - issue a challenge"""
        user_id = event.get("user", "")
        text = event.get("text", "").strip()

        # Try to extract team name from message, or auto-detect from user profile
        match = re.search(r'(?:ready|start|begin|challenge|quiz)\s+(\S+)', text, re.IGNORECASE)
        if match:
            team_name = match.group(1)
        else:
            # Auto-detect team from Slack profile
            team_name = await self._get_user_team(client, user_id)
            if not team_name:
                await say(
                    "I couldn't find your team assignment.\n"
                    "Either specify your team: `ready team_alpha`\n"
                    "Or ask your instructor to add you to the teams.csv file."
                )
                return

        # Check if team already passed
        if team_name in self.team_scores and self.team_scores[team_name].get("passed"):
            await say(f"Team *{team_name}* has already passed! Congratulations! :tada:")
            return

        # Check if we have enough state history
        challenge = self._get_random_challenge()
        if not challenge:
            await say(
                "Not enough market data history yet. Please wait a minute and try again.\n"
                "Make sure you're running your order book client to capture the data!"
            )
            return

        # Set team name and store challenge
        challenge.team_name = team_name
        self.active_challenges[user_id] = challenge

        # Initialize team score if needed
        if team_name not in self.team_scores:
            self.team_scores[team_name] = {"correct": 0, "incorrect": 0, "passed": False}

        symbol_name = SYMBOL_NAMES.get(challenge.symbol, f"Symbol {challenge.symbol}")
        score = self.team_scores[team_name]

        if challenge.challenge_type == ChallengeType.BBO:
            await say(
                f"*Challenge for team {team_name}!*\n\n"
                f"What was the BBO for *{symbol_name}* at sequence number *{challenge.seq_num}*?\n\n"
                f"Reply with: `bid_qty bid_price ask_qty ask_price`\n"
                f"Example: `100 1250 50 1300`\n\n"
                f"_Current progress: {score['correct']}/{REQUIRED_CORRECT}_"
            )
        else:  # VOLUME
            await say(
                f"*Challenge for team {team_name}!*\n\n"
                f"What was the total traded volume for *{symbol_name}* "
                f"from sequence *{challenge.start_seq}* to *{challenge.end_seq}*?\n\n"
                f"Reply with a single number (the total quantity traded)\n"
                f"Example: `5000`\n\n"
                f"_Current progress: {score['correct']}/{REQUIRED_CORRECT}_"
            )

    async def _handle_answer(self, event, say, client):
        """Handle answer to a challenge"""
        user_id = event.get("user", "")
        text = event.get("text", "").strip()

        challenge = self.active_challenges.get(user_id)
        if not challenge:
            return

        team_name = challenge.team_name
        symbol_name = SYMBOL_NAMES.get(challenge.symbol, f"Symbol {challenge.symbol}")

        # Parse and check answer based on challenge type
        numbers = re.findall(r'\d+', text)

        if challenge.challenge_type == ChallengeType.BBO:
            if len(numbers) != 4:
                await say(
                    "Please provide 4 numbers: `bid_qty bid_price ask_qty ask_price`\n"
                    f"Example: `100 1250 50 1300`"
                )
                return

            bid_qty, bid_price, ask_qty, ask_price = map(int, numbers)
            is_correct = (
                bid_qty == challenge.expected_bid_qty and
                bid_price == challenge.expected_bid_price and
                ask_qty == challenge.expected_ask_qty and
                ask_price == challenge.expected_ask_price
            )
            user_answer = f"{bid_qty} {bid_price} {ask_qty} {ask_price}"
            log_seq_num = challenge.seq_num

        else:  # VOLUME
            if len(numbers) != 1:
                await say(
                    "Please provide a single number for the total volume.\n"
                    f"Example: `5000`"
                )
                return

            volume = int(numbers[0])
            is_correct = (volume == challenge.expected_volume)
            user_answer = str(volume)
            log_seq_num = challenge.start_seq  # Use start_seq for logging

        # Update score
        if is_correct:
            self.team_scores[team_name]["correct"] += 1
        else:
            self.team_scores[team_name]["incorrect"] += 1

        # Log and save
        self._log_challenge(
            team_name, user_id, log_seq_num, challenge.symbol,
            is_correct, user_answer
        )
        self._save_score(team_name)

        # Clear challenge
        del self.active_challenges[user_id]

        score = self.team_scores[team_name]

        if is_correct:
            # Check if team just passed
            if score["correct"] >= REQUIRED_CORRECT and not score["passed"]:
                score["passed"] = True
                self._save_score(team_name)

                await say(
                    f":tada: *CORRECT!* :tada:\n\n"
                    f"*Congratulations team {team_name}!* You've completed the homework!\n"
                    f"Final score: {score['correct']} correct, {score['incorrect']} incorrect"
                )

                # Announce to class channel
                await self._announce_completion(client, team_name, score)
            else:
                remaining = REQUIRED_CORRECT - score["correct"]
                await say(
                    f":white_check_mark: *Correct!*\n\n"
                    f"Progress: {score['correct']}/{REQUIRED_CORRECT} ({remaining} more to go)\n\n"
                    f"Type `ready` for the next challenge!"
                )
        else:
            # Build error message based on challenge type
            if challenge.challenge_type == ChallengeType.BBO:
                error_detail = (
                    f"For {symbol_name} BBO at seq_num {challenge.seq_num}:\n"
                    f"• Expected: `{challenge.expected_bid_qty} {challenge.expected_bid_price} "
                    f"{challenge.expected_ask_qty} {challenge.expected_ask_price}`\n"
                    f"• You said: `{user_answer}`"
                )
            else:  # VOLUME
                error_detail = (
                    f"For {symbol_name} volume from seq {challenge.start_seq} to {challenge.end_seq}:\n"
                    f"• Expected: `{challenge.expected_volume}`\n"
                    f"• You said: `{user_answer}`"
                )

            await say(
                f":x: *Incorrect*\n\n"
                f"{error_detail}\n\n"
                f"Progress: {score['correct']}/{REQUIRED_CORRECT}\n\n"
                f"Type `ready` to try again!"
            )

    # ---- HW2: Exchange Connectivity ----

    def _get_current_bbo(self, symbol: int) -> Optional[dict]:
        """Get the latest BBO for a symbol from state_history"""
        # Walk backwards through cache to find latest entry for this symbol
        for key in reversed(list(self.state_history.keys())):
            if key[1] == symbol:
                return self.state_history[key]
        return None

    def _generate_hw2_steps(self, session: HW2Session) -> Optional[list]:
        """Generate ordered HW2 steps based on current BBO. Returns list of (result_key, instruction_text)."""
        gold_bbo = self._get_current_bbo(1)
        blue_bbo = self._get_current_bbo(2)

        if not gold_bbo or not blue_bbo:
            return None

        gb = gold_bbo["best_bid_price"]
        ga = gold_bbo["best_ask_price"]
        bb = blue_bbo["best_bid_price"]
        ba = blue_bbo["best_ask_price"]

        if not all([gb, ga, bb, ba]):
            return None

        gold_tick = TICK_SIZES[1]  # 10
        blue_tick = TICK_SIZES[2]  # 5

        buy_gold_price = gb - 50 * gold_tick
        sell_gold_price = ga + 50 * gold_tick
        buy_blue_price = bb - 50 * blue_tick
        sell_blue_price = ba + 50 * blue_tick
        cross_buy_gold_price = ga
        modify_sell_gold_price = ga + 30 * gold_tick
        modify_cross_blue_price = bb

        # Invalid prices/quantities for reject testing
        bad_tick_price = ga + 1  # Not a multiple of GOLD tick (10)

        return [
            ("login",
             "*Step 1 — Login*\nLog in with your team's credentials"),
            ("buy_gold",
             f"*Step 2 — BUY GOLD* (resting order)\n```symbol: GOLD, side: BUY, qty: 5, price: {buy_gold_price}```"),
            ("sell_gold",
             f"*Step 3 — SELL GOLD* (resting order)\n```symbol: GOLD, side: SELL, qty: 3, price: {sell_gold_price}```"),
            ("buy_blue",
             f"*Step 4 — BUY BLUE* (resting order)\n```symbol: BLUE, side: BUY, qty: 7, price: {buy_blue_price}```"),
            ("sell_blue",
             f"*Step 5 — SELL BLUE* (resting order)\n```symbol: BLUE, side: SELL, qty: 2, price: {sell_blue_price}```"),
            ("reject_bad_tick",
             f"*Step 6 — Send an order with an invalid tick* (expect a REJECT)\n"
             f"```symbol: GOLD, side: BUY, qty: 5, price: {bad_tick_price}```\n"
             f"Price {bad_tick_price} is not a multiple of GOLD's tick size (10). "
             f"Handle the reject message without crashing."),
            ("reject_dup_oid",
             "*Step 7 — Send an order with a duplicate order ID* (expect a REJECT)\n"
             "Re-send a new order using the *same order ID* you used in step 2. "
             "Handle the reject message without crashing."),
            ("reject_zero_qty",
             "*Step 8 — Send an order with zero quantity* (expect a REJECT)\n"
             f"```symbol: GOLD, side: BUY, qty: 0, price: {buy_gold_price}```\n"
             "Handle the reject message without crashing."),
            ("fill",
             f"*Step 9 — BUY GOLD at the ask* (this will fill!)\n```symbol: GOLD, side: BUY, qty: 4, price: {cross_buy_gold_price}```"),
            ("cancel",
             "*Step 10 — Cancel the order from step 2*\nSend a cancel/delete for your resting GOLD BUY order"),
            ("modify_2",
             f"*Step 11 — Modify orders*\nModify the order from step 3:\n```new price: {modify_sell_gold_price}```\n"
             f"Then modify the order from step 5:\n```new price: {modify_cross_blue_price}``` (this one will fill!)"),
            ("modify_fill",
             None),  # No separate instruction — completing step 11's second modify triggers this
        ]

    def _get_next_step_message(self, session: HW2Session) -> Optional[str]:
        """Get the instruction text for the next incomplete step."""
        if not session.steps:
            return None
        for key, text in session.steps:
            if not session.results.get(key) and text is not None:
                return text
        return None

    async def _resolve_hw2_team(self, event, client) -> Optional[str]:
        """Extract team name from HW2 command or auto-detect from profile"""
        text = event.get("text", "").strip()
        user_id = event.get("user", "")
        # Try to extract team name after "hw2" keyword
        match = re.search(r'hw2\s+(\S+)', text, re.IGNORECASE)
        if match:
            candidate = match.group(1)
            # Ignore if it's another keyword
            if candidate.lower() not in ("ready", "start", "begin", "restart", "check", "status", "progress"):
                return candidate
        # Also try: "ready test2 hw2" style
        match = re.search(r'(?:ready|start|begin|restart|check)\s+(\S+)', text, re.IGNORECASE)
        if match:
            candidate = match.group(1)
            if candidate.lower() != "hw2":
                return candidate
        # Fall back to profile lookup
        return await self._get_user_team(client, user_id)

    async def _handle_hw2_ready(self, event, say, client):
        """Handle 'start hw2' — start HW2 session"""
        user_id = event.get("user", "")
        channel = event.get("channel", "")

        # Resolve team
        team_name = await self._resolve_hw2_team(event, client)
        if not team_name:
            await say(
                "I couldn't find your team assignment.\n"
                "Try: `start hw2 <team_name>`"
            )
            return

        # Look up client_id (team_name should match username in users.txt)
        username = team_name  # convention: team name = exchange username
        if username not in self.team_client_ids:
            await say(
                f"No exchange account found for team *{team_name}*.\n"
                "Ask your instructor to set up your exchange credentials."
            )
            return

        client_id = self.team_client_ids[username]

        # Check if already passed
        existing = self.hw2_sessions.get(team_name)
        if existing and existing.passed:
            await say(f"Team *{team_name}* has already completed HW2! :tada:")
            return

        # Validate market data available
        gold_bbo = self._get_current_bbo(1)
        blue_bbo = self._get_current_bbo(2)
        if (not gold_bbo or not blue_bbo or
            not all([gold_bbo["best_bid_price"], gold_bbo["best_ask_price"],
                     blue_bbo["best_bid_price"], blue_bbo["best_ask_price"]])):
            await say(
                "Market data not available yet (need valid BBO for both GOLD and BLUE).\n"
                "Please wait a moment and try again."
            )
            return

        # Create session
        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
        session = HW2Session(
            team_name=team_name,
            client_id=client_id,
            username=username,
            started_at=now,
            started_by=user_id,
            dm_channel=channel,
        )

        # Generate steps
        steps = self._generate_hw2_steps(session)
        if not steps:
            await say("Failed to generate instructions — market data may be incomplete. Try again shortly.")
            return
        session.steps = steps

        # Store and persist
        self.hw2_sessions[team_name] = session
        self._save_hw2_session(session)

        # Send intro + first step
        first_step = self._get_next_step_message(session)
        await say(
            f"*HW2: Exchange Connectivity*\n\n"
            f"I'll walk you through 12 steps. I'm monitoring the exchange logs — "
            f"complete each step and I'll send the next one.\n\n"
            f"_If you disconnect, type `restart hw2` to start over with fresh prices._\n\n"
            + first_step
        )

        # Cancel existing monitor if any
        old_task = self.hw2_monitor_tasks.get(team_name)
        if old_task and not old_task.done():
            old_task.cancel()

        # Start background monitor
        self.hw2_monitor_tasks[team_name] = asyncio.create_task(
            self._monitor_hw2(session, client)
        )
        logger.info(f"Started HW2 session for team {team_name} (client_id={client_id})")

    async def _handle_hw2_restart(self, event, say, client):
        """Handle 'restart hw2' — reset session with fresh prices"""
        user_id = event.get("user", "")
        channel = event.get("channel", "")

        team_name = await self._resolve_hw2_team(event, client)
        if not team_name:
            await say("I couldn't find your team assignment. Try: `restart hw2 <team_name>`")
            return

        existing = self.hw2_sessions.get(team_name)
        if not existing:
            await say("No HW2 session found. Type `start hw2` to start.")
            return

        if existing.passed:
            await say(f"Team *{team_name}* has already completed HW2! :tada:")
            return

        # Cancel existing monitor
        old_task = self.hw2_monitor_tasks.get(team_name)
        if old_task and not old_task.done():
            old_task.cancel()

        username = existing.username
        client_id = existing.client_id

        # Create fresh session
        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
        session = HW2Session(
            team_name=team_name,
            client_id=client_id,
            username=username,
            started_at=now,
            started_by=user_id,
            dm_channel=channel,
        )

        steps = self._generate_hw2_steps(session)
        if not steps:
            await say("Failed to generate instructions — market data may be incomplete. Try again shortly.")
            return
        session.steps = steps

        self.hw2_sessions[team_name] = session
        self._save_hw2_session(session)

        first_step = self._get_next_step_message(session)
        await say("*Session restarted with fresh prices!*\n\n" + first_step)

        self.hw2_monitor_tasks[team_name] = asyncio.create_task(
            self._monitor_hw2(session, client)
        )
        logger.info(f"Restarted HW2 session for team {team_name}")

    async def _handle_hw2_check(self, event, say, client):
        """Handle 'check hw2' — show current checklist status"""
        user_id = event.get("user", "")

        team_name = await self._resolve_hw2_team(event, client)
        if not team_name:
            await say("I couldn't find your team assignment. Try: `check hw2 <team_name>`")
            return

        session = self.hw2_sessions.get(team_name)
        if not session:
            await say("No HW2 session found. Type `start hw2` to start.")
            return

        self._log_hw2_check(team_name, user_id, session.results, all(session.results.values()))
        await say(self._format_hw2_checklist(session))

    def _format_hw2_checklist(self, session: HW2Session) -> str:
        """Format the HW2 checklist for display"""
        labels = {
            "login": "Login successfully",
            "buy_gold": "Buy order for GOLD",
            "sell_gold": "Sell order for GOLD",
            "buy_blue": "Buy order for BLUE",
            "sell_blue": "Sell order for BLUE",
            "reject_bad_tick": "Handle reject: invalid tick price",
            "reject_dup_oid": "Handle reject: duplicate order ID",
            "reject_zero_qty": "Handle reject: zero quantity",
            "fill": "At least 1 order filled",
            "cancel": "Cancel a resting order",
            "modify_2": "Modify at least 2 orders",
            "modify_fill": "Modify that results in a fill",
        }
        done_count = sum(1 for v in session.results.values() if v)
        total = len(session.results)
        lines = [f"*HW2 Checklist for {session.team_name}* ({done_count}/{total})\n"]
        for key, label in labels.items():
            check = ":white_check_mark:" if session.results.get(key) else ":x:"
            lines.append(f"{check} {label}")

        if session.passed:
            lines.append("\n:tada: *All complete!*")
        elif session.disconnected:
            lines.append("\n:warning: _You are disconnected. Reconnect and type `restart hw2`._")
        return "\n".join(lines)

    async def _monitor_hw2(self, session: HW2Session, slack_client):
        """Background task to monitor ME logs for HW2 progress"""
        cid = session.client_id
        cid_str = str(cid)

        # Find the current log file
        today = date.today().isoformat()
        log_file = os.path.join(ME_LOG_DIR, f"ME_{today}")

        # Start reading from end of file (only new activity)
        try:
            offset = os.path.getsize(log_file) if os.path.exists(log_file) else 0
        except OSError:
            offset = 0

        # Track order state for cancel/modify_fill detection
        filled_order_ids = set()   # order_ids that have received fills
        modified_order_ids = set() # order_ids that have been modified
        modify_count = 0

        logger.info(f"HW2 monitor started for {session.team_name} (client {cid}), log={log_file}, offset={offset}")

        try:
            while not session.passed:
                await asyncio.sleep(5)

                # Handle log file rollover at midnight
                new_today = date.today().isoformat()
                if new_today != today:
                    today = new_today
                    log_file = os.path.join(ME_LOG_DIR, f"ME_{today}")
                    offset = 0

                if not os.path.exists(log_file):
                    continue

                try:
                    file_size = os.path.getsize(log_file)
                except OSError:
                    continue

                if file_size <= offset:
                    continue

                # Read new bytes
                try:
                    with open(log_file, 'r') as f:
                        f.seek(offset)
                        new_data = f.read()
                    offset = file_size
                except OSError as e:
                    logger.error(f"HW2 monitor read error for {session.team_name}: {e}")
                    continue

                # Filter for lines mentioning this client (case-insensitive — login uses "Client", others use "client")
                client_patterns = (f"client {cid_str} ", f"client {cid_str}:", f"client {cid_str},", f"client {cid_str}\n")
                client_end = f"client {cid_str}"
                new_lines = []
                for line in new_data.splitlines():
                    ll = line.lower()
                    if any(p in ll for p in client_patterns) or ll.endswith(client_end):
                        new_lines.append(line)

                if not new_lines:
                    continue

                # Check for disconnect
                disconnect_seen = False
                for line in new_lines:
                    m = self.RE_CANCEL_ALL.search(line)
                    if m and int(m.group(1)) == cid:
                        disconnect_seen = True
                        break

                if disconnect_seen and not session.disconnected:
                    session.disconnected = True
                    try:
                        await slack_client.chat_postMessage(
                            channel=session.dm_channel,
                            text=":warning: *You disconnected!* Connect again and type `restart hw2` to start over."
                        )
                    except Exception as e:
                        logger.error(f"Failed to send disconnect warning to {session.team_name}: {e}")
                    continue

                # Check for re-login after disconnect
                if session.disconnected:
                    for line in new_lines:
                        m = self.RE_LOGIN.search(line)
                        if m and int(m.group(1)) == cid:
                            session.disconnected = False
                            break
                    if session.disconnected:
                        continue  # Still disconnected, skip progress

                # Parse events and update results
                old_results = dict(session.results)

                for line in new_lines:
                    # Login
                    m = self.RE_LOGIN.search(line)
                    if m and int(m.group(1)) == cid:
                        session.results["login"] = True
                        continue

                    # New order
                    m = self.RE_NEW_ORDER.search(line)
                    if m and int(m.group(1)) == cid:
                        symbol = int(m.group(3))
                        side = int(m.group(4))
                        if symbol == 1 and side == 1:
                            session.results["buy_gold"] = True
                        elif symbol == 1 and side == 2:
                            session.results["sell_gold"] = True
                        elif symbol == 2 and side == 1:
                            session.results["buy_blue"] = True
                        elif symbol == 2 and side == 2:
                            session.results["sell_blue"] = True
                        continue

                    # Fill
                    m = self.RE_FILL.search(line)
                    if m and int(m.group(1)) == cid:
                        order_id = int(m.group(2))
                        session.results["fill"] = True
                        filled_order_ids.add(order_id)
                        # Check if this order was previously modified → modify_fill
                        if order_id in modified_order_ids:
                            session.results["modify_fill"] = True
                        continue

                    # Modify
                    m = self.RE_MODIFY.search(line)
                    if m and int(m.group(1)) == cid:
                        order_id = int(m.group(2))
                        modify_count += 1
                        modified_order_ids.add(order_id)
                        if modify_count >= 2:
                            session.results["modify_2"] = True
                        continue

                    # Close (cancel) — only count if order was NOT filled
                    m = self.RE_CLOSE.search(line)
                    if m and int(m.group(1)) == cid:
                        order_id = int(m.group(2))
                        if order_id not in filled_order_ids:
                            session.results["cancel"] = True
                        continue

                    # Reject — check reason code
                    m = self.RE_REJECT.search(line)
                    if m and int(m.group(1)) == cid:
                        reason = int(m.group(3))
                        if reason == 5:  # INVALID_PRICE (bad tick)
                            session.results["reject_bad_tick"] = True
                        elif reason == 3:  # DUPLICATE_ORDER_ID
                            session.results["reject_dup_oid"] = True
                        elif reason == 6:  # INVALID_QUANTITY (zero qty)
                            session.results["reject_zero_qty"] = True
                        continue

                # DM progress for newly completed items + next step
                labels = {
                    "login": "Login",
                    "buy_gold": "Buy GOLD",
                    "sell_gold": "Sell GOLD",
                    "buy_blue": "Buy BLUE",
                    "sell_blue": "Sell BLUE",
                    "reject_bad_tick": "Reject: bad tick",
                    "reject_dup_oid": "Reject: dup order ID",
                    "reject_zero_qty": "Reject: zero qty",
                    "fill": "Order filled",
                    "cancel": "Order cancelled",
                    "modify_2": "2+ modifications",
                    "modify_fill": "Modify-to-fill",
                }
                newly_completed = [k for k, done in session.results.items() if done and not old_results.get(k)]
                if newly_completed:
                    done_count = sum(1 for v in session.results.values() if v)
                    total = len(session.results)
                    # Build message: checkmarks for completed items + next step
                    parts = []
                    for key in newly_completed:
                        parts.append(f":white_check_mark: *{labels.get(key, key)}* — complete! ({done_count}/{total})")
                    next_step = self._get_next_step_message(session)
                    if next_step:
                        parts.append(f"\n{next_step}")
                    try:
                        await slack_client.chat_postMessage(
                            channel=session.dm_channel,
                            text="\n".join(parts)
                        )
                    except Exception as e:
                        logger.error(f"Failed to send progress to {session.team_name}: {e}")

                # Save progress
                self._save_hw2_session(session)

                # Check if all done
                if all(session.results.values()):
                    session.passed = True
                    self._save_hw2_session(session)
                    try:
                        await slack_client.chat_postMessage(
                            channel=session.dm_channel,
                            text=(
                                ":tada: *Congratulations!* All HW2 items complete!\n\n"
                                + self._format_hw2_checklist(session)
                            )
                        )
                    except Exception as e:
                        logger.error(f"Failed to send completion to {session.team_name}: {e}")
                    await self._announce_hw2_completion(slack_client, session)
                    break

        except asyncio.CancelledError:
            logger.info(f"HW2 monitor cancelled for {session.team_name}")
        except Exception as e:
            logger.error(f"HW2 monitor error for {session.team_name}: {e}", exc_info=True)

    async def _announce_hw2_completion(self, client, session: HW2Session):
        """Announce HW2 completion to class channel and instructor"""
        team_name = session.team_name
        if team_name in self.hw2_announced_teams:
            return

        self.hw2_announced_teams.add(team_name)

        hw2_passed_count = sum(1 for s in self.hw2_sessions.values() if s.passed)

        if SLACK_CHANNEL_ID:
            try:
                await client.chat_postMessage(
                    channel=SLACK_CHANNEL_ID,
                    text=(
                        f":tada: *Team {team_name} has completed HW2 (Exchange Connectivity)!* :tada:\n\n"
                        f"Congratulations! :clap:"
                    )
                )
                logger.info(f"Announced HW2 completion for team {team_name}")
            except Exception as e:
                logger.error(f"Failed to announce HW2 completion: {e}")

        if INSTRUCTOR_USER_ID:
            try:
                await client.chat_postMessage(
                    channel=INSTRUCTOR_USER_ID,
                    text=(
                        f":bell: *Team {team_name}* just completed HW2!\n"
                        f"HW2 teams completed: {hw2_passed_count}"
                    )
                )
            except Exception as e:
                logger.error(f"Failed to DM instructor about HW2: {e}")

    async def _handle_status(self, event, say, client):
        """Handle status request"""
        user_id = event.get("user", "")
        text = event.get("text", "").strip()

        match = re.search(r'(?:status|score|progress)\s+(\S+)', text, re.IGNORECASE)
        if match:
            team_name = match.group(1)
        else:
            # Auto-detect team from Slack profile
            team_name = await self._get_user_team(client, user_id)
            if not team_name:
                await say("Usage: `status <team_name>` (or ask your instructor to add you to teams.csv)")
                return
        score = self.team_scores.get(team_name, {"correct": 0, "incorrect": 0, "passed": False})

        if score["passed"]:
            status_emoji = ":trophy:"
            status_text = "PASSED"
        else:
            status_emoji = ":hourglass:"
            status_text = f"{score['correct']}/{REQUIRED_CORRECT}"

        hw2_status = ""
        hw2_session = self.hw2_sessions.get(team_name)
        if hw2_session:
            hw2_done = sum(1 for v in hw2_session.results.values() if v)
            hw2_total = len(hw2_session.results)
            if hw2_session.passed:
                hw2_status = f"\n\n*HW2*: :trophy: PASSED ({hw2_done}/{hw2_total})"
            else:
                hw2_status = f"\n\n*HW2*: {hw2_done}/{hw2_total} items complete"

        await say(
            f"{status_emoji} *Team {team_name}*\n\n"
            f"*HW1*: {status_text}\n"
            f"Correct: {score['correct']}\n"
            f"Incorrect: {score['incorrect']}\n"
            + ("Congratulations! :tada:" if score["passed"] else "Type `ready` to continue!")
            + hw2_status
        )

    async def _handle_help(self, event, say):
        """Handle help request"""
        await say(
            "*NDFEX Order Book Homework Bot* :robot_face:\n\n"
            "*HW1 — Order Book Challenge/Response:*\n"
            "1. Run your order book client to capture market data\n"
            "2. DM me `ready` to get a challenge\n"
            "3. I'll ask you one of two types of questions:\n"
            "   • *BBO*: What was the best bid/offer at a sequence number?\n"
            "   • *Volume*: What was the total traded volume in a range?\n"
            "4. Reply with your answer\n"
            f"5. Get {REQUIRED_CORRECT} correct to pass!\n\n"
            "*Answer formats:*\n"
            "• BBO: `bid_qty bid_price ask_qty ask_price` (e.g., `100 1250 50 1300`)\n"
            "• Volume: a single number (e.g., `5000`)\n\n"
            "*HW2 — Exchange Connectivity:*\n"
            "1. DM me `start hw2` to get step-by-step instructions\n"
            "2. Connect to the exchange and follow the steps\n"
            "3. I'll monitor your progress and update you in real time\n"
            "4. If you disconnect, type `restart hw2` for fresh instructions\n"
            "5. Type `check hw2` to see your current checklist\n\n"
            "*Commands:*\n"
            "• `ready` - Start an HW1 challenge\n"
            "• `start hw2` - Start HW2 (exchange connectivity)\n"
            "• `check hw2` - Show HW2 progress\n"
            "• `restart hw2` - Restart HW2 with fresh prices\n"
            "• `status` - Check your team's progress\n"
            "• `help` - Show this message"
        )

    async def _handle_leaderboard(self, say):
        """Handle leaderboard request"""
        if not self.team_scores and not self.hw2_sessions:
            await say("No teams have submitted yet!")
            return

        lines = []

        # HW1 leaderboard
        if self.team_scores:
            sorted_teams = sorted(
                self.team_scores.items(),
                key=lambda x: (x[1]["passed"], x[1]["correct"]),
                reverse=True
            )
            lines.append("*HW1 Leaderboard* :trophy:\n")
            for i, (team, score) in enumerate(sorted_teams[:20], 1):
                if score["passed"]:
                    status = ":white_check_mark: PASSED"
                else:
                    status = f"{score['correct']}/{REQUIRED_CORRECT}"
                lines.append(f"{i}. *{team}*: {status}")

        # HW2 leaderboard
        if self.hw2_sessions:
            if lines:
                lines.append("")
            lines.append("*HW2 Leaderboard* :trophy:\n")
            sorted_hw2 = sorted(
                self.hw2_sessions.items(),
                key=lambda x: (x[1].passed, sum(v for v in x[1].results.values())),
                reverse=True
            )
            for i, (team, session) in enumerate(sorted_hw2[:20], 1):
                done = sum(1 for v in session.results.values() if v)
                total = len(session.results)
                if session.passed:
                    status = ":white_check_mark: PASSED"
                else:
                    status = f"{done}/{total}"
                lines.append(f"{i}. *{team}*: {status}")

        await say("\n".join(lines))

    async def _announce_completion(self, client, team_name: str, score: dict):
        """Announce team completion to class channel and notify instructor"""
        if team_name in self.announced_teams:
            return

        self.announced_teams.add(team_name)

        # Count how many teams have passed
        passed_count = sum(1 for s in self.team_scores.values() if s.get("passed"))
        total_teams = len(self.team_scores)

        # Announce to class channel
        if SLACK_CHANNEL_ID:
            try:
                await client.chat_postMessage(
                    channel=SLACK_CHANNEL_ID,
                    text=(
                        f":tada: *Team {team_name} has completed the order book homework!* :tada:\n\n"
                        f"They correctly answered {score['correct']} challenges "
                        f"(with {score['incorrect']} incorrect attempts).\n\n"
                        f"Congratulations! :clap:"
                    )
                )
                logger.info(f"Announced completion for team {team_name}")
            except Exception as e:
                logger.error(f"Failed to announce completion: {e}")
        else:
            logger.warning("SLACK_CHANNEL_ID not set, skipping channel announcement")

        # DM instructor
        if INSTRUCTOR_USER_ID:
            try:
                await client.chat_postMessage(
                    channel=INSTRUCTOR_USER_ID,
                    text=(
                        f":bell: *Team {team_name}* just completed the homework!\n\n"
                        f"Score: {score['correct']} correct, {score['incorrect']} incorrect\n"
                        f"Teams completed: {passed_count}/{total_teams}"
                    )
                )
                logger.info(f"Notified instructor about team {team_name}")
            except Exception as e:
                logger.error(f"Failed to DM instructor: {e}")

    async def run(self):
        """Run the bot"""
        # Start state sync task
        asyncio.create_task(self.sync_state_from_checker())

        # Restart HW2 monitors for non-passed sessions (bot restart resilience)
        # We need a Slack client reference for monitors; get it from the app
        slack_client = self.app.client
        for team_name, session in self.hw2_sessions.items():
            if not session.passed:
                self.hw2_monitor_tasks[team_name] = asyncio.create_task(
                    self._monitor_hw2(session, slack_client)
                )
                logger.info(f"Restarted HW2 monitor for team {team_name} (from DB)")

        # Start Slack bot
        handler = AsyncSocketModeHandler(self.app, os.environ.get("SLACK_APP_TOKEN"))
        logger.info(f"Starting Slack bot (required correct: {REQUIRED_CORRECT})")
        logger.info(f"Announcement channel: {SLACK_CHANNEL_ID or 'NOT SET'}")
        await handler.start_async()


async def main():
    # Validate environment
    required_vars = ["SLACK_BOT_TOKEN", "SLACK_APP_TOKEN"]
    missing = [v for v in required_vars if not os.environ.get(v)]
    if missing:
        print(f"ERROR: Missing required environment variables: {missing}")
        print("\nRequired:")
        print("  SLACK_BOT_TOKEN - Bot User OAuth Token (xoxb-...)")
        print("  SLACK_APP_TOKEN - App-Level Token (xapp-...)")
        print("\nOptional:")
        print("  SLACK_CHANNEL_ID - Channel for completion announcements")
        print("  INSTRUCTOR_USER_ID - Slack user ID to DM when teams complete (e.g., U01234567)")
        print("  REQUIRED_CORRECT - Number of correct answers needed (default: 10)")
        print("  HOMEWORK_CHECKER_URL - URL of homework checker (default: http://localhost:8080)")
        return

    bot = HomeworkBot()
    await bot.run()


if __name__ == "__main__":
    asyncio.run(main())
