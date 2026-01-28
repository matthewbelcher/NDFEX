#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIDFILE="$SCRIPT_DIR/.homework_checker.pid"
SLACK_PIDFILE="$SCRIPT_DIR/.slack_bot.pid"
LOGFILE="/tmp/homework_checker.log"
SLACK_LOGFILE="/tmp/slack_bot.log"

# Default options
PORT=8080
REQUIRED=10
WS_HOST=localhost
WS_PORT=9002

usage() {
    echo "Usage: $0 {start|stop|restart|status} [options]"
    echo ""
    echo "Commands:"
    echo "  start    Start the homework checker server"
    echo "  stop     Stop the homework checker server"
    echo "  restart  Restart the homework checker server"
    echo "  status   Check if the server is running"
    echo ""
    echo "Options:"
    echo "  --port PORT        HTTP API port (default: $PORT)"
    echo "  --required NUM     Required correct submissions (default: $REQUIRED)"
    echo "  --ws-host HOST     WebSocket host (default: $WS_HOST)"
    echo "  --ws-port PORT     WebSocket port (default: $WS_PORT)"
    exit 1
}

check_web_data() {
    if ! pgrep -f "web_data.*239" > /dev/null 2>&1; then
        echo "ERROR: web_data is not running!"
        echo "The homework checker requires web_data to be running for market data."
        echo "Start web_data first with:"
        echo "  ./web_data <md_mcast_ip> <snapshot_mcast_ip> <clearing_mcast_ip> <mcast_bind_ip>"
        return 1
    fi
    return 0
}

get_pid() {
    if [ -f "$PIDFILE" ]; then
        local pid=$(cat "$PIDFILE")
        if ps -p "$pid" > /dev/null 2>&1; then
            echo "$pid"
            return 0
        fi
    fi
    # Check for running process even without pidfile
    local pid=$(pgrep -f "server.py.*--port.*$PORT" | head -1)
    if [ -n "$pid" ]; then
        echo "$pid"
        return 0
    fi
    return 1
}

get_slack_pid() {
    if [ -f "$SLACK_PIDFILE" ]; then
        local pid=$(cat "$SLACK_PIDFILE")
        if ps -p "$pid" > /dev/null 2>&1; then
            echo "$pid"
            return 0
        fi
    fi
    # Check for running process even without pidfile
    local pid=$(pgrep -f "slack_bot.py" | head -1)
    if [ -n "$pid" ]; then
        echo "$pid"
        return 0
    fi
    return 1
}

start_slack_bot() {
    local pid=$(get_slack_pid)
    if [ -n "$pid" ]; then
        echo "Slack bot is already running (PID: $pid), restarting..."
        stop_slack_bot
    fi

    if [ ! -f "$SCRIPT_DIR/.env" ]; then
        echo "WARNING: .env file not found, skipping Slack bot"
        return 1
    fi

    echo "Starting Slack bot..."
    cd "$SCRIPT_DIR"
    (source .env && uv run slack_bot.py) > "$SLACK_LOGFILE" 2>&1 &
    local new_pid=$!
    echo "$new_pid" > "$SLACK_PIDFILE"

    sleep 2
    if ps -p "$new_pid" > /dev/null 2>&1; then
        echo "Slack bot started (PID: $new_pid)"
        return 0
    else
        echo "WARNING: Failed to start Slack bot"
        echo "Check log file: $SLACK_LOGFILE"
        rm -f "$SLACK_PIDFILE"
        return 1
    fi
}

stop_slack_bot() {
    local pid=$(get_slack_pid)
    if [ -z "$pid" ]; then
        rm -f "$SLACK_PIDFILE"
        return 0
    fi

    echo "Stopping Slack bot (PID: $pid)..."
    kill "$pid" 2>/dev/null

    for i in {1..10}; do
        if ! ps -p "$pid" > /dev/null 2>&1; then
            echo "Slack bot stopped"
            rm -f "$SLACK_PIDFILE"
            return 0
        fi
        sleep 0.5
    done

    kill -9 "$pid" 2>/dev/null
    rm -f "$SLACK_PIDFILE"
    echo "Slack bot stopped"
    return 0
}

do_start() {
    local pid=$(get_pid)
    if [ -n "$pid" ]; then
        echo "Homework checker is already running (PID: $pid)"
        return 1
    fi

    if ! check_web_data; then
        return 1
    fi

    echo "Starting homework checker on port $PORT..."
    cd "$SCRIPT_DIR"
    uv run server.py --port "$PORT" --required "$REQUIRED" --ws-host "$WS_HOST" --ws-port "$WS_PORT" > "$LOGFILE" 2>&1 &
    local new_pid=$!
    echo "$new_pid" > "$PIDFILE"

    sleep 2
    if ps -p "$new_pid" > /dev/null 2>&1; then
        echo "Homework checker started (PID: $new_pid)"
        echo "Log file: $LOGFILE"
        start_slack_bot
        return 0
    else
        echo "ERROR: Failed to start homework checker"
        echo "Check log file: $LOGFILE"
        rm -f "$PIDFILE"
        return 1
    fi
}

do_stop() {
    stop_slack_bot

    local pid=$(get_pid)
    if [ -z "$pid" ]; then
        echo "Homework checker is not running"
        rm -f "$PIDFILE"
        return 0
    fi

    echo "Stopping homework checker (PID: $pid)..."
    kill "$pid" 2>/dev/null

    # Wait for process to stop
    for i in {1..10}; do
        if ! ps -p "$pid" > /dev/null 2>&1; then
            echo "Homework checker stopped"
            rm -f "$PIDFILE"
            return 0
        fi
        sleep 0.5
    done

    # Force kill if still running
    echo "Force killing..."
    kill -9 "$pid" 2>/dev/null
    rm -f "$PIDFILE"
    echo "Homework checker stopped"
    return 0
}

do_status() {
    local pid=$(get_pid)
    if [ -n "$pid" ]; then
        echo "Homework checker is running (PID: $pid)"

        # Check slack bot status
        local slack_pid=$(get_slack_pid)
        if [ -n "$slack_pid" ]; then
            echo "Slack bot is running (PID: $slack_pid)"
        else
            echo "Slack bot is NOT running"
        fi

        # Check web_data status
        if pgrep -f "web_data.*239" > /dev/null 2>&1; then
            echo "web_data is running"
        else
            echo "WARNING: web_data is NOT running"
        fi

        # Try to get current state
        local response=$(curl -s "http://localhost:$PORT/current" 2>/dev/null)
        if [ -n "$response" ]; then
            local connected=$(echo "$response" | python3 -c "import sys,json; print(json.load(sys.stdin).get('connected', False))" 2>/dev/null)
            local seq=$(echo "$response" | python3 -c "import sys,json; print(json.load(sys.stdin).get('current_seq_num', 'N/A'))" 2>/dev/null)
            echo "WebSocket connected: $connected"
            echo "Current sequence: $seq"
        fi
        return 0
    else
        echo "Homework checker is not running"
        return 1
    fi
}

# Parse command
if [ $# -lt 1 ]; then
    usage
fi

COMMAND=$1
shift

# Parse options
while [ $# -gt 0 ]; do
    case "$1" in
        --port)
            PORT="$2"
            shift 2
            ;;
        --required)
            REQUIRED="$2"
            shift 2
            ;;
        --ws-host)
            WS_HOST="$2"
            shift 2
            ;;
        --ws-port)
            WS_PORT="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

case "$COMMAND" in
    start)
        do_start
        ;;
    stop)
        do_stop
        ;;
    restart)
        do_stop
        sleep 1
        do_start
        ;;
    status)
        do_status
        ;;
    *)
        usage
        ;;
esac
