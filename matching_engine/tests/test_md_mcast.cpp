#include "../md_mcast.H"

#include <gtest/gtest.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <iostream>
#include <poll.h>
#include <cstdlib>
#include <cstring>
#include <cerrno>

using namespace ndfex;

class MarketDataPublisherTest : public ::testing::Test {

protected:
    void SetUp() override {
        mcast_ip = get_env_or_default("NDFEX_TEST_MCAST_IP", "239.0.0.1");
        mcast_iface = get_env_or_default("NDFEX_TEST_MCAST_IFACE", "127.0.0.1");
        read_sock = socket(AF_INET, SOCK_DGRAM, 0);

        int reuse = 1;
        if (setsockopt(read_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            throw std::runtime_error("Failed to set SO_REUSEADDR");
        }

        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(0);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(read_sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            throw std::runtime_error(std::string("Failed to bind read socket: ") + strerror(errno));
        }
        struct sockaddr_in bound_addr;
        socklen_t bound_len = sizeof(bound_addr);
        if (getsockname(read_sock, reinterpret_cast<struct sockaddr*>(&bound_addr), &bound_len) < 0) {
            throw std::runtime_error(std::string("Failed to get bound port: ") + strerror(errno));
        }
        mcast_port = ntohs(bound_addr.sin_port);

        // set read_sock to non-blocking
        int flags = fcntl(read_sock, F_GETFL, 0);
        if (flags == -1) {
            throw std::runtime_error("Failed to get socket flags");
        }
        fcntl(read_sock, F_SETFL, flags | O_NONBLOCK);

        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = inet_addr(mcast_ip.c_str());
        mreq.imr_interface.s_addr = inet_addr(mcast_iface.c_str());
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
    std::string mcast_ip;
    std::string mcast_iface;
    uint16_t mcast_port{0};

    static std::string get_env_or_default(const char* name, const char* fallback) {
        const char* value = std::getenv(name);
        return value ? value : fallback;
    }

    int recv_with_timeout(void* buffer, size_t length) {
        struct pollfd pfd;
        pfd.fd = read_sock;
        pfd.events = POLLIN;
        int ready = poll(&pfd, 1, 1000);
        if (ready <= 0) {
            return -1;
        }
        return recvfrom(read_sock, buffer, length, 0, (struct sockaddr *)&addr, (socklen_t*) &addrlen);
    }
};

TEST_F(MarketDataPublisherTest, NewOrder) {

    auto logger = spdlog::stdout_color_mt("test_logger_new_order");
    MarketDataPublisher publisher(mcast_ip, mcast_port, mcast_iface, logger);
    publisher.publish_new_order(1, 0, md::SIDE::BUY, 10, 50, 0);

    md::new_order msg;

    int bytes = recv_with_timeout(&msg, sizeof(md::new_order));
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

    MarketDataPublisher publisher(mcast_ip, mcast_port, mcast_iface, logger);
    publisher.publish_delete_order(1);

    md::delete_order msg;

    int bytes = recv_with_timeout(&msg, sizeof(md::delete_order));
    if (bytes == -1) {
        throw std::runtime_error("Failed to receive message");
    }

    EXPECT_EQ(msg.header.magic_number, md::MAGIC_NUMBER);
    EXPECT_EQ(msg.header.length, sizeof(md::delete_order));

    EXPECT_EQ(msg.order_id, 1);

}

TEST_F(MarketDataPublisherTest, ModifyOrder) {
    auto logger = spdlog::stdout_color_mt("test_logger_modify_order");

    MarketDataPublisher publisher(mcast_ip, mcast_port, mcast_iface, logger);

    publisher.publish_modify_order(1, md::SIDE::BUY, 10, 50);

    md::modify_order msg;

    int bytes = recv_with_timeout(&msg, sizeof(md::modify_order));
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

    MarketDataPublisher publisher(mcast_ip, mcast_port, mcast_iface, logger);

    publisher.queue_trade(1, 10, 50);

    publisher.publish_queued_trades(0, md::SIDE::BUY);

    char buf[1500];
    int bytes = recv_with_timeout(buf, sizeof(buf));
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

