/**
 * Run the bots that simulate exchange participants.
 */
#include "oe_client.H"
#include "md_client.H"
#include "matching_engine/symbol_definition.H"
#include "fair_value.H"
#include "random_walk_fair_value.H"
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
        {1, 10, 1, 1000, 10000000, 0}, // GOLD
        {2, 5, 1, 1000, 10000000, 0}, // BLUE
    };

    std::vector<ndfex::bots::FairValue*> fair_values{
        new ndfex::bots::RandomWalkFairValue(1200, symbols[0]),
        new ndfex::bots::RandomWalkFairValue(900, symbols[1]),
    };

    // create market data client
    ndfex::bots::MDClient md_client(mcast_ip, 12345, snapshot_ip, 12345, mcast_bind_ip, logger);
    md_client.wait_for_snapshot();

    std::vector<std::vector<int32_t>> variances = { {0, 0}, {1, 1}, {-1, -1}, {1, 0}, {2, 0}, {0, 2}, {3, 0} };

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