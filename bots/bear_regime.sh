#!/bin/bash
#
# Operator-fired "bear regime" event for the trading competition.
#
# At T1 (now): BEAR_START on the LP admin port =>
#   - All RandomWalk fair values start a deterministic linear down-drift
#     applied as an overlay on top of the random walk, so the high-frequency
#     noise stays but the medium-term direction is decisively negative.
#   - All RandomTakers flip from 50/50 side selection to a sell-heavy bias
#     (~5.7:1 sell:buy by default), so component bid sides get hit more often
#     than asks, layering selling pressure on top of the FV drift.
# After <duration_seconds>: BEAR_END =>
#   - end_directional_drift bakes the cumulative drift into each FV so
#     prices don't snap back when the regime ends.
#   - RandomTaker sell_bias resets to 0.5.
#
# Usage:
#     bots/bear_regime.sh                   # default 1500 s = 25 minutes
#     bots/bear_regime.sh 1800              # 30 minutes
#     BOTS_BEAR_DRIFT_PER_MIN=-2 \
#       BOTS_BEAR_SELL_BIAS=0.91 \
#       bots/bear_regime.sh 1800            # harder bear (10:1 sell:buy)
#
# Env vars (forwarded to BEAR_START on the admin port):
#   BOTS_BEAR_DRIFT_PER_MIN  signed int, units per minute (default -1)
#   BOTS_BEAR_SELL_BIAS      double in [0.0, 1.0]         (default 0.85)
#
# Expected price drops at the default drift_per_min=-1 over 30 min:
#   dorms (~500 base):    ~6%
#   GOLD (~1240 base):    ~2.4%
#   BLUE (~900 base):     ~3.3%
#   UNDY (sum of dorms):  ~5.5%  (basket follows components automatically)
# At drift_per_min=-2 the drops roughly double; stay there or below to keep
# any single product above its floor.

set -euo pipefail

DURATION="${1:-1500}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LP_CMD="${REPO_ROOT}/bots/lp_cmd.sh"

DRIFT="${BOTS_BEAR_DRIFT_PER_MIN:--1}"
BIAS="${BOTS_BEAR_SELL_BIAS:-0.85}"

today=$(date +%Y-%m-%d)
log_path="${REPO_ROOT}/bots/logs/bot_runner_${today}"

echo "[$(date '+%H:%M:%S')] BEAR_REGIME START for ${DURATION}s"
echo "  drift_per_min: ${DRIFT}"
echo "  sell_bias:     ${BIAS}"
"$LP_CMD" BEAR_START "drift=${DRIFT}" "bias=${BIAS}"

# Trap so an interrupted script still ends the regime instead of leaving the
# bot biased forever. Best-effort — if the LP died in the meantime, ignore.
cleanup() {
    echo "[$(date '+%H:%M:%S')] BEAR_REGIME END (cleanup)"
    "$LP_CMD" BEAR_END 2>/dev/null || true
}
trap cleanup INT TERM

sleep "$DURATION"

echo "[$(date '+%H:%M:%S')] BEAR_REGIME END after ${DURATION}s"
"$LP_CMD" BEAR_END

trap - INT TERM
echo "see ${log_path} for [bear_regime] log lines"
