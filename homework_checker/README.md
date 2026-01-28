# NDFEX Homework Checker

Automated validation server for the order book homework assignment. Students subscribe to the market data multicast feed, build their own order book, and submit their computed BBO (Best Bid/Offer) values to this server for validation.

## Requirements

- Python 3.8+
- The NDFEX system running (matching engine, bots, web_data)

## Installation

```bash
cd homework_checker
uv sync  # or: pip install -e .
```

## Running the Server

First, ensure NDFEX is running:
```bash
./ndfex.sh start
```

Then start the homework checker:
```bash
python server.py --ws-host localhost --ws-port 9002 --port 8080 --required 1000
```

Options:
- `--ws-host`: WebSocket server host (default: localhost)
- `--ws-port`: WebSocket server port (default: 9002)
- `--port`: HTTP API port (default: 8080)
- `--required`: Number of correct submissions required for passing (default: 1000)

## Student API

### Submit BBO Observation

**POST** `/submit/bbo`

Submit your computed best bid/offer for validation. You must include the sequence number from the market data feed - submissions are validated against the market state at that specific sequence number.

```json
{
    "student_id": "jsmith",
    "seq_num": 12345,
    "symbol": 1,
    "best_bid_price": 1250,
    "best_bid_qty": 100,
    "best_ask_price": 1260,
    "best_ask_qty": 50
}
```

Response:
```json
{
    "correct": true,
    "message": "Correct",
    "score": {
        "student_id": "jsmith",
        "bbo_correct": 42,
        "bbo_incorrect": 3,
        "trades_correct": 0,
        "trades_incorrect": 0,
        "bbo_total": 45,
        "trades_total": 0
    },
    "passing": false
}
```

### Submit Trade Observation

**POST** `/submit/trade`

Submit a trade summary you observed.

```json
{
    "student_id": "jsmith",
    "symbol": 1,
    "aggressor_side": 1,
    "quantity": 10,
    "price": 1255
}
```

Notes:
- `aggressor_side`: 1 = BUY (buyer initiated), 2 = SELL (seller initiated)

### Check Status

**GET** `/status/{student_id}`

Get your current score and progress.

Response:
```json
{
    "student_id": "jsmith",
    "bbo_correct": 1042,
    "bbo_incorrect": 15,
    "trades_correct": 50,
    "trades_incorrect": 2,
    "bbo_total": 1057,
    "trades_total": 52,
    "passing": true,
    "required_correct": 1000
}
```

### Leaderboard

**GET** `/leaderboard`

View all student scores.

### Current Market State (Debug)

**GET** `/current`

View the current market state the server sees (useful for debugging).

### Health Check

**GET** `/health`

Check if the server is connected to the market data feed.

## Example Student Client (Python)

```python
import requests
import time

CHECKER_URL = "http://localhost:8080"
STUDENT_ID = "jsmith"

def submit_bbo(symbol, bid_price, bid_qty, ask_price, ask_qty):
    response = requests.post(f"{CHECKER_URL}/submit/bbo", json={
        "student_id": STUDENT_ID,
        "symbol": symbol,
        "best_bid_price": bid_price,
        "best_bid_qty": bid_qty,
        "best_ask_price": ask_price,
        "best_ask_qty": ask_qty
    })
    return response.json()

def check_status():
    response = requests.get(f"{CHECKER_URL}/status/{STUDENT_ID}")
    return response.json()

# Example: submit your computed BBO
result = submit_bbo(
    symbol=1,
    bid_price=1250,
    bid_qty=100,
    ask_price=1260,
    ask_qty=50
)
print(f"Correct: {result['correct']}, Score: {result['score']['bbo_correct']}")
```

## Example Student Client (C++)

```cpp
// Using libcurl for HTTP requests
#include <curl/curl.h>
#include <string>
#include <sstream>

void submit_bbo(const std::string& student_id, int symbol,
                int bid_price, int bid_qty, int ask_price, int ask_qty) {
    CURL* curl = curl_easy_init();
    if (curl) {
        std::ostringstream json;
        json << "{\"student_id\":\"" << student_id << "\","
             << "\"symbol\":" << symbol << ","
             << "\"best_bid_price\":" << bid_price << ","
             << "\"best_bid_qty\":" << bid_qty << ","
             << "\"best_ask_price\":" << ask_price << ","
             << "\"best_ask_qty\":" << ask_qty << "}";

        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:8080/submit/bbo");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.str().c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
}
```

## Grading

Students pass the assignment when they achieve the required number of correct BBO submissions (default: 1000). The server validates submissions against a rolling window of recent market states to account for network latency.

## Slack Bot (Challenge/Response Mode)

The Slack bot provides an interactive challenge/response system where students prove they've built a working order book.

### How It Works

1. Student DMs the bot: `ready team_alpha`
2. Bot responds with a challenge: "What was the BBO for GOLD at seq_num 12345?"
3. Student replies with: `100 1250 50 1300` (bid_qty bid_price ask_qty ask_price)
4. Bot validates and tracks correct answers
5. After N correct answers, the team passes and the bot announces to the class channel

### Slack App Setup

1. Go to https://api.slack.com/apps and click "Create New App"
2. Choose "From scratch", name it "NDFEX Homework Bot"
3. Under **OAuth & Permissions**, add these Bot Token Scopes:
   - `chat:write` - Send messages
   - `im:history` - Read DM history
   - `im:read` - View DMs
   - `im:write` - Send DMs
   - `commands` - Slash commands (optional)
4. Under **Event Subscriptions**, enable events and subscribe to:
   - `message.im` - Messages in DMs
5. Under **App-Level Tokens**, create a token with `connections:write` scope
6. Install the app to your workspace

### Running the Bot

```bash
export SLACK_BOT_TOKEN=xoxb-your-bot-token
export SLACK_APP_TOKEN=xapp-your-app-token
export SLACK_CHANNEL_ID=C0123456789  # Channel for announcements
export REQUIRED_CORRECT=10  # Number correct to pass

cd homework_checker
uv run slack_bot.py
```

### Student Commands (DM the bot)

- `ready <team_name>` - Start a challenge
- `status <team_name>` - Check team progress
- `help` - Show instructions

### Slash Commands (optional)

- `/homework leaderboard` - Show all team scores
- `/homework status <team_name>` - Check a team's progress

## Testing

Run the automated test suite:

```bash
cd homework_checker
uv run test_homework_checker.py
```

## Instructor Notes

- HTTP API scores are persisted in `scores.db` (SQLite)
- Slack bot scores are persisted in `slack_scores.db` (SQLite)
- Use `/leaderboard` to export grades
- The `--required` flag sets the passing threshold
- Students can submit as many times as they want; only correct submissions count toward passing
