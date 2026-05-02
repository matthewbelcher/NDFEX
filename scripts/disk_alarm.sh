#!/bin/bash
#
# Disk and log-size alarm. Designed to run from cron every few minutes.
#
# Always appends to logs/disk_alarm.log when something is wrong. Optionally
# DMs the instructor on Slack if SLACK_BOT_TOKEN and INSTRUCTOR_USER_ID are
# set in the environment (the script will source homework_checker/.env if it
# exists, so the same credentials the slack_bot uses are picked up).
#
# A signature-based cooldown prevents repeat DMs for the same condition;
# Slack only re-fires when the set of problems changes or COOLDOWN_SECONDS
# elapses. The local log file always records every triggered run.
#
# Tuning via env vars:
#   DISK_PCT_THRESHOLD     percent disk usage that triggers an alert (default 80)
#   LOG_BYTES_THRESHOLD    per-file byte threshold for logs/* (default 1 GB)
#   COOLDOWN_SECONDS       min seconds between identical Slack DMs (default 3600)
#
# matching_engine/logs/ is intentionally excluded — those files are routinely
# ~20 GB/day. Aggregate disk usage catches that case.

set -uo pipefail

DRY_RUN=0
if [[ "${1:-}" == "--dry-run" ]]; then
    DRY_RUN=1
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ALARM_LOG="${REPO_ROOT}/logs/disk_alarm.log"
STATE_FILE="${REPO_ROOT}/logs/.disk_alarm_state"

DISK_PCT_THRESHOLD="${DISK_PCT_THRESHOLD:-80}"
LOG_BYTES_THRESHOLD="${LOG_BYTES_THRESHOLD:-1073741824}"  # 1 GB
COOLDOWN_SECONDS="${COOLDOWN_SECONDS:-3600}"

# Slack credentials live alongside the slack_bot — source them if present.
if [[ -f "${REPO_ROOT}/homework_checker/.env" ]]; then
    # shellcheck disable=SC1091
    source "${REPO_ROOT}/homework_checker/.env"
fi

declare -a problems=()

# --- Disk check ---------------------------------------------------------------
disk_pct=$(df --output=pcent "$REPO_ROOT" | tail -n1 | tr -d ' %')
if [[ -n "$disk_pct" && "$disk_pct" =~ ^[0-9]+$ && "$disk_pct" -ge "$DISK_PCT_THRESHOLD" ]]; then
    disk_avail=$(df -h --output=avail "$REPO_ROOT" | tail -n1 | tr -d ' ')
    problems+=("DISK ${disk_pct}% used (${disk_avail} free)")
fi

# --- Per-file log check -------------------------------------------------------
# Only check logs/, not matching_engine/logs/ (expected to be ~20 GB/day).
for f in "${REPO_ROOT}/logs/"*; do
    [[ -f "$f" ]] || continue
    bytes=$(stat -c %s "$f" 2>/dev/null) || continue
    if [[ "$bytes" -ge "$LOG_BYTES_THRESHOLD" ]]; then
        gb=$(awk "BEGIN { printf \"%.1f\", ${bytes} / 1073741824 }")
        problems+=("LOG ${gb}G $(basename "$f")")
    fi
done

if (( ${#problems[@]} == 0 )); then
    exit 0
fi

# --- Build message ------------------------------------------------------------
ts=$(date '+%Y-%m-%d %H:%M:%S')
host_short=$(hostname -s 2>/dev/null || hostname)
header="NDFEX disk alarm @ ${ts} on ${host_short}"
problem_lines=$(printf '  - %s\n' "${problems[@]}")
message="${header}"$'\n'"${problem_lines}"

if (( DRY_RUN )); then
    printf '[dry-run] would alert:\n%s\n' "$message"
    exit 0
fi

# --- Always log locally -------------------------------------------------------
mkdir -p "$(dirname "$ALARM_LOG")"
printf '%s\n' "$message" >> "$ALARM_LOG"

# --- Slack cooldown -----------------------------------------------------------
state_signature=$(printf '%s\n' "${problems[@]}" | sort | sha256sum | cut -d' ' -f1)
last_signature=""
last_alerted=0
if [[ -f "$STATE_FILE" ]]; then
    last_signature=$(awk -F= '/^signature=/{print $2}' "$STATE_FILE" 2>/dev/null || true)
    last_alerted=$(awk -F= '/^last_alerted=/{print $2}' "$STATE_FILE" 2>/dev/null || true)
    last_alerted=${last_alerted:-0}
fi
now=$(date +%s)
should_dm=1
if [[ "$state_signature" == "$last_signature" && $((now - last_alerted)) -lt "$COOLDOWN_SECONDS" ]]; then
    should_dm=0
fi

# --- Slack DM (best-effort) ---------------------------------------------------
if (( should_dm )) && [[ -n "${SLACK_BOT_TOKEN:-}" && -n "${INSTRUCTOR_USER_ID:-}" ]]; then
    if command -v jq >/dev/null 2>&1; then
        payload=$(jq -nc --arg c "$INSTRUCTOR_USER_ID" --arg t "$message" \
                  '{channel: $c, text: $t}')
    else
        # Fallback: escape with python (always available here).
        payload=$(python3 -c '
import json, os, sys
print(json.dumps({"channel": os.environ["C"], "text": os.environ["T"]}))
' C="$INSTRUCTOR_USER_ID" T="$message")
    fi
    if ! curl -fsS -m 10 -X POST https://slack.com/api/chat.postMessage \
        -H "Authorization: Bearer ${SLACK_BOT_TOKEN}" \
        -H "Content-Type: application/json; charset=utf-8" \
        --data "$payload" >/dev/null; then
        echo "[disk_alarm] slack post failed at ${ts}" >> "$ALARM_LOG"
    fi
    cat > "$STATE_FILE" <<EOF
signature=${state_signature}
last_alerted=${now}
EOF
fi
