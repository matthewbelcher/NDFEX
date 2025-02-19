/**
 * Run bots which make a constant wide market and random slow takers.
 */
#include "oe_client.H"
#include "md_client.H"
#include "matching_engine/symbol_definition.H"
#include "fair_value.H"
#include "fair_value_mm.H"
#include "constant_fair_value.H"
#include "random_taker.H"

#include <spdlog/spdlog.h>
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

    auto logger = spdlog::default_logger();

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
        new ndfex::bots::ConstantFairValue(1200),
        new ndfex::bots::ConstantFairValue(400),
    };

    // create market data client
    ndfex::bots::MDClient md_client(mcast_ip, 12345, snapshot_ip, 12345, mcast_bind_ip, logger);
    md_client.wait_for_snapshot();

    uint32_t last_order_id = 1;
    std::vector<ndfex::bots::FairValueMarketMaker> market_makers;
    std::vector<int32_t> variances = {0, 0};
    market_makers.emplace_back(client1, md_client, fair_values, variances,
        symbols, 10, 400, last_order_id, logger);

    std::vector<ndfex::bots::RandomTaker*> takers;
    takers.push_back(new ndfex::bots::RandomTaker(client1, md_client, symbols, 1, last_order_id, 10, logger));

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
    }

    return 0;
}