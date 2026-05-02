# NDFEX

**Notre Dame Fake Exchange**

This project is intended for the course on High-Frequency Trading Technologies at the University of Notre Dame. It is a toy version of a real exchange for students to practice trading strategies.

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           NDFEX System                                  │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────┐        ┌──────────────────────────────────────┐   │
│  │  Trading Bots   │◄──────►│         Matching Engine               │   │
│  │  (bot_runner)   │  TCP   │  ┌────────────┐  ┌────────────────┐  │   │
│  └─────────────────┘        │  │ Order Entry│  │ Order Ladder   │  │   │
│                             │  │   Server   │  │ (Book Matching)│  │   │
│  ┌─────────────────┐        │  └────────────┘  └────────────────┘  │   │
│  │   MD Viewer     │◄───────│  ┌────────────┐  ┌────────────────┐  │   │
│  │  (md_viewer)    │ Mcast  │  │Market Data │  │   Clearing     │  │   │
│  └─────────────────┘        │  │  Publisher │  │   Publisher    │  │   │
│                             │  └────────────┘  └────────────────┘  │   │
│  ┌─────────────────┐        └──────────────────────────────────────┘   │
│  │  MD Snapshots   │◄───────── Multicast                               │
│  │ (md_snapshots)  │                                                   │
│  └─────────────────┘        ┌──────────────────────────────────────┐   │
│                             │         Web Interface                 │   │
│  ┌─────────────────┐        │  ┌────────────┐  ┌────────────────┐  │   │
│  │    Web Data     │◄──────►│  │ web_data   │  │ clearing-web   │  │   │
│  │   (WebSocket)   │        │  │  server    │  │    -app        │  │   │
│  └─────────────────┘        │  └────────────┘  └────────────────┘  │   │
│                             └──────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

### Components

- **Matching Engine** (`matching_engine/`): Core exchange functionality
  - Market Data Publisher: Sends order book changes via multicast
  - Order Entry Server: TCP server for order entry protocol
  - Order Ladder: Book management and order matching
  - ME Broker: Queues between client order entry server and matching engine
  - Clearing Publisher: Broadcasts trade/position data

- **Bot Runner** (`bots/`): Simulated trading strategies
  - `bot_runner`: Random walk fair value market makers and takers; provides
    UNDY ETF liquidity via a basket-aware market maker. Hosts an admin
    command port (loopback TCP 1235) used to drive thin-market and
    bear-regime events during the competition.
  - `stable_bot_runner`: Constant fair value bots for stable markets
  - `smarter_bots`: Advanced strategies including imbalance and pressure
    takers. Trades against `bot_runner`, so `ndfex.sh start --bot-type
    smarter_bots` co-launches a `bot_runner` instance as the LP.
  - `reject_bot`: For testing order rejection scenarios.
  - `rule_breaker`: Test bot that deliberately breaches the position
    limit and PnL floor — used to verify the leaderboard's breach
    indicators.

- **Market Data** (`market_data/`): Market data snapshot service
  - Listens to multicast market data
  - Provides TCP snapshots for late-joining clients

- **Viewer** (`viewer/`): Terminal-based market data viewer using FTXUI

- **Web Data** (`web_data/`): WebSocket server for web-based dashboards.
  Tails `logs/etf_adjustments.log` to fold ETF create/redeem activity into
  the leaderboard, and tracks competition rule breaches (per-symbol
  position limit, aggregate PnL floor).

- **Clearing Web App** (`clearing-web-app/`): Next.js leaderboard. Reads
  the web_data WebSocket and surfaces per-team aggregates + breach badges.

- **ETF Service** (`etf_service/`): Flask app exposing UNDY create/redeem
  via REST. HTTP Basic Auth against `matching_engine/users.txt`; every
  successful adjustment is appended to `logs/etf_adjustments.log` for
  consumption by `web_data` and `scripts/replay_pnl.py`.

- **Homework Checker** (`homework_checker/`): Student-facing TCP server
  + Slack bot that grades order-book reconstruction assignments.

- **Scripts** (`scripts/`): Day-of operations tooling — `replay_pnl.py`,
  `team_position.py`, `rotate_logs.sh`, `disk_alarm.sh`. See [Operations](#operations).

## Prerequisites

### Required Dependencies (Linux)

- **CMake**: Version 3.16 or higher
- **Compiler**: `clang++` or `g++` with C++17 support
- **spdlog**: Logging library (included as git submodule)
- **SPSCQueue**: Lock-free queue (included as git submodule)
- **pthread**: Provided by glibc on Linux

### Optional Dependencies

- **FTXUI**: Terminal UI library for `md_viewer` (included as git submodule, requires building)
- **websocketpp**: WebSocket library for `web_data`
- **libpcap**: For `pcap_printer` tool (Linux package, optional)
- **GoogleTest**: For running C++ tests
- **Node.js**: For the clearing web app

### Installing Dependencies

The core dependencies (spdlog, SPSCQueue, FTXUI) are included as git submodules.

```bash
# Clone the repository with submodules
git clone --recursive https://github.com/matthewbelcher/NDFEX.git

# Or if already cloned, initialize submodules
git submodule update --init --recursive

# Build FTXUI (required for viewer)
cd 3rdparty/FTXUI && mkdir build && cd build && cmake .. && make && cd ../../..

# Install GoogleTest (for tests)
# Ubuntu/Debian:
#   sudo apt-get install -y libgtest-dev
# Fedora:
#   sudo dnf install -y gtest-devel
# Arch:
#   sudo pacman -S gtest

# For web app
cd clearing-web-app && npm install
```

## Building

The project uses CMake for building. All executables are output to `build/bin/`.

### Quick Start

```bash
# Initialize submodules (if not already done)
git submodule update --init --recursive

# Create build directory and configure
mkdir build && cd build
cmake ..

# Build all core components
make -j$(nproc)
```

### Build Options

CMake options can be set during configuration:

```bash
# Release build (optimized)
cmake -DCMAKE_BUILD_TYPE=Release ..

# Debug build (with symbols)
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Build with viewer (requires FTXUI to be built first)
cmake -DBUILD_VIEWER=ON ..

# Build with web data server (requires websocketpp)
cmake -DBUILD_WEB_DATA=ON ..

# Build with tests (requires GoogleTest)
cmake -DBUILD_TESTS=ON ..

# Build everything
cmake -DBUILD_VIEWER=ON -DBUILD_WEB_DATA=ON -DBUILD_TESTS=ON ..
```

### Building FTXUI (for viewer)

```bash
cd 3rdparty/FTXUI
mkdir build && cd build
cmake ..
make -j$(nproc)
cd ../../..
```

### Building and Running Tests (Linux)

```bash
# Configure with tests enabled
cmake -S . -B build -DBUILD_TESTS=ON

# Build tests (and core targets)
cmake --build build -j$(nproc)

# Run all tests
ctest --test-dir build --output-on-failure

# Run a single test binary
ctest --test-dir build -R test_md_mcast --output-on-failure
```

### Built Executables

After building, executables are located in `build/bin/`:

| Executable | Description |
|------------|-------------|
| `matching_engine` | Core exchange server |
| `md_snapshots` | Market data snapshot service |
| `bot_runner` | Main trading bot runner |
| `stable_bot_runner` | Constant fair value bots |
| `smarter_bots` | Advanced trading strategies |
| `reject_bot` | Rejection testing bot |
| `print_snapshots` | Snapshot printing utility |
| `md_viewer` | Terminal UI viewer (optional) |
| `bbo_printer` | BBO printer (optional) |
| `pcap_printer` | PCAP printer (optional, Linux only) |
| `web_data` | WebSocket server (optional) |
| `rule_breaker` | Position-limit / PnL-floor breach test bot |
| `print_snapshots` | Dump snapshot multicast to stdout |

### Clean Build

```bash
rm -rf build
```

## Running the System

### Quick Start with Control Script

The easiest way to run NDFEX is using the control script:

```bash
# Create users file first (ndfex.sh runs from matching_engine/)
echo "99 test testuser" > matching_engine/users.txt

# Start all components
./ndfex.sh start

# Check status
./ndfex.sh status

# View logs
./ndfex.sh logs

# Stop all components
./ndfex.sh stop
```

#### Control Script Options

```bash
# Start with custom network configuration
./ndfex.sh start --bind-ip 192.168.1.100 --mcast-ip 239.1.1.1

# Start with different bot type
./ndfex.sh start --bot-type smarter_bots

# Start without bots (matching engine + snapshots only)
./ndfex.sh start --no-bots

# Start without snapshot service
./ndfex.sh start --no-snapshots

# Restart all components
./ndfex.sh restart
```

#### Available Bot Types

| Bot Type | Description |
|----------|-------------|
| `bot_runner` | Random walk fair value market makers + UNDY basket LP (default) |
| `stable_bot_runner` | Constant fair value bots for stable markets |
| `smarter_bots` | Advanced strategies with imbalance/pressure takers (auto-launches `bot_runner` as LP) |
| `reject_bot` | For testing order rejection scenarios |
| `rule_breaker` | Deliberately breaches position-limit and PnL-floor competition rules |

### Manual Startup

For more control, you can start components individually:

#### 1. Create Users File

The matching engine reads user credentials from `users.txt` in the working directory:

```bash
# Format: client_id username password
echo "99 test testuser" > users.txt
echo "1 student1 password1" >> users.txt
```

#### 2. Start the Matching Engine

```bash
./build/bin/matching_engine <bind_ip> <mcast_ip> <clearing_ip>

# Example (localhost):
./build/bin/matching_engine 127.0.0.1 239.0.0.1 239.0.0.2
```

Arguments:
- `bind_ip`: IP address to bind the order entry server
- `mcast_ip`: Multicast IP for market data (e.g., 239.0.0.1)
- `clearing_ip`: Multicast IP for clearing data

#### 3. Start Market Data Snapshot Service

```bash
./build/bin/md_snapshots <md_mcast_ip> <md_port> <snapshot_mcast_ip> <snapshot_port> <mcast_bind_ip>

# Example:
./build/bin/md_snapshots 239.0.0.1 12345 239.0.0.3 12345 127.0.0.1
```

#### 4. Start Trading Bots

```bash
./build/bin/bot_runner <oe_ip> <oe_port> <mcast_ip> <snapshot_ip> <mcast_bind_ip>

# Example:
./build/bin/bot_runner 127.0.0.1 1234 239.0.0.1 239.0.0.3 127.0.0.1
```

#### 5. Optional: Start Viewer

```bash
./build/bin/md_viewer <mcast_ip> <snapshot_ip> <mcast_bind_ip>

# Example:
./build/bin/md_viewer 239.0.0.1 239.0.0.3 127.0.0.1
```

#### 6. Optional: Start Web Interface

```bash
# Terminal 1: Start web data server
./build/bin/web_data <md_mcast_ip> <snapshot_mcast_ip> <clearing_mcast_ip> <mcast_bind_ip>

# Example:
./build/bin/web_data 239.0.0.1 239.0.0.3 239.0.0.2 127.0.0.1

# Terminal 2: Start Next.js web app
cd clearing-web-app
npm run dev
```

## Symbols

The exchange supports the following symbols by default:

| Symbol ID | Name | Tick Size | Min Qty | Max Qty | Max Price | Notes |
|-----------|------|-----------|---------|---------|-----------|-------|
| 1 | GOLD | 10 | 1 | 1000 | 10000000 | Stand-alone |
| 2 | BLUE | 5 | 1 | 1000 | 10000000 | Stand-alone |
| 3 | KNAN | 5 | 1 | 1000 | 10000000 | UNDY component (Notre Dame dorm) |
| 4 | STED | 5 | 1 | 1000 | 10000000 | UNDY component |
| 5 | FISH | 5 | 1 | 1000 | 10000000 | UNDY component |
| 6 | DILN | 5 | 1 | 1000 | 10000000 | UNDY component |
| 7 | SORN | 5 | 1 | 1000 | 10000000 | UNDY component |
| 8 | RYAN | 5 | 1 | 1000 | 10000000 | UNDY component |
| 9 | LYON | 5 | 1 | 1000 | 10000000 | UNDY component |
| 10 | WLSH | 5 | 1 | 1000 | 10000000 | UNDY component |
| 11 | LEWI | 5 | 1 | 1000 | 10000000 | UNDY component |
| 12 | BDIN | 5 | 1 | 1000 | 10000000 | UNDY component |
| 13 | UNDY | 10 | 1 | 1000 | 10000000 | ETF; basket of symbols 3–12 |

UNDY is a 1:1:…:1 basket of the ten dorm components. Create/redeem is
handled out-of-band by the ETF service (see [ETF Service](#etf-service))
— it does not trade on the matching engine; positions are settled via
an adjustment log shared with `web_data` and `scripts/replay_pnl.py`.

## Network Configuration

| Service | Protocol | Default Port |
|---------|----------|--------------|
| Order Entry | TCP | 1234 |
| LP Admin Command Port | TCP (loopback only) | 1235 |
| Market Data Multicast | UDP | 12345 |
| Clearing Multicast | UDP | 12346 |
| Market Data Snapshot | TCP | 12345 |
| Web Data WebSocket | TCP | 9002 |
| ETF Service REST | HTTP | 5000 |
| ETF Service WebSocket | TCP | 9003 |
| Clearing Web App | HTTP | 3000 |

## ETF Service

UNDY shares are created and redeemed via a separate Flask service that
runs alongside the matching engine.

```bash
# Started by ndfex.sh by default; manual run:
cd etf_service && uv run app.py \
    <md_mcast_ip> <clearing_mcast_ip> <mcast_bind_ip>
```

REST endpoints (all behind HTTP Basic Auth against
`matching_engine/users.txt`):

| Endpoint | Description |
|----------|-------------|
| `POST /create` | Body `{"amount": N}` — burn N of each UNDY component, mint N UNDY |
| `POST /redeem` | Body `{"amount": N}` — burn N UNDY, mint N of each component |
| `GET /whoami` | Identity bound to the current Basic Auth credentials |
| `GET /positions/<client_id>` | Effective positions including ETF adjustments |
| `GET /history` | Per-client create/redeem history |

Successful create/redeem operations are appended to
`logs/etf_adjustments.log` (one line per `(client_id, symbol, delta)`
pair, format `<unix_ns> <op> <client_id> <symbol> <delta>`). `web_data`
tails this file to fold ETF positions into the leaderboard, and
`scripts/replay_pnl.py` reads it for end-of-day scoring.

## Operator Events

`bots/bot_runner` listens on `127.0.0.1:1235` for line-based admin
commands. Two operator-fired competition events are wired up:

```bash
# Pause LP quoting, drain cancels, re-quote with random per-symbol FV jumps
bots/thin_market_event.sh 60        # 60 s pause, default
BOTS_THIN_SEED=42 bots/thin_market_event.sh 5  # reproducible drill

# Directional down-drift overlay + sell-heavy taker bias for 25 minutes
bots/bear_regime.sh                            # default 1500 s
bots/bear_regime.sh 1800                       # 30 min
BOTS_BEAR_DRIFT_PER_MIN=-2 BOTS_BEAR_SELL_BIAS=0.91 \
  bots/bear_regime.sh 1800                     # harder bear
```

Both wrappers trap INT/TERM and always issue the corresponding END
command, so an interrupted script does not leave the LP biased forever.
For ad-hoc commands use `bots/lp_cmd.sh THIN_PAUSE`, `STATUS`, etc. —
it speaks `/dev/tcp` so no `nc` dependency.

## Operations

`scripts/` holds the day-of tooling:

| Script | Purpose |
|--------|---------|
| `replay_pnl.py` | Reconstruct per-client positions, volume, and PnL from `matching_engine/logs/ME_*` + `logs/etf_adjustments.log`, applying competition rules. Per-session CSVs + cross-session aggregate. Optional `--from`/`--to` window for the official scoring period. |
| `team_position.py` | One-team variant for in-session "what's my authoritative position" lookups. |
| `rotate_logs.sh` | Daily gzip of dated spdlog files (skips today's open file). |
| `disk_alarm.sh` | cron-friendly disk + per-log-file size check; optional Slack DM via `SLACK_BOT_TOKEN` / `INSTRUCTOR_USER_ID` with a signature-based cooldown. |

## Lab Setup (Solarflare + Arista)

This section captures the current lab setup and the single-script workflow.

### Build with clang (local Makefiles, Linux)

The lab build uses the per-component Makefiles (clang).

```bash
make -C matching_engine -j$(nproc)
make -C market_data -j$(nproc)
make -C bots -j$(nproc)
```

### Host networking

Solarflare connectivity for the exchange host (port 47) should be configured on a private network.
The monitor port (48) is configured on the switch and should remain unnumbered.

On client hosts (`hftt0-3`), two Solarflare NICs should be assigned with sequential IP addresses:

| Host | Interface 1 | Interface 2 |
|------|-------------|-------------|
| hftt0 | `<NETWORK>.10` | `<NETWORK>.11` |
| hftt1 | `<NETWORK>.12` | `<NETWORK>.13` |
| hftt2 | `<NETWORK>.14` | `<NETWORK>.15` |
| hftt3 | `<NETWORK>.16` | `<NETWORK>.17` |

### Start everything (single script)

The `ndfex.sh` wrapper now resolves clang-built binaries, starts components with the correct
arguments, and can add the multicast route on the Solarflare interface.

```bash
./ndfex.sh start \
  --bind-ip <EXCHANGE_IP> \
  --mcast-bind-ip <EXCHANGE_IP> \
  --add-mcast-route
```

Notes:
- `md_snapshots` must join the multicast group (not the host IP).
- Snapshots are published on a separate multicast group (`239.0.0.3`) to avoid
  mixing live market data and snapshot traffic.
- Running from the `matching_engine/` directory is required so `users.txt` is found.
- The multicast route is added as `239.0.0.0/8` via the interface that owns `MCAST_BIND_IP`.

### Viewer (FTXUI)

The FTXUI market data viewer uses both multicast groups and is available as a
subcommand in `ndfex.sh`.

```bash
./ndfex.sh viewer --mcast-bind-ip <EXCHANGE_IP>
```

If the viewer binary is missing, build it with:

```bash
make -C viewer
```

### Verify multicast on clients

Use the helper script to configure/check Solarflare IPs and verify multicast reception:

```bash
python3 scripts/check_hftt_solarflare.py --target-ip <EXCHANGE_IP>
```

The script warns if the target IP is on the wrong switch port.

## Logs

All components write logs to a `logs/` directory in their working directory:
- `logs/ME*` - Matching engine logs
- `logs/SNAPSHOT*` - Snapshot service logs
- `logs/bot_runner*` - Bot runner logs
- `logs/viewer*` - Viewer logs
- `logs/web_data*` - Web data server logs
- `logs/etf_adjustments.log` - Append-only ETF create/redeem log (single
  source of truth for non-fill position changes; consumed by `web_data`
  and `scripts/replay_pnl.py`)

## Project Structure

```
NDFEX/
├── CMakeLists.txt       # Top-level CMake configuration
├── ndfex.sh             # System control script (start/stop/status)
├── matching_engine/     # Core exchange: order matching, market data, clearing
│   ├── tests/           # Unit tests for matching engine
│   └── CMakeLists.txt
├── order_entry/         # Order entry protocol and server
│   └── tests/           # Unit tests for order entry
├── market_data/         # Market data snapshot service
│   └── CMakeLists.txt
├── bots/                # Trading bot implementations
│   ├── *.sh             # thin_market_event.sh, bear_regime.sh, lp_cmd.sh
│   └── CMakeLists.txt
├── viewer/              # Terminal UI viewer
│   └── CMakeLists.txt
├── web_data/            # WebSocket server for web clients (C++ + websocketpp)
│   └── CMakeLists.txt
├── clearing-web-app/    # Next.js web dashboard / leaderboard
│   └── package.json
├── etf_service/         # Flask service for UNDY create/redeem
├── homework_checker/    # Student-facing order-book grader + Slack bot
├── scripts/             # Operations tooling (replay_pnl, rotate_logs, etc.)
├── 3rdparty/spdlog/      # Logging library (git submodule)
├── 3rdparty/SPSCQueue/   # Lock-free queue library (git submodule)
├── 3rdparty/FTXUI/       # Terminal UI library (git submodule)
├── build/               # Build output directory (created by cmake)
│   └── bin/             # Compiled executables
└── README.md
```