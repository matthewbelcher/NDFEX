/**
 * Run the bots that simulate exchange participants.
 */
#include "oe_client.H"

#include <spdlog/spdlog.h>
#include <iostream>

int main(int argc, char* argv[]) {

    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <ip address> <port>" << std::endl;
        return 1;
    }

    std::string ip = argv[1];
    uint16_t port = std::stoi(argv[2]);

    // create bots
    ndfex::bots::user_info user1 = {"good", "password", 1};

    auto logger = spdlog::default_logger();
    ndfex::bots::OEClient client1(user1, ip, port, logger);

    if (!client1.login()) {
        std::cerr << "Failed to login" << std::endl;
        return 1;
    }

    // send some orders
    client1.send_order(1, 1, ndfex::md::SIDE::BUY, 100, 1000, 0);
    client1.send_order(1, 2, ndfex::md::SIDE::SELL, 100, 1000, 0);

    // cancel an order
    client1.cancel_order(1);

    // process messages from the server
    while (true) {
        client1.process();
    }


    return 0;
}