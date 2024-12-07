#pragma once

#include "oe_protocol.H"
#include "matching_engine/utils.H"

#include <cstdint>
#include <unordered_map>
#include <random>

/**
 * Handles incoming client messages, validates them, forwards them to the matching engine, and sends responses back to the client.
 */

namespace ndfex {
    class OrderNotFoundException : public std::runtime_error;
}

namespace ndfex::oe {

struct session_info {
    uint64_t session_id;
    uint32_t client_id;
    uint32_t last_seq_num;
};

struct user_info {
    std::string username;
    std::string password;
    uint32_t client_id;
};

template <typename OrderHandler>
class ClientHandler {

    public:

        ClientHandler(std::unordered_map<std::string, user_info>& users)
            : users(users) {}

        void on_login(int sock_fd, const login& msg) {
            // validate username and password
            auto it = users.find(std::string(reinterpret_cast<const char*>(msg.username)));
            if (it == users.end()
                || it->second.password != std::string(reinterpret_cast<const char*>(msg.password))
                || it->second.client_id != msg.header.client_id) {

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

                ssize_t len = write(sock_fd, reinterpret_cast<char*>(&response), sizeof(login_response));
                // TODO handle send error

                shutdown(sock_fd, SHUT_RDWR);
                close(sock_fd);

                return;
            }

            // check if this client id is already logged in
            for (const auto& [other_fd, session] : sock_to_session_info) {
                if (session.client_id == msg.header.client_id) {

                    // send login response with status = 2
                    login_response response;
                    response.header.length = sizeof(login_response);
                    response.header.msg_type = static_cast<uint8_t>(MSG_TYPE::LOGIN_RESPONSE);
                    response.header.seq_num = msg.header.seq_num;
                    response.header.client_id = msg.header.client_id;
                    response.status = static_cast<uint8_t>(LOGIN_STATUS::SESSION_ALREADY_ACTIVE);

                    ssize_t len = write(sock_fd, reinterpret_cast<char*>(&response), sizeof(login_response));
                    // TODO handle send error

                    shutdown(sock_fd, SHUT_RDWR);
                    close(sock_fd);

                    // send an error message to the other socket
                    error_message err_msg;
                    err_msg.header.length = sizeof(error_message);
                    err_msg.header.msg_type = static_cast<uint8_t>(MSG_TYPE::ERROR);
                    err_msg.header.seq_num = session.last_seq_num;
                    err_msg.header.client_id = session.client_id;
                    err_msg.error_code = static_cast<uint8_t>(REJECT_REASON::DUPLICATE_LOGIN);
                    err_msg.error_message = std::string("Duplicate login").c_str();
                    err_msg.error_message_length = strlen(err_msg.error_message);

                    len = write(other_fd, reinterpret_cast<char*>(&err_msg), sizeof(error_message));
                    // TODO handle send error

                    // TODO cancel all orders for this client id

                    // close the other socket
                    shutdown(other_fd, SHUT_RDWR);
                    close(other_fd);
                    return;
                }
            }

            // generate session id
            uint64_t session_id = dist(gen);
            sock_to_session_info[sock_fd] = {session_id, msg.header.client_id, login.header.seq_num};

            // send login response with status = 0
            login_response response;
            response.header.length = sizeof(login_response);
            response.header.msg_type = static_cast<uint8_t>(MSG_TYPE::LOGIN_RESPONSE);
            response.header.seq_num = msg.header.seq_num;
            response.header.client_id = msg.header.client_id;
            response.status = static_cast<uint8_t>(LOGIN_STATUS::SUCCESS);
            response.session_id = session_id;

            ssize_t len = write(sock_fd, reinterpret_cast<char*>(&response), sizeof(login_response));
            // TODO handle send error
        }

        void on_new_order(int sock_fd, const new_order& msg) {
            if (!validate_session(sock_fd, msg.header.seq_num, msg.header.client_id)) {
                send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UNKNOWN_SESSION_ID), msg.order_id);
                return;
            }

            auto it = symbol_to_matching_engine.find(msg.symbol);
            if (it == symbol_to_matching_engine.end()) {
                send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UNKNOWN_SYMBOL), msg.order_id);
                return;
            }

            auto& client_order_id_map = client_order_id_to_exch_order_id[msg.header.client_id];
            if (client_order_id_map.find(msg.order_id) != client_order_id_map.end()) {
                send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::DUPLICATE_ORDER_ID), msg.order_id);
                return;
            }

            uint64_t exch_order_id = nanotime();
            client_order_id_map[msg.order_id] = exch_order_id;

            it->second->new_order(exch_order_id, msg.side, msg.quantity, msg.price, msg.flags);
        }

        void on_delete_order(int sock_fd, const delete_order& msg) {
            if (!validate_session(sock_fd, msg.header.seq_num, msg.header.client_id)) {
                send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UNKNOWN_SESSION_ID), msg.order_id);
                return;
            }

            auto it = symbol_to_matching_engine.find(msg.symbol);
            if (it == symbol_to_matching_engine.end()) {
                send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UNKNOWN_SYMBOL), msg.order_id);
                return;
            }

            // check the client order id exists
            auto it = client_order_id_to_exch_order_id.find(msg.header.client_id);
            if (it == client_order_id_to_exch_order_id.end()) {
                send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UKNOWN_ORDER_ID), msg.order_id);
                return;
            }

            try {
                uint64_t exch_order_id = client_order_id_to_exch_order_id.at(msg.header.client_id).at(msg.order_id);
                it->second->delete_order(exch_order_id);
            } catch (const OrderNotFoundException& e) {
                send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UKNOWN_ORDER_ID), msg.order_id);
            }
        }

        void on_modify_order(int sock_fd, const modify_order& msg) {
            if (!validate_session(sock_fd, msg.header.seq_num, msg.header.client_id)) {
                send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UNKNOWN_SESSION_ID), msg.order_id);
                return;
            }

            auto it = symbol_to_matching_engine.find(msg.symbol);
            if (it == symbol_to_matching_engine.end()) {
                send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UNKNOWN_SYMBOL), msg.order_id);
                return;
            }

            auto it = client_order_id_to_exch_order_id.find(msg.header.client_id);
            if (it == client_order_id_to_exch_order_id.end()) {
                send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UKNOWN_ORDER_ID), msg.order_id);
                return;
            }

            try {
                uint64_t exch_order_id = client_order_id_to_exch_order_id.at(msg.header.client_id).at(msg.order_id);
                it->second->modify_order(exch_order_id, msg.side, msg.quantity, msg.price);
            } catch (const OrderNotFoundException& e) {
                send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UKNOWN_ORDER_ID), msg.order_id);
            }
        }

        void set_matching_engine(uint32_t symbol, OrderHandler* matching_engine) {
            symbol_to_matching_engine[symbol] = matching_engine;
        }

    private:

        // write all bytes to the socket even in non-blocking mode TODO (maybe add a timeout)
        ssize_t write(int sock_fd, const char* buf, size_t len) {
            ssize_t bytes_sent = 0;
            while (bytes_sent < len) {
                ssize_t n = ::write(sock_fd, buf + bytes_sent, len - bytes_sent);
                if (n == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    }
                    return -1;
                }
                bytes_sent += n;
            }
            return bytes_sent;
        }

        void send_order_reject(int sock_fd, uint32_t seq_num, uint32_t client_id, uint8_t reason_code, uint64_t order_id) {
            order_reject reject;
            reject.header.length = sizeof(order_reject);
            reject.header.msg_type = static_cast<uint8_t>(MSG_TYPE::ORDER_REJECT);
            reject.header.seq_num = seq_num;
            reject.header.client_id = client_id;
            reject.reject_reason = reason_code;
            reject.order_id = order_id;

            ssize_t len = write(sock_fd, reinterpret_cast<char*>(&reject), sizeof(order_reject));
            // TODO handle send error
        }

        void cancel_all_client_orders(uint32_t client_id) {
            auto it = client_order_id_to_exch_order_id.find(client_id);
            if (it == client_order_id_to_exch_order_id.end()) {
                return;
            }

            for (const auto& [client_order_id, exch_order_id] : it->second) {
                auto it = symbol_to_matching_engine.find(client_order_id);
                if (it != symbol_to_matching_engine.end()) {
                    it->second->delete_order(exch_order_id);
                }
            }
        }

        bool validate_session(int sock_fd, uint64_t session_id, uint32_t client_id) {
            auto it = sock_to_session_info.find(sock_fd);
            if (it == sock_to_session_info.end()) {
                return false;
            }
            return it->second.session_id == session_id && it->second.client_id == client_id;
        }

        std::unordered_map<std::string, user_info>& users;

        std::unordered_map<int, session_info> sock_to_session_info;
        std::unordered_map<uint32_t, std::unordered_map<uint64_t, uint64_t>> client_order_id_to_exch_order_id;

        std::unordered_map<uint32_t, OrderHandler*> symbol_to_matching_engine;

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<std::uint64_t> dist(
            std::numeric_limits<std::uint64_t>::min(),
            std::numeric_limits<std::uint64_t>::max());
};

} // namespace ndfex::oe