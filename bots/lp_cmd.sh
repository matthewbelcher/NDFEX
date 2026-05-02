#!/bin/bash
#
# Send a single command to the bot_runner admin port and print the response.
#
# Usage:
#     bots/lp_cmd.sh THIN_PAUSE
#     bots/lp_cmd.sh THIN_RESUME
#     bots/lp_cmd.sh BEAR_START drift=-1 bias=0.85
#     bots/lp_cmd.sh BEAR_END
#     bots/lp_cmd.sh STATUS
#     bots/lp_cmd.sh HELP
#
# Override the target via env:
#     LP_CMD_HOST (default 127.0.0.1)
#     LP_CMD_PORT (default 1235)

set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <COMMAND> [ARG ...]" >&2
    exit 2
fi

HOST="${LP_CMD_HOST:-127.0.0.1}"
PORT="${LP_CMD_PORT:-1235}"

# Compose the line; bash /dev/tcp handles the connection, no nc required.
line="$*"

exec 3<>"/dev/tcp/${HOST}/${PORT}" || {
    echo "lp_cmd: cannot connect to ${HOST}:${PORT}" >&2
    exit 1
}

printf '%s\n' "$line" >&3
# bot_runner sends one line per command; read it back with a short timeout
# so we don't hang if the server happened to drop the connection.
IFS= read -r -t 5 reply <&3 || reply=""
exec 3<&-
exec 3>&-

if [[ -z "$reply" ]]; then
    echo "lp_cmd: no response from ${HOST}:${PORT}" >&2
    exit 1
fi

printf '%s\n' "$reply"
case "$reply" in
    OK*) exit 0 ;;
    *)   exit 1 ;;
esac
