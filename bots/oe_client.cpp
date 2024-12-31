#include "oe_client.H"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace ndfex::bots {

OEClient::OEClient(user_info user, std::string ip, uint16_t port, std::shared_ptr<spdlog::logger> logger)
    : user(user), ip(ip), port(port), logger(logger) {}

bool OEClient::login() {
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        logger->error("Failed to create socket: {}", strerror(errno));
        return false;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    if (connect(sock_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        logger->error("Failed to connect to server: {}", strerror(errno));
        return false;
    }

    oe::login login_msg;
    login_msg.header.length = sizeof(oe::login);
    login_msg.header.version = oe::OE_PROTOCOL_VERSION;
    login_msg.header.msg_type = static_cast<uint8_t>(oe::MSG_TYPE::LOGIN);
    login_msg.header.seq_num = client_seq++;
    login_msg.header.client_id = user.client_id;
    strncpy(reinterpret_cast<char*>(login_msg.username), user.username.c_str(), sizeof(login_msg.username));
    strncpy(reinterpret_cast<char*>(login_msg.password), user.password.c_str(), sizeof(login_msg.password));

    ssize_t len = write(sock_fd, &login_msg, sizeof(login_msg));
    if (len == -1) {
        logger->error("Failed to send login message: {}", strerror(errno));
        return false;
    }

    // read login response
    oe::login_response response;
    len = read(sock_fd, &response, sizeof(response));
    if (len == -1) {
        logger->error("Failed to read login response: {}", strerror(errno));
        return false;
    }

    if (response.status != static_cast<uint8_t>(oe::LOGIN_STATUS::SUCCESS)) {
        logger->error("Login failed with status: {}", response.status);
        return false;
    }

    session_id = response.session_id;
    return true;
}

void OEClient::send_order(uint32_t symbol, uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags) {
    oe::new_order order;
    order.header.length = sizeof(oe::new_order);
    order.header.msg_type = static_cast<uint8_t>(oe::MSG_TYPE::NEW_ORDER);
    order.header.seq_num = client_seq++;
    order.header.client_id = user.client_id;
    order.header.session_id = session_id;
    order.header.version = oe::OE_PROTOCOL_VERSION;

    order.symbol = symbol;
    order.order_id = order_id;
    order.side = side;
    order.quantity = quantity;
    order.price = price;
    order.flags = flags;

    ssize_t len = write(sock_fd, &order, sizeof(order));
    if (len == -1) {
        logger->error("Failed to send order: {}", strerror(errno));
    }

}

void OEClient::cancel_order(uint64_t order_id) {
    oe::delete_order order;
    order.header.length = sizeof(oe::delete_order);
    order.header.msg_type = static_cast<uint8_t>(oe::MSG_TYPE::DELETE_ORDER);
    order.header.seq_num = client_seq++;
    order.header.client_id = user.client_id;
    order.header.version = oe::OE_PROTOCOL_VERSION;
    order.header.session_id = session_id;
    order.order_id = order_id;

    ssize_t len = write(sock_fd, &order, sizeof(order));
    if (len == -1) {
        logger->error("Failed to send cancel order: {}", strerror(errno));
    }

}

void OEClient::process() {

    // read messages from the server
    oe::oe_response_header header;
    ssize_t len = read(sock_fd, &header, sizeof(header));
    if (len == -1) {
        logger->error("Failed to read message header: {}", strerror(errno));
        return;
    }

    switch (header.msg_type) {
        case static_cast<uint8_t>(oe::MSG_TYPE::ACK): {
            oe::order_ack ack;
            len = read(sock_fd, &ack, sizeof(ack));
            if (len == -1) {
                logger->error("Failed to read ack message: {}", strerror(errno));
                return;
            }

            logger->info("Received ack for order id: {}", ack.order_id);
            break;
        }
        case static_cast<uint8_t>(oe::MSG_TYPE::REJECT): {
            oe::order_reject reject;
            len = read(sock_fd, &reject, sizeof(reject));
            if (len == -1) {
                logger->error("Failed to read reject message: {}", strerror(errno));
                return;
            }
            logger->warn("Received reject for order id: {} reason: {}", reject.order_id, static_cast<int>(reject.reject_reason));
            break;
        }
        case static_cast<uint8_t>(oe::MSG_TYPE::CLOSE): {
            oe::order_closed closed;
            len = read(sock_fd, &closed, sizeof(closed));
            logger->info("Received close message for order id: {}", closed.order_id);
            break;
        }
        case static_cast<uint8_t>(oe::MSG_TYPE::FILL): {
            oe::order_fill fill;
            len = read(sock_fd, &fill, sizeof(fill));
            if (len == -1) {
                logger->error("Failed to read fill message: {}", strerror(errno));
                return;
            }
            logger->info("Received fill for order id: {} quantity: {} price: {}", fill.order_id, fill.quantity, fill.price);
            break;
        }
        case static_cast<uint8_t>(oe::MSG_TYPE::ERROR): {
            oe::error_message err;
            len = read(sock_fd, &err, sizeof(err));
            if (len == -1) {
                logger->error("Failed to read error message: {}", strerror(errno));
                return;
            }
            logger->error("Received error message: {}", reinterpret_cast<char*>(err.error_message));
            break;
        }
        default:
            logger->warn("Unknown message type: {}", header.msg_type);
            break;
    }
}
} // namespace ndfex::bots