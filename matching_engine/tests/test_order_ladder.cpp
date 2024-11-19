#include <gtest/gtest.h>
#include "order_ladder.H"

#include <iostream>

class MockSubscriber {
public:
    std::vector<uint64_t> new_orders;
    std::vector<uint64_t> deleted_orders;
    std::vector<std::tuple<uint64_t, uint32_t, uint32_t>> trades;

    void onNewOrder(uint64_t order_id, uint8_t side, uint32_t quantity, uint32_t price, uint8_t flags) {
        new_orders.push_back(order_id);
    }

    void onDeleteOrder(uint64_t order_id) {
        deleted_orders.push_back(order_id);
    }

    void onTrade(uint64_t order_id, uint32_t quantity, uint32_t price) {
        trades.emplace_back(order_id, quantity, price);
    }
};

TEST(OrderLadderTest, NewOrder) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber);

    orderLadder.new_order(1, ndfex::Side::Sell, 10, 50, 0);

    ASSERT_EQ(subscriber.new_orders.size(), 1);
    EXPECT_EQ(subscriber.new_orders[0], 1);
}

TEST(OrderLadderTest, DeleteOrder) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber);

    orderLadder.new_order(1, ndfex::Side::Sell, 10, 50, 0);
    orderLadder.delete_order(1);

    ASSERT_EQ(subscriber.deleted_orders.size(), 1);
    EXPECT_EQ(subscriber.deleted_orders[0], 1);
}

TEST(OrderLadderTest, SimpleCrossTrade) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber);

    orderLadder.new_order(1, ndfex::Side::Buy, 10, 50, 0); // Buy order
    orderLadder.new_order(2, ndfex::Side::Sell, 10, 50, 0); // Sell order

    ASSERT_EQ(subscriber.trades.size(), 1);
    EXPECT_EQ(std::get<0>(subscriber.trades[0]), 1);
    EXPECT_EQ(std::get<1>(subscriber.trades[0]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[0]), 50);
}

TEST(OrderLadderTest, CrossMultipleLevels) {
    MockSubscriber subscriber;

    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber);

    orderLadder.new_order(1, ndfex::Side::Buy, 10, 50, 0); // Buy order at 50
    orderLadder.new_order(2, ndfex::Side::Buy, 10, 55, 0); // Buy order at 55
    orderLadder.new_order(3, ndfex::Side::Buy, 10, 60, 0); // Buy order at 60

    orderLadder.new_order(4, ndfex::Side::Sell, 25, 50, 0); // Sell order at 50

    ASSERT_EQ(subscriber.trades.size(), 3);
    EXPECT_EQ(std::get<0>(subscriber.trades[0]), 3);
    EXPECT_EQ(std::get<1>(subscriber.trades[0]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[0]), 60);

    EXPECT_EQ(std::get<0>(subscriber.trades[1]), 2);
    EXPECT_EQ(std::get<1>(subscriber.trades[1]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[1]), 55);

    EXPECT_EQ(std::get<0>(subscriber.trades[2]), 1);
    EXPECT_EQ(std::get<1>(subscriber.trades[2]), 5);
    EXPECT_EQ(std::get<2>(subscriber.trades[2]), 50);
}

TEST(OrderLadderTest, VeryComplicatedTest) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber);

    // Add 10 buy orders
    for (uint64_t i = 1; i <= 10; ++i) {
        orderLadder.new_order(i, ndfex::Side::Buy, 10, 50 + i, 0);
    }

    // Add 10 sell orders
    for (uint64_t i = 11; i <= 20; ++i) {
        orderLadder.new_order(i, ndfex::Side::Sell, 10, 70 - (i - 10), 0);
    }

    // Delete some orders
    orderLadder.delete_order(5);
    orderLadder.delete_order(15);

    // Add more orders to create matches
    orderLadder.new_order(21, ndfex::Side::Sell, 15, 55, 0); // Should match with buy orders at 55 and 56
    orderLadder.new_order(22, ndfex::Side::Buy, 20, 65, 0); // Should match with sell orders at 65 and 64

    // Add more orders
    for (uint64_t i = 23; i <= 30; ++i) {
        orderLadder.new_order(i, ndfex::Side::Buy, 10, 50 + i, 0);
    }

    for (uint64_t i = 31; i <= 40; ++i) {
        orderLadder.new_order(i, ndfex::Side::Sell, 10, 70 - (i - 30), 0);
    }

    // Delete some more orders
    orderLadder.delete_order(3);

    // Check that delete_order throws an exception for orders that matched and were not added to the book
    EXPECT_THROW(orderLadder.delete_order(13), std::runtime_error);
    EXPECT_THROW(orderLadder.delete_order(25), std::runtime_error);

    // Add more orders to create more matches
    orderLadder.new_order(41, ndfex::Side::Sell, 30, 60, 0); // Should match with buy orders at 60, 59, and 58
    orderLadder.new_order(42, ndfex::Side::Buy, 25, 70, 0); // Should match with sell orders at 70, 69, and 68

    // Verify the trades
    ASSERT_EQ(subscriber.trades.size(), 15);

    EXPECT_EQ(std::get<0>(subscriber.trades[0]), 10); // Buy order at 60
    EXPECT_EQ(std::get<1>(subscriber.trades[0]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[0]), 60);

    EXPECT_EQ(std::get<0>(subscriber.trades[1]), 9); // Buy order at 59
    EXPECT_EQ(std::get<1>(subscriber.trades[1]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[1]), 59);

    EXPECT_EQ(std::get<0>(subscriber.trades[2]), 8); // Buy order at 58
    EXPECT_EQ(std::get<1>(subscriber.trades[2]), 5);
    EXPECT_EQ(std::get<2>(subscriber.trades[2]), 58);

    EXPECT_EQ(std::get<0>(subscriber.trades[3]), 19); // Sell order at 61
    EXPECT_EQ(std::get<1>(subscriber.trades[3]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[3]), 61);

    EXPECT_EQ(std::get<0>(subscriber.trades[4]), 18); // Sell order at 62
    EXPECT_EQ(std::get<1>(subscriber.trades[4]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[4]), 62);

    EXPECT_EQ(std::get<0>(subscriber.trades[5]), 17); // Sell order at 63
    EXPECT_EQ(std::get<1>(subscriber.trades[5]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[5]), 63);

    EXPECT_EQ(std::get<0>(subscriber.trades[6]), 16); // Sell order at 64
    EXPECT_EQ(std::get<1>(subscriber.trades[6]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[6]), 64);

    EXPECT_EQ(std::get<0>(subscriber.trades[7]), 14); // Sell order at 66
    EXPECT_EQ(std::get<1>(subscriber.trades[7]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[7]), 66);

    EXPECT_EQ(std::get<0>(subscriber.trades[8]), 13); // Sell order at 67
    EXPECT_EQ(std::get<1>(subscriber.trades[8]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[8]), 67);

    EXPECT_EQ(std::get<0>(subscriber.trades[9]), 12); // Sell order at 68
    EXPECT_EQ(std::get<1>(subscriber.trades[9]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[9]), 68);

    EXPECT_EQ(std::get<0>(subscriber.trades[10]), 11); // Sell order at 69
    EXPECT_EQ(std::get<1>(subscriber.trades[10]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[10]), 69);

    EXPECT_EQ(std::get<0>(subscriber.trades[11]), 30); // Buy order at 80
    EXPECT_EQ(std::get<1>(subscriber.trades[11]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[11]), 80);

    EXPECT_EQ(std::get<0>(subscriber.trades[12]), 29); // Buy order at 79
    EXPECT_EQ(std::get<1>(subscriber.trades[12]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[12]), 79);

    EXPECT_EQ(std::get<0>(subscriber.trades[13]), 40); // Sell order at 60
    EXPECT_EQ(std::get<1>(subscriber.trades[13]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[13]), 60);

    EXPECT_EQ(std::get<0>(subscriber.trades[14]), 41); // Sell order at 60
    EXPECT_EQ(std::get<1>(subscriber.trades[14]), 15);
    EXPECT_EQ(std::get<2>(subscriber.trades[14]), 60);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
