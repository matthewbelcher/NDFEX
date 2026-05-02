#!/bin/bash
#
# Operator-fired "thin market" event for the trading competition.
#
# At T1 (now): THIN_PAUSE on the LP admin port -> all market makers cancel
# every open order and stop quoting.
# After <duration_seconds>: THIN_RESUME -> for each fair value, draw a
# multiplier from U(0.8, 1.2) and apply it. The next process() tick re-quotes
# around the new fair values.
#
# Usage:
#     bots/thin_market_event.sh                  # default 60 s pause
#     bots/thin_market_event.sh 30               # 30 s pause
#     BOTS_THIN_SEED=42 bots/thin_market_event.sh 5
#
# Note: BOTS_THIN_SEED is read by bot_runner itself, not this script.
# Export it in the bot_runner's environment (e.g. relaunch via
# `BOTS_THIN_SEED=42 ndfex.sh start`) if you want a reproducible drill.

set -euo pipefail

DURATION="${1:-60}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LP_CMD="${REPO_ROOT}/bots/lp_cmd.sh"

today=$(date +%Y-%m-%d)
log_path="${REPO_ROOT}/bots/logs/bot_runner_${today}"

echo "[$(date '+%H:%M:%S')] PAUSE liquidity_bots for ${DURATION}s"
"$LP_CMD" THIN_PAUSE

# Trap so an interrupted script still resumes the bots instead of leaving
# them paused. Best-effort — if the LP died in the meantime, ignore.
cleanup() {
    echo "[$(date '+%H:%M:%S')] THIN_RESUME (cleanup)"
    "$LP_CMD" THIN_RESUME 2>/dev/null || true
}
trap cleanup INT TERM

sleep "$DURATION"

echo "[$(date '+%H:%M:%S')] RESUME with random fair-value jumps"
"$LP_CMD" THIN_RESUME

trap - INT TERM
echo
echo "tail -n 4 ${log_path} | grep '\\[thin_event\\]'  to see the per-symbol multipliers"
