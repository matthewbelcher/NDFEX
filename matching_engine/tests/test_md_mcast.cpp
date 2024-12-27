#include "../md_mcast.H"

#include <gtest/gtest.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <iostream>

using namespace ndfex;

class MarketDataPublisherTest : public ::testing::Test {

protected:
    void SetUp() override {
        read_sock = socket(AF_INET, SOCK_DGRAM, 0);

        addr.sin_family = AF_INET;
        addr.sin_port = htons(12345);
        addr.sin_addr.s_addr = inet_addr("239.1.3.37");
        bind(read_sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

        // set read_sock to non-blocking
        int flags = fcntl(read_sock, F_GETFL, 0);
        if (flags == -1) {
            throw std::runtime_error("Failed to get socket flags");
        }
        fcntl(read_sock, F_SETFL, flags | O_NONBLOCK);

        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = inet_addr("239.1.3.37");
        mreq.imr_interface.s_addr = inet_addr("127.0.0.1");
        if (setsockopt(read_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) < 0)
        {
            throw std::runtime_error("Failed to join multicast group");
        }

    }

    void TearDown() override {
        close(read_sock);
    }

    int read_sock;
    struct sockaddr_in addr;
    std::size_t addrlen = sizeof(addr);
};

TEST_F(MarketDataPublisherTest, NewOrder) {

    auto logger = spdlog::stdout_color_mt("test_logger_new_order");
    MarketDataPublisher publisher("239.1.3.37", 12345, "127.0.0.1", logger);
    publisher.publish_new_order(1, 0, md::SIDE::BUY, 10, 50, 0);

    md::new_order msg;

    int bytes = recvfrom(read_sock, &msg, sizeof(md::new_order), 0, (struct sockaddr *)&addr, (socklen_t*) &addrlen);
    if (bytes == -1) {
        throw std::runtime_error("Failed to receive message");
    }

    EXPECT_EQ(msg.header.magic_number, md::MAGIC_NUMBER);
    EXPECT_EQ(msg.header.length, sizeof(md::new_order));

    EXPECT_EQ(msg.order_id, 1);
    EXPECT_EQ(msg.symbol, 0);
    EXPECT_EQ(msg.side, md::SIDE::BUY);
    EXPECT_EQ(msg.quantity, 10);
    EXPECT_EQ(msg.price, 50);
    EXPECT_EQ(msg.flags, 0);
}

TEST_F(MarketDataPublisherTest, DeleteOrder) {
    auto logger = spdlog::stdout_color_mt("test_logger_delete_order");

    MarketDataPublisher publisher("239.1.3.37", 12345, "127.0.0.1", logger);
    publisher.publish_delete_order(1);

    md::delete_order msg;

    int bytes = recvfrom(read_sock, &msg, sizeof(md::delete_order), 0, (struct sockaddr *)&addr, (socklen_t*) &addrlen);
    if (bytes == -1) {
        throw std::runtime_error("Failed to receive message");
    }

    EXPECT_EQ(msg.header.magic_number, md::MAGIC_NUMBER);
    EXPECT_EQ(msg.header.length, sizeof(md::delete_order));

    EXPECT_EQ(msg.order_id, 1);

}

TEST_F(MarketDataPublisherTest, ModifyOrder) {
    auto logger = spdlog::stdout_color_mt("test_logger_modify_order");

    MarketDataPublisher publisher("239.1.3.37", 12345, "127.0.0.1", logger);

    publisher.publish_modify_order(1, md::SIDE::BUY, 10, 50);

    md::modify_order msg;

    int bytes = recvfrom(read_sock, &msg, sizeof(md::modify_order), 0, (struct sockaddr *)&addr, (socklen_t*) &addrlen);
    if (bytes == -1) {
        throw std::runtime_error("Failed to receive message");
    }

    EXPECT_EQ(msg.header.magic_number, md::MAGIC_NUMBER);
    EXPECT_EQ(msg.header.length, sizeof(md::modify_order));

    EXPECT_EQ(msg.order_id, 1);
    EXPECT_EQ(msg.side, md::SIDE::BUY);
    EXPECT_EQ(msg.quantity, 10);
    EXPECT_EQ(msg.price, 50);
}

TEST_F(MarketDataPublisherTest, Trade) {
    auto logger = spdlog::stdout_color_mt("test_logger_trade");

    MarketDataPublisher publisher("239.1.3.37", 12345, "127.0.0.1", logger);

    publisher.queue_trade(1, 10, 50);

    publisher.publish_queued_trades(0, md::SIDE::BUY);

    char buf[1500];
    int bytes = recvfrom(read_sock, buf, sizeof(buf), 0, (struct sockaddr *)&addr, (socklen_t*) &addrlen);
    if (bytes == -1) {
        throw std::runtime_error("Failed to receive message");
    }

    EXPECT_EQ(bytes, sizeof(md::trade) + sizeof(md::trade_summary));

    md::trade_summary* trade_summary = reinterpret_cast<md::trade_summary*>(buf);
    md::trade* trade = reinterpret_cast<md::trade*>(buf + sizeof(md::trade_summary));

    EXPECT_EQ(trade_summary->header.magic_number, md::MAGIC_NUMBER);
    EXPECT_EQ(trade_summary->header.length, sizeof(md::trade_summary));
    EXPECT_EQ(trade_summary->last_price, 50);
    EXPECT_EQ(trade_summary->total_quantity, 10);

    EXPECT_EQ(trade->header.magic_number, md::MAGIC_NUMBER);
    EXPECT_EQ(trade->header.length, sizeof(md::trade));
    EXPECT_EQ(trade->order_id, 1);
    EXPECT_EQ(trade->price, 50);
    EXPECT_EQ(trade->quantity, 10);
}

