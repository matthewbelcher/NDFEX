#include <gtest/gtest.h>
#include "order_ladder.H"

#include <iostream>

class MockSubscriber {
public:

    std::vector<std::tuple<uint64_t, ndfex::md::SIDE, uint32_t, int32_t>> new_orders;
    std::vector<uint64_t> deleted_orders;
    std::vector<std::tuple<uint64_t, ndfex::md::SIDE, uint32_t, int32_t>> modified_orders;
    std::vector<std::tuple<uint64_t, uint32_t, int32_t>> trades;
    std::vector<std::tuple<uint64_t, ndfex::md::SIDE, uint32_t, int32_t>> fills;

    void onNewOrder(uint64_t order_id, uint32_t symbol, ndfex::md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags) {
        new_orders.emplace_back(order_id, side, quantity, price);
    }

    void onDeleteOrder(uint64_t order_id) {
        deleted_orders.push_back(order_id);
    }

    void onModifyOrder(uint64_t order_id, uint32_t symbol, ndfex::md::SIDE side, uint32_t quantity, int32_t price) {
        modified_orders.emplace_back(order_id, side, quantity, price);
    }

    void onTrade(uint64_t order_id, uint32_t quantity, int32_t price) {
        trades.emplace_back(order_id, quantity, price);
    }

    void onFill(uint64_t order_id, uint32_t symbol, ndfex::md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags) {
        fills.emplace_back(order_id, side, quantity, price);
    }
};

TEST(OrderLadderTest, NewOrder) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    orderLadder.new_order(1, ndfex::md::SIDE::SELL, 10, 50, 0);

    ASSERT_EQ(subscriber.new_orders.size(), 1);
    EXPECT_EQ(std::get<0>(subscriber.new_orders[0]), 1);
}

TEST(OrderLadderTest, DeleteOrder) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    orderLadder.new_order(1, ndfex::md::SIDE::SELL, 10, 50, 0);
    orderLadder.delete_order(1);

    ASSERT_EQ(subscriber.deleted_orders.size(), 1);
    EXPECT_EQ(subscriber.deleted_orders[0], 1);
}

TEST(OrderLadderTest, SimpleCrossTrade) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    orderLadder.new_order(1, ndfex::md::SIDE::BUY, 10, 50, 0); // BUY order
    orderLadder.new_order(2, ndfex::md::SIDE::SELL, 10, 50, 0); // SELL order

    ASSERT_EQ(subscriber.trades.size(), 1);
    EXPECT_EQ(std::get<0>(subscriber.trades[0]), 1);
    EXPECT_EQ(std::get<1>(subscriber.trades[0]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[0]), 50);

    ASSERT_EQ(subscriber.fills.size(), 2);
    EXPECT_EQ(std::get<0>(subscriber.fills[0]), 1);
    EXPECT_EQ(std::get<1>(subscriber.fills[0]), ndfex::md::SIDE::BUY);
    EXPECT_EQ(std::get<2>(subscriber.fills[0]), 10);
    EXPECT_EQ(std::get<3>(subscriber.fills[0]), 50);

    EXPECT_EQ(std::get<0>(subscriber.fills[1]), 2);
    EXPECT_EQ(std::get<1>(subscriber.fills[1]), ndfex::md::SIDE::SELL);
    EXPECT_EQ(std::get<2>(subscriber.fills[1]), 10);
    EXPECT_EQ(std::get<3>(subscriber.fills[1]), 50);

}

TEST(OrderLadderTest, CrossMultipleLevels) {
    MockSubscriber subscriber;

    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    orderLadder.new_order(1, ndfex::md::SIDE::BUY, 10, 50, 0); // BUY order at 50
    orderLadder.new_order(2, ndfex::md::SIDE::BUY, 10, 55, 0); // BUY order at 55
    orderLadder.new_order(3, ndfex::md::SIDE::BUY, 10, 60, 0); // BUY order at 60

    orderLadder.new_order(4, ndfex::md::SIDE::SELL, 25, 50, 0); // SELL order at 50

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

    ASSERT_EQ(subscriber.fills.size(), 6);
    EXPECT_EQ(std::get<0>(subscriber.fills[0]), 3);
    EXPECT_EQ(std::get<1>(subscriber.fills[0]), ndfex::md::SIDE::BUY);
    EXPECT_EQ(std::get<2>(subscriber.fills[0]), 10);
    EXPECT_EQ(std::get<3>(subscriber.fills[0]), 60);

    EXPECT_EQ(std::get<0>(subscriber.fills[1]), 4);
    EXPECT_EQ(std::get<1>(subscriber.fills[1]), ndfex::md::SIDE::SELL);
    EXPECT_EQ(std::get<2>(subscriber.fills[1]), 10);
    EXPECT_EQ(std::get<3>(subscriber.fills[1]), 60);

    EXPECT_EQ(std::get<0>(subscriber.fills[2]), 2);
    EXPECT_EQ(std::get<1>(subscriber.fills[2]), ndfex::md::SIDE::BUY);
    EXPECT_EQ(std::get<2>(subscriber.fills[2]), 10);
    EXPECT_EQ(std::get<3>(subscriber.fills[2]), 55);

    EXPECT_EQ(std::get<0>(subscriber.fills[3]), 4);
    EXPECT_EQ(std::get<1>(subscriber.fills[3]), ndfex::md::SIDE::SELL);
    EXPECT_EQ(std::get<2>(subscriber.fills[3]), 10);
    EXPECT_EQ(std::get<3>(subscriber.fills[3]), 55);

    EXPECT_EQ(std::get<0>(subscriber.fills[4]), 1);
    EXPECT_EQ(std::get<1>(subscriber.fills[4]), ndfex::md::SIDE::BUY);
    EXPECT_EQ(std::get<2>(subscriber.fills[4]), 5);
    EXPECT_EQ(std::get<3>(subscriber.fills[4]), 50);

    EXPECT_EQ(std::get<0>(subscriber.fills[5]), 4);
    EXPECT_EQ(std::get<1>(subscriber.fills[5]), ndfex::md::SIDE::SELL);
    EXPECT_EQ(std::get<2>(subscriber.fills[5]), 5);
    EXPECT_EQ(std::get<3>(subscriber.fills[5]), 50);
}

TEST(OrderLadderTest, VeryComplicatedTest) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    // Add 10 BUY orders
    for (uint64_t i = 1; i <= 10; ++i) {
        orderLadder.new_order(i, ndfex::md::SIDE::BUY, 10, 50 + i, 0);
    }

    // Add 10 SELL orders
    for (uint64_t i = 11; i <= 20; ++i) {
        orderLadder.new_order(i, ndfex::md::SIDE::SELL, 10, 70 - (i - 10), 0);
    }

    // Delete some orders
    orderLadder.delete_order(5);
    orderLadder.delete_order(15);

    // Add more orders to create matches
    orderLadder.new_order(21, ndfex::md::SIDE::SELL, 15, 55, 0); // Should match with BUY orders at 55 and 56
    orderLadder.new_order(22, ndfex::md::SIDE::BUY, 20, 65, 0); // Should match with SELL orders at 65 and 64

    // Add more orders
    for (uint64_t i = 23; i <= 30; ++i) {
        orderLadder.new_order(i, ndfex::md::SIDE::BUY, 10, 50 + i, 0);
    }

    for (uint64_t i = 31; i <= 40; ++i) {
        orderLadder.new_order(i, ndfex::md::SIDE::SELL, 10, 70 - (i - 30), 0);
    }

    // Delete some more orders
    orderLadder.delete_order(3);

    // Check that delete_order throws an exception for orders that matched and were not added to the book
    EXPECT_THROW(orderLadder.delete_order(13), std::runtime_error);
    EXPECT_THROW(orderLadder.delete_order(25), std::runtime_error);

    // Add more orders to create more matches
    orderLadder.new_order(41, ndfex::md::SIDE::SELL, 30, 60, 0); // Should match with BUY orders at 60, 59, and 58
    orderLadder.new_order(42, ndfex::md::SIDE::BUY, 25, 70, 0); // Should match with SELL orders at 70, 69, and 68

    // Verify the trades
    ASSERT_EQ(subscriber.trades.size(), 15);

    EXPECT_EQ(std::get<0>(subscriber.trades[0]), 10); // BUY order at 60
    EXPECT_EQ(std::get<1>(subscriber.trades[0]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[0]), 60);

    EXPECT_EQ(std::get<0>(subscriber.trades[1]), 9); // BUY order at 59
    EXPECT_EQ(std::get<1>(subscriber.trades[1]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[1]), 59);

    EXPECT_EQ(std::get<0>(subscriber.trades[2]), 8); // BUY order at 58
    EXPECT_EQ(std::get<1>(subscriber.trades[2]), 5);
    EXPECT_EQ(std::get<2>(subscriber.trades[2]), 58);

    EXPECT_EQ(std::get<0>(subscriber.trades[3]), 19); // SELL order at 61
    EXPECT_EQ(std::get<1>(subscriber.trades[3]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[3]), 61);

    EXPECT_EQ(std::get<0>(subscriber.trades[4]), 18); // SELL order at 62
    EXPECT_EQ(std::get<1>(subscriber.trades[4]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[4]), 62);

    EXPECT_EQ(std::get<0>(subscriber.trades[5]), 17); // SELL order at 63
    EXPECT_EQ(std::get<1>(subscriber.trades[5]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[5]), 63);

    EXPECT_EQ(std::get<0>(subscriber.trades[6]), 16); // SELL order at 64
    EXPECT_EQ(std::get<1>(subscriber.trades[6]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[6]), 64);

    EXPECT_EQ(std::get<0>(subscriber.trades[7]), 14); // SELL order at 66
    EXPECT_EQ(std::get<1>(subscriber.trades[7]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[7]), 66);

    EXPECT_EQ(std::get<0>(subscriber.trades[8]), 13); // SELL order at 67
    EXPECT_EQ(std::get<1>(subscriber.trades[8]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[8]), 67);

    EXPECT_EQ(std::get<0>(subscriber.trades[9]), 12); // SELL order at 68
    EXPECT_EQ(std::get<1>(subscriber.trades[9]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[9]), 68);

    EXPECT_EQ(std::get<0>(subscriber.trades[10]), 11); // SELL order at 69
    EXPECT_EQ(std::get<1>(subscriber.trades[10]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[10]), 69);

    EXPECT_EQ(std::get<0>(subscriber.trades[11]), 30); // BUY order at 80
    EXPECT_EQ(std::get<1>(subscriber.trades[11]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[11]), 80);

    EXPECT_EQ(std::get<0>(subscriber.trades[12]), 29); // BUY order at 79
    EXPECT_EQ(std::get<1>(subscriber.trades[12]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[12]), 79);

    EXPECT_EQ(std::get<0>(subscriber.trades[13]), 40); // SELL order at 60
    EXPECT_EQ(std::get<1>(subscriber.trades[13]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[13]), 60);

    EXPECT_EQ(std::get<0>(subscriber.trades[14]), 41); // SELL order at 60
    EXPECT_EQ(std::get<1>(subscriber.trades[14]), 15);
    EXPECT_EQ(std::get<2>(subscriber.trades[14]), 60);
}

TEST(OrderLadderTest, ModifyPrice) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    orderLadder.new_order(1, ndfex::md::SIDE::BUY, 10, 50, 0); // BUY order at 50
    orderLadder.modify_order(1, ndfex::md::SIDE::BUY, 10, 55); // Modify to 55

    ASSERT_EQ(subscriber.modified_orders.size(), 1);
    EXPECT_EQ(std::get<0>(subscriber.modified_orders[0]), 1);
    EXPECT_EQ(std::get<1>(subscriber.modified_orders[0]), ndfex::md::SIDE::BUY);
    EXPECT_EQ(std::get<2>(subscriber.modified_orders[0]), 10);
    EXPECT_EQ(std::get<3>(subscriber.modified_orders[0]), 55);

    orderLadder.modify_order(1, ndfex::md::SIDE::BUY, 10, 60); // Modify to 60

    ASSERT_EQ(subscriber.modified_orders.size(), 2);
    EXPECT_EQ(std::get<0>(subscriber.modified_orders[1]), 1);
    EXPECT_EQ(std::get<1>(subscriber.modified_orders[1]), ndfex::md::SIDE::BUY);
    EXPECT_EQ(std::get<2>(subscriber.modified_orders[1]), 10);
    EXPECT_EQ(std::get<3>(subscriber.modified_orders[1]), 60);

    // now change the side
    orderLadder.modify_order(1, ndfex::md::SIDE::SELL, 10, 60); // Modify to 60

    ASSERT_EQ(subscriber.modified_orders.size(), 3);
    EXPECT_EQ(std::get<0>(subscriber.modified_orders[2]), 1);
    EXPECT_EQ(std::get<1>(subscriber.modified_orders[2]), ndfex::md::SIDE::SELL);
    EXPECT_EQ(std::get<2>(subscriber.modified_orders[2]), 10);
    EXPECT_EQ(std::get<3>(subscriber.modified_orders[2]), 60);
}

TEST(OrderLadderTest, ModifyQuantityToZero) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    orderLadder.new_order(1, ndfex::md::SIDE::BUY, 10, 50, 0); // BUY order at 50
    orderLadder.modify_order(1, ndfex::md::SIDE::BUY, 0, 50); // Modify to 0

    ASSERT_EQ(subscriber.deleted_orders.size(), 1);
    EXPECT_EQ(subscriber.deleted_orders[0], 1);
}

TEST(OrderLadderTest, ModifyIntoTrade) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    orderLadder.new_order(1, ndfex::md::SIDE::BUY, 10, 50, 0); // BUY order at 50
    orderLadder.new_order(2, ndfex::md::SIDE::SELL, 10, 60, 0); // SELL order at 60

    orderLadder.modify_order(1, ndfex::md::SIDE::BUY, 15, 60); // Modify to 15

    ASSERT_EQ(subscriber.trades.size(), 1);
    EXPECT_EQ(std::get<0>(subscriber.trades[0]), 2);
    EXPECT_EQ(std::get<1>(subscriber.trades[0]), 10);
    EXPECT_EQ(std::get<2>(subscriber.trades[0]), 60);

    ASSERT_EQ(subscriber.fills.size(), 2);
    EXPECT_EQ(std::get<0>(subscriber.fills[0]), 2);
    EXPECT_EQ(std::get<1>(subscriber.fills[0]), ndfex::md::SIDE::SELL);
    EXPECT_EQ(std::get<2>(subscriber.fills[0]), 10);
    EXPECT_EQ(std::get<3>(subscriber.fills[0]), 60);

    EXPECT_EQ(std::get<0>(subscriber.fills[1]), 1);
    EXPECT_EQ(std::get<1>(subscriber.fills[1]), ndfex::md::SIDE::BUY);
    EXPECT_EQ(std::get<2>(subscriber.fills[1]), 10);
    EXPECT_EQ(std::get<3>(subscriber.fills[1]), 60);

    EXPECT_EQ(subscriber.modified_orders.size(), 1);
    EXPECT_EQ(std::get<0>(subscriber.modified_orders[0]), 1);
    EXPECT_EQ(std::get<1>(subscriber.modified_orders[0]), ndfex::md::SIDE::BUY);
    EXPECT_EQ(std::get<2>(subscriber.modified_orders[0]), 15);
    EXPECT_EQ(std::get<3>(subscriber.modified_orders[0]), 60);
}

TEST(OrderLadderTest, PartialFillModifyAutomaticallyClose) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    orderLadder.new_order(1, ndfex::md::SIDE::BUY, 10, 50, 0); // BUY order at 50
    orderLadder.new_order(2, ndfex::md::SIDE::SELL, 5, 50, 0); // SELL order at 50

    orderLadder.modify_order(1, ndfex::md::SIDE::BUY, 2, 50); // Modify to 5

    ASSERT_EQ(subscriber.trades.size(), 1);
    EXPECT_EQ(std::get<0>(subscriber.trades[0]), 1);
    EXPECT_EQ(std::get<1>(subscriber.trades[0]), 5);
    EXPECT_EQ(std::get<2>(subscriber.trades[0]), 50);

    ASSERT_EQ(subscriber.fills.size(), 2);
    EXPECT_EQ(std::get<0>(subscriber.fills[0]), 1);
    EXPECT_EQ(std::get<1>(subscriber.fills[0]), ndfex::md::SIDE::BUY);
    EXPECT_EQ(std::get<2>(subscriber.fills[0]), 5);
    EXPECT_EQ(std::get<3>(subscriber.fills[0]), 50);

    EXPECT_EQ(std::get<0>(subscriber.fills[1]), 2);
    EXPECT_EQ(std::get<1>(subscriber.fills[1]), ndfex::md::SIDE::SELL);
    EXPECT_EQ(std::get<2>(subscriber.fills[1]), 5);
    EXPECT_EQ(std::get<3>(subscriber.fills[1]), 50);

    ASSERT_EQ(subscriber.deleted_orders.size(), 1);
    EXPECT_EQ(subscriber.deleted_orders[0], 1);
}

TEST(OrderLadderTest, PartialFillOnEntryThenModifyAutoClose) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    orderLadder.new_order(1, ndfex::md::SIDE::BUY, 5, 50, 0); // BUY order at 50
    orderLadder.new_order(2, ndfex::md::SIDE::SELL, 10, 50, 0); // SELL order at 50

    orderLadder.modify_order(2, ndfex::md::SIDE::SELL, 5, 50); // Modify to 5

    ASSERT_EQ(subscriber.trades.size(), 1);
    EXPECT_EQ(std::get<0>(subscriber.trades[0]), 1);
    EXPECT_EQ(std::get<1>(subscriber.trades[0]), 5);
    EXPECT_EQ(std::get<2>(subscriber.trades[0]), 50);

    ASSERT_EQ(subscriber.fills.size(), 2);
    EXPECT_EQ(std::get<0>(subscriber.fills[0]), 1);
    EXPECT_EQ(std::get<1>(subscriber.fills[0]), ndfex::md::SIDE::BUY);
    EXPECT_EQ(std::get<2>(subscriber.fills[0]), 5);
    EXPECT_EQ(std::get<3>(subscriber.fills[0]), 50);

    EXPECT_EQ(std::get<0>(subscriber.fills[1]), 2);
    EXPECT_EQ(std::get<1>(subscriber.fills[1]), ndfex::md::SIDE::SELL);
    EXPECT_EQ(std::get<2>(subscriber.fills[1]), 5);
    EXPECT_EQ(std::get<3>(subscriber.fills[1]), 50);

    ASSERT_EQ(subscriber.deleted_orders.size(), 1);
    EXPECT_EQ(subscriber.deleted_orders[0], 2);
}

TEST(OrderLadderTest, PartialFillOnEntryThenModifyUpThenDownAutoClose) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    orderLadder.new_order(1, ndfex::md::SIDE::BUY, 5, 50, 0); // BUY order at 50
    orderLadder.new_order(2, ndfex::md::SIDE::SELL, 10, 50, 0); // SELL order at 50

    orderLadder.modify_order(2, ndfex::md::SIDE::SELL, 15, 50); // Modify to 15

    ASSERT_EQ(subscriber.trades.size(), 1);
    EXPECT_EQ(std::get<0>(subscriber.trades[0]), 1);
    EXPECT_EQ(std::get<1>(subscriber.trades[0]), 5);
    EXPECT_EQ(std::get<2>(subscriber.trades[0]), 50);

    ASSERT_EQ(subscriber.fills.size(), 2);
    EXPECT_EQ(std::get<0>(subscriber.fills[0]), 1);
    EXPECT_EQ(std::get<1>(subscriber.fills[0]), ndfex::md::SIDE::BUY);
    EXPECT_EQ(std::get<2>(subscriber.fills[0]), 5);
    EXPECT_EQ(std::get<3>(subscriber.fills[0]), 50);

    EXPECT_EQ(std::get<0>(subscriber.fills[1]), 2);
    EXPECT_EQ(std::get<1>(subscriber.fills[1]), ndfex::md::SIDE::SELL);
    EXPECT_EQ(std::get<2>(subscriber.fills[1]), 5);
    EXPECT_EQ(std::get<3>(subscriber.fills[1]), 50);

    ASSERT_EQ(subscriber.new_orders.size(), 2);
    EXPECT_EQ(std::get<0>(subscriber.new_orders[0]), 1);
    EXPECT_EQ(std::get<1>(subscriber.new_orders[0]), ndfex::md::SIDE::BUY);
    EXPECT_EQ(std::get<2>(subscriber.new_orders[0]), 5);
    EXPECT_EQ(std::get<3>(subscriber.new_orders[0]), 50);

    EXPECT_EQ(std::get<0>(subscriber.new_orders[1]), 2);
    EXPECT_EQ(std::get<1>(subscriber.new_orders[1]), ndfex::md::SIDE::SELL);
    EXPECT_EQ(std::get<2>(subscriber.new_orders[1]), 5);
    EXPECT_EQ(std::get<3>(subscriber.new_orders[1]), 50);

    EXPECT_EQ(subscriber.modified_orders.size(), 1);
    EXPECT_EQ(std::get<0>(subscriber.modified_orders[0]), 2);
    EXPECT_EQ(std::get<1>(subscriber.modified_orders[0]), ndfex::md::SIDE::SELL);
    EXPECT_EQ(std::get<2>(subscriber.modified_orders[0]), 10);
    EXPECT_EQ(std::get<3>(subscriber.modified_orders[0]), 50);

    ASSERT_EQ(subscriber.deleted_orders.size(), 0);

    orderLadder.modify_order(2, ndfex::md::SIDE::SELL, 5, 50); // Modify to 5

    ASSERT_EQ(subscriber.deleted_orders.size(), 1);
    EXPECT_EQ(subscriber.deleted_orders[0], 2);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
