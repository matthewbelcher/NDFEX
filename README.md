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
  - `bot_runner`: Random walk fair value market makers and takers
  - `stable_bot_runner`: Constant fair value bots for stable markets
  - `smarter_bots`: Advanced strategies including imbalance and pressure takers

- **Market Data** (`market_data/`): Market data snapshot service
  - Listens to multicast market data
  - Provides TCP snapshots for late-joining clients

- **Viewer** (`viewer/`): Terminal-based market data viewer using FTXUI

- **Web Data** (`web_data/`): WebSocket server for web-based dashboards

- **Clearing Web App** (`clearing-web-app/`): Next.js web interface for clearing data

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

### Running Tests (Linux)

```bash
cd build
cmake -DBUILD_TESTS=ON ..
make -j$(nproc)
ctest --output-on-failure
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
| `bot_runner` | Random walk fair value market makers (default) |
| `stable_bot_runner` | Constant fair value bots for stable markets |
| `smarter_bots` | Advanced strategies with imbalance/pressure takers |
| `reject_bot` | For testing order rejection scenarios |

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

| Symbol ID | Name | Tick Size | Min Qty | Max Qty | Max Price |
|-----------|------|-----------|---------|---------|-----------|
| 1 | GOLD | 10 | 1 | 1000 | 10000000 |
| 2 | BLUE | 5 | 1 | 1000 | 10000000 |

## Network Configuration

| Service | Protocol | Default Port |
|---------|----------|--------------|
| Order Entry | TCP | 1234 |
| Market Data Multicast | UDP | 12345 |
| Clearing Multicast | UDP | 12346 |
| Market Data Snapshot | TCP | 12345 |
| Web Data WebSocket | TCP | 9002 |
| Clearing Web App | HTTP | 3000 |

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
│   └── CMakeLists.txt
├── viewer/              # Terminal UI viewer
│   └── CMakeLists.txt
├── web_data/            # WebSocket server for web clients
│   └── CMakeLists.txt
├── clearing-web-app/    # Next.js web dashboard
│   └── package.json
├── 3rdparty/spdlog/      # Logging library (git submodule)
├── 3rdparty/SPSCQueue/   # Lock-free queue library (git submodule)
├── 3rdparty/FTXUI/       # Terminal UI library (git submodule)
├── build/               # Build output directory (created by cmake)
│   └── bin/             # Compiled executables
└── README.md
```