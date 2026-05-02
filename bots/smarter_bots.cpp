/**
 * Run the bots that simulate exchange participants.
 */
#include "oe_client.H"
#include "md_client.H"
#include "matching_engine/symbol_definition.H"
#include "fair_value.H"
#include "random_walk_fair_value.H"
#include "fair_value_stack_mm.H"
#include "imbalance_taker.H"
#include "pressure_taker.H"
#include "resting_orders.H"

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

    auto logger = spdlog::daily_logger_mt<spdlog::async_factory>("async_logger", "logs/smarter_bots");

    // create bots
    ndfex::bots::user_info user1 = {"test2", "testuser", 98};
    ndfex::bots::OEClient client1(user1, ip, port, logger);

    if (!client1.login()) {
        std::cerr << "Failed to login" << std::endl;
        return 1;
    }

    std::vector<symbol_definition> symbols = {
        {1, 10, 1, 1000, 10000000, 0},  // GOLD
        {2, 5, 1, 1000, 10000000, 0},   // BLUE
        {3, 5, 1, 1000, 10000000, 0},   // KNAN
        {4, 5, 1, 1000, 10000000, 0},   // STED
        {5, 5, 1, 1000, 10000000, 0},   // FISH
        {6, 5, 1, 1000, 10000000, 0},   // DILN
        {7, 5, 1, 1000, 10000000, 0},   // SORN
        {8, 5, 1, 1000, 10000000, 0},   // RYAN
        {9, 5, 1, 1000, 10000000, 0},   // LYON
        {10, 5, 1, 1000, 10000000, 0},  // WLSH
        {11, 5, 1, 1000, 10000000, 0},  // LEWI
        {12, 5, 1, 1000, 10000000, 0},  // BDIN
        {13, 10, 1, 1000, 10000000, 0}, // UNDY
    };

    // create market data client
    ndfex::bots::MDClient md_client(mcast_ip, 12345, snapshot_ip, 12345, mcast_bind_ip, logger);
    md_client.wait_for_snapshot();

    uint32_t last_order_id = 1;

    ndfex::bots::ImbalanceTaker imbalance_taker(client1, md_client, symbols, 40,
        {200, 400, 300, 300, 300, 300, 300, 300, 300, 300, 300, 300, 250}, last_order_id, logger);

    ndfex::bots::PressureTaker pressure_taker(client1, md_client, symbols, 5, last_order_id, logger);

    ndfex::bots::RestingOrders resting_orders(client1, md_client, symbols, 25, last_order_id, logger);

    // process messages from the server
    while (true) {
        client1.process();
        md_client.process();

        if (!client1.can_send()) {
            continue;
        }

        imbalance_taker.process();
        pressure_taker.process();
        resting_orders.process();
    }

    return 0;
}