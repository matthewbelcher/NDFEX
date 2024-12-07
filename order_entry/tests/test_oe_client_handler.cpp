
#include "md_protocol.H"
#include "oe_client_handler.H"

#include <gtest/gtest.h>
#include <cstring>

using namespace ndfex;
using namespace oe;

class MockOrderHandler {
public:
    void new_order(uint64_t exch_order_id, md::SIDE side, uint32_t quantity, uint32_t price, uint8_t flags) {}
    void delete_order(uint64_t exch_order_id) {}
    void modify_order(uint64_t exch_order_id, md::SIDE side, uint32_t quantity, uint32_t price) {}


};

TEST(OEClientHandlerTest, LoginRejectBadUsername) {
    std::unordered_map<std::string, user_info> users;
    auto& user = users["good"];
    user.username = "good";
    user.password = "password";
    user.client_id = 1;

    ClientHandler<MockOrderHandler> handler(users);

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

TEST(OEClientHandlerTest, LoginRejectBadPassword) {
    std::unordered_map<std::string, user_info> users;
    auto& user = users["good"];
    user.username = "good";
    user.password = "password";
    user.client_id = 1;

    ClientHandler<MockOrderHandler> handler(users);

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

TEST(OEClientHandlerTest, LoginRejectAlreadyLoggedIn) {
    std::unordered_map<std::string, user_info> users;
    auto& user = users["good"];
    user.username = "good";
    user.password = "password";
    user.client_id = 1;

    ClientHandler<MockOrderHandler> handler(users);

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
    handler.on_login(fd[1], msg);

    // read the response
    read(fd2[0], &response, sizeof(login_response));

    EXPECT_EQ(response.status, static_cast<uint8_t>(LOGIN_STATUS::DUPLICATE_LOGIN));

    // check error on the first socket
    error_message err_msg;
    read(fd[0], &err_msg, sizeof(error_message));

    EXPECT_EQ(err_msg.header.msg_type, static_cast<uint8_t>(MSG_TYPE::ERROR));
    EXPECT_EQ(err_msg.error_message_length, 15);
    EXPECT_EQ(std::string(reinterpret_cast<char*>(err_msg.error_message), 15), "Duplicate login");
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}