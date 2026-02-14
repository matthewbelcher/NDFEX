#!/usr/bin/env python3
"""
End-to-end test for HW2 Slack bot flow.

Simulates the complete student experience by:
1. Creating an isolated bot instance with test fixtures
2. Triggering "start hw2" via the bot's handler
3. Writing matching engine log lines to simulate exchange activity
4. Waiting for the background monitor to detect each step
5. Verifying the session completes with all 12 checks passed

Usage:
    cd homework_checker
    uv run test_hw2_flow.py
"""

import asyncio
import os
import shutil
import sys
import tempfile
from datetime import date
from unittest.mock import AsyncMock

# ---- Test constants ----
TEST_TEAM = "test_hw2_e2e"
TEST_USER_ID = "UTESTE2E001"
TEST_CHANNEL = "DTESTE2E001"
TEST_CLIENT_ID = 999

GREEN = "\033[92m"
RED = "\033[91m"
BOLD = "\033[1m"
RESET = "\033[0m"


def setup_environment():
    """Create isolated temp environment and set env vars before importing bot."""
    temp_dir = tempfile.mkdtemp(prefix="hw2_test_")

    with open(os.path.join(temp_dir, "users.txt"), "w") as f:
        f.write(f"{TEST_CLIENT_ID} {TEST_TEAM} testpass\n")

    with open(os.path.join(temp_dir, "teams.csv"), "w") as f:
        f.write("slack_name,team\n")

    today = date.today().isoformat()
    me_log = os.path.join(temp_dir, f"ME_{today}")
    open(me_log, "w").close()

    os.environ.update({
        "SLACK_BOT_TOKEN": "xoxb-test",
        "SLACK_APP_TOKEN": "xapp-test",
        "ME_LOG_DIR": temp_dir,
        "USERS_FILE": os.path.join(temp_dir, "users.txt"),
        "TEAMS_FILE": os.path.join(temp_dir, "teams.csv"),
        "SLACK_CHANNEL_ID": "",
        "INSTRUCTOR_USER_ID": "",
        "REQUIRED_CORRECT": "10",
        "HOMEWORK_CHECKER_URL": "http://localhost:8080",
    })

    return temp_dir, me_log


def populate_bbo(bot):
    """Populate state_history with BBO data for GOLD and BLUE."""
    for i in range(20):
        seq = 100000 + i
        bot.state_history[(seq, 1)] = {
            "best_bid_price": 1200,
            "best_bid_qty": 100,
            "best_ask_price": 1300,
            "best_ask_qty": 50,
        }
        bot.state_history[(seq, 2)] = {
            "best_bid_price": 500,
            "best_bid_qty": 200,
            "best_ask_price": 550,
            "best_ask_qty": 100,
        }
    bot.current_seq_num = 100019


def generate_me_log_lines(client_id):
    """Generate ME log lines that complete all 12 HW2 steps.

    Uses the real ME log format:
      [YYYY-MM-DD HH:MM:SS.mmm] [async_logger] [info] <message>
    """
    cid = client_id
    ts = date.today().isoformat()
    prefix = f"[{ts} 10:00:0"

    return [
        # 1. Login
        f"{prefix}0.001] [async_logger] [info] Client {cid} logged in successfully with session id 1234567890",
        # 2. Buy GOLD (order 1001, symbol 1, side 1)
        f"{prefix}1.001] [async_logger] [info] Received new order from client {cid}: order id: 1001 symbol: 1 side: 1 quantity: 5 price: 700 flags: 0",
        # 3. Sell GOLD (order 1002, symbol 1, side 2)
        f"{prefix}2.001] [async_logger] [info] Received new order from client {cid}: order id: 1002 symbol: 1 side: 2 quantity: 3 price: 1800 flags: 0",
        # 4. Buy BLUE (order 1003, symbol 2, side 1)
        f"{prefix}3.001] [async_logger] [info] Received new order from client {cid}: order id: 1003 symbol: 2 side: 1 quantity: 7 price: 250 flags: 0",
        # 5. Sell BLUE (order 1004, symbol 2, side 2)
        f"{prefix}4.001] [async_logger] [info] Received new order from client {cid}: order id: 1004 symbol: 2 side: 2 quantity: 2 price: 800 flags: 0",
        # 6. Reject: bad tick price (reason 5 = INVALID_PRICE)
        f"{prefix}5.001] [async_logger] [info] Sending reject to client {cid} seq 100 order id 1005 reason 5",
        # 7. Reject: duplicate order ID (reason 3)
        f"{prefix}6.001] [async_logger] [info] Sending reject to client {cid} seq 101 order id 1001 reason 3",
        # 8. Reject: zero quantity (reason 6)
        f"{prefix}7.001] [async_logger] [info] Sending reject to client {cid} seq 102 order id 1006 reason 6",
        # 9. Fill (crossing buy at the ask)
        f"{prefix}8.001] [async_logger] [info] Sending fill to client {cid} seq 103 order id 1007 quantity 4 price 1300 flags 0",
        # 10. Cancel resting order 1001 (not filled, so counts as cancel)
        f"{prefix}9.001] [async_logger] [info] Sending close to client {cid} seq 104 order id 1001",
        # 11a. Modify order 1002 (first modify)
        f"{prefix}9.101] [async_logger] [info] Received modify order from client {cid}, order id: 1002",
        # 11b. Modify order 1004 (second modify -> modify_2 = True)
        f"{prefix}9.201] [async_logger] [info] Received modify order from client {cid}, order id: 1004",
        # 12. Fill on modified order 1004 -> modify_fill = True
        f"{prefix}9.301] [async_logger] [info] Sending fill to client {cid} seq 105 order id 1004 quantity 2 price 500 flags 0",
    ]


STEP_LABELS = [
    ("login", "Login"),
    ("buy_gold", "Buy GOLD order"),
    ("sell_gold", "Sell GOLD order"),
    ("buy_blue", "Buy BLUE order"),
    ("sell_blue", "Sell BLUE order"),
    ("reject_bad_tick", "Reject: invalid tick"),
    ("reject_dup_oid", "Reject: duplicate order ID"),
    ("reject_zero_qty", "Reject: zero quantity"),
    ("fill", "Order filled"),
    ("cancel", "Cancel resting order"),
    ("modify_2", "Modify 2+ orders"),
    ("modify_fill", "Modify resulting in fill"),
]


async def run_test(temp_dir, me_log):
    print(f"{BOLD}HW2 Slack Bot — End-to-End Test{RESET}")
    print(f"Temp dir: {temp_dir}\n")

    # Import bot module (env vars already set)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    if script_dir not in sys.path:
        sys.path.insert(0, script_dir)

    # Change to temp dir so slack_scores.db is created there
    original_cwd = os.getcwd()
    os.chdir(temp_dir)

    import slack_bot
    slack_bot.ME_LOG_DIR = temp_dir

    bot = slack_bot.HomeworkBot()
    # Make db_path absolute so it works after restoring cwd
    bot.db_path = os.path.join(temp_dir, "slack_scores.db")
    os.chdir(original_cwd)

    populate_bbo(bot)

    # Mock Slack interactions
    messages_said = []

    async def mock_say(text):
        messages_said.append(text)

    mock_client = AsyncMock()

    # ---- Phase 1: Start HW2 session ----
    print(f"{BOLD}Phase 1: Start HW2 session{RESET}")

    event = {
        "user": TEST_USER_ID,
        "channel": TEST_CHANNEL,
        "channel_type": "im",
        "text": f"start hw2 {TEST_TEAM}",
    }
    await bot._handle_hw2_ready(event, mock_say, mock_client)

    ok = True

    if TEST_TEAM not in bot.hw2_sessions:
        print(f"  {RED}FAIL{RESET} Session not created")
        return False
    session = bot.hw2_sessions[TEST_TEAM]

    if session.client_id != TEST_CLIENT_ID:
        print(f"  {RED}FAIL{RESET} Wrong client_id: {session.client_id}")
        ok = False
    else:
        print(f"  {GREEN}ok{RESET}   Session created (client_id={TEST_CLIENT_ID})")

    if not session.steps:
        print(f"  {RED}FAIL{RESET} Steps not generated")
        return False
    print(f"  {GREEN}ok{RESET}   Generated {len(session.steps)} steps")

    if not messages_said:
        print(f"  {RED}FAIL{RESET} No intro message sent")
        ok = False
    elif "HW2" not in messages_said[0] or "Step 1" not in messages_said[0]:
        print(f"  {RED}FAIL{RESET} Intro message missing HW2/Step 1")
        ok = False
    else:
        print(f"  {GREEN}ok{RESET}   Bot sent intro message with Step 1")

    # ---- Phase 2: Simulate exchange activity ----
    print(f"\n{BOLD}Phase 2: Simulate exchange activity{RESET}")

    # Yield control so the monitor task starts and reads initial offset (0)
    await asyncio.sleep(0.1)

    log_lines = generate_me_log_lines(TEST_CLIENT_ID)
    with open(me_log, "a") as f:
        for line in log_lines:
            f.write(line + "\n")
    print(f"  Wrote {len(log_lines)} ME log lines")

    # ---- Phase 3: Wait for monitor ----
    print(f"\n{BOLD}Phase 3: Wait for monitor to detect activity{RESET}")
    print(f"  (monitor polls every 5s)", end="", flush=True)

    timeout = 30
    start = asyncio.get_event_loop().time()
    while not session.passed:
        elapsed = asyncio.get_event_loop().time() - start
        if elapsed > timeout:
            print()
            break
        print(".", end="", flush=True)
        await asyncio.sleep(1)
    print()

    # ---- Phase 4: Verify all 12 results ----
    print(f"\n{BOLD}Phase 4: Verify results{RESET}")

    for key, label in STEP_LABELS:
        passed = session.results.get(key, False)
        if passed:
            print(f"  {GREEN}ok{RESET}   {label}")
        else:
            print(f"  {RED}FAIL{RESET} {label}")
            ok = False

    # ---- Phase 5: Verify "check hw2" status command ----
    print(f"\n{BOLD}Phase 5: Verify 'check hw2' command{RESET}")

    messages_said.clear()
    check_event = {
        "user": TEST_USER_ID,
        "channel": TEST_CHANNEL,
        "channel_type": "im",
        "text": f"check hw2 {TEST_TEAM}",
    }
    await bot._handle_hw2_check(check_event, mock_say, mock_client)

    if messages_said and "All complete" in messages_said[-1]:
        print(f"  {GREEN}ok{RESET}   'check hw2' shows all complete")
    else:
        print(f"  {RED}FAIL{RESET} 'check hw2' didn't show completion")
        ok = False

    # ---- Phase 6: Verify restart blocked after pass ----
    print(f"\n{BOLD}Phase 6: Verify restart blocked after completion{RESET}")

    messages_said.clear()
    restart_event = {
        "user": TEST_USER_ID,
        "channel": TEST_CHANNEL,
        "channel_type": "im",
        "text": f"restart hw2 {TEST_TEAM}",
    }
    await bot._handle_hw2_restart(restart_event, mock_say, mock_client)

    if messages_said and "already completed" in messages_said[-1]:
        print(f"  {GREEN}ok{RESET}   Restart correctly blocked")
    else:
        print(f"  {RED}FAIL{RESET} Restart was not blocked (got: {messages_said[-1][:80] if messages_said else 'no message'})")
        ok = False

    # ---- Phase 7: Verify DM progress messages were sent ----
    print(f"\n{BOLD}Phase 7: Verify progress DMs{RESET}")

    dm_count = mock_client.chat_postMessage.call_count
    # All steps complete in one poll cycle, so the bot batches progress into
    # fewer DMs (1 progress + 1 completion = 2 minimum)
    if dm_count >= 2:
        print(f"  {GREEN}ok{RESET}   Bot sent {dm_count} DM(s) during the flow")
    else:
        print(f"  {RED}FAIL{RESET} Bot only sent {dm_count} DM(s), expected >= 2")
        ok = False

    # ---- Phase 8: Verify DB persistence ----
    print(f"\n{BOLD}Phase 8: Verify database persistence{RESET}")

    import sqlite3
    db_path = os.path.join(temp_dir, "slack_scores.db")
    conn = sqlite3.connect(db_path)
    c = conn.cursor()
    c.execute("SELECT team_name, passed FROM hw2_sessions WHERE team_name = ?", (TEST_TEAM,))
    row = c.fetchone()
    conn.close()

    if row and row[1] == 1:
        print(f"  {GREEN}ok{RESET}   Session persisted with passed=1")
    else:
        print(f"  {RED}FAIL{RESET} Session not persisted correctly (row={row})")
        ok = False

    # Summary
    print(f"\n{'=' * 50}")
    if session.passed and ok:
        print(f"{GREEN}{BOLD}ALL TESTS PASSED{RESET} — HW2 flow works end to end")
    else:
        print(f"{RED}{BOLD}SOME TESTS FAILED{RESET} — see details above")
        if not session.passed:
            print(f"  session.passed = {session.passed}")

    # Cleanup async tasks
    for task in bot.hw2_monitor_tasks.values():
        task.cancel()
    await asyncio.sleep(0.1)

    return session.passed and ok


async def main():
    temp_dir, me_log = setup_environment()
    try:
        passed = await run_test(temp_dir, me_log)
    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)

    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    asyncio.run(main())
