#include <thread>

#include <gtest/gtest.h>
#include "spsc_subscriber.H"

using namespace ndfex;
using namespace md;

TEST(SPSCSubscriberTest, MDSubscriber) {
    SPSCMDQueue queue(1000);
    SPSC_Subscriber subscriber(queue);

    std::thread t([&queue] {
        while (!queue.front());
        md_payload payload = *queue.front();
        queue.pop();

        EXPECT_EQ(payload.msg_type, MSG_TYPE::NEW_ORDER);
        EXPECT_EQ(payload.order_id, 1);
        EXPECT_EQ(payload.symbol, 0);
        EXPECT_EQ(payload.side, SIDE::BUY);
        EXPECT_EQ(payload.quantity, 10);
        EXPECT_EQ(payload.price, 50);
        EXPECT_EQ(payload.flags, 0);

        while (!queue.front());
        payload = *queue.front();
        queue.pop();
        EXPECT_EQ(payload.msg_type, MSG_TYPE::DELETE_ORDER);
        EXPECT_EQ(payload.order_id, 1);
        EXPECT_EQ(payload.symbol, 0);
        EXPECT_EQ(payload.side, SIDE::BUY);
        EXPECT_EQ(payload.quantity, 0);
        EXPECT_EQ(payload.price, 0);
        EXPECT_EQ(payload.flags, 0);

        while (!queue.front());
        payload = *queue.front();
        queue.pop();
        EXPECT_EQ(payload.msg_type, MSG_TYPE::TRADE);
        EXPECT_EQ(payload.order_id, 1);
        EXPECT_EQ(payload.symbol, 0);
        EXPECT_EQ(payload.side, SIDE::BUY);
        EXPECT_EQ(payload.quantity, 10);
        EXPECT_EQ(payload.price, 50);
        EXPECT_EQ(payload.flags, 0);

        while (!queue.front());
        payload = *queue.front();
        queue.pop();
        EXPECT_EQ(payload.msg_type, MSG_TYPE::TRADE_SUMMARY);
        EXPECT_EQ(payload.order_id, 0);
        EXPECT_EQ(payload.symbol, 0);
        EXPECT_EQ(payload.side, SIDE::BUY);
        EXPECT_EQ(payload.quantity, 10);
        EXPECT_EQ(payload.price, 50);
        EXPECT_EQ(payload.flags, 0);

        while (!queue.front());
        payload = *queue.front();
        queue.pop();
        EXPECT_EQ(payload.msg_type, MSG_TYPE::MODIFY_ORDER);
        EXPECT_EQ(payload.order_id, 1);
        EXPECT_EQ(payload.symbol, 0);
        EXPECT_EQ(payload.side, SIDE::BUY);
        EXPECT_EQ(payload.quantity, 10);
        EXPECT_EQ(payload.price, 50);
        EXPECT_EQ(payload.flags, 0);
    });

    subscriber.onNewOrder(1, 0, SIDE::BUY, 10, 50, 0);
    subscriber.onDeleteOrder(1);
    subscriber.onTrade(1, 10, 50);
    subscriber.onTradeSummary(0, SIDE::BUY, 10, 50);
    subscriber.onModifyOrder(1, SIDE::BUY, 10, 50);

    t.join();
}

TEST(SPSCSubscriberTest, Performance) {
    SPSCMDQueue queue(1000);
    SPSC_Subscriber subscriber(queue);

    std::thread t([&queue] {
        for (int i = 0; i < 1000000; ++i) {
            while (!queue.front());
            md_payload p = *queue.front();
            queue.pop();
        }
    });

    for (int i = 0; i < 1000000; ++i) {
        subscriber.onNewOrder(1, 0, SIDE::BUY, 10, 50, 0);
    }
    t.join();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
