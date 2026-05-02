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
SNAPSHOT_MCAST_IP="${SNAPSHOT_MCAST_IP:-239.0.0.2}"
CLEARING_MCAST_IP="${CLEARING_MCAST_IP:-239.0.0.3}"
MCAST_BIND_IP="${MCAST_BIND_IP:-$BIND_IP}"

# Default ports
OE_PORT="${OE_PORT:-1234}"
MD_PORT="${MD_PORT:-12345}"
CLEARING_PORT="${CLEARING_PORT:-12346}"
SNAPSHOT_PORT="${SNAPSHOT_PORT:-12345}"

# Bot selection
BOT_TYPE="${BOT_TYPE:-bot_runner}"

# Onload (Solarflare kernel-bypass) integration.
# Components listed in ONLOAD_COMPONENTS will be launched under `onload` when the
# kernel module is loaded (i.e. /dev/onload exists). Set USE_ONLOAD=no to disable,
# or override ONLOAD_COMPONENTS to restrict the set.
USE_ONLOAD="${USE_ONLOAD:-auto}"
ONLOAD_COMPONENTS="${ONLOAD_COMPONENTS:-matching_engine md_snapshots bots web_data}"
ONLOAD_PROFILE="${ONLOAD_PROFILE:-latency}"

# Same-host multicast loopback under onload.
# By default onload accelerates multicast TX but does NOT loop packets back to
# receivers in other onload stacks on the same machine — so a matching_engine
# publishing under onload is invisible to an onload-accelerated bot_runner /
# web_data on the same box. ONLOAD_MCAST_LOOP selects how to recover loopback:
#
#   yes  (default) - EF_FORCE_SEND_MULTICAST=0
#                    Onload declines to accelerate multicast send on sockets
#                    that have IP_MULTICAST_LOOP enabled (matching_engine's
#                    publisher socket does). Mcast TX goes via the kernel
#                    stack so kernel loopback delivers to same-host receivers.
#                    Works on any Solarflare NIC. Costs matching_engine's
#                    mcast sends the onload fast path (OE TCP is unaffected).
#
#   hw             - EF_MCAST_SEND=2, EF_MCAST_RECV_HW_LOOP=1
#                    Keep mcast TX accelerated and use NIC hardware loopback
#                    between onload stacks. Requires a 7000-series or newer
#                    Solarflare NIC AND a firmware variant with loopback
#                    support enabled (configure via sfboot).
#
#   no             - do nothing; same-host mcast will not flow under onload
ONLOAD_MCAST_LOOP="${ONLOAD_MCAST_LOOP:-yes}"

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
    --snapshot-mcast-ip IP Multicast IP for snapshots (default: 239.0.0.2)
    --clearing-ip IP      Multicast IP for clearing data (default: 239.0.0.3)
    --mcast-bind-ip IP    IP to bind multicast listeners (default: 127.0.0.1)
    --oe-port PORT        Order entry port (default: 1234)
    --md-port PORT        Market data multicast port (default: 12345)
    --snapshot-port PORT  Snapshot service port (default: 12345)
    --bot-type TYPE       Bot type: bot_runner, stable_bot_runner, smarter_bots (default: bot_runner)
    --no-bots             Don't start trading bots
    --no-snapshots        Don't start snapshot service
    --no-web-data         Don't start web_data WebSocket server
    --no-homework         Don't start homework checker
    --no-etf              Don't start the ETF service (UNDY create/redeem API)
    --add-mcast-route     Add 239.0.0.0/8 route via MCAST_BIND_IP
    --no-onload-mcast-loop Do not set EF_MCAST_SEND/EF_MCAST_RECV_HW_LOOP
                          (under onload, same-host mcast will not flow)
    -h, --help            Show this help message

Environment Variables:
    BIND_IP, MCAST_IP, SNAPSHOT_MCAST_IP, CLEARING_MCAST_IP, MCAST_BIND_IP
    OE_PORT, MD_PORT, CLEARING_PORT, SNAPSHOT_PORT
    BOT_TYPE
    USE_ONLOAD (auto|yes|no, default auto)
    ONLOAD_COMPONENTS (default: matching_engine md_snapshots bots web_data)
    ONLOAD_PROFILE (default: latency)
    ONLOAD_MCAST_LOOP (yes|hw|no, default yes) - same-host mcast under onload
                      yes: kernel fallback via EF_FORCE_SEND_MULTICAST=0
                      hw:  EF_MCAST_SEND=2 + EF_MCAST_RECV_HW_LOOP=1
                           (requires sfboot firmware loopback variant)
                      no:  leave onload defaults (mcast will not flow)

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
    [[ -n "$(get_running_pid "$name")" ]]
}

get_running_pid() {
    local name="$1"
    local pid=$(get_pid "$name")

    # Check if PID from file is the actual process (not a wrapper script)
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
        # Verify it's the actual binary, not a shell wrapper
        local cmdline=$(ps -p "$pid" -o comm= 2>/dev/null)
        case "$name" in
            matching_engine)
                if [[ "$cmdline" == "matching_engin" || "$cmdline" == "matching_engine" ]]; then
                    echo "$pid"
                    return 0
                fi
                ;;
            md_snapshots)
                if [[ "$cmdline" == "md_snapshots" ]]; then
                    echo "$pid"
                    return 0
                fi
                ;;
            bots)
                # Accept any bot binary for the live-pidfile path — this way
                # `status` works even when the user doesn't re-pass --bot-type.
                # The fallback pgrep (below) is BOT_TYPE-pinned so it won't
                # cross-match liquidity_bots.
                if [[ "$cmdline" =~ ^(bot_runner|stable_bot_ru|smarter_bots|reject_bot)$ ]]; then
                    echo "$pid"
                    return 0
                fi
                ;;
            liquidity_bots)
                if [[ "$cmdline" == "bot_runner" ]]; then
                    echo "$pid"
                    return 0
                fi
                ;;
            web_data)
                if [[ "$cmdline" == "web_data" ]]; then
                    echo "$pid"
                    return 0
                fi
                ;;
            homework_checker)
                if [[ "$cmdline" == "python3" || "$cmdline" == "python" ]]; then
                    echo "$pid"
                    return 0
                fi
                ;;
            slack_bot)
                if [[ "$cmdline" == "python3" || "$cmdline" == "python" ]]; then
                    echo "$pid"
                    return 0
                fi
                ;;
            etf_service)
                if [[ "$cmdline" == "python3" || "$cmdline" == "python" || "$cmdline" == "uv" ]]; then
                    echo "$pid"
                    return 0
                fi
                ;;
        esac
    fi

    # Fallback: find by process name pattern (pgrep uses ERE, so | not \|)
    local pattern=""
    case "$name" in
        matching_engine) pattern="/matching_engine " ;;
        md_snapshots) pattern="/md_snapshots " ;;
        bots) pattern="/${BOT_TYPE} " ;;
        liquidity_bots) pattern="/bot_runner " ;;
        web_data) pattern="web_data 239" ;;
        homework_checker) pattern="server.py.*--port" ;;
        slack_bot) pattern="slack_bot.py" ;;
        etf_service) pattern="etf_service/\.venv/.*app\.py" ;;
    esac

    if [[ -n "$pattern" ]]; then
        pgrep -f "$pattern" 2>/dev/null | head -1
    fi
}

onload_available() {
    [[ "$USE_ONLOAD" == "no" ]] && return 1
    [[ -e /dev/onload ]] || return 1
    command -v onload >/dev/null 2>&1 || return 1
    return 0
}

onload_wanted_for() {
    local name="$1"
    local c
    for c in $ONLOAD_COMPONENTS; do
        if [[ "$c" == "$name" ]]; then return 0; fi
    done
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

    local launcher=()
    if onload_available && onload_wanted_for "$name"; then
        launcher=(onload --profile="$ONLOAD_PROFILE")
        log_info "Starting $name under onload (profile=$ONLOAD_PROFILE)..."
    elif [[ "$USE_ONLOAD" == "yes" ]] && onload_wanted_for "$name"; then
        log_error "USE_ONLOAD=yes but onload is unavailable (module loaded? /dev/onload present?)"
        return 1
    else
        if onload_wanted_for "$name" && [[ ! -e /dev/onload ]]; then
            log_warn "$name: onload requested but /dev/onload missing - using kernel stack"
        fi
        log_info "Starting $name..."
    fi

    # Start the process in background, redirect outside subshell so exec works properly
    (cd "$cwd" && exec "${launcher[@]}" "$exe" $args) > "${LOG_DIR}/${name}.log" 2>&1 &
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
    local found_running=0

    # First try the PID from the PID file
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
        found_running=1
        log_info "Stopping $name (PID: $pid)..."
        kill -TERM "$pid" 2>/dev/null || true

        # Wait up to 5 seconds for graceful shutdown
        local count=0
        while kill -0 "$pid" 2>/dev/null && [[ $count -lt 50 ]]; do
            sleep 0.1
            count=$((count + 1))
        done

        # Force kill if still running
        if kill -0 "$pid" 2>/dev/null; then
            log_warn "$name didn't stop gracefully, forcing..."
            kill -9 "$pid" 2>/dev/null || true
        fi
    fi

    rm -f "${PID_DIR}/${name}.pid"

    # Also try to find and kill by process name pattern (fallback for orphaned processes)
    # pgrep uses ERE, so | not \|
    local pattern=""
    case "$name" in
        matching_engine) pattern="/matching_engine " ;;
        md_snapshots) pattern="/md_snapshots " ;;
        bots) pattern="/${BOT_TYPE} " ;;
        liquidity_bots) pattern="/bot_runner " ;;
        web_data) pattern="web_data 239" ;;
        homework_checker) pattern="server.py.*--port" ;;
        slack_bot) pattern="slack_bot.py" ;;
        etf_service) pattern="etf_service/\.venv/.*app\.py" ;;
    esac

    if [[ -n "$pattern" ]]; then
        local orphan_pids=$(pgrep -f "$pattern" 2>/dev/null || true)
        for opid in $orphan_pids; do
            if [[ "$opid" != "$pid" ]]; then
                found_running=1
                log_info "Found orphaned $name process (PID: $opid), stopping..."
                kill -TERM "$opid" 2>/dev/null || true
                sleep 0.5
                if kill -0 "$opid" 2>/dev/null; then
                    kill -9 "$opid" 2>/dev/null || true
                fi
            fi
        done
    fi

    if [[ $found_running -eq 1 ]]; then
        log_info "$name stopped"
    else
        log_warn "$name was not running"
    fi
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

        # smarter_bots trades against the liquidity providers in bot_runner,
        # so we need bot_runner alongside it.
        if [[ "$BOT_TYPE" == "smarter_bots" ]]; then
            LIQUIDITY_BOTS_BIN="$(find_executable \
                "${BUILD_DIR}/bot_runner" \
                "${SCRIPT_DIR}/bots/out/bot_runner")"
            if [[ -z "$LIQUIDITY_BOTS_BIN" ]]; then
                log_error "bot_runner binary not found (required as LP alongside smarter_bots)"
                exit 1
            fi
        fi
    fi

    if [[ "$START_WEB_DATA" != "no" ]]; then
        WEB_DATA_BIN="$(find_executable \
            "${BUILD_DIR}/web_data" \
            "${SCRIPT_DIR}/web_data/web_data")"
        if [[ -z "$WEB_DATA_BIN" ]]; then
            log_error "web_data binary not found (build/bin or web_data/)"
            exit 1
        fi
    fi

    if [[ "$START_HOMEWORK" != "no" ]]; then
        HOMEWORK_CHECKER_DIR="${SCRIPT_DIR}/homework_checker"
        if [[ ! -f "${HOMEWORK_CHECKER_DIR}/server.py" ]]; then
            log_error "homework_checker not found at ${HOMEWORK_CHECKER_DIR}"
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

apply_onload_env() {
    case "$ONLOAD_MCAST_LOOP" in
        yes)
            export EF_FORCE_SEND_MULTICAST=0
            log_info "Onload multicast loopback: kernel fallback (EF_FORCE_SEND_MULTICAST=0)"
            ;;
        hw)
            export EF_MCAST_SEND=2
            export EF_MCAST_RECV_HW_LOOP=1
            log_info "Onload multicast loopback: hw (EF_MCAST_SEND=2, EF_MCAST_RECV_HW_LOOP=1)"
            log_warn "hw mode requires sfboot firmware variant with multicast loopback enabled"
            ;;
        no)
            log_info "Onload multicast loopback: disabled (same-host mcast will not flow under onload)"
            ;;
        *)
            log_error "Unknown ONLOAD_MCAST_LOOP value: $ONLOAD_MCAST_LOOP (expected yes|hw|no)"
            exit 1
            ;;
    esac
}

start_all() {
    ensure_dirs
    resolve_binaries
    apply_onload_env
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
        # smarter_bots trades against the bot_runner liquidity providers, so
        # start the LP bots first.
        if [[ "$BOT_TYPE" == "smarter_bots" ]]; then
            start_component "liquidity_bots" "$LIQUIDITY_BOTS_BIN" "${SCRIPT_DIR}/bots" \
                "$BIND_IP" "$OE_PORT" "$MCAST_IP" "$SNAPSHOT_MCAST_IP" "$MCAST_BIND_IP"
            sleep 0.5
        fi

        start_component "bots" "$BOTS_BIN" "${SCRIPT_DIR}/bots" \
            "$BIND_IP" "$OE_PORT" "$MCAST_IP" "$SNAPSHOT_MCAST_IP" "$MCAST_BIND_IP"
    fi

    # 4. Start Web Data WebSocket Server
    if [[ "$START_WEB_DATA" != "no" ]]; then
        start_component "web_data" "$WEB_DATA_BIN" "${SCRIPT_DIR}/web_data" \
            "$MCAST_IP" "$SNAPSHOT_MCAST_IP" "$CLEARING_MCAST_IP" "$MCAST_BIND_IP"
        sleep 0.5
    fi

    # 5. Start Homework Checker (and Slack bot)
    if [[ "$START_HOMEWORK" != "no" ]]; then
        start_homework_checker
    fi

    # 6. Start ETF Service (Python - provides dashboard + create/redeem API)
    if [[ "$START_ETF" != "no" ]]; then
        if command -v uv &> /dev/null; then
            log_info "Starting ETF service..."
            (cd "${SCRIPT_DIR}/etf_service" && exec uv run app.py \
                "$MCAST_IP" "$CLEARING_MCAST_IP" "$MCAST_BIND_IP") \
                > "${LOG_DIR}/etf_service.log" 2>&1 &
            local pid=$!
            sleep 2
            if kill -0 "$pid" 2>/dev/null; then
                save_pid "etf_service" "$pid"
                log_info "ETF service started (PID: $pid)"
            else
                log_error "ETF service failed to start. Check ${LOG_DIR}/etf_service.log"
            fi
        else
            log_warn "uv not found, skipping ETF service"
        fi
    fi

    echo ""
    log_info "NDFEX system started!"
    log_info "Logs available in: $LOG_DIR"
}

start_homework_checker() {
    # Check if web_data is running (required dependency)
    if ! is_running "web_data"; then
        log_warn "web_data is not running - homework_checker requires it for market data"
        log_warn "Skipping homework_checker start"
        return 1
    fi

    local hw_dir="${SCRIPT_DIR}/homework_checker"

    # Start homework checker server
    if is_running "homework_checker"; then
        log_warn "homework_checker is already running (PID: $(get_running_pid homework_checker))"
    else
        log_info "Starting homework_checker..."
        (cd "$hw_dir" && exec uv run server.py --port 8080 --required 10) > "${LOG_DIR}/homework_checker.log" 2>&1 &
        local pid=$!
        sleep 2
        if kill -0 "$pid" 2>/dev/null; then
            save_pid "homework_checker" "$pid"
            log_info "homework_checker started (PID: $pid)"
        else
            log_error "homework_checker failed to start. Check ${LOG_DIR}/homework_checker.log"
            return 1
        fi
    fi

    # Start slack bot
    if is_running "slack_bot"; then
        log_warn "slack_bot is already running (PID: $(get_running_pid slack_bot))"
    else
        if [[ -f "${hw_dir}/.env" ]]; then
            log_info "Starting slack_bot..."
            (cd "$hw_dir" && source .env && exec uv run slack_bot.py) > "${LOG_DIR}/slack_bot.log" 2>&1 &
            local pid=$!
            sleep 2
            if kill -0 "$pid" 2>/dev/null; then
                save_pid "slack_bot" "$pid"
                log_info "slack_bot started (PID: $pid)"
            else
                log_warn "slack_bot failed to start. Check ${LOG_DIR}/slack_bot.log"
            fi
        else
            log_warn "Slack bot .env not found, skipping slack_bot"
        fi
    fi
}

stop_all() {
    log_info "Stopping NDFEX system..."

    # Stop in reverse order
    stop_component "etf_service"
    stop_component "slack_bot"
    stop_component "homework_checker"
    stop_component "web_data"
    stop_component "bots"
    stop_component "liquidity_bots"
    stop_component "md_snapshots"
    stop_component "matching_engine"

    log_info "NDFEX system stopped"
}

show_status() {
    echo "NDFEX System Status"
    echo "==================="

    local components=("matching_engine" "md_snapshots" "liquidity_bots" "bots" "web_data" "homework_checker" "slack_bot" "etf_service")

    for comp in "${components[@]}"; do
        local running_pid=$(get_running_pid "$comp")
        if [[ -n "$running_pid" ]]; then
            local pidfile_pid=$(get_pid "$comp")
            if [[ "$running_pid" != "$pidfile_pid" ]]; then
                echo -e "$comp: ${GREEN}RUNNING${NC} (PID: $running_pid) ${YELLOW}[PID file stale]${NC}"
            else
                echo -e "$comp: ${GREEN}RUNNING${NC} (PID: $running_pid)"
            fi
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
START_WEB_DATA="yes"
START_HOMEWORK="yes"
START_ETF="yes"
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
        --no-web-data)
            START_WEB_DATA="no"
            shift
            ;;
        --no-homework)
            START_HOMEWORK="no"
            shift
            ;;
        --no-etf)
            START_ETF="no"
            shift
            ;;
        --add-mcast-route)
            ADD_MCAST_ROUTE="yes"
            shift
            ;;
        --no-onload-mcast-loop)
            ONLOAD_MCAST_LOOP="no"
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

