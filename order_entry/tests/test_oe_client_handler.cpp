#include "../market_data/md_protocol.H"
#include "oe_client_handler.H"
#include "oe_validator.H"
#include "spsc_oe_queue.H"

#include <gtest/gtest.h>
#include <cstring>
#include <spdlog/sinks/stdout_color_sinks.h>

using namespace ndfex;
using namespace oe;

class ValidateMatchingEngineQueue {
public:
    ValidateMatchingEngineQueue(SPSCOEQueue& queue) : queue(queue) {}

    void operator()(MSG_TYPE msg_type, uint64_t exch_order_id, uint32_t symbol, uint32_t seq_num, md::SIDE side, uint32_t quantity, uint32_t price, uint8_t flags) {
        if (queue.front()) {
            oe_payload& payload = *queue.front();
            queue.pop();

            EXPECT_EQ(payload.msg_type, msg_type);
            EXPECT_EQ(payload.exch_order_id, exch_order_id);
            EXPECT_EQ(payload.symbol, symbol);
            EXPECT_EQ(payload.client_seq, seq_num);
            EXPECT_EQ(payload.side, side);
            EXPECT_EQ(payload.quantity, quantity);
            EXPECT_EQ(payload.price, price);
            EXPECT_EQ(payload.flags, flags);
        } else {
            FAIL() << "Queue is empty";
        }
    }
private:
    SPSCOEQueue& queue;
};

class OEClientHandlerTest : public ::testing::Test {

protected:

    void SetUp() override {
        auto& user = users["good"];
        user.username = "good";
        user.password = "password";
        user.client_id = 1;

        symbol_definition symbol;
        symbol.symbol = 1;
        symbol.tick_size = 10;
        symbol.min_lot_size = 1;
        symbol.max_lot_size = 100;
        symbol.max_price = 1000;
        symbol.min_price = 1;

        symbols[1] = symbol;

        symbol_definition symbol2;
        symbol2.symbol = 2;
        symbol2.tick_size = 20;
        symbol2.min_lot_size = 5;
        symbol2.max_lot_size = 50;
        symbol2.max_price = 2000;
        symbol2.min_price = -100;

        symbols[2] = symbol2;
    }

    void TearDown() override {
    }

    std::shared_ptr<spdlog::logger> logger = spdlog::default_logger();

    std::unordered_map<std::string, user_info> users;
    std::unordered_map<uint32_t, symbol_definition> symbols;

    OrderEntryValidator validator{symbols, logger};

    SPSCOEQueue to_matching_engine{1000};
    SPSCOEQueue from_matching_engine{1000};
};


TEST_F(OEClientHandlerTest, LoginRejectBadUsername) {
    ClientHandler handler(validator, to_matching_engine, from_matching_engine, users, logger);

    login msg;
    msg.header.length = sizeof(login);
    msg.header.msg_type = static_cast<uint8_t>(MSG_TYPE::LOGIN);
    msg.header.seq_num = 0;
    msg.header.client_id = 1;
    std::memcpy(msg.username, "bad", 3);
    std::memcpy(msg.password, "password", 8);

    int fd[2];
    pipe(fd);

    handler.on_login(fd[1], msg);

    // read the response
    login_response response;
    read(fd[0], &response, sizeof(login_response));

    EXPECT_EQ(response.status, static_cast<uint8_t>(LOGIN_STATUS::INVALID_USERNAME));
}

TEST_F(OEClientHandlerTest, LoginRejectBadPassword) {
    ClientHandler handler(validator, to_matching_engine, from_matching_engine, users, logger);

    login msg;
    msg.header.length = sizeof(login);
    msg.header.msg_type = static_cast<uint8_t>(MSG_TYPE::LOGIN);
    msg.header.seq_num = 0;
    msg.header.client_id = 1;
    std::memcpy(msg.username, "good", 4);
    std::memcpy(msg.password, "bad", 3);

    int fd[2];
    pipe(fd);

    handler.on_login(fd[1], msg);

    // read the response
    login_response response;
    read(fd[0], &response, sizeof(login_response));

    EXPECT_EQ(response.status, static_cast<uint8_t>(LOGIN_STATUS::INVALID_PASSWORD));
}

TEST_F(OEClientHandlerTest, LoginRejectAlreadyLoggedIn) {
    ClientHandler handler(validator, to_matching_engine, from_matching_engine, users, logger);

    login msg;
    msg.header.length = sizeof(login);
    msg.header.msg_type = static_cast<uint8_t>(MSG_TYPE::LOGIN);
    msg.header.seq_num = 0;
    msg.header.client_id = 1;
    std::memcpy(msg.username, "good", 4);
    std::memcpy(msg.password, "password", 8);

    int fd[2];
    pipe(fd);

    handler.on_login(fd[1], msg);

    // read the response
    login_response response;
    read(fd[0], &response, sizeof(login_response));

    EXPECT_EQ(response.status, static_cast<uint8_t>(LOGIN_STATUS::SUCCESS));

    // login again
    int fd2[2];
    pipe(fd2);
    handler.on_login(fd2[1], msg);

    // read the response
    read(fd2[0], &response, sizeof(login_response));

    EXPECT_EQ(response.status, static_cast<uint8_t>(LOGIN_STATUS::SESSION_ALREADY_ACTIVE));

    // check error on the first socket
    error_message err_msg;
    read(fd[0], &err_msg, sizeof(error_message));

    EXPECT_EQ(err_msg.header.msg_type, static_cast<uint8_t>(MSG_TYPE::ERROR));
    EXPECT_EQ(err_msg.error_message_length, 15);
    EXPECT_EQ(err_msg.error_code, static_cast<uint8_t>(REJECT_REASON::DUPLICATE_LOGIN));
    EXPECT_EQ(std::string(reinterpret_cast<char*>(err_msg.error_message), 15), "Duplicate login");
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}