#include "oe_client_handler.H"

namespace ndfex::oe {

ClientHandler::ClientHandler(
    OrderEntryValidator& validator,
    SPSCOEQueue& to_matching_engine,
    SPSCOEQueue& from_matching_engine,
    std::unordered_map<std::string, user_info>& users, std::shared_ptr<spdlog::logger> logger)
    : validator(validator),
      to_matching_engine(to_matching_engine),
      from_matching_engine(from_matching_engine),
      users(users),
      logger(logger) {}

void ClientHandler::on_login(int sock_fd, const login& msg) {
    logger->info("Received login request from client {} {}", msg.header.client_id, users.begin()->second.username);
    // validate username and password
    auto it = users.find(std::string(reinterpret_cast<const char*>(msg.username)));
    if (it == users.end()
        || it->second.password != std::string(reinterpret_cast<const char*>(msg.password))
        || it->second.client_id != msg.header.client_id) {

        logger->warn("Invalid login attempt from client {} {}", msg.header.client_id,
        std::string(reinterpret_cast<const char*>(msg.username)));

        // send login response with status = 1
        login_response response;
        response.header.length = sizeof(login_response);
        response.header.msg_type = static_cast<uint8_t>(MSG_TYPE::LOGIN_RESPONSE);
        response.header.seq_num = msg.header.seq_num;
        response.header.client_id = msg.header.client_id;

        if (it == users.end()) {
            response.status = static_cast<uint8_t>(LOGIN_STATUS::INVALID_USERNAME);
        } else if (it->second.password != std::string(reinterpret_cast<const char*>(msg.password))) {
            response.status = static_cast<uint8_t>(LOGIN_STATUS::INVALID_PASSWORD);
        } else {
            response.status = static_cast<uint8_t>(LOGIN_STATUS::INVALID_CLIENT_ID);
        }

        ssize_t len = write_msg(sock_fd, response);
        // don't care if this send fails since we are closing the socket anyway
        (void) len;

        shutdown(sock_fd, SHUT_RDWR);
        close(sock_fd);
        return;
    }

    // check if this client id is already logged in
    if (client_to_sock_fd.find(msg.header.client_id) != client_to_sock_fd.end()) {
        auto other_fd = client_to_sock_fd[msg.header.client_id];
        auto session = sock_to_session_info[other_fd];

        logger->warn("Duplicate login attempt from client {}", msg.header.client_id);

        // send login response with status = 2
        login_response response;
        response.header.length = sizeof(login_response);
        response.header.msg_type = static_cast<uint8_t>(MSG_TYPE::LOGIN_RESPONSE);
        response.header.seq_num = msg.header.seq_num;
        response.header.client_id = msg.header.client_id;
        response.status = static_cast<uint8_t>(LOGIN_STATUS::SESSION_ALREADY_ACTIVE);

        ssize_t len = write(sock_fd, &response, sizeof(login_response));
        if (len == -1) {
            log_network_send_error(sock_fd, len);
        }

        shutdown(sock_fd, SHUT_RDWR);
        close(sock_fd);

        // send an error message to the other socket
        error_message err_msg;
        err_msg.header.length = sizeof(error_message);
        err_msg.header.msg_type = static_cast<uint8_t>(MSG_TYPE::ERROR);
        err_msg.header.seq_num = session.last_seq_num;
        err_msg.header.client_id = session.client_id;
        err_msg.error_code = static_cast<uint8_t>(REJECT_REASON::DUPLICATE_LOGIN);
        strncpy(reinterpret_cast<char*>(err_msg.error_message), "Duplicate login", sizeof(err_msg.error_message));
        err_msg.error_message_length = strlen(reinterpret_cast<char*>(err_msg.error_message));

        len = write_msg(other_fd, err_msg);
        if (len == -1) {
            log_network_send_error(other_fd, len);
        }

        cancel_all_client_orders(msg.header.client_id);

        // close the other socket
        shutdown(other_fd, SHUT_RDWR);
        close(other_fd);
        return;
    }

    // generate session id
    uint64_t session_id = dist(gen);
    sock_to_session_info[sock_fd] = {session_id, msg.header.client_id, msg.header.seq_num};
    client_to_sock_fd[msg.header.client_id] = sock_fd;

    logger->info("Client {} logged in successfully with session id {}", msg.header.client_id, session_id);

    // send login response with status = 0
    login_response response;
    response.header.length = sizeof(login_response);
    response.header.msg_type = static_cast<uint8_t>(MSG_TYPE::LOGIN_RESPONSE);
    response.header.seq_num = msg.header.seq_num;
    response.header.client_id = msg.header.client_id;
    response.status = static_cast<uint8_t>(LOGIN_STATUS::SUCCESS);
    response.session_id = session_id;

    ssize_t len = write_msg(sock_fd, response);
    if (len == -1) {
        // login was successful but we could not send a response, something went wrong
        log_network_send_error(sock_fd, len);

        // close the socket
        cancel_all_client_orders(msg.header.client_id);
        shutdown(sock_fd, SHUT_RDWR);
        close(sock_fd);
    }
}

void ClientHandler::on_socket_closed(int sock_fd) {
    // clear client session info
    logger->info("ClientHandler: socket {} closed", sock_fd);
    logger->flush();

    cancel_all_client_orders(sock_to_session_info[sock_fd].client_id);

    auto it = sock_to_session_info.find(sock_fd);
    if (it != sock_to_session_info.end()) {
        client_to_sock_fd.erase(it->second.client_id);
        sock_to_session_info.erase(it);
    }
}

void ClientHandler::on_new_order(int sock_fd, const new_order& msg) {
    logger->info("Received new order from client {}: order id: {} symbol: {} side: {} quantity: {} price: {} flags: {}",
                 msg.header.client_id, msg.order_id, msg.symbol, static_cast<uint8_t>(msg.side), msg.quantity, msg.price, msg.flags);

    if (!validate_session(sock_fd, msg.header.session_id, msg.header.client_id)) {
        logger->warn("Invalid session for new order from client {}", msg.header.client_id);
        send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UNKNOWN_SESSION_ID), msg.order_id);
        return;
    }

    auto reject = validator.validate_new_order(msg.order_id, msg.symbol, msg.side, msg.quantity, msg.price, msg.flags);
    if (reject != REJECT_REASON::NONE) {
        send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(reject), msg.order_id);
        return;
    }

    auto& client_order_id_map = client_to_open_orders[msg.header.client_id];
    if (client_order_id_map.find(msg.order_id) != client_order_id_map.end()) {
        logger->warn("Duplicate order id for new order from client {}", msg.header.client_id);
        send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::DUPLICATE_ORDER_ID),
                          msg.order_id);
        return;
    }

    uint64_t exch_order_id = nanotime();
    client_order_id_map[msg.order_id] = {exch_order_id, msg.symbol};
    exch_to_client_orders[exch_order_id] = {msg.header.client_id, msg.order_id};

    logger->info("Client order id {} mapped to exch order id {}", msg.order_id, exch_order_id);
    to_matching_engine.emplace(MSG_TYPE::NEW_ORDER, exch_order_id, msg.symbol, msg.header.seq_num, msg.header.client_id,
        msg.side, msg.quantity, msg.price, msg.flags);
}

void ClientHandler::on_delete_order(int sock_fd, const delete_order& msg) {
    logger->info("Received delete order from client {}", msg.header.client_id);
    if (!validate_session(sock_fd, msg.header.session_id, msg.header.client_id)) {
        logger->warn("Invalid session for delete order from client {}", msg.header.client_id);
        send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UNKNOWN_SESSION_ID), msg.order_id);
        return;
    }

    // check the client order id exists
    auto it = client_to_open_orders.find(msg.header.client_id);
    if (it == client_to_open_orders.end() || it->second.find(msg.order_id) == it->second.end()) {
        logger->warn("Unknown order id {} for delete order from client {}", msg.order_id, msg.header.client_id);
        send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UKNOWN_ORDER_ID), msg.order_id);
        return;
    }

    auto client_order = client_to_open_orders.at(msg.header.client_id).at(msg.order_id);
    to_matching_engine.emplace(MSG_TYPE::DELETE_ORDER, client_order.exch_order_id, client_order.symbol,
        msg.header.seq_num, msg.header.client_id, md::SIDE::BUY, 0, 0, 0);
}

void ClientHandler::on_modify_order(int sock_fd, const modify_order& msg) {
    logger->info("Received modify order from client {}, order id: {} side: {} quantity: {} price: {}",
                 msg.header.client_id, msg.order_id, static_cast<uint8_t>(msg.side), msg.quantity, msg.price);
    if (!validate_session(sock_fd, msg.header.session_id, msg.header.client_id)) {
        logger->warn("Invalid session for modify order from client {}", msg.header.client_id);
        send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UNKNOWN_SESSION_ID), msg.order_id);
        return;
    }

    auto it = client_to_open_orders.find(msg.header.client_id);
    if (it == client_to_open_orders.end() || it->second.find(msg.order_id) == it->second.end()) {
        logger->warn("Unknown order id {} for modify order from client {}", msg.order_id, msg.header.client_id);
        send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UKNOWN_ORDER_ID), msg.order_id);
        return;
    }

    auto client_order = client_to_open_orders.at(msg.header.client_id).at(msg.order_id);
    auto reject = validator.validate_new_order(msg.order_id, client_order.symbol, msg.side, msg.quantity, msg.price, 0);
    if (reject != REJECT_REASON::NONE) {
        send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(reject), msg.order_id);
        return;
    }
    to_matching_engine.emplace(MSG_TYPE::MODIFY_ORDER, client_order.exch_order_id, client_order.symbol,
                                msg.header.seq_num, msg.header.client_id, msg.side, msg.quantity, msg.price, 0);
}

void ClientHandler::process() {
    while (from_matching_engine.front()) {
        oe_payload& payload = *from_matching_engine.front();

        auto it = exch_to_client_orders.find(payload.exch_order_id);
        if (payload.msg_type != MSG_TYPE::REJECT && it == exch_to_client_orders.end()) {
            uint64_t exch_order_id = payload.exch_order_id;
            logger->warn("Unknown exch order id {} msg type {}", exch_order_id,
                static_cast<int>(payload.msg_type));
            from_matching_engine.pop();
            continue;
        }

        // handle rejects first since they don't have an exch order id
        if (payload.msg_type == MSG_TYPE::REJECT) {
            int sock_fd = get_sock_fd(payload.client_id);
            if (sock_fd == -1) {
                logger->warn("Client {} not found for reject", payload.client_id);
                from_matching_engine.pop();
                continue;
            }

            order_reject reject;
            reject.header.msg_type = static_cast<uint8_t>(MSG_TYPE::REJECT);
            reject.reject_reason = payload.flags; // flags is the reject reason
            reject.order_id = payload.exch_order_id; // no exch order id is generated for rejects so pass this through

            logger->info("Sending reject to client {} seq {} order id {} reason {}", payload.client_id,
                payload.client_seq, payload.exch_order_id, payload.flags);

            write_msg_to_client(payload.client_id, payload.client_seq, reject);
            from_matching_engine.pop();
            continue;
        }

        uint32_t client_id = exch_to_client_orders[payload.exch_order_id].client_id;
        uint64_t client_order_id = exch_to_client_orders[payload.exch_order_id].order_id;

        switch (payload.msg_type) {
            case MSG_TYPE::ACK: {
                order_ack ack;
                ack.header.msg_type = static_cast<uint8_t>(MSG_TYPE::ACK);
                ack.order_id = client_order_id;
                ack.exch_order_id = payload.exch_order_id;
                ack.quantity = payload.quantity;
                ack.price = payload.price;

                logger->info("Sending ack to client {} seq {} order id {} exch order id {} quantity {} price {}",
                             client_id, payload.client_seq, client_order_id, payload.exch_order_id, payload.quantity, payload.price);

                write_msg_to_client(client_id, payload.client_seq, ack);
                break;
            }
            case MSG_TYPE::FILL: {
                order_fill fill;
                fill.header.msg_type = static_cast<uint8_t>(MSG_TYPE::FILL);
                fill.order_id = client_order_id;
                fill.quantity = payload.quantity;
                fill.price = payload.price;
                fill.flags = payload.flags;

                logger->info("Sending fill to client {} seq {} order id {} quantity {} price {} flags {}",
                             client_id, payload.client_seq, client_order_id, payload.quantity, payload.price, payload.flags);

                write_msg_to_client(client_id, payload.client_seq, fill);

                // remove the order if it was fully filled
                if (fill.flags & static_cast<uint8_t>(FILL_FLAGS::CLOSED)) {
                    client_to_open_orders[client_id].erase(client_order_id);
                    exch_to_client_orders.erase(payload.exch_order_id);
                }
                break;
            }
            case MSG_TYPE::CLOSE: {
                order_closed closed;
                closed.header.msg_type = static_cast<uint8_t>(MSG_TYPE::CLOSE);
                closed.order_id = client_order_id;

                logger->info("Sending close to client {} seq {} order id {}", client_id, payload.client_seq, client_order_id);

                write_msg_to_client(client_id, payload.client_seq, closed);

                // remove the order from the client's open orders
                client_to_open_orders[client_id].erase(client_order_id);
                exch_to_client_orders.erase(payload.exch_order_id);
                break;
            }
            case oe::MSG_TYPE::REJECT: {
                // already handled above
                break;
            }
            default:
                logger->warn("Unknown message type from matching engine: {}", static_cast<uint8_t>(payload.msg_type));
                break;
        }
        from_matching_engine.pop();
    }
}

template <typename Msg>
void ClientHandler::write_msg_to_client(uint32_t client_id, uint32_t last_seq_num, Msg& msg) {
    int sock_fd = get_sock_fd(client_id);
    if (sock_fd == -1) {
        logger->warn("Client {} not found for message", client_id);
        return;
    }

    update_last_seq_num(sock_fd, last_seq_num);

    ssize_t len = write_msg(sock_fd, msg);
    if (len == -1) {
        log_network_send_error(sock_fd, len);

        // close the socket
        shutdown(sock_fd, SHUT_RDWR);
        close(sock_fd);
    }
    return;
}

template <typename Msg>
ssize_t ClientHandler::write_msg(int sock_fd, Msg& msg) {
    // fill in the header fields
    auto& session_info = sock_to_session_info[sock_fd];

    msg.header.length = sizeof(Msg);
    msg.header.version = OE_PROTOCOL_VERSION;
    msg.header.seq_num = session_info.exch_seq_num++;
    msg.header.last_seq_num = session_info.last_seq_num;
    msg.header.client_id = session_info.client_id;

    // timeout after 10 milliseconds
    uint64_t start = nanotime();

    ssize_t bytes_sent = 0;
    while (bytes_sent < msg.header.length) {
        ssize_t n = ::write(sock_fd, reinterpret_cast<char*>(&msg) + bytes_sent, msg.header.length - bytes_sent);
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return -1;
        }
        bytes_sent += n;

        if (nanotime() - start > 10000000) {
            logger->error("Timed out sending message to client {}", sock_fd);
            errno = ETIMEDOUT;
            return -1;
        }
    }
    return bytes_sent;
}

void ClientHandler::update_last_seq_num(int sock_fd, uint32_t seq_num) {
    sock_to_session_info[sock_fd].last_seq_num = seq_num;
}

void ClientHandler::log_network_send_error(int sock_fd, ssize_t err) {
    logger->error("Failed to send message to client {}: {}", sock_fd, strerror(errno));
}

void ClientHandler::send_order_reject(int sock_fd, uint32_t seq_num, uint32_t client_id, uint8_t reason_code, uint64_t order_id) {
    logger->warn("Sending order reject to client {} seq {} reason {}", client_id, seq_num, static_cast<int>(reason_code));

    // send the reject message to the client via the broker so the sequence numbers are correct
    to_matching_engine.emplace(MSG_TYPE::REJECT, order_id, 0, seq_num, client_id, md::SIDE::BUY, 0, 0, reason_code);
}

void ClientHandler::cancel_all_client_orders(uint32_t client_id) {
    logger->info("Cancelling all orders for client {}", client_id);
    auto it = client_to_open_orders.find(client_id);
    if (it == client_to_open_orders.end()) {
        return;
    }

    for (auto& [order_id, order] : it->second) {
        to_matching_engine.emplace(MSG_TYPE::DELETE_ORDER, order.exch_order_id, order.symbol, 0, client_id,
        md::SIDE::BUY, 0, 0, 0);
    }

    client_to_open_orders.erase(it);
}

bool ClientHandler::validate_session(int sock_fd, uint64_t session_id, uint32_t client_id) {
    auto it = sock_to_session_info.find(sock_fd);
    if (it == sock_to_session_info.end()) {
        return false;
    }

    auto& session_info = it->second;
    return session_info.session_id == session_id && session_info.client_id == client_id;
}

int ClientHandler::get_sock_fd(uint32_t client_id) {
    auto it = client_to_sock_fd.find(client_id);
    if (it == client_to_sock_fd.end()) {
        return -1;
    }
    return it->second;
}

} // namespace ndfex::oe
