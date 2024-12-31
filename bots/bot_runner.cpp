/**
 * Run the bots that simulate exchange participants.
 */
#include "oe_client.H"
#include "md_client.H"

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

    // send some orders
    client1.send_order(1, 1, ndfex::md::SIDE::BUY, 100, 1000, 0);
    //client1.send_order(1, 2, ndfex::md::SIDE::SELL, 100, 1000, 0);
    client1.send_order(1, 3, ndfex::md::SIDE::BUY, 100, 1000, 0);
    client1.send_order(1, 4, ndfex::md::SIDE::BUY, 100, 800, 0);
    client1.send_order(1, 5, ndfex::md::SIDE::SELL, 100, 1200, 0);

    // cancel an order
    client1.cancel_order(1);

    // process messages from the server
    while (true) {
        client1.process();
        md_client.process();
    }

    return 0;
}