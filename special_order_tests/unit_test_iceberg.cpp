#include "test_framework.H"
#include "order_ladder_wrapper.cpp"
#include "../order_entry/oe_protocol.H"
#include "../market_data/md_protocol.H"

#include <iostream>

using namespace ndfex;
using namespace test;

void test_iceberg_order_display() {
    TestOrderLadder test_ladder(1);
    
    // Add an iceberg order
    uint32_t total_quantity = 100;
    uint32_t display_quantity = 20;
    test_ladder.addOrder(1001, md::SIDE::BUY, total_quantity, 100, 
                     static_cast<uint8_t>(oe::ORDER_FLAGS::ICEBERG), display_quantity);
    
    // Verify the order was added
    assert_equal(1, test_ladder.subscriber.new_order_count, "Order should be added to the book");
    
    // Verify only display quantity is visible
    // std::cout << "DEBUG: display qty" << test_ladder.subscriber.new_orders[0].quantity << std::endl;
    assert_true(
        !test_ladder.subscriber.new_orders.empty() && 
        test_ladder.subscriber.new_orders[0].display_quantity == display_quantity,
        "Only display quantity should be visible"
    );
    
    // Verify iceberg flag is not published
    assert_true(
        !test_ladder.subscriber.new_orders.empty() && 
        !(test_ladder.subscriber.new_orders[0].flags & static_cast<uint8_t>(oe::ORDER_FLAGS::ICEBERG)),
        "Iceberg flag should not be published"
    );
}

void test_iceberg_order_partial_fill_refresh() {
    TestOrderLadder test_ladder(1);

    // Reset subscriber to clear events from initial order
    test_ladder.subscriber.reset();
    
    // Add an iceberg order
    uint32_t total_quantity = 100;
    uint32_t display_quantity = 20;
    uint64_t order_id = 1001;
    test_ladder.addOrder(order_id, md::SIDE::BUY, total_quantity, 100, 
                     static_cast<uint8_t>(oe::ORDER_FLAGS::ICEBERG), display_quantity);
    
    // Add a sell order that will partially fill the iceberg
    test_ladder.addOrder(2001, md::SIDE::SELL, 15, 100);
    
    // Verify trade occurred
    assert_equal(1, test_ladder.subscriber.trade_count, "Trade should occur");
    
    // Verify fill quantity
    assert_equal(15, test_ladder.subscriber.calculateTotalFilled(order_id), "Fill quantity should match");
    
    // Verify iceberg was refreshed (this requires checking the order book internals)
    // For now, we'll verify the order still exists
    assert_true(test_ladder.orderExists(1001), "Order should still exist after partial fill");
}

void test_iceberg_order_complete_execution() {
    TestOrderLadder test_ladder(1);
    
    // Reset subscriber to clear events from initial order
    test_ladder.subscriber.reset();
    
    // Add an iceberg order
    uint32_t total_quantity = 100;
    uint32_t display_quantity = 20;
    uint64_t order_id = 1001;
    test_ladder.addOrder(order_id, md::SIDE::BUY, total_quantity, 100, 
                     static_cast<uint8_t>(oe::ORDER_FLAGS::ICEBERG), display_quantity);
    
    // Add multiple sell orders to completely fill the iceberg
    test_ladder.addOrder(2001, md::SIDE::SELL, 40, 100);
    assert_equal(40, test_ladder.subscriber.calculateTotalFilled(order_id), "Total filled should match total quantity");
    test_ladder.addOrder(2002, md::SIDE::SELL, 60, 100);
    
    // Verify multiple trades occurred
    assert_true(test_ladder.subscriber.trade_count > 1, "Multiple trades should occur");
    
    // Verify total filled quantity matches total order quantity
    assert_equal(60, test_ladder.subscriber.calculateTotalFilled(order_id), "Total filled should match total quantity");
    
    // After complete execution, the order should no longer exist
    assert_false(test_ladder.orderExists(1001), "Order should be removed after complete execution");
}

void test_iceberg_order_priority_loss() {
    TestOrderLadder test_ladder(1);
    
    // Reset subscriber to clear events from initial order
    test_ladder.subscriber.reset();
    
    // Add an iceberg order 
    uint32_t total_quantity = 100;
    uint32_t display_quantity = 20;
    uint64_t iceberg_order_id = 1001;
    test_ladder.addOrder(iceberg_order_id, md::SIDE::BUY, total_quantity, 100, 
                     static_cast<uint8_t>(oe::ORDER_FLAGS::ICEBERG), display_quantity);
    
    // Add another buy order at the same price level (lower time priority)
    uint64_t regular_order_id = 1002;
    test_ladder.addOrder(regular_order_id, md::SIDE::BUY, 30, 100);
    
    // Verify both orders exist and initial priority is correct
    assert_true(test_ladder.orderExists(iceberg_order_id), "Iceberg order should exist");
    assert_true(test_ladder.orderExists(regular_order_id), "Regular order should exist");
    
    // Partially fill the iceberg order to trigger a refresh
    test_ladder.addOrder(2001, md::SIDE::SELL, 20, 100);
    
    // Verify the iceberg was partially filled
    assert_equal(20, test_ladder.subscriber.calculateTotalFilled(iceberg_order_id), 
                "Iceberg order should be fully filled");
    
    // Reset subscriber to clear events for clean test
    test_ladder.subscriber.reset();
    
    // Add a sell order that should match with both buy orders
    // If priority is correct, the regular order should match first (since iceberg lost priority),
    // then the refreshed iceberg
    test_ladder.addOrder(2002, md::SIDE::SELL, 35, 100);
    
    // Verify trades occurred in correct order (by time of fills)
    bool correct_order = false;
    //assert_false(test_ladder.orderExists(1001), "Order should be removed after complete execution");
    
    // Check if there are at least two fills and the first one is for the regular order
    if (test_ladder.subscriber.fills.size() >= 2) {
        uint64_t first_filled_order = test_ladder.subscriber.fills[0].order_id;
        uint64_t second_filled_order = test_ladder.subscriber.fills[2].order_id; // Index 2 because each fill generates 2 entries
        correct_order = (first_filled_order == regular_order_id && 
                        second_filled_order == iceberg_order_id);
    }
    
    assert_true(correct_order, "Regular order should be matched before refreshed iceberg order");
    
    // Verify fill quantities
    assert_equal(30, test_ladder.subscriber.calculateTotalFilled(regular_order_id), 
                "Regular order should be fully filled");
    assert_equal(5, test_ladder.subscriber.calculateTotalFilled(iceberg_order_id), 
                "Only 5 additional units of iceberg should be filled");
    
    // Verify regular order is gone (fully executed) but iceberg remains
    assert_false(test_ladder.orderExists(regular_order_id), "Regular order should be removed");
    assert_true(test_ladder.orderExists(iceberg_order_id), "Iceberg order should still exist");
}

int main() {
    TestSuite iceberg_suite("Iceberg Orders Tests");
    
    iceberg_suite.add_test("Iceberg Order Display Quantity", test_iceberg_order_display);
    iceberg_suite.add_test("Iceberg Order Partial Fill and Refresh", test_iceberg_order_partial_fill_refresh);
    iceberg_suite.add_test("Iceberg Order Complete Execution", test_iceberg_order_complete_execution);
    iceberg_suite.add_test("Iceberg Order Priority Loss on Refresh", test_iceberg_order_priority_loss);
    
    auto results = iceberg_suite.run_all();
    
    // Count passed/failed tests
    int passed = 0;
    for (const auto& result : results) {
        if (result.passed) passed++;
    }
    
    std::cout << "Results: " << passed << " passed, " << (results.size() - passed) << " failed" << std::endl;
    
    return (passed == results.size()) ? 0 : 1;
}