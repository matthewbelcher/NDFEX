/**
 * Run the bots that simulate exchange participants.
 */
#include "oe_client.H"
#include "md_client.H"
#include "matching_engine/symbol_definition.H"
#include "fair_value.H"
#include "random_walk_fair_value.H"
#include "basket_fair_value.H"
#include "fair_value_mm.H"
#include "fair_value_stack_mm.H"
#include "random_taker.H"

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/daily_file_sink.h>

#include <iostream>

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
    ndfex::bots::OEClient client1(user1, ip, port, logger);

    if (!client1.login()) {
        std::cerr << "Failed to login" << std::endl;
        return 1;
    }

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
        new ndfex::bots::RandomWalkFairValue(1200, symbols[0]),  // GOLD
        new ndfex::bots::RandomWalkFairValue(900, symbols[1]),   // BLUE
        // Dorm underlyings - prices around 500-700
        new ndfex::bots::RandomWalkFairValue(550, symbols[2]),   // KNAN
        new ndfex::bots::RandomWalkFairValue(520, symbols[3]),   // STED
        new ndfex::bots::RandomWalkFairValue(580, symbols[4]),   // FISH
        new ndfex::bots::RandomWalkFairValue(510, symbols[5]),   // DILN
        new ndfex::bots::RandomWalkFairValue(600, symbols[6]),   // SORN
        new ndfex::bots::RandomWalkFairValue(530, symbols[7]),   // RYAN
        new ndfex::bots::RandomWalkFairValue(540, symbols[8]),   // LYON
        new ndfex::bots::RandomWalkFairValue(560, symbols[9]),   // WLSH
        new ndfex::bots::RandomWalkFairValue(570, symbols[10]),  // LEWI
        new ndfex::bots::RandomWalkFairValue(590, symbols[11]),  // BDIN
        // UNDY ETF - computed from component market mid-prices
        new ndfex::bots::BasketFairValue(md_client, undy_components,
                                         /*initial_fv=*/5550, logger),
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
    for (size_t i = 0; i < variances.size(); i++) {
        market_makers.emplace_back(client1, md_client, fair_values, variances[i],
            symbols, i < 3 ? 2 : 3, 100 - 10*i, last_order_id, logger);
    }

    std::vector<ndfex::bots::FairValueStackingMarketMaker> stackers;
    stackers.reserve(symbols.size());
    for (const auto& symbol : symbols) {
        stackers.emplace_back(client1, md_client, symbol, 1, 2, last_order_id, logger);
    }

    std::vector<ndfex::bots::RandomTaker*> takers;
    for (size_t i = 0; i < 4; i++) {
        takers.push_back(new ndfex::bots::RandomTaker(client1, md_client, symbols, i+1, last_order_id, 6+i, logger));
    }

    // process messages from the server
    while (true) {
        client1.process();
        md_client.process();

        // Global OE rate gate: if this bot has already written
        // max_msgs_per_sec messages to the wire in the trailing 1-second
        // window, skip all order-emitting work this tick. We still read
        // fills/acks and MD updates, so the bot stays in sync; we just
        // stop generating new traffic until the sliding window opens up.
        // This keeps us below the matching engine's MAX_MSGS_PER_SECOND
        // limit and prevents the OE TCP socket from being torn down.
        if (!client1.can_send()) {
            continue;
        }

        for (auto& mm : market_makers) {
            mm.process();
        }

        for (auto taker : takers) {
            taker->process();
        }

        for (auto& stacker : stackers) {
            stacker.process();
        }
    }

    return 0;
}