/**
 * Run the bots that simulate exchange participants.
 */
#include "oe_client.H"
#include "md_client.H"
#include "matching_engine/symbol_definition.H"
#include "fair_value.H"
#include "fair_value_mm.H"

#include <spdlog/spdlog.h>
#include <iostream>

int main(int argc, char* argv[]) {

    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <ip address> <port> <mcast ip address> <mcast bind ip>" << std::endl;
        return 1;
    }

    std::string ip = argv[1];
    uint16_t port = std::stoi(argv[2]);

    std::string mcast_ip = argv[3];
    std::string mcast_bind_ip = argv[4];

    auto logger = spdlog::default_logger();

    // create market data client
    ndfex::bots::MDClient md_client(mcast_ip, 12345, mcast_bind_ip, logger);
    md_client.wait_for_hearbeat();

    // create bots
    ndfex::bots::user_info user1 = {"good", "password", 1};
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
        new ndfex::bots::FairValue(120, symbols[0]),
        new ndfex::bots::FairValue(50, symbols[1]),
    };

    ndfex::bots::FairValueMarketMaker mm(client1, md_client, fair_values, symbols, logger);

    // process messages from the server
    while (true) {
        client1.process();
        md_client.process();

        mm.process();
    }

    return 0;
}