#include <gtest/gtest.h>
#include "order_ladder.H"

#include <iostream>

class MockSubscriber {
public:

    std::vector<std::tuple<uint64_t, ndfex::md::SIDE, uint32_t, int32_t>> new_orders;
    std::vector<std::tuple<uint64_t, bool>> deleted_orders;
    std::vector<std::tuple<uint64_t, ndfex::md::SIDE, uint32_t, int32_t>> modified_orders;
    std::vector<std::tuple<uint64_t, uint32_t, int32_t>> trades;
    std::vector<std::tuple<uint64_t, ndfex::md::SIDE, uint32_t, int32_t>> fills;
    std::vector<uint64_t> cancel_rejects;

    void onNewOrder(uint64_t order_id, uint32_t symbol, ndfex::md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags) {
        (void)symbol;
        (void)flags;
        new_orders.emplace_back(order_id, side, quantity, price);
    }

    void onDeleteOrder(uint64_t order_id, bool publish = true) {
        deleted_orders.emplace_back(order_id, publish);
    }

    void onModifyOrder(uint64_t order_id, uint32_t symbol, ndfex::md::SIDE side, uint32_t quantity, int32_t price) {
        (void)symbol;
        modified_orders.emplace_back(order_id, side, quantity, price);
    }

    void onTrade(uint64_t order_id, uint32_t quantity, int32_t price) {
        trades.emplace_back(order_id, quantity, price);
    }

    void onFill(uint64_t order_id, uint32_t symbol, ndfex::md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags, bool active) {
        (void)symbol;
        (void)flags;
        (void)active;
        fills.emplace_back(order_id, side, quantity, price);
    }

    void onCancelReject(uint64_t order_id) {
        cancel_rejects.push_back(order_id);
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
    EXPECT_EQ(std::get<0>(subscriber.deleted_orders[0]), 1);
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

    // check no deleted orders are emitted
    ASSERT_EQ(subscriber.deleted_orders.size(), 0);
    ASSERT_EQ(subscriber.modified_orders.size(), 0);
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

    // these cancels should be rejected
    orderLadder.delete_order(13);
    orderLadder.delete_order(25);

    ASSERT_EQ(subscriber.cancel_rejects.size(), 2);
    EXPECT_EQ(subscriber.cancel_rejects[0], 13);
    EXPECT_EQ(subscriber.cancel_rejects[1], 25);

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

    ASSERT_EQ(subscriber.deleted_orders.size(), 0);
}

TEST(OrderLadderTest, ModifyQuantityToZero) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    orderLadder.new_order(1, ndfex::md::SIDE::BUY, 10, 50, 0); // BUY order at 50
    orderLadder.modify_order(1, ndfex::md::SIDE::BUY, 0, 50); // Modify to 0

    ASSERT_EQ(subscriber.deleted_orders.size(), 1);
    EXPECT_EQ(std::get<0>(subscriber.deleted_orders[0]), 1);
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
    EXPECT_EQ(std::get<2>(subscriber.modified_orders[0]), 5);
    EXPECT_EQ(std::get<3>(subscriber.modified_orders[0]), 60);

    ASSERT_EQ(subscriber.deleted_orders.size(), 0);
}

TEST(OrderLadderTest, ModifyIntoFullyFilled) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    orderLadder.new_order(1, ndfex::md::SIDE::BUY, 10, 50, 0); // BUY order at 50
    orderLadder.new_order(2, ndfex::md::SIDE::SELL, 20, 60, 0); // SELL order at 50

    orderLadder.modify_order(1, ndfex::md::SIDE::BUY, 15, 60); // Modify to 15

    ASSERT_EQ(subscriber.trades.size(), 1);
    EXPECT_EQ(std::get<0>(subscriber.trades[0]), 2);
    EXPECT_EQ(std::get<1>(subscriber.trades[0]), 15);
    EXPECT_EQ(std::get<2>(subscriber.trades[0]), 60);

    ASSERT_EQ(subscriber.fills.size(), 2);
    EXPECT_EQ(std::get<0>(subscriber.fills[0]), 2);
    EXPECT_EQ(std::get<1>(subscriber.fills[0]), ndfex::md::SIDE::SELL);
    EXPECT_EQ(std::get<2>(subscriber.fills[0]), 15);
    EXPECT_EQ(std::get<3>(subscriber.fills[0]), 60);

    EXPECT_EQ(std::get<0>(subscriber.fills[1]), 1);
    EXPECT_EQ(std::get<1>(subscriber.fills[1]), ndfex::md::SIDE::BUY);
    EXPECT_EQ(std::get<2>(subscriber.fills[1]), 15);
    EXPECT_EQ(std::get<3>(subscriber.fills[1]), 60);

    ASSERT_EQ(subscriber.deleted_orders.size(), 1);
    EXPECT_EQ(std::get<0>(subscriber.deleted_orders[0]), 1);
    EXPECT_EQ(std::get<1>(subscriber.deleted_orders[0]), true);
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
    EXPECT_EQ(std::get<0>(subscriber.deleted_orders[0]), 1);
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
    EXPECT_EQ(std::get<0>(subscriber.deleted_orders[0]), 2);
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
    EXPECT_EQ(std::get<0>(subscriber.deleted_orders[0]), 2);
}

// =============================================================================
// BUG EXPOSURE TESTS
// These tests expose critical bugs in the OrderLadder implementation
// =============================================================================

// BUG #1: Pointer invalidation when unordered_map rehashes
// The Order struct contains next/prev pointers to other Order objects.
// When the unordered_map rehashes (due to insertions exceeding load factor),
// all Order objects are relocated, but the pointers in PriceLevel and linked
// lists become dangling pointers.
//
// This test inserts enough orders to trigger rehash, then verifies the
// linked list integrity by deleting orders (which traverses the pointers).
TEST(OrderLadderBugTest, PointerInvalidationOnRehash) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    // The default initial capacity is 1e7 which is huge, so we need a smaller
    // test. However, std::unordered_map will still rehash when load_factor > max_load_factor.
    // Default max_load_factor is 1.0, so we need to insert more than the bucket count.
    //
    // To expose this bug more reliably, we insert orders at the SAME price level
    // so they form a linked list. Then we trigger rehash and try to traverse.

    const int NUM_ORDERS = 1000;  // Should be enough to trigger some rehashing
    const int32_t PRICE = 100;

    // Insert many BUY orders at the same price level (forming a linked list)
    for (uint64_t i = 1; i <= NUM_ORDERS; ++i) {
        orderLadder.new_order(i, ndfex::md::SIDE::BUY, 10, PRICE, 0);
    }

    // Now delete orders from the middle to traverse the linked list
    // If pointers are invalid, this will crash or corrupt memory
    for (uint64_t i = NUM_ORDERS / 4; i <= NUM_ORDERS / 2; ++i) {
        orderLadder.delete_order(i);
    }

    // Verify deletions happened
    size_t expected_deletes = (NUM_ORDERS / 2) - (NUM_ORDERS / 4) + 1;
    EXPECT_EQ(subscriber.deleted_orders.size(), expected_deletes);

    // Now insert a crossing SELL order to match remaining BUY orders
    // This will traverse the linked list again
    subscriber.trades.clear();
    orderLadder.new_order(NUM_ORDERS + 1, ndfex::md::SIDE::SELL, NUM_ORDERS * 10, PRICE, 0);

    // Should have matched with all remaining orders
    // Remaining orders = NUM_ORDERS - deleted orders
    size_t remaining = NUM_ORDERS - expected_deletes;
    EXPECT_EQ(subscriber.trades.size(), remaining);
}

// More aggressive rehash test - forces rehash by manipulating load
TEST(OrderLadderBugTest, PointerInvalidationOnRehashAggressive) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    // Insert orders at multiple price levels to create multiple linked lists
    // Then force rehash and verify all lists are intact

    const int ORDERS_PER_LEVEL = 50;
    const int NUM_LEVELS = 20;
    uint64_t order_id = 1;

    // Create orders at multiple price levels
    for (int level = 0; level < NUM_LEVELS; ++level) {
        int32_t price = 100 + level;
        for (int i = 0; i < ORDERS_PER_LEVEL; ++i) {
            orderLadder.new_order(order_id++, ndfex::md::SIDE::BUY, 10, price, 0);
        }
    }

    // Now delete every other order from every level to stress-test pointer integrity
    for (uint64_t id = 2; id < order_id; id += 2) {
        orderLadder.delete_order(id);
    }

    // Insert a large crossing order that sweeps through all levels
    subscriber.trades.clear();
    orderLadder.new_order(order_id++, ndfex::md::SIDE::SELL, 10000, 100, 0);

    // Verify we got trades (exact count depends on what's left)
    EXPECT_GT(subscriber.trades.size(), 0);
}

// BUG #2: Iterator invalidation in modify_order
// After delete_order is called, the iterator `it` pointing into `orders` map
// is invalidated because erase() was called. But the code then accesses
// it->second.side and it->second.flags which is undefined behavior.
//
// This test modifies an order's price (which triggers delete+re-insert path)
// and verifies the order ends up on the correct side.
TEST(OrderLadderBugTest, IteratorInvalidationInModifyOrder) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    // Create a BUY order
    orderLadder.new_order(1, ndfex::md::SIDE::BUY, 100, 50, 0);

    ASSERT_EQ(subscriber.new_orders.size(), 1);
    EXPECT_EQ(std::get<1>(subscriber.new_orders[0]), ndfex::md::SIDE::BUY);

    // Modify the price - this goes through the delete+re-insert path
    // which has the iterator invalidation bug
    orderLadder.modify_order(1, ndfex::md::SIDE::BUY, 100, 60);

    // The modify should have worked
    ASSERT_EQ(subscriber.modified_orders.size(), 1);
    EXPECT_EQ(std::get<0>(subscriber.modified_orders[0]), 1);
    EXPECT_EQ(std::get<1>(subscriber.modified_orders[0]), ndfex::md::SIDE::BUY);
    EXPECT_EQ(std::get<2>(subscriber.modified_orders[0]), 100);
    EXPECT_EQ(std::get<3>(subscriber.modified_orders[0]), 60);

    // Now verify the order is actually on the BUY side by crossing it
    subscriber.trades.clear();
    orderLadder.new_order(2, ndfex::md::SIDE::SELL, 100, 60, 0);

    // Should have matched with order 1
    ASSERT_EQ(subscriber.trades.size(), 1);
    EXPECT_EQ(std::get<0>(subscriber.trades[0]), 1);
}

// Test that exposes the bug more clearly - modify changes side
// The bug: after delete_order, it->second.side is accessed but iterator is invalid
// The order should go to the NEW side (passed to modify_order), but the buggy
// code reads the OLD side from an invalidated iterator
TEST(OrderLadderBugTest, ModifyOrderSideChangeUsesInvalidatedIterator) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    // Create a BUY order at price 50
    orderLadder.new_order(1, ndfex::md::SIDE::BUY, 100, 50, 0);

    // Create a SELL order at price 60 (no match)
    orderLadder.new_order(2, ndfex::md::SIDE::SELL, 100, 60, 0);

    // Now modify order 1: change side from BUY to SELL, and price to 70
    // The buggy code will read `it->second.side` after delete_order invalidates `it`
    // This should put the order on the SELL side
    orderLadder.modify_order(1, ndfex::md::SIDE::SELL, 100, 70);

    ASSERT_EQ(subscriber.modified_orders.size(), 1);
    // Verify the modify callback got the correct side
    EXPECT_EQ(std::get<1>(subscriber.modified_orders[0]), ndfex::md::SIDE::SELL);

    // Now send a BUY order at 70 - it should match the best SELL (order 2 at 60),
    // then the modified order 1 (now SELL at 70)
    subscriber.trades.clear();
    orderLadder.new_order(3, ndfex::md::SIDE::BUY, 200, 70, 0);

    // If the bug causes order 1 to be on wrong side, it won't be matched at 70
    ASSERT_EQ(subscriber.trades.size(), 2);
    EXPECT_EQ(std::get<0>(subscriber.trades[0]), 2);  // Best price SELL at 60
    EXPECT_EQ(std::get<1>(subscriber.trades[0]), 100);
    EXPECT_EQ(std::get<2>(subscriber.trades[0]), 60);
    EXPECT_EQ(std::get<0>(subscriber.trades[1]), 1);  // Then the modified order at 70
    EXPECT_EQ(std::get<1>(subscriber.trades[1]), 100);
    EXPECT_EQ(std::get<2>(subscriber.trades[1]), 70);
}

// Test rapid modify operations that stress the iterator invalidation
TEST(OrderLadderBugTest, RapidModifyOperations) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    // Create several orders
    for (uint64_t i = 1; i <= 10; ++i) {
        orderLadder.new_order(i, ndfex::md::SIDE::BUY, 100, 50 + i, 0);
    }

    // Rapidly modify all orders - each modify triggers delete+insert
    for (uint64_t i = 1; i <= 10; ++i) {
        orderLadder.modify_order(i, ndfex::md::SIDE::BUY, 100, 100 + i);
    }

    // All orders should have been modified
    EXPECT_EQ(subscriber.modified_orders.size(), 10);

    // Verify they're all at their new prices by crossing
    subscriber.trades.clear();
    orderLadder.new_order(100, ndfex::md::SIDE::SELL, 1000, 100, 0);

    // Should match all 10 orders
    EXPECT_EQ(subscriber.trades.size(), 10);
}

// BUG #3: filled_quantity is overwritten instead of accumulated
// In the match() function, line 300 does:
//   order->filled_quantity = quantity;
// instead of:
//   order->filled_quantity += quantity;
TEST(OrderLadderBugTest, FilledQuantityNotAccumulated) {
    MockSubscriber subscriber;
    ndfex::OrderLadder<MockSubscriber> orderLadder(&subscriber, 1337);

    // Create a large BUY order
    orderLadder.new_order(1, ndfex::md::SIDE::BUY, 100, 50, 0);

    // Partially fill it with a small SELL order
    orderLadder.new_order(2, ndfex::md::SIDE::SELL, 30, 50, 0);

    // Order 1 should now have 70 remaining (100 - 30)
    ASSERT_EQ(subscriber.trades.size(), 1);
    EXPECT_EQ(std::get<1>(subscriber.trades[0]), 30);

    // Partially fill again
    subscriber.trades.clear();
    orderLadder.new_order(3, ndfex::md::SIDE::SELL, 20, 50, 0);

    // Order 1 should now have 50 remaining (70 - 20)
    ASSERT_EQ(subscriber.trades.size(), 1);
    EXPECT_EQ(std::get<1>(subscriber.trades[0]), 20);

    // Now modify order 1's quantity to 60
    // The order has been filled 50 (30 + 20), so if filled_quantity is correct,
    // the new resting quantity should be 60 - 50 = 10
    // But if filled_quantity was overwritten (not accumulated), it would be wrong
    subscriber.modified_orders.clear();
    orderLadder.modify_order(1, ndfex::md::SIDE::BUY, 60, 50);

    // Check what quantity was reported in the modify
    ASSERT_EQ(subscriber.modified_orders.size(), 1);
    // Expected: 60 - 50 = 10 remaining
    // Buggy: filled_quantity might be wrong, leading to incorrect remaining
    EXPECT_EQ(std::get<2>(subscriber.modified_orders[0]), 10);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
