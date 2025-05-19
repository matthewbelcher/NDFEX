#include "oe_client.H"
#include "md_client.H"
#include "matching_engine/symbol_definition.H"
#include <market_data/md_protocol.H>

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
    ndfex::bots::user_info user1 = {"test2", "testuser", 98};
    ndfex::bots::OEClient client1(user1, ip, port, logger);

    if (!client1.login()) {
        std::cerr << "Failed to login" << std::endl;
        return 1;
    }

    // create market data client
    ndfex::bots::MDClient md_client(mcast_ip, 12345, snapshot_ip, 12345, mcast_bind_ip, logger);
    md_client.wait_for_snapshot();

    std::vector<symbol_definition> symbols = {
        {1, 10, 1, 1000, 10000000, 0}, // GOLD
        {2, 5, 1, 1000, 10000000, 0}, // BLUE
    };

    for (int i = 0; i < 5000; ++i) {
        client1.send_order(1, i + 1, ndfex::md::SIDE::BUY, 10, 10, 0);
    }

    while (true) {
        md_client.process();
        client1.process();
    }

    logger->info("Exiting");
}