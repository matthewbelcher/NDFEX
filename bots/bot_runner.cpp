/**
 * Run the bots that simulate exchange participants.
 */
#include "oe_client.H"
#include "md_client.H"
#include "matching_engine/symbol_definition.H"
#include "matching_engine/utils.H"
#include "fair_value.H"
#include "random_walk_fair_value.H"
#include "basket_fair_value.H"
#include "fair_value_mm.H"
#include "fair_value_stack_mm.H"
#include "random_taker.H"
#include "command_port.H"

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/daily_file_sink.h>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {
// Thin-market event flags. The command-port handler thread flips request
// atomics; the main loop transitions between states. There are two
// "in-event" states:
//   pausing — cancel drain in progress; quoting suspended
//   paused  — drain complete; quoting suspended; waiting for resume
// We don't burst all cancels in one tick — that hits the matching engine's
// MAX_MSGS_PER_SECOND=1000 limit and tears down the OE socket.
std::atomic<bool> g_thin_pause_requested{false};
std::atomic<bool> g_thin_resume_requested{false};
std::atomic<bool> g_thin_pausing{false};
std::atomic<bool> g_thin_paused{false};

// Per-MM cancel budget per main-loop tick. The OE rate gate (700/sec) is
// what actually throttles the burst — this just bounds how many cancels we
// queue from any one MM in a single tick before checking the gate again.
constexpr size_t THIN_CANCEL_BUDGET_PER_MM = 16;

// Bear-regime event flags. The operator sends BEAR_START on the command
// port to enter a directional down-drift + sell-bias regime, and BEAR_END
// (typically after a sleep in bots/bear_regime.sh) to exit it. Per-event
// tunables (drift magnitude / sell bias) live in g_bear_params and are
// updated by the handler thread before the request flag is set, then
// read by the main loop when it processes the request.
std::atomic<bool> g_bear_start_requested{false};
std::atomic<bool> g_bear_end_requested{false};
std::atomic<bool> g_bear_active{false};

std::mutex g_bear_params_mu;
int32_t g_bear_drift_per_min = -1;
double g_bear_sell_bias = 0.85;

// Default bind for the admin command port. 1235 sits one above the OE port
// (1234) and is loopback-only — never exposed to student bots.
constexpr const char* COMMAND_PORT_BIND = "127.0.0.1";
constexpr uint16_t COMMAND_PORT = 1235;

// Trim ASCII whitespace from both ends.
std::string trim(std::string s) {
    auto issp = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    size_t b = 0; while (b < s.size() && issp(s[b])) ++b;
    size_t e = s.size(); while (e > b && issp(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

// Uppercase ASCII for case-insensitive command matching.
std::string upper(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return s;
}
}  // namespace

int main(int argc, char* argv[]) {

    if (argc != 6) {
        std::cerr << "Usage: " << argv[0] << " <ip address> <port> <mcast ip address> <snapshot ip address> <mcast bind ip>" << std::endl;
        return 1;
    }

    std::string ip = argv[1];
    uint16_t port = std::stoi(argv[2]);

    std::string mcast_ip = argv[3];
    std::string snapshot_ip = argv[4];
    std::string mcast_bind_ip = argv[5];

    auto logger =  spdlog::daily_logger_mt<spdlog::async_factory>("async_logger", "logs/bot_runner");

    // create bots
    ndfex::bots::user_info user1 = {"test", "testuser", 99};
    ndfex::bots::OEClient client1(user1, ip, port, logger, 700);

    if (!client1.login()) {
        std::cerr << "Failed to login" << std::endl;
        return 1;
    }

    // Admin command port. Replaces the previous SIGUSR1/SIGUSR2 (thin event)
    // and SIGRTMIN+1/+2 (bear regime) signal interface with a line-based
    // TCP protocol so new operator controls can be added without burning
    // additional signals. Loopback-only.
    //
    // Commands (case-insensitive):
    //   THIN_PAUSE                          -> pause + drain LP cancels
    //   THIN_RESUME                         -> jump FVs and resume quoting
    //   BEAR_START [drift=N] [bias=F]       -> start directional down-regime
    //                                          (defaults: drift=-1, bias=0.85)
    //   BEAR_END                            -> end regime, bake in drift,
    //                                          reset takers to 0.5 sell bias
    //   STATUS                              -> one-line state report
    //   HELP                                -> command list
    auto cmd_handler = [](const std::string& raw_line) -> std::string {
        const std::string line = trim(raw_line);
        if (line.empty()) return "OK";
        const auto toks = split_ws(line);
        const std::string cmd = upper(toks[0]);

        if (cmd == "THIN_PAUSE") {
            g_thin_pause_requested.store(true);
            return "OK thin pause requested";
        }
        if (cmd == "THIN_RESUME") {
            g_thin_resume_requested.store(true);
            return "OK thin resume requested";
        }
        if (cmd == "BEAR_START") {
            int32_t drift = -1;
            double bias = 0.85;
            for (size_t i = 1; i < toks.size(); i++) {
                const auto& a = toks[i];
                const auto eq = a.find('=');
                if (eq == std::string::npos) {
                    return "ERR bad arg '" + a + "' (expected key=value)";
                }
                const std::string k = upper(a.substr(0, eq));
                const std::string v = a.substr(eq + 1);
                try {
                    if (k == "DRIFT") drift = static_cast<int32_t>(std::stol(v));
                    else if (k == "BIAS") bias = std::stod(v);
                    else return "ERR unknown key '" + k + "' (want DRIFT or BIAS)";
                } catch (const std::exception& e) {
                    return std::string("ERR parse ") + k + ": " + e.what();
                }
            }
            if (bias < 0.0 || bias > 1.0) return "ERR bias must be in [0.0, 1.0]";
            {
                std::lock_guard<std::mutex> lk(g_bear_params_mu);
                g_bear_drift_per_min = drift;
                g_bear_sell_bias = bias;
            }
            g_bear_start_requested.store(true);
            std::ostringstream oss;
            oss << "OK bear start requested drift=" << drift << " bias=" << bias;
            return oss.str();
        }
        if (cmd == "BEAR_END") {
            g_bear_end_requested.store(true);
            return "OK bear end requested";
        }
        if (cmd == "STATUS") {
            std::ostringstream oss;
            oss << "OK thin_pausing=" << g_thin_pausing.load()
                << " thin_paused=" << g_thin_paused.load()
                << " bear_active=" << g_bear_active.load();
            return oss.str();
        }
        if (cmd == "HELP") {
            return "OK commands: THIN_PAUSE THIN_RESUME "
                   "BEAR_START [drift=N] [bias=F] BEAR_END STATUS HELP";
        }
        return "ERR unknown command '" + cmd + "' (try HELP)";
    };

    ndfex::bots::CommandPort command_port(COMMAND_PORT_BIND, COMMAND_PORT, cmd_handler, logger);
    command_port.start();

    std::vector<symbol_definition> symbols = {
        {1, 10, 1, 1000, 10000000, 0},  // GOLD
        {2, 5, 1, 1000, 10000000, 0},   // BLUE
        // UNDY ETF underlying components (Notre Dame dorms)
        // Men's dorms
        {3, 5, 1, 1000, 10000000, 0},   // KNAN - Keenan Hall
        {4, 5, 1, 1000, 10000000, 0},   // STED - St. Edward's Hall
        {5, 5, 1, 1000, 10000000, 0},   // FISH - Fisher Hall
        {6, 5, 1, 1000, 10000000, 0},   // DILN - Dillon Hall
        {7, 5, 1, 1000, 10000000, 0},   // SORN - Sorin Hall
        // Women's dorms
        {8, 5, 1, 1000, 10000000, 0},   // RYAN - Ryan Hall
        {9, 5, 1, 1000, 10000000, 0},   // LYON - Lyons Hall
        {10, 5, 1, 1000, 10000000, 0},  // WLSH - Walsh Hall
        {11, 5, 1, 1000, 10000000, 0},  // LEWI - Lewis Hall
        {12, 5, 1, 1000, 10000000, 0},  // BDIN - Badin Hall
        // ETF (1 UNDY = 1 share of each underlying)
        {13, 10, 1, 1000, 10000000, 0}, // UNDY - Notre Dame Dorm ETF
    };

    // create market data client first so the UNDY BasketFairValue can read
    // component mid-prices out of its order books
    ndfex::bots::MDClient md_client(mcast_ip, 12345, snapshot_ip, 12345, mcast_bind_ip, logger,
                                    /*recover_on_drops=*/true);
    md_client.wait_for_snapshot();

    // UNDY = 1 share of each of the 10 dorms (symbols 3..12).
    // The BasketFairValue reads dorm mids out of md_client's order books and
    // sums them — it deliberately does NOT share the dorms' RandomWalkFairValue
    // objects, so UNDY's quotes track the component market prices, not the
    // underlying random walk.
    std::vector<std::pair<uint32_t, int32_t>> undy_components = {
        {3, 1}, {4, 1}, {5, 1}, {6, 1}, {7, 1},
        {8, 1}, {9, 1}, {10, 1}, {11, 1}, {12, 1},
    };

    std::vector<ndfex::bots::FairValue*> fair_values{
        new ndfex::bots::RandomWalkFairValue(12000, symbols[0]),  // GOLD
        new ndfex::bots::RandomWalkFairValue(9000, symbols[1]),   // BLUE
        // Dorm underlyings - prices around 500-700
        new ndfex::bots::RandomWalkFairValue(5500, symbols[2]),   // KNAN
        new ndfex::bots::RandomWalkFairValue(5200, symbols[3]),   // STED
        new ndfex::bots::RandomWalkFairValue(5800, symbols[4]),   // FISH
        new ndfex::bots::RandomWalkFairValue(5100, symbols[5]),   // DILN
        new ndfex::bots::RandomWalkFairValue(6000, symbols[6]),   // SORN
        new ndfex::bots::RandomWalkFairValue(5300, symbols[7]),   // RYAN
        new ndfex::bots::RandomWalkFairValue(5400, symbols[8]),   // LYON
        new ndfex::bots::RandomWalkFairValue(5600, symbols[9]),   // WLSH
        new ndfex::bots::RandomWalkFairValue(5700, symbols[10]),  // LEWI
        new ndfex::bots::RandomWalkFairValue(5900, symbols[11]),  // BDIN
        // UNDY ETF - computed from component market mid-prices
        new ndfex::bots::BasketFairValue(md_client, undy_components,
                                         /*initial_fv=*/55500, logger),
    };

    // Variances for each market maker (one value per symbol).
    // UNDY (last column) is always 0: its fair value is computed from the
    // component market prices, so biasing it would mean "quote the basket
    // away from the basket" which we never want.
    // Format: {GOLD, BLUE, KNAN, STED, FISH, DILN, SORN, RYAN, LYON, WLSH, LEWI, BDIN, UNDY}
    std::vector<std::vector<int32_t>> variances = {
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},       // Neutral
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0},       // Slightly bullish
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0},  // Slightly bearish
        {1, 0, 2, 0, 1, 0, 2, 0, 1, 0, 2, 0, 0},       // Mixed
        {2, 0, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2, 0},       // Alternating
        {0, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0},       // Dorm focused
        {3, 0, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, 0},  // Contrarian
    };

    uint32_t last_order_id = 1;
    std::vector<ndfex::bots::FairValueMarketMaker> market_makers;

    // Per-symbol quote widths. GOLD (idx 0) and BLUE (idx 1) keep their
    // historical 2/3-tick widths since they aren't part of the UNDY basket.
    // The 10 dorm components (idx 2..11) and UNDY itself (idx 12) get
    // tightened by 1 tick — narrower component mids → less noise feeding
    // BasketFairValue → smaller residual ETF arb edge for taker scalpers.
    // Tightened 2026-04-28 (effective next session).
    auto widths_for_mm = [&symbols](size_t mm_idx) {
        const uint32_t base_w   = (mm_idx < 3 ? 2 : 3);   // GOLD / BLUE
        const uint32_t basket_w = (mm_idx < 3 ? 1 : 2);   // dorms 3-12, UNDY
        std::vector<uint32_t> w;
        w.reserve(symbols.size());
        for (size_t i = 0; i < symbols.size(); i++) {
            w.push_back(i < 2 ? base_w : basket_w);
        }
        return w;
    };

    for (size_t i = 0; i < variances.size(); i++) {
        market_makers.emplace_back(client1, md_client, fair_values, variances[i],
            symbols, widths_for_mm(i), 50 - 4*i, last_order_id, logger);
    }

    // UNDY-dedicated basket market makers. The generic MMs above quote every
    // symbol and all refresh UNDY at once when any component mid moves —
    // these staggered-width MMs keep a resting UNDY book during those
    // refresh windows so the basket is never unquoted.
    // NOTE: undy_only_fv must outlive undy_market_makers (stored by reference).
    //
    // Tightened 2026-04-28 (effective next session): widths {2,3,5} → {1,2,3}.
    // Day-1 review showed team1 running an ETF arb against the wider top-of-
    // book, taking ~$0.80/lot edge per cross. Halving the front-of-book
    // half-spread (from 2 ticks → 1 tick on layer 1, with corresponding
    // narrowing on the back layers) cuts that edge roughly in half without
    // reducing depth — sizes are unchanged so the LP still posts the same
    // qty per layer, just at tighter prices.
    std::vector<symbol_definition> undy_only_symbols = {symbols.back()};
    std::vector<ndfex::bots::FairValue*> undy_only_fv = {fair_values.back()};
    std::vector<std::vector<int32_t>> undy_only_variances = {{0}, {0}, {0}};
    const std::array<uint32_t, 3> undy_widths = {1, 2, 3};
    const std::array<uint32_t, 3> undy_quantities = {10, 15, 20};
    std::vector<ndfex::bots::FairValueMarketMaker> undy_market_makers;
    undy_market_makers.reserve(undy_widths.size());
    for (size_t i = 0; i < undy_widths.size(); i++) {
        undy_market_makers.emplace_back(client1, md_client, undy_only_fv,
            undy_only_variances[i], undy_only_symbols,
            undy_widths[i], undy_quantities[i], last_order_id, logger);
    }

    // UNDY is symbols.back(); its FV is fair_values.back() (a BasketFairValue).
    // The stacker for UNDY runs in basket-aware mode so it stays outside the
    // structural arb range. Other symbols use the legacy BBO-anchored mode.
    const uint32_t etf_symbol_id = symbols.back().symbol;
    std::vector<ndfex::bots::FairValueStackingMarketMaker> stackers;
    stackers.reserve(symbols.size());
    for (size_t i = 0; i < symbols.size(); i++) {
        const auto& symbol = symbols[i];
        if (symbol.symbol == etf_symbol_id) {
            stackers.emplace_back(client1, md_client, symbol, fair_values[i],
                                  /*width_in_ticks=*/1, /*quantity=*/2,
                                  last_order_id, logger);
        } else {
            stackers.emplace_back(client1, md_client, symbol,
                                  /*width_in_ticks=*/1, /*quantity=*/2,
                                  last_order_id, logger);
        }
    }

    std::vector<ndfex::bots::RandomTaker*> takers;
    for (size_t i = 0; i < 4; i++) {
        takers.push_back(new ndfex::bots::RandomTaker(client1, md_client, symbols, i+1, last_order_id, 6+i, logger));
    }

    // Dedicated infrequent 1-lot taker on UNDY so the basket always sees
    // some occasional cross-flow independent of the generic takers' cadence.
    // time_scale=60 gives a per-tick wait uniformly in [0.6s, 30s].
    takers.push_back(new ndfex::bots::RandomTaker(client1, md_client,
        std::vector<symbol_definition>{symbols.back()}, /*quantity=*/1,
        last_order_id, /*time_scale=*/120, logger));

    // process messages from the server
    while (true) {
        client1.process();
        md_client.process();

        // -------- Thin-market event state machine --------
        // Flag transitions are cheap (atomic store + maybe in-memory FV
        // jumps); they don't issue any wire traffic, so they always run
        // before the rate gate to keep signal handling prompt.
        if (g_thin_pause_requested.exchange(false)) {
            g_thin_pausing.store(true);
            g_thin_paused.store(false);
            logger->warn("[thin_event] PAUSE requested — draining cancels");
        }

        if (g_thin_resume_requested.exchange(false)) {
            // Seed: BOTS_THIN_SEED env var if set (for reproducible drills),
            // otherwise std::random_device for true surprise.
            std::mt19937 rng;
            const char* seed_env = std::getenv("BOTS_THIN_SEED");
            if (seed_env != nullptr && *seed_env != '\0') {
                rng.seed(static_cast<uint32_t>(std::strtoul(seed_env, nullptr, 10)));
            } else {
                std::random_device rd;
                rng.seed(rd());
            }
            std::uniform_real_distribution<double> dist(0.6, 1.4);

            std::ostringstream summary;
            for (size_t i = 0; i < fair_values.size(); i++) {
                const double m = dist(rng);
                fair_values[i]->apply_jump(m);
                if (i > 0) summary << ' ';
                summary << "sym" << symbols[i].symbol << '=' << m;
            }
            // Resume immediately — quoting may still ramp slowly because the
            // rate gate naturally throttles the re-quote burst across ticks.
            g_thin_pausing.store(false);
            g_thin_paused.store(false);
            logger->warn("[thin_event] RESUME jumps {}", summary.str());
        }

        // -------- Bear regime state machine --------
        // No wire traffic in the flag-handling path; FVs and takers update
        // in-process state. Sits before the rate gate alongside the thin
        // event so the regime entry/exit is processed promptly.
        if (g_bear_start_requested.exchange(false)) {
            // Tunables are set by the command-port handler (BEAR_START
            // [drift=N] [bias=F]) before it raises the request flag.
            int32_t drift_per_min;
            double sell_bias;
            {
                std::lock_guard<std::mutex> lk(g_bear_params_mu);
                drift_per_min = g_bear_drift_per_min;
                sell_bias = g_bear_sell_bias;
            }
            const uint64_t now = ndfex::nanotime();
            for (auto* fv_ptr : fair_values) fv_ptr->start_directional_drift(drift_per_min, now);
            for (auto* taker : takers) taker->set_sell_bias(sell_bias);
            g_bear_active.store(true);
            logger->warn("[bear_regime] START drift={}/min sell_bias={}", drift_per_min, sell_bias);
        }

        if (g_bear_end_requested.exchange(false)) {
            if (g_bear_active.load()) {
                const uint64_t now = ndfex::nanotime();
                for (auto* fv_ptr : fair_values) fv_ptr->end_directional_drift(now);
                for (auto* taker : takers) taker->set_sell_bias(0.5);
                g_bear_active.store(false);
                logger->warn("[bear_regime] END (drift baked in, sell_bias reset to 0.5)");
            } else {
                logger->warn("[bear_regime] END requested but no active regime — ignored");
            }
        }
        // -------------------------------------------------

        // Global OE rate gate: tracks msgs-per-second; if exhausted we skip
        // all wire-emitting work this tick. This is the load-bearing piece
        // that keeps us below the matching engine's MAX_MSGS_PER_SECOND.
        if (!client1.can_send()) {
            continue;
        }

        // -------- PAUSING: drain cancels respecting the rate gate --------
        if (g_thin_pausing.load()) {
            size_t remaining = 0;
            for (auto& mm : market_makers) {
                if (!client1.can_send()) { remaining = 1; break; }
                remaining += mm.cancel_some_open_orders(THIN_CANCEL_BUDGET_PER_MM);
            }
            for (auto& mm : undy_market_makers) {
                if (!client1.can_send()) { remaining = 1; break; }
                remaining += mm.cancel_some_open_orders(THIN_CANCEL_BUDGET_PER_MM);
            }
            for (auto& stacker : stackers) {
                if (!client1.can_send()) { remaining = 1; break; }
                remaining += stacker.cancel_some_open_orders(THIN_CANCEL_BUDGET_PER_MM);
            }
            if (remaining == 0) {
                g_thin_pausing.store(false);
                g_thin_paused.store(true);
                logger->warn("[thin_event] PAUSE drain complete");
            }
            continue;
        }

        if (g_thin_paused.load()) {
            // Drain done — stay quiet until resume.
            continue;
        }
        // -------------------------------------------------

        // Per-MM rate-gate check: in steady-state a single tick across all
        // MMs can emit 200+ messages, which combined with normal traffic
        // crosses the matching engine's 1000/sec ceiling and tears down the
        // OE socket. Checking can_send() between MMs spreads the burst.
        for (auto& mm : market_makers) {
            if (!client1.can_send()) break;
            mm.process();
        }

        for (auto& mm : undy_market_makers) {
            if (!client1.can_send()) break;
            mm.process();
        }

        for (auto taker : takers) {
            if (!client1.can_send()) break;
            taker->process();
        }

        for (auto& stacker : stackers) {
            if (!client1.can_send()) break;
            stacker.process();
        }
    }

    return 0;
}
