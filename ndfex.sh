#!/bin/bash
#
# NDFEX Control Script
# Start, stop, and manage the Notre Dame Fake Exchange system
#

set -e

# Default configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build/bin"
PID_DIR="${SCRIPT_DIR}/.pids"
LOG_DIR="${SCRIPT_DIR}/logs"

# Default network settings
BIND_IP="${BIND_IP:-127.0.0.1}"
MCAST_IP="${MCAST_IP:-239.0.0.1}"
SNAPSHOT_MCAST_IP="${SNAPSHOT_MCAST_IP:-239.0.0.3}"
CLEARING_MCAST_IP="${CLEARING_MCAST_IP:-239.0.0.2}"
MCAST_BIND_IP="${MCAST_BIND_IP:-$BIND_IP}"

# Default ports
OE_PORT="${OE_PORT:-1234}"
MD_PORT="${MD_PORT:-12345}"
CLEARING_PORT="${CLEARING_PORT:-12346}"
SNAPSHOT_PORT="${SNAPSHOT_PORT:-12345}"

# Bot selection
BOT_TYPE="${BOT_TYPE:-bot_runner}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

usage() {
    cat << EOF
NDFEX Control Script

Usage: $0 <command> [options]

Commands:
    start       Start all NDFEX components
    stop        Stop all NDFEX components
    restart     Restart all components
    status      Show status of all components
    logs        Tail logs from all components
    viewer      Start the FTXUI market data viewer

Options:
    --bind-ip IP          IP address to bind services (default: 127.0.0.1)
    --mcast-ip IP         Multicast IP for market data (default: 239.0.0.1)
    --snapshot-mcast-ip IP Multicast IP for snapshots (default: 239.0.0.3)
    --clearing-ip IP      Multicast IP for clearing data (default: 239.0.0.2)
    --mcast-bind-ip IP    IP to bind multicast listeners (default: 127.0.0.1)
    --oe-port PORT        Order entry port (default: 1234)
    --md-port PORT        Market data multicast port (default: 12345)
    --snapshot-port PORT  Snapshot service port (default: 12345)
    --bot-type TYPE       Bot type: bot_runner, stable_bot_runner, smarter_bots (default: bot_runner)
    --no-bots             Don't start trading bots
    --no-snapshots        Don't start snapshot service
    --add-mcast-route     Add 239.0.0.0/8 route via MCAST_BIND_IP
    -h, --help            Show this help message

Environment Variables:
    BIND_IP, MCAST_IP, SNAPSHOT_MCAST_IP, CLEARING_MCAST_IP, MCAST_BIND_IP
    OE_PORT, MD_PORT, CLEARING_PORT, SNAPSHOT_PORT
    BOT_TYPE

Examples:
    $0 start
    $0 start --bind-ip 192.168.1.100 --mcast-ip 239.1.1.1
    $0 start --bot-type smarter_bots
    $0 start --no-bots
    $0 stop
    $0 status
EOF
}

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_executable() {
    local exe="$1"
    if [[ ! -x "$exe" ]]; then
        log_error "Executable not found: $exe"
        exit 1
    fi
}

find_executable() {
    for candidate in "$@"; do
        if [[ -x "$candidate" ]]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

ensure_dirs() {
    mkdir -p "$PID_DIR"
    mkdir -p "$LOG_DIR"
}

save_pid() {
    local name="$1"
    local pid="$2"
    echo "$pid" > "${PID_DIR}/${name}.pid"
}

get_pid() {
    local name="$1"
    local pid_file="${PID_DIR}/${name}.pid"
    if [[ -f "$pid_file" ]]; then
        cat "$pid_file"
    fi
}

is_running() {
    local name="$1"
    local pid=$(get_pid "$name")
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
        return 0
    fi
    return 1
}

start_component() {
    local name="$1"
    shift
    local exe="$1"
    shift
    local cwd="$1"
    shift
    local args="$@"

    if is_running "$name"; then
        log_warn "$name is already running (PID: $(get_pid $name))"
        return 0
    fi

    check_executable "$exe"

    log_info "Starting $name..."

    # Start the process in background, redirect output to log file
    (cd "$cwd" && "$exe" $args > "${LOG_DIR}/${name}.log" 2>&1) &
    local pid=$!

    # Give it a moment to start
    sleep 0.5

    if kill -0 "$pid" 2>/dev/null; then
        save_pid "$name" "$pid"
        log_info "$name started (PID: $pid)"
        return 0
    else
        log_error "$name failed to start. Check ${LOG_DIR}/${name}.log"
        return 1
    fi
}

stop_component() {
    local name="$1"
    local pid=$(get_pid "$name")

    if [[ -z "$pid" ]]; then
        log_warn "$name is not running (no PID file)"
        return 0
    fi

    if ! kill -0 "$pid" 2>/dev/null; then
        log_warn "$name is not running (stale PID file)"
        rm -f "${PID_DIR}/${name}.pid"
        return 0
    fi

    log_info "Stopping $name (PID: $pid)..."

    # Try graceful shutdown first
    kill -TERM "$pid" 2>/dev/null || true

    # Wait up to 5 seconds for graceful shutdown
    local count=0
    while kill -0 "$pid" 2>/dev/null && [[ $count -lt 50 ]]; do
        sleep 0.1
        ((count++))
    done

    # Force kill if still running
    if kill -0 "$pid" 2>/dev/null; then
        log_warn "$name didn't stop gracefully, forcing..."
        kill -9 "$pid" 2>/dev/null || true
    fi

    rm -f "${PID_DIR}/${name}.pid"
    log_info "$name stopped"
}

get_iface_for_ip() {
    local ip="$1"
    local line
    while IFS= read -r line; do
        local iface="${line#*: }"
        iface="${iface%% *}"
        local addr="${line##* inet }"
        addr="${addr%% *}"
        if [[ "${addr%%/*}" == "$ip" ]]; then
            echo "$iface"
            return 0
        fi
    done < <(ip -o -4 addr show)
    return 1
}

ensure_mcast_route() {
    if [[ -n "$MCAST_BIND_IP" && "$MCAST_BIND_IP" != "127.0.0.1" ]]; then
        local existing
        existing="$(ip route show 239.0.0.0/8)"
        if [[ -z "$existing" ]]; then
            local iface
            iface="$(get_iface_for_ip "$MCAST_BIND_IP" || true)"
            if [[ -n "$iface" ]]; then
                if sudo -n ip route add 239.0.0.0/8 dev "$iface" 2>/dev/null; then
                    log_info "Added multicast route 239.0.0.0/8 via $iface"
                else
                    log_warn "Multicast route missing. Run: sudo ip route add 239.0.0.0/8 dev $iface"
                fi
            else
                log_warn "Could not find interface for MCAST_BIND_IP=$MCAST_BIND_IP"
            fi
        fi
    fi
}

resolve_binaries() {
    MATCHING_ENGINE_BIN="$(find_executable \
        "${BUILD_DIR}/matching_engine" \
        "${SCRIPT_DIR}/matching_engine/out/matching_engine")"
    if [[ -z "$MATCHING_ENGINE_BIN" ]]; then
        log_error "matching_engine binary not found (build/bin or matching_engine/out)"
        exit 1
    fi

    MD_SNAPSHOTS_BIN="$(find_executable \
        "${BUILD_DIR}/md_snapshots" \
        "${SCRIPT_DIR}/market_data/md_snapshots")"
    if [[ -z "$MD_SNAPSHOTS_BIN" ]]; then
        log_error "md_snapshots binary not found (build/bin or market_data/md_snapshots)"
        exit 1
    fi

    if [[ "$START_BOTS" != "no" ]]; then
        BOTS_BIN="$(find_executable \
            "${BUILD_DIR}/${BOT_TYPE}" \
            "${SCRIPT_DIR}/bots/out/${BOT_TYPE}")"
        if [[ -z "$BOTS_BIN" ]]; then
            log_error "bots binary not found (build/bin or bots/out): ${BOT_TYPE}"
            exit 1
        fi
    fi
}

resolve_viewer_binary() {
    VIEWER_BIN="$(find_executable \
        "${BUILD_DIR}/md_viewer" \
        "${SCRIPT_DIR}/viewer/md_viewer")"
    if [[ -z "$VIEWER_BIN" ]]; then
        log_error "md_viewer binary not found (build/bin or viewer/md_viewer)"
        log_error "Build it with: make -C viewer"
        exit 1
    fi
}

run_viewer() {
    resolve_viewer_binary
    log_info "Starting md_viewer (FTXUI)..."
    (cd "${SCRIPT_DIR}/viewer" && "$VIEWER_BIN" "$MCAST_IP" "$SNAPSHOT_MCAST_IP" "$MCAST_BIND_IP")
}

start_all() {
    ensure_dirs
    resolve_binaries
    if [[ "$ADD_MCAST_ROUTE" == "yes" ]]; then
        ensure_mcast_route
    fi

    log_info "Starting NDFEX system..."
    log_info "Configuration:"
    log_info "  Bind IP: $BIND_IP"
    log_info "  Multicast IP: $MCAST_IP"
    log_info "  Snapshot Multicast IP: $SNAPSHOT_MCAST_IP"
    log_info "  Clearing Multicast IP: $CLEARING_MCAST_IP"
    log_info "  Multicast Bind IP: $MCAST_BIND_IP"
    log_info "  Order Entry Port: $OE_PORT"
    log_info "  Market Data Port: $MD_PORT"
    echo ""

    # 1. Start Matching Engine first
    start_component "matching_engine" "$MATCHING_ENGINE_BIN" "${SCRIPT_DIR}/matching_engine" \
        "$BIND_IP" "$MCAST_IP" "$CLEARING_MCAST_IP"

    # Wait for matching engine to be ready
    sleep 1

    # 2. Start Market Data Snapshot Service
    if [[ "$START_SNAPSHOTS" != "no" ]]; then
        start_component "md_snapshots" "$MD_SNAPSHOTS_BIN" "${SCRIPT_DIR}" \
            "$MCAST_IP" "$MD_PORT" "$SNAPSHOT_MCAST_IP" "$SNAPSHOT_PORT" "$MCAST_BIND_IP"
        sleep 0.5
    fi

    # 3. Start Trading Bots
    if [[ "$START_BOTS" != "no" ]]; then
        start_component "bots" "$BOTS_BIN" "${SCRIPT_DIR}/bots" \
            "$BIND_IP" "$OE_PORT" "$MCAST_IP" "$SNAPSHOT_MCAST_IP" "$MCAST_BIND_IP"
    fi

    echo ""
    log_info "NDFEX system started!"
    log_info "Logs available in: $LOG_DIR"
}

stop_all() {
    log_info "Stopping NDFEX system..."

    # Stop in reverse order
    stop_component "bots"
    stop_component "md_snapshots"
    stop_component "matching_engine"

    log_info "NDFEX system stopped"
}

show_status() {
    echo "NDFEX System Status"
    echo "==================="

    local components=("matching_engine" "md_snapshots" "bots")

    for comp in "${components[@]}"; do
        if is_running "$comp"; then
            echo -e "$comp: ${GREEN}RUNNING${NC} (PID: $(get_pid $comp))"
        else
            echo -e "$comp: ${RED}STOPPED${NC}"
        fi
    done
}

tail_logs() {
    log_info "Tailing logs (Ctrl+C to stop)..."
    tail -f "${LOG_DIR}"/*.log 2>/dev/null || log_warn "No log files found"
}

# Parse command line arguments
COMMAND=""
START_BOTS="yes"
START_SNAPSHOTS="yes"
ADD_MCAST_ROUTE="no"

while [[ $# -gt 0 ]]; do
    case $1 in
        start|stop|restart|status|logs|viewer)
            COMMAND="$1"
            shift
            ;;
        --bind-ip)
            BIND_IP="$2"
            shift 2
            ;;
        --mcast-ip)
            MCAST_IP="$2"
            shift 2
            ;;
        --snapshot-mcast-ip)
            SNAPSHOT_MCAST_IP="$2"
            shift 2
            ;;
        --clearing-ip)
            CLEARING_MCAST_IP="$2"
            shift 2
            ;;
        --mcast-bind-ip)
            MCAST_BIND_IP="$2"
            shift 2
            ;;
        --oe-port)
            OE_PORT="$2"
            shift 2
            ;;
        --md-port)
            MD_PORT="$2"
            shift 2
            ;;
        --snapshot-port)
            SNAPSHOT_PORT="$2"
            shift 2
            ;;
        --bot-type)
            BOT_TYPE="$2"
            shift 2
            ;;
        --no-bots)
            START_BOTS="no"
            shift
            ;;
        --no-snapshots)
            START_SNAPSHOTS="no"
            shift
            ;;
        --add-mcast-route)
            ADD_MCAST_ROUTE="yes"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Validate bot type
case "$BOT_TYPE" in
    bot_runner|stable_bot_runner|smarter_bots|reject_bot)
        ;;
    *)
        log_error "Invalid bot type: $BOT_TYPE"
        log_error "Valid types: bot_runner, stable_bot_runner, smarter_bots, reject_bot"
        exit 1
        ;;
esac

# Execute command
case "$COMMAND" in
    start)
        start_all
        ;;
    stop)
        stop_all
        ;;
    restart)
        stop_all
        sleep 1
        start_all
        ;;
    status)
        show_status
        ;;
    logs)
        tail_logs
        ;;
    viewer)
        run_viewer
        ;;
    "")
        log_error "No command specified"
        usage
        exit 1
        ;;
    *)
        log_error "Unknown command: $COMMAND"
        usage
        exit 1
        ;;
esac

