/**
 * Test bot that deliberately breaches competition rules:
 *   1. Builds position > 10 in GOLD (position limit breach)
 *   2. Loses enough money to drop below -5000 PnL (min PnL breach)
 *
 * Usage: rule_breaker <ip> <port> <mcast_ip> <snapshot_ip> <bind_ip>
 */
#include "oe_client.H"
#include "md_client.H"

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/daily_file_sink.h>

#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char* argv[]) {
    if (argc != 6) {
        std::cerr << "Usage: " << argv[0]
                  << " <ip> <port> <mcast_ip> <snapshot_ip> <bind_ip>"
                  << std::endl;
        return 1;
    }

    std::string ip          = argv[1];
    uint16_t    port        = std::stoi(argv[2]);
    std::string mcast_ip    = argv[3];
    std::string snapshot_ip = argv[4];
    std::string bind_ip     = argv[5];

    auto logger = spdlog::daily_logger_mt<spdlog::async_factory>(
        "rule_breaker_logger", "logs/rule_breaker");

    // Login as the test "RuleBreaker" account (client_id 95)
    ndfex::bots::user_info user = {"rulebreaker", "testuser", 95};
    ndfex::bots::OEClient client(user, ip, port, logger, 100);

    if (!client.login()) {
        std::cerr << "Failed to login as rulebreaker" << std::endl;
        return 1;
    }
    std::cout << "Logged in as RuleBreaker (client 95)" << std::endl;

    // Market data client so we can read best bid/ask
    ndfex::bots::MDClient md(mcast_ip, 12345, snapshot_ip, 12345, bind_ip,
                             logger, true);
    md.wait_for_snapshot();
    std::cout << "Got market data snapshot" << std::endl;

    uint64_t order_id = 1;
    constexpr uint32_t GOLD = 1;
    constexpr uint32_t BLUE = 2;

    // ---------- Phase 1: Position limit breach on GOLD ----------
    // Buy 15 lots of GOLD via IOC orders to exceed the 10-lot limit.
    std::cout << "\n=== Phase 1: Breaching position limit on GOLD ===" << std::endl;
    for (int i = 0; i < 15; i++) {
        // Drain messages
        for (int j = 0; j < 50; j++) { client.process(); md.process(); }

        auto ask = md.get_best_ask(GOLD);
        if (ask.quantity == 0) {
            std::cout << "  No ask on GOLD, waiting..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            i--;
            continue;
        }

        std::cout << "  Buying 1 GOLD at " << ask.price
                  << " (pos will be " << (client.get_position(GOLD) + 1) << ")"
                  << std::endl;
        client.send_order(GOLD, order_id++, ndfex::md::SIDE::BUY, 1,
                          ask.price,
                          static_cast<uint8_t>(ndfex::oe::ORDER_FLAGS::IOC));

        // Let the fill come back
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (int j = 0; j < 100; j++) { client.process(); md.process(); }
    }

    std::cout << "  Final GOLD position: " << client.get_position(GOLD)
              << " (limit is 10)" << std::endl;

    // ---------- Phase 2: PnL breach ----------
    // Repeatedly cross the spread on BLUE to burn money.
    // IMPORTANT: Keep position within the limit (<=10) so PnL isn't clamped.
    // Buy 1 at ask, wait for fill, sell 1 at bid, wait for fill. Each round
    // trip loses the spread. Position stays near 0.
    std::cout << "\n=== Phase 2: Burning PnL on BLUE ===" << std::endl;
    std::cout << "  Round-tripping BLUE to lose money (keeping pos <= 10)..." << std::endl;

    for (int round = 0; round < 2000; round++) {
        // Drain messages
        for (int j = 0; j < 20; j++) { client.process(); md.process(); }

        // Safety: if BLUE position is getting too high, sell down first
        int32_t blue_pos = client.get_position(BLUE);
        if (blue_pos > 5) {
            auto bid = md.get_best_bid(BLUE);
            if (bid.quantity > 0) {
                client.send_order(BLUE, order_id++, ndfex::md::SIDE::SELL, blue_pos,
                                  bid.price,
                                  static_cast<uint8_t>(ndfex::oe::ORDER_FLAGS::IOC));
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                for (int j = 0; j < 50; j++) { client.process(); md.process(); }
            }
            continue;
        }
        if (blue_pos < -5) {
            auto ask = md.get_best_ask(BLUE);
            if (ask.quantity > 0) {
                client.send_order(BLUE, order_id++, ndfex::md::SIDE::BUY, -blue_pos,
                                  ask.price,
                                  static_cast<uint8_t>(ndfex::oe::ORDER_FLAGS::IOC));
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                for (int j = 0; j < 50; j++) { client.process(); md.process(); }
            }
            continue;
        }

        auto ask = md.get_best_ask(BLUE);
        auto bid = md.get_best_bid(BLUE);
        if (ask.quantity == 0 || bid.quantity == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            round--;
            continue;
        }

        int32_t spread = ask.price - bid.price;
        if (spread <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            round--;
            continue;
        }

        // Buy 1 at the ask
        client.send_order(BLUE, order_id++, ndfex::md::SIDE::BUY, 1,
                          ask.price,
                          static_cast<uint8_t>(ndfex::oe::ORDER_FLAGS::IOC));

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        for (int j = 0; j < 50; j++) { client.process(); md.process(); }

        // Sell 1 at the bid
        client.send_order(BLUE, order_id++, ndfex::md::SIDE::SELL, 1,
                          bid.price,
                          static_cast<uint8_t>(ndfex::oe::ORDER_FLAGS::IOC));

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        for (int j = 0; j < 50; j++) { client.process(); md.process(); }

        if (round % 100 == 0) {
            std::cout << "  Round " << round
                      << " | BLUE pos: " << client.get_position(BLUE)
                      << " | spread: " << spread
                      << " | loss/trip ~" << spread
                      << std::endl;
        }
    }

    std::cout << "\n=== Done! Check the scoreboard for breach indicators ===" << std::endl;
    std::cout << "  - GOLD should show orange POS breach" << std::endl;
    std::cout << "  - If PnL dropped below -5000, PNL breach should appear" << std::endl;
    std::cout << "  Keeping connection alive so position stays on scoreboard..." << std::endl;

    // Keep alive so the position stays visible
    while (true) {
        client.process();
        md.process();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}
