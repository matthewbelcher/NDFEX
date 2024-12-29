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

    uint64_t operator()(MSG_TYPE msg_type, uint64_t, uint32_t symbol, uint32_t seq_num, md::SIDE side, uint32_t quantity, uint32_t price, uint8_t flags) {
        uint64_t exch_order_id = 0;
        if (queue.front()) {
            oe_payload& payload = *queue.front();
            queue.pop();

            EXPECT_EQ(payload.msg_type, msg_type);
            EXPECT_EQ(payload.symbol, symbol);
            EXPECT_EQ(payload.client_seq, seq_num);
            EXPECT_EQ(payload.side, side);
            EXPECT_EQ(payload.quantity, quantity);
            EXPECT_EQ(payload.price, price);
            EXPECT_EQ(payload.flags, flags);

            exch_order_id = payload.exch_order_id;
        }
        EXPECT_NE(exch_order_id, 0);
        return exch_order_id;
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
    std::memcpy(msg.username, "bad\0", 4);
    std::memcpy(msg.password, "password\0", 9);

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
    std::memcpy(msg.username, "good\0", 5);
    std::memcpy(msg.password, "bad\0", 4);

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
    std::memcpy(msg.username, "good\0", 5);
    std::memcpy(msg.password, "password\0", 9);

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

TEST_F(OEClientHandlerTest, SendOrderWithoutLoggingIn) {
    ClientHandler handler(validator, to_matching_engine, from_matching_engine, users, logger);

    new_order msg;
    msg.header.length = sizeof(new_order);
    msg.header.msg_type = static_cast<uint8_t>(MSG_TYPE::NEW_ORDER);
    msg.header.seq_num = 0;
    msg.header.client_id = 1;
    msg.order_id = 1;
    msg.symbol = 1;
    msg.side = md::SIDE::BUY;
    msg.quantity = 10;
    msg.price = 50;
    msg.flags = 0;

    int fd[2];
    pipe(fd);

    handler.on_new_order(fd[1], msg);

    /*// read the response
    order_reject reject;
    read(fd[0], &reject, sizeof(order_reject));

    EXPECT_EQ(reject.header.msg_type, static_cast<uint8_t>(MSG_TYPE::REJECT));
    EXPECT_EQ(reject.reject_reason, static_cast<uint8_t>(REJECT_REASON::UNKNOWN_SESSION_ID));*/

    // reject is put into the queue
    ValidateMatchingEngineQueue validate(to_matching_engine);
    validate(MSG_TYPE::REJECT, 1, 0, 0, md::SIDE::BUY, 0, 0, static_cast<uint8_t>(REJECT_REASON::UNKNOWN_SESSION_ID));
}

TEST_F(OEClientHandlerTest, SendOrderWithWrongSessionID) {
    ClientHandler handler(validator, to_matching_engine, from_matching_engine, users, logger);

    login msg;
    msg.header.length = sizeof(login);
    msg.header.msg_type = static_cast<uint8_t>(MSG_TYPE::LOGIN);
    msg.header.seq_num = 0;
    msg.header.client_id = 1;
    std::memcpy(msg.username, "good\0", 5);
    std::memcpy(msg.password, "password\0", 9);

    int fd[2];
    pipe(fd);

    handler.on_login(fd[1], msg);

    // read the response
    login_response response;
    read(fd[0], &response, sizeof(login_response));

    EXPECT_EQ(response.status, static_cast<uint8_t>(LOGIN_STATUS::SUCCESS));

    new_order msg2;
    msg2.header.length = sizeof(new_order);
    msg2.header.msg_type = static_cast<uint8_t>(MSG_TYPE::NEW_ORDER);
    msg2.header.seq_num = 0;
    msg2.header.client_id = 1;
    msg2.header.session_id = 0;
    msg2.order_id = 1;
    msg2.symbol = 1;
    msg2.side = md::SIDE::BUY;
    msg2.quantity = 10;
    msg2.price = 50;
    msg2.flags = 0;

    int fd2[2];
    pipe(fd2);

    handler.on_new_order(fd2[1], msg2);

    // read the response
    /*order_reject reject;
    read(fd2[0], &reject, sizeof(order_reject));

    EXPECT_EQ(reject.header.msg_type, static_cast<uint8_t>(MSG_TYPE::REJECT));
    EXPECT_EQ(reject.reject_reason, static_cast<uint8_t>(REJECT_REASON::UNKNOWN_SESSION_ID));*/

    // reject is put into the queue
    ValidateMatchingEngineQueue validate(to_matching_engine);
    validate(MSG_TYPE::REJECT, 1, 0, 0, md::SIDE::BUY, 0, 0, static_cast<uint8_t>(REJECT_REASON::UNKNOWN_SESSION_ID));
}

TEST_F(OEClientHandlerTest, SendOrder) {
    ClientHandler handler(validator, to_matching_engine, from_matching_engine, users, logger);

    login msg;
    msg.header.length = sizeof(login);
    msg.header.msg_type = static_cast<uint8_t>(MSG_TYPE::LOGIN);
    msg.header.seq_num = 0;
    msg.header.client_id = 1;
    std::memcpy(msg.username, "good\0", 5);
    std::memcpy(msg.password, "password\0", 9);

    int fd[2];
    pipe(fd);

    handler.on_login(fd[1], msg);

    // read the response
    login_response response;
    read(fd[0], &response, sizeof(login_response));

    EXPECT_EQ(response.status, static_cast<uint8_t>(LOGIN_STATUS::SUCCESS));

    new_order msg2;
    msg2.header.length = sizeof(new_order);
    msg2.header.msg_type = static_cast<uint8_t>(MSG_TYPE::NEW_ORDER);
    msg2.header.seq_num = 0;
    msg2.header.client_id = 1;
    msg2.header.session_id = response.session_id;
    msg2.order_id = 1;
    msg2.symbol = 1;
    msg2.side = md::SIDE::BUY;
    msg2.quantity = 10;
    msg2.price = 50;
    msg2.flags = 0;

    handler.on_new_order(fd[1], msg2);

    ValidateMatchingEngineQueue validate(to_matching_engine);
    validate(MSG_TYPE::NEW_ORDER, 1, 1, 0, md::SIDE::BUY, 10, 50, 0);
}

TEST_F(OEClientHandlerTest, SendOrderWithBadSymbol) {
    ClientHandler handler(validator, to_matching_engine, from_matching_engine, users, logger);

    login msg;
    msg.header.length = sizeof(login);
    msg.header.msg_type = static_cast<uint8_t>(MSG_TYPE::LOGIN);
    msg.header.seq_num = 0;
    msg.header.client_id = 1;
    std::memcpy(msg.username, "good\0", 5);
    std::memcpy(msg.password, "password\0", 9);

    int fd[2];
    pipe(fd);

    handler.on_login(fd[1], msg);

    // read the response
    login_response response;
    read(fd[0], &response, sizeof(login_response));

    EXPECT_EQ(response.status, static_cast<uint8_t>(LOGIN_STATUS::SUCCESS));

    new_order msg2;
    msg2.header.length = sizeof(new_order);
    msg2.header.msg_type = static_cast<uint8_t>(MSG_TYPE::NEW_ORDER);
    msg2.header.seq_num = 0;
    msg2.header.client_id = 1;
    msg2.header.session_id = response.session_id;
    msg2.order_id = 1;
    msg2.symbol = 3;
    msg2.side = md::SIDE::BUY;
    msg2.quantity = 10;
    msg2.price = 50;
    msg2.flags = 0;

    handler.on_new_order(fd[1], msg2);

    ValidateMatchingEngineQueue validate(to_matching_engine);
    validate(MSG_TYPE::REJECT, 1, 0, 0, md::SIDE::BUY, 0, 0, static_cast<uint8_t>(REJECT_REASON::UNKNOWN_SYMBOL));
}

TEST_F(OEClientHandlerTest, SendAnOrderAndThenSendADuplicateLogin) {
    ClientHandler handler(validator, to_matching_engine, from_matching_engine, users, logger);

    login msg;
    msg.header.length = sizeof(login);
    msg.header.msg_type = static_cast<uint8_t>(MSG_TYPE::LOGIN);
    msg.header.seq_num = 0;
    msg.header.client_id = 1;
    std::memcpy(msg.username, "good\0", 5);
    std::memcpy(msg.password, "password\0", 9);

    int fd[2];
    pipe(fd);

    handler.on_login(fd[1], msg);

    // read the response
    login_response response;
    read(fd[0], &response, sizeof(login_response));

    EXPECT_EQ(response.status, static_cast<uint8_t>(LOGIN_STATUS::SUCCESS));

    new_order msg2;
    msg2.header.length = sizeof(new_order);
    msg2.header.msg_type = static_cast<uint8_t>(MSG_TYPE::NEW_ORDER);
    msg2.header.seq_num = 0;
    msg2.header.client_id = 1;
    msg2.header.session_id = response.session_id;
    msg2.order_id = 1;
    msg2.symbol = 1;
    msg2.side = md::SIDE::BUY;
    msg2.quantity = 10;
    msg2.price = 50;
    msg2.flags = 0;

    handler.on_new_order(fd[1], msg2);

    ValidateMatchingEngineQueue validate(to_matching_engine);
    validate(MSG_TYPE::NEW_ORDER, 1, 1, 0, md::SIDE::BUY, 10, 50, 0);

    // login again
    int fd2[2];
    pipe(fd2);
    handler.on_login(fd2[1], msg);

    // read the response
    read(fd2[0], &response, sizeof(login_response));

    EXPECT_EQ(response.status, static_cast<uint8_t>(LOGIN_STATUS::SESSION_ALREADY_ACTIVE));

    // check order cancel was sent
    validate(MSG_TYPE::DELETE_ORDER, 1, 1, 0, md::SIDE::BUY, 0, 0, 0);
}

TEST_F(OEClientHandlerTest, MultipleOrderCancelModify) {
    ClientHandler handler(validator, to_matching_engine, from_matching_engine, users, logger);

    login msg;
    msg.header.length = sizeof(login);
    msg.header.msg_type = static_cast<uint8_t>(MSG_TYPE::LOGIN);
    msg.header.seq_num = 0;
    msg.header.client_id = 1;
    std::memcpy(msg.username, "good\0", 5);
    std::memcpy(msg.password, "password\0", 9);

    int fd[2];
    pipe(fd);

    handler.on_login(fd[1], msg);

    // read the response
    login_response response;
    read(fd[0], &response, sizeof(login_response));

    EXPECT_EQ(response.status, static_cast<uint8_t>(LOGIN_STATUS::SUCCESS));

    new_order msg2;
    msg2.header.length = sizeof(new_order);
    msg2.header.msg_type = static_cast<uint8_t>(MSG_TYPE::NEW_ORDER);
    msg2.header.seq_num = 0;
    msg2.header.client_id = 1;
    msg2.header.session_id = response.session_id;
    msg2.order_id = 1;
    msg2.symbol = 1;
    msg2.side = md::SIDE::BUY;
    msg2.quantity = 10;
    msg2.price = 50;
    msg2.flags = 0;

    handler.on_new_order(fd[1], msg2);

    ValidateMatchingEngineQueue validate(to_matching_engine);
    uint64_t exch_order_id = validate(MSG_TYPE::NEW_ORDER, 1, 1, 0, md::SIDE::BUY, 10, 50, 0);

    modify_order msg3;
    msg3.header.length = sizeof(modify_order);
    msg3.header.msg_type = static_cast<uint8_t>(MSG_TYPE::MODIFY_ORDER);
    msg3.header.seq_num = 1;
    msg3.header.client_id = 1;
    msg3.header.session_id = response.session_id;
    msg3.order_id = 1;
    msg3.side = md::SIDE::BUY;
    msg3.quantity = 20;
    msg3.price = 60;

    handler.on_modify_order(fd[1], msg3);

    validate(MSG_TYPE::MODIFY_ORDER, 1, 1, 1, md::SIDE::BUY, 20, 60, 0);

    delete_order msg4;
    msg4.header.length = sizeof(delete_order);
    msg4.header.msg_type = static_cast<uint8_t>(MSG_TYPE::DELETE_ORDER);
    msg4.header.seq_num = 2;
    msg4.header.client_id = 1;
    msg4.header.session_id = response.session_id;
    msg4.order_id = 1;

    handler.on_delete_order(fd[1], msg4);

    validate(MSG_TYPE::DELETE_ORDER, 1, 1, 2, md::SIDE::BUY, 0, 0, 0);

    // send close back
    from_matching_engine.emplace(MSG_TYPE::CLOSE, exch_order_id, 1, 2, md::SIDE::BUY, 0, 0, 0);
    handler.process(); // process the close

    // read the close response
    order_closed closed;
    read(fd[0], &closed, sizeof(order_closed));

    EXPECT_EQ(closed.header.msg_type, static_cast<uint8_t>(MSG_TYPE::CLOSE));
    EXPECT_EQ(closed.header.last_seq_num, 2);
    EXPECT_EQ(closed.order_id, 1);

    // try deleting again
    delete_order msg5;
    msg5.header.length = sizeof(delete_order);
    msg5.header.msg_type = static_cast<uint8_t>(MSG_TYPE::DELETE_ORDER);
    msg5.header.seq_num = 3;
    msg5.header.client_id = 1;
    msg5.header.session_id = response.session_id;
    msg5.order_id = 1;

    handler.on_delete_order(fd[1], msg5);

    // check order cancel was sent
    validate(MSG_TYPE::REJECT, 1, 0, 3, md::SIDE::BUY, 0, 0, static_cast<uint8_t>(REJECT_REASON::UKNOWN_ORDER_ID));
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}