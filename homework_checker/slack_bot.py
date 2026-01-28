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
    def __init__(self):
        self.app = AsyncApp(token=os.environ.get("SLACK_BOT_TOKEN"))

        # State history from homework checker - (seq_num, symbol) -> BBOState
        self.state_history: LRUCache = LRUCache(maxsize=5000)
        self.current_seq_num = 0

        # Volume bounds from homework checker - symbol -> (min_seq, max_seq)
        self.volume_bounds: Dict[int, Tuple[int, int]] = {}

        # Active challenges per user - slack_user_id -> Challenge
        self.active_challenges: Dict[str, Challenge] = {}

        # Team scores - team_name -> {"correct": int, "incorrect": int, "passed": bool}
        self.team_scores: Dict[str, dict] = {}

        # Teams that have already been announced
        self.announced_teams: set = set()

        # Team membership: slack_name (lowercase) -> team_name
        self.team_membership: Dict[str, str] = {}

        # Cache: slack_user_id -> team_name (to avoid repeated API lookups)
        self.user_team_cache: Dict[str, Optional[str]] = {}

        # Load team configuration
        self._load_teams()

        # Load persisted scores
        self._init_db()
        self._load_scores()

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

        # Pick a random symbol that has volume data
        available_symbols = [s for s in self.volume_bounds if self.volume_bounds[s][1] - self.volume_bounds[s][0] > 100]
        if not available_symbols:
            return None

        symbol = random.choice(available_symbols)
        min_seq, max_seq = self.volume_bounds[symbol]

        # Pick a range within the available bounds (not too recent)
        # Use 10-80% of range to avoid edge cases
        range_size = max_seq - min_seq
        usable_start = min_seq + int(range_size * 0.1)
        usable_end = min_seq + int(range_size * 0.8)

        if usable_end - usable_start < 50:
            return None

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
            return symbol, start_seq, end_seq, expected_volume
        except Exception as e:
            logger.error(f"Failed to fetch volume for challenge: {e}")
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

            # Check for ready/start command
            if any(word in text for word in ["ready", "start", "begin", "challenge", "quiz"]):
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
                    "• `ready` - Start a challenge (auto-detects your team)\n"
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
                    status = "PASSED" if score["passed"] else f"{score['correct']}/{REQUIRED_CORRECT}"
                    await say(f"Team *{team_name}*: {status} ({score['correct']} correct, {score['incorrect']} incorrect)")
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

        await say(
            f"{status_emoji} *Team {team_name}*\n\n"
            f"Status: {status_text}\n"
            f"Correct: {score['correct']}\n"
            f"Incorrect: {score['incorrect']}\n\n"
            + ("Congratulations! :tada:" if score["passed"] else "Type `ready` to continue!")
        )

    async def _handle_help(self, event, say):
        """Handle help request"""
        await say(
            "*NDFEX Order Book Homework Bot* :robot_face:\n\n"
            "*How it works:*\n"
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
            "*Commands:*\n"
            "• `ready` - Start a challenge (auto-detects your team)\n"
            "• `status` - Check your team's progress\n"
            "• `help` - Show this message\n\n"
            "*Tips:*\n"
            "• Log BBO state indexed by sequence number\n"
            "• Track cumulative traded volume by sequence number\n"
            "• Work together as a team - anyone can answer for the team!"
        )

    async def _handle_leaderboard(self, say):
        """Handle leaderboard request"""
        if not self.team_scores:
            await say("No teams have submitted yet!")
            return

        # Sort by passed first, then by correct count
        sorted_teams = sorted(
            self.team_scores.items(),
            key=lambda x: (x[1]["passed"], x[1]["correct"]),
            reverse=True
        )

        lines = ["*Homework Leaderboard* :trophy:\n"]
        for i, (team, score) in enumerate(sorted_teams[:20], 1):
            if score["passed"]:
                status = ":white_check_mark: PASSED"
            else:
                status = f"{score['correct']}/{REQUIRED_CORRECT}"
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
