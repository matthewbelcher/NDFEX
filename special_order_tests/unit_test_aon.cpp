#include "test_framework.H"
#include "order_ladder_wrapper.cpp"
#include "../order_entry/oe_protocol.H"
#include "../market_data/md_protocol.H"

#include <iostream>

using namespace ndfex;
using namespace test;

void test_aon_order_insufficient_liquidity() {
    TestOrderLadder test_ladder(1);
    
    // Add some liquidity to the sell side (not enough for AON)
    test_ladder.addOrder(1001, md::SIDE::SELL, 5, 100);
    test_ladder.addOrder(1002, md::SIDE::SELL, 3, 101);
    
    // Reset subscriber to clear events from initial orders
    test_ladder.subscriber.reset();

    // Add an AON order that requires more liquidity
    test_ladder.addOrder(2001, md::SIDE::BUY, 10, 101, static_cast<uint8_t>(oe::ORDER_FLAGS::AON));
    
    // Verify no trades occurred
    assert_equal(0, test_ladder.subscriber.trade_count, "No trades should occur with insufficient liquidity");
    
    // Verify the order was added to the book
    assert_equal(1, test_ladder.subscriber.new_order_count, "Order should be added to the book");
    
    // Verify the order still exists
    assert_true(test_ladder.orderExists(2001), "Order should exist in the book");
}

void test_aon_order_sufficient_liquidity() {
    TestOrderLadder test_ladder(1);
    
    // Add enough liquidity to the sell side
    test_ladder.addOrder(1001, md::SIDE::SELL, 7, 100);
    test_ladder.addOrder(1002, md::SIDE::SELL, 5, 101);

    // Reset subscriber to clear events from initial orders
    test_ladder.subscriber.reset();
    
    // Add an AON order with sufficient liquidity
    uint64_t order_id = 2001;
    test_ladder.addOrder(order_id, md::SIDE::BUY, 10, 101, static_cast<uint8_t>(oe::ORDER_FLAGS::AON));
    
    // Verify trades occurred
    assert_true(test_ladder.subscriber.trade_count > 0, "Trades should occur with sufficient liquidity");
    
    // Verify total filled quantity matches order quantity
    assert_equal(10, test_ladder.subscriber.calculateTotalFilled(order_id), "Total filled quantity should match order quantity");
}

void test_aon_ioc_order_insufficient_liquidity() {
    TestOrderLadder test_ladder(1);
    
    // Add some liquidity to the sell side (not enough for AON)
    test_ladder.addOrder(1001, md::SIDE::SELL, 5, 100);
    
    // Reset subscriber to clear events from initial orders
    test_ladder.subscriber.reset();
    
    // Add an AON+IOC order with insufficient liquidity
    uint8_t flags = static_cast<uint8_t>(oe::ORDER_FLAGS::AON) | static_cast<uint8_t>(oe::ORDER_FLAGS::IOC);
    test_ladder.addOrder(2001, md::SIDE::BUY, 10, 100, flags);
    
    // Verify no trades occurred
    assert_equal(0, test_ladder.subscriber.trade_count, "No trades should occur with insufficient liquidity");
    
    // Verify the order was deleted (IOC behavior)
    assert_equal(1, test_ladder.subscriber.delete_order_count, "Order should be deleted (IOC behavior)");
    
    // Verify the deleted order ID
    assert_true(
        !test_ladder.subscriber.deleted_orders.empty() && 
        test_ladder.subscriber.deleted_orders[0].order_id == 2001,
        "Deleted order should match our order ID"
    );
}

int main() {
    TestSuite aon_suite("All-or-None Orders Tests");
    
    aon_suite.add_test("AON Order with Insufficient Liquidity", test_aon_order_insufficient_liquidity);
    aon_suite.add_test("AON Order with Sufficient Liquidity", test_aon_order_sufficient_liquidity);
    aon_suite.add_test("AON+IOC Order with Insufficient Liquidity", test_aon_ioc_order_insufficient_liquidity);
    
    auto results = aon_suite.run_all();
    
    // Count passed/failed tests
    int passed = 0;
    for (const auto& result : results) {
        if (result.passed) passed++;
    }
    
    std::cout << "Results: " << passed << " passed, " << (results.size() - passed) << " failed" << std::endl;
    
    return (passed == results.size()) ? 0 : 1;
}