#include "test_framework.H"
#include "order_ladder_wrapper.cpp"
#include "../order_entry/oe_protocol.H"
#include "../market_data/md_protocol.H"

#include <iostream>

using namespace ndfex;
using namespace test;

void test_ioc_iceberg_order() {
    TestOrderLadder test_ladder(1);
    
    // Add some liquidity to the sell side
    test_ladder.addOrder(1001, md::SIDE::SELL, 50, 100);
    
    // Reset subscriber to clear events from initial order
    test_ladder.subscriber.reset();
    
    // Add an IOC+Iceberg order
    uint8_t flags = static_cast<uint8_t>(oe::ORDER_FLAGS::IOC) | static_cast<uint8_t>(oe::ORDER_FLAGS::ICEBERG);
    uint32_t total_quantity = 100;
    uint32_t display_quantity = 20;
    test_ladder.addOrder(2001, md::SIDE::BUY, total_quantity, 100, flags, display_quantity);
    
    // Verify trades occurred
    assert_equal(1, test_ladder.subscriber.trade_count, "Trade should occur for matching quantity");
    
    // Verify fill quantity
    assert_equal(50, test_ladder.subscriber.calculateTotalFilled(), "Fill quantity should match available liquidity");
    
    // Verify the order was deleted (IOC behavior)
    assert_equal(1, test_ladder.subscriber.delete_order_count, "Order should be deleted (IOC behavior)");
    
    // Verify the order no longer exists
    assert_false(test_ladder.orderExists(2001), "Order should not exist after IOC execution");
}

void test_aon_iceberg_order() {
    TestOrderLadder test_ladder(1);
    
    // Add liquidity to the sell side (enough for AON)
    test_ladder.addOrder(1001, md::SIDE::SELL, 80, 100);
    test_ladder.addOrder(1002, md::SIDE::SELL, 40, 100);
    
    // Reset subscriber to clear events from initial orders
    test_ladder.subscriber.reset();
    
    // Add an AON+Iceberg order
    uint8_t flags = static_cast<uint8_t>(oe::ORDER_FLAGS::AON) | static_cast<uint8_t>(oe::ORDER_FLAGS::ICEBERG);
    uint32_t total_quantity = 100;
    uint32_t display_quantity = 20;
    test_ladder.addOrder(2001, md::SIDE::BUY, total_quantity, 100, flags, display_quantity);
    
    // Verify trades occurred
    assert_true(test_ladder.subscriber.trade_count > 0, "Trades should occur with sufficient liquidity");
    
    // Verify total filled quantity matches order quantity
    assert_equal(100, test_ladder.subscriber.calculateTotalFilled(), "Total filled quantity should match order quantity");
}

int main() {
    TestSuite combined_suite("Combined Order Types Tests");
    
    combined_suite.add_test("IOC+Iceberg Order", test_ioc_iceberg_order);
    combined_suite.add_test("AON+Iceberg Order", test_aon_iceberg_order);
    
    auto results = combined_suite.run_all();
    
    // Count passed/failed tests
    int passed = 0;
    for (const auto& result : results) {
        if (result.passed) passed++;
    }
    
    std::cout << "Results: " << passed << " passed, " << (results.size() - passed) << " failed" << std::endl;
    
    return (passed == results.size()) ? 0 : 1;
}