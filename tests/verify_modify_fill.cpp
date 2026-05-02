// One-off verification of the order_ladder.H:281 fix:
// "same-price modify after partial fill must not let an aggressor pull more
//  than the displayed remaining qty (i.e. no phantom liquidity)."
//
// Replays the exact incident shape from 2026-04-28 02:15:14 UNDY:
//   A places SELL qty=10 at PRICE
//   B sends BUY qty=3 at PRICE   → partial fill, A has 7 remaining
//   A sends MODIFY same-price qty=10 (the buggy modify)
//   B sends BUY qty=10 at PRICE
//
// Without the fix, B receives 10 fills against A and A is over-sold to -13.
// With the fix, B receives only 7 fills (the displayed remaining), 3 lots
// of B's BUY rest as a new bid, and A ends at exactly -10.
//
// Uses team1 (cid=1) and rulebreaker (cid=95) — both inactive accounts that
// won't collide with the running LP/smarter_bots. Picks a price 9500 on UNDY,
// far above the LP's quote band so no other participant interferes.

#include "bots/oe_client.H"
#include "matching_engine/symbol_definition.H"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <iostream>
#include <thread>

namespace {

void drain(ndfex::bots::OEClient& a, ndfex::bots::OEClient& b, int ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        a.process();
        b.process();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

}

int main() {
    auto logger = spdlog::stdout_color_mt("verify");
    logger->set_level(spdlog::level::warn);  // quiet OEClient info logs

    const std::string ip = "192.168.13.100";
    const uint16_t port = 1234;
    const uint32_t SYM = 13;     // UNDY
    const int32_t PRICE = 5500;  // gap between LP bids (≤5450) and asks (≥5550)
    const uint64_t SELL_OID = 8888001;
    const uint64_t BUY1_OID = 8888002;
    const uint64_t BUY2_OID = 8888003;

    ndfex::bots::user_info userA = {"team1", "1RD1ijyY", 1};
    ndfex::bots::user_info userB = {"rulebreaker", "testuser", 95};

    ndfex::bots::OEClient A(userA, ip, port, logger, /*max_msgs_per_sec=*/100);
    ndfex::bots::OEClient B(userB, ip, port, logger, /*max_msgs_per_sec=*/100);

    if (!A.login()) { std::cerr << "FAIL: A login\n"; return 1; }
    if (!B.login()) { std::cerr << "FAIL: B login\n"; return 1; }
    drain(A, B, 200);

    int32_t a_baseline = A.get_position(SYM);
    int32_t b_baseline = B.get_position(SYM);
    std::cout << "baseline:  A=" << a_baseline << " B=" << b_baseline << "\n";

    // 1. A places SELL qty=10 at PRICE
    std::cout << "[step 1] A SELL qty=10 @ " << PRICE << "\n";
    A.send_order(SYM, SELL_OID, ndfex::md::SIDE::SELL, 10, PRICE, 0);
    drain(A, B, 300);

    // 2. B partial-fills 3 lots
    std::cout << "[step 2] B BUY qty=3 @ " << PRICE << "  (partial fill)\n";
    B.send_order(SYM, BUY1_OID, ndfex::md::SIDE::BUY, 3, PRICE, 0);
    drain(A, B, 300);
    int32_t a_partial = A.get_position(SYM) - a_baseline;
    int32_t b_partial = B.get_position(SYM) - b_baseline;
    std::cout << "   after partial: A delta=" << a_partial << " B delta=" << b_partial << "\n";
    if (a_partial != -3 || b_partial != 3) {
        std::cout << "   (expected -3/+3 — partial-fill setup didn't go through cleanly; aborting)\n";
        return 2;
    }

    // 3. A modifies its SELL same-price back to qty=10 (the trigger)
    std::cout << "[step 3] A MODIFY SELL qty=10 @ " << PRICE << "  (same-price reset)\n";
    A.modify_order(SELL_OID, ndfex::md::SIDE::SELL, 10, PRICE);
    drain(A, B, 300);

    // 4. B BUY qty=10. With the fix, only 7 should fill against A.
    std::cout << "[step 4] B BUY qty=10 @ " << PRICE << "\n";
    B.send_order(SYM, BUY2_OID, ndfex::md::SIDE::BUY, 10, PRICE, 0);
    drain(A, B, 500);

    int32_t a_total = A.get_position(SYM) - a_baseline;
    int32_t b_total = B.get_position(SYM) - b_baseline;
    std::cout << "final:     A delta=" << a_total << " B delta=" << b_total << "\n";

    // Cleanup: cancel B's resting BUY (3 lots if fix worked, 0 if it didn't)
    B.cancel_order(BUY2_OID);
    drain(A, B, 200);

    std::cout << "\n--- verdict ---\n";
    if (a_total == -10 && b_total == 10) {
        std::cout << "PASS  fix is live: A=-10 (sold exactly the qty placed), "
                     "B=+10 (received 3 + 7), 3 lots of B's BUY rested briefly\n";
        return 0;
    }
    if (a_total == -13 && b_total == 13) {
        std::cout << "FAIL  bug is present: A=-13 (oversold by 3), "
                     "B=+13 (took the phantom liquidity)\n";
        return 1;
    }
    std::cout << "INCONCLUSIVE  unexpected positions A=" << a_total << " B=" << b_total
              << " — likely interference from another taker\n";
    return 2;
}
