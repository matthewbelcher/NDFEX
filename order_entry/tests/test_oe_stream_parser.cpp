#include "../oe_stream_parser.H"
#include "../oe_protocol.H"
#include "../../market_data/md_protocol.H"

#include <vector>
#include <gtest/gtest.h>
#include <unistd.h> // for pipe
#include <fcntl.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

using namespace ndfex;
using namespace oe;

class MockHandler {
public:
  std::vector<uint64_t> new_orders;
  void on_new_order(int sock_fd, const new_order& msg) {
    new_orders.push_back(msg.order_id);
  }
  void on_delete_order(int sock_fd, const delete_order& msg) {}
  void on_modify_order(int sock_fd, const modify_order& msg) {}
  void on_login(int sock_fd, const login& msg) {}

};

TEST(OEStreamParserTest, NewOrder) {
    MockHandler handler;
    auto logger = spdlog::stdout_color_mt("test_logger_new_order");
    StreamParser<MockHandler> parser(handler, logger);
    oe_request_header header = {sizeof(new_order), static_cast<uint8_t>(MSG_TYPE::NEW_ORDER), 0, 0};

    new_order msg = {header, 1, 0, md::SIDE::BUY, 10, 50, 0};
    parser.parse(0, reinterpret_cast<char*>(&msg), sizeof(new_order));

    EXPECT_EQ(handler.new_orders.size(), 1);
    EXPECT_EQ(handler.new_orders[0], 1);
}

TEST(OEStreamParserTest, NewOrderPartial) {
    MockHandler handler;
    auto logger = spdlog::stdout_color_mt("test_logger_new_order_partial");
    StreamParser<MockHandler> parser(handler, logger);
    oe_request_header header = {sizeof(new_order), static_cast<uint8_t>(MSG_TYPE::NEW_ORDER), 0, 0};
    new_order msg = {header, 1, 0, md::SIDE::BUY, 10, 50, 0};
    std::string buf(reinterpret_cast<char*>(&msg), sizeof(new_order));
    parser.parse(0, buf.data(), 1);
    parser.parse(0, buf.data() + 1, buf.size() - 1);

    EXPECT_EQ(handler.new_orders.size(), 1);
    EXPECT_EQ(handler.new_orders[0], 1);
}

TEST(OEStreamParserTest, NewOrderMultiple) {
    MockHandler handler;
    auto logger = spdlog::stdout_color_mt("test_logger_new_order_multiple");
    StreamParser<MockHandler> parser(handler, logger);
    oe_request_header header = {sizeof(new_order), static_cast<uint8_t>(MSG_TYPE::NEW_ORDER), 0, 0};
    new_order msg = {header, 1, 0, md::SIDE::BUY, 10, 50, 0};
    std::string buf(reinterpret_cast<char*>(&msg), sizeof(new_order));
    parser.parse(0, buf.data(), buf.size());
    parser.parse(0, buf.data(), buf.size());

    EXPECT_EQ(handler.new_orders.size(), 2);
    EXPECT_EQ(handler.new_orders[0], 1);
    EXPECT_EQ(handler.new_orders[1], 1);
}

TEST(OEStreamParserTest, NewOrderMultipleInSameBuffer) {
    MockHandler handler;
    auto logger = spdlog::stdout_color_mt("test_logger_new_order_multiple_same_buffer");
    StreamParser<MockHandler> parser(handler, logger);
    oe_request_header header = {sizeof(new_order), static_cast<uint8_t>(MSG_TYPE::NEW_ORDER), 0, 0};
    new_order msg = {header, 1, 0, md::SIDE::BUY, 10, 50, 0};
    std::string buf(reinterpret_cast<char*>(&msg), sizeof(new_order));
    buf += buf;

    parser.parse(0, buf.data(), buf.size());

    EXPECT_EQ(handler.new_orders.size(), 2);
    EXPECT_EQ(handler.new_orders[0], 1);
    EXPECT_EQ(handler.new_orders[1], 1);
}

TEST(OEStreamParserTest, ErrorOnUnknownType) {
    MockHandler handler;
    auto logger = spdlog::stdout_color_mt("test_logger_error_on_unknown_type");
    StreamParser<MockHandler> parser(handler, logger);
    oe_request_header header = {sizeof(new_order), 0, 0, 0};
    new_order msg = {header, 1, 0, md::SIDE::BUY, 10, 50, 0};
    std::string buf(reinterpret_cast<char*>(&msg), sizeof(new_order));

    int fds[2];
    pipe(fds); // create a pair of connected file descriptors

    parser.parse(fds[1], buf.data(), buf.size());

    EXPECT_EQ(handler.new_orders.size(), 0);

    // read the error message from the pipe
    error_message err_msg;
    read(fds[0], reinterpret_cast<char*>(&err_msg), sizeof(error_message));
    EXPECT_EQ(err_msg.header.length, sizeof(error_message));
    EXPECT_EQ(err_msg.header.msg_type, static_cast<uint8_t>(MSG_TYPE::ERROR));
    //EXPECT_EQ(err_msg.error_code, static_cast<uint8_t>(ERROR_CODE::UNKNOWN_MSG_TYPE));
    EXPECT_EQ(std::string(reinterpret_cast<char*>(err_msg.error_message), err_msg.error_message_length), "Unknown message type: 0");

    // check the parser closed the socket
    int flags = fcntl(fds[1], F_GETFL, 0);
    EXPECT_EQ(flags, -1);

    close(fds[0]);
    close(fds[1]);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}