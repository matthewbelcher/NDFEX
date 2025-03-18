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

    // set socket to non-blocking
    int flags = fcntl(sock_fd, F_GETFL, 0);
    if (flags == -1) {
        logger->error("Failed to get socket flags: {}", strerror(errno));
        return false;
    }

    if (fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        logger->error("Failed to set socket to non-blocking: {}", strerror(errno));
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
    while (len == -1) {
        // wait for login response
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            len = read(sock_fd, &response, sizeof(response));
            continue;
        } else {
            logger->error("Failed to read login response: {}", strerror(errno));
            return false;
        }
    }

    if (response.status != static_cast<uint8_t>(oe::LOGIN_STATUS::SUCCESS)) {
        logger->error("Login failed with status: {}", response.status);
        return false;
    }

    session_id = response.session_id;

    logger->info("Logged in with session id: {} {}", session_id, sock_fd);
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
    char buf[1500];
    ssize_t len = read(sock_fd, buf, sizeof(buf));
    if (len == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        logger->error("Failed to read message header: {} {}", strerror(errno), sock_fd);
        return;
    }

    buffer.insert(buffer.end(), buf, buf + len);
    if (buffer.size() < sizeof(oe::oe_response_header)) {
        return;
    }

    while (buffer.size() >= sizeof(oe::oe_response_header)) {
        oe::oe_response_header header = *reinterpret_cast<oe::oe_response_header*>(buffer.data());

        logger->info("Received exch msg: {} {}", (int) header.msg_type, header.seq_num);

        switch (header.msg_type) {
            case static_cast<uint8_t>(oe::MSG_TYPE::ACK): {
                if (buffer.size() < sizeof(oe::order_ack)) {
                    return;
                }

                oe::order_ack ack = *reinterpret_cast<oe::order_ack*>(buffer.data());
                logger->info("Received ack for order id: {} {}", ack.order_id, ack.exch_order_id);
                break;
            }
            case static_cast<uint8_t>(oe::MSG_TYPE::REJECT): {
                if (buffer.size() < sizeof(oe::order_reject)) {
                    return;
                }
                oe::order_reject reject = *reinterpret_cast<oe::order_reject*>(buffer.data());

                logger->warn("Received reject for order id: {} reason: {}", reject.order_id, static_cast<int>(reject.reject_reason));
                break;
            }
            case static_cast<uint8_t>(oe::MSG_TYPE::CLOSE): {
                if (buffer.size() < sizeof(oe::order_closed)) {
                    return;
                }
                oe::order_closed closed = *reinterpret_cast<oe::order_closed*>(buffer.data());
                logger->info("Received close message for order id: {}", closed.order_id);
                break;
            }
            case static_cast<uint8_t>(oe::MSG_TYPE::FILL): {
                if (buffer.size() < sizeof(oe::order_fill)) {
                    return;
                }
                oe::order_fill fill = *reinterpret_cast<oe::order_fill*>(buffer.data());
                logger->info("Received fill for order id: {} quantity: {} price: {}", fill.order_id, fill.quantity, fill.price);
                break;
            }
            case static_cast<uint8_t>(oe::MSG_TYPE::ERROR): {
                if (buffer.size() < sizeof(oe::error_message)) {
                    return;
                }
                oe::error_message err = *reinterpret_cast<oe::error_message*>(buffer.data());
                logger->error("Received error message: {}", reinterpret_cast<char*>(err.error_message));
                throw std::runtime_error("Received error message " + std::string(reinterpret_cast<char*>(err.error_message)));
                break;
            }
            default:
                logger->warn("Unknown message type: {}", header.msg_type);
                throw std::runtime_error("Unknown message type " + std::to_string(header.msg_type));
                break;
        }
        buffer.erase(buffer.begin(), buffer.begin() + header.length);
    }
}
} // namespace ndfex::bots