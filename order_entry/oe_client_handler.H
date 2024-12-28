#pragma once

#include "oe_protocol.H"
#include "matching_engine/utils.H"
#include "matching_engine/spsc_oe_queue.H"

#include <spdlog/logger.h>

#include <sys/socket.h>
#include <cstdint>
#include <unordered_map>
#include <random>
#include <memory>

/**
 * Handles incoming client messages, validates them, forwards them to the matching engine, and sends responses back to the client.
 */

namespace ndfex::oe {

struct session_info {
    uint64_t session_id;
    uint32_t client_id;
    uint32_t last_seq_num;
    uint32_t exch_seq_num;
};

struct user_info {
    std::string username;
    std::string password;
    uint32_t client_id;
};

struct open_order {
    uint64_t exch_order_id;
    uint32_t symbol;
};

struct exch_order {
    uint32_t client_id;
    uint64_t order_id;
};

class ClientHandler {

    typedef std::unordered_map<uint64_t, open_order> open_orders_map;
    typedef std::unordered_map<uint64_t, exch_order> exch_orders_map;

    public:

        ClientHandler(
            SPSCOEQueue& to_matching_engine,
            SPSCOEQueue& from_matching_engine,
            std::unordered_map<std::string, user_info>& users, std::shared_ptr<spdlog::logger> logger)
            : to_matching_engine(to_matching_engine),
              from_matching_engine(from_matching_engine),
              users(users),
              logger(logger) {}

        void on_login(int sock_fd, const login& msg) {
            logger->info("Received login request from client {}", msg.header.client_id);
            // validate username and password
            auto it = users.find(std::string(reinterpret_cast<const char*>(msg.username)));
            if (it == users.end()
                || it->second.password != std::string(reinterpret_cast<const char*>(msg.password))
                || it->second.client_id != msg.header.client_id) {

                logger->warn("Invalid login attempt from client {}", msg.header.client_id);

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

                ssize_t len = write_msg(sock_fd, response);
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

        void on_new_order(int sock_fd, const new_order& msg) {
            logger->info("Received new order from client {}", msg.header.client_id);
            if (!validate_session(sock_fd, msg.header.seq_num, msg.header.client_id)) {
                logger->warn("Invalid session for new order from client {}", msg.header.client_id);
                send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UNKNOWN_SESSION_ID), msg.order_id);
                return;
            }

           // TODO validate symbol and price / quantity for order

            auto& client_order_id_map = client_to_open_orders[msg.header.client_id];
            if (client_order_id_map.find(msg.order_id) != client_order_id_map.end()) {
                logger->warn("Duplicate order id for new order from client {}", msg.header.client_id);
                send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::DUPLICATE_ORDER_ID), msg.order_id);
                return;
            }

            // TODO validate the order fields (e.g. price, quantity)

            uint64_t exch_order_id = nanotime();
            client_order_id_map[msg.order_id] = {exch_order_id, msg.symbol};
            exch_to_client_orders[exch_order_id] = {msg.header.client_id, msg.order_id};

            to_matching_engine.emplace(MSG_TYPE::NEW_ORDER, exch_order_id, msg.symbol, msg.header.seq_num, msg.side, msg.quantity, msg.price, msg.flags);
        }

        void on_delete_order(int sock_fd, const delete_order& msg) {
            logger->info("Received delete order from client {}", msg.header.client_id);
            if (!validate_session(sock_fd, msg.header.seq_num, msg.header.client_id)) {
                logger->warn("Invalid session for delete order from client {}", msg.header.client_id);
                send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UNKNOWN_SESSION_ID), msg.order_id);
                return;
            }

            // check the client order id exists
            auto it = client_to_open_orders.find(msg.header.client_id);
            if (it == client_to_open_orders.end()) {
                logger->warn("Unknown order id for delete order from client {}", msg.header.client_id);
                send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UKNOWN_ORDER_ID), msg.order_id);
                return;
            }

            auto client_order = client_to_open_orders.at(msg.header.client_id).at(msg.order_id);
            to_matching_engine.emplace(MSG_TYPE::DELETE_ORDER, client_order.exch_order_id, client_order.symbol, msg.header.seq_num, md::SIDE::BUY, 0, 0, 0);
        }

        void on_modify_order(int sock_fd, const modify_order& msg) {
            logger->info("Received modify order from client {}", msg.header.client_id);
            if (!validate_session(sock_fd, msg.header.seq_num, msg.header.client_id)) {
                logger->warn("Invalid session for modify order from client {}", msg.header.client_id);
                send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UNKNOWN_SESSION_ID), msg.order_id);
                return;
            }

            auto it = client_to_open_orders.find(msg.header.client_id);
            if (it == client_to_open_orders.end()) {
                logger->warn("Unknown order id for modify order from client {}", msg.header.client_id);
                send_order_reject(sock_fd, msg.header.seq_num, msg.header.client_id, static_cast<uint8_t>(REJECT_REASON::UKNOWN_ORDER_ID), msg.order_id);
                return;
            }

            // TODO validate price / quantity for symbol

            auto client_order = client_to_open_orders.at(msg.header.client_id).at(msg.order_id);
            to_matching_engine.emplace(MSG_TYPE::MODIFY_ORDER, client_order.exch_order_id, client_order.symbol,
                                        msg.header.seq_num, msg.side, msg.quantity, msg.price, 0);
        }

        void process() {
            while (from_matching_engine.front()) {
                oe_payload& payload = *from_matching_engine.front();
                auto it = exch_to_client_orders.find(payload.exch_order_id);
                if (it == exch_to_client_orders.end()) {
                    logger->warn("Unknown exch order id {}", payload.exch_order_id);
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

                        int sock_fd = get_sock_fd(client_id);
                        if (sock_fd == -1) {
                            logger->warn("Client {} not found for ack", client_id);
                            break;
                        }

                        update_last_seq_num(sock_fd, payload.client_seq);

                        ssize_t len = write_msg(sock_fd, ack);
                        if (len == -1) {
                            log_network_send_error(sock_fd, len);

                            // close the socket
                            shutdown(sock_fd, SHUT_RDWR);
                            close(sock_fd);
                        }
                        break;
                    }
                    case MSG_TYPE::FILL: {
                        order_fill fill;
                        fill.header.msg_type = static_cast<uint8_t>(MSG_TYPE::FILL);
                        fill.order_id = client_order_id;
                        fill.quantity = payload.quantity;
                        fill.price = payload.price;
                        fill.flags = payload.flags;

                        int sock_fd = get_sock_fd(client_id);
                        if (sock_fd == -1) {
                            logger->warn("Client {} not found for fill", client_id);
                            break;
                        }

                        update_last_seq_num(sock_fd, payload.client_seq);

                        ssize_t len = write_msg(sock_fd, fill);
                        if (len == -1) {
                            log_network_send_error(sock_fd, len);

                            // close the socket
                            shutdown(sock_fd, SHUT_RDWR);
                            close(sock_fd);
                        }

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

                        int sock_fd = get_sock_fd(client_id);
                        if (sock_fd == -1) {
                            logger->warn("Client {} not found for close", client_id);
                            break;
                        }

                        update_last_seq_num(sock_fd, payload.client_seq);

                        ssize_t len = write_msg(sock_fd, closed);
                        if (len == -1) {
                            log_network_send_error(sock_fd, len);

                            // close the socket
                            shutdown(sock_fd, SHUT_RDWR);
                            close(sock_fd);
                        }

                        // remove the order from the client's open orders
                        client_to_open_orders[client_id].erase(client_order_id);
                        exch_to_client_orders.erase(payload.exch_order_id);
                        break;
                    }
                    default:
                        logger->warn("Unknown message type from matching engine: {}", static_cast<uint8_t>(payload.msg_type));
                        break;
                }
                from_matching_engine.pop();
            }
        }

    private:

        // write all bytes to the socket even in non-blocking mode TODO (maybe add a timeout)
        template <typename Msg>
        ssize_t write_msg(int sock_fd, Msg& msg) {

            // fill in the header fields
            auto session_info = sock_to_session_info[sock_fd];
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

                if (nanotime() - start > 10000) {
                    errno = ETIMEDOUT;
                    return -1;
                }
            }
            return bytes_sent;
        }

        void update_last_seq_num(int sock_fd, uint32_t seq_num) {
            sock_to_session_info[sock_fd].last_seq_num = seq_num;
        }

        void log_network_send_error(int sock_fd, ssize_t err) {
            logger->error("Failed to send message to client {}: {}", sock_fd, strerror(errno));
        }

        void send_order_reject(int sock_fd, uint32_t seq_num, uint32_t client_id, uint8_t reason_code, uint64_t order_id) {
            order_reject reject;
            reject.header.length = sizeof(order_reject);
            reject.header.msg_type = static_cast<uint8_t>(MSG_TYPE::REJECT);
            reject.header.seq_num = seq_num;
            reject.header.client_id = client_id;
            reject.reject_reason = reason_code;
            reject.order_id = order_id;

            ssize_t len = write_msg(sock_fd, reject);
            if (len == -1) {
                log_network_send_error(sock_fd, len);

                // close the socket
                shutdown(sock_fd, SHUT_RDWR);
                close(sock_fd);
            }
            logger->warn("Sent order reject to client {} seq {} reason", client_id, seq_num, reason_code);
        }

        void cancel_all_client_orders(uint32_t client_id) {
            auto it = client_to_open_orders.find(client_id);
            if (it == client_to_open_orders.end()) {
                return;
            }

            for (const auto& [client_order_id, exch_order_id] : it->second) {
                to_matching_engine.emplace(MSG_TYPE::DELETE_ORDER, exch_order_id.exch_order_id, 0, 0, md::SIDE::BUY, 0, 0, 0);
            }
        }

        bool validate_session(int sock_fd, uint64_t session_id, uint32_t client_id) {
            auto it = sock_to_session_info.find(sock_fd);
            if (it == sock_to_session_info.end()) {
                return false;
            }
            return it->second.session_id == session_id && it->second.client_id == client_id;
        }

        int get_sock_fd(uint32_t client_id) {
            auto it = client_to_sock_fd.find(client_id);
            if (it == client_to_sock_fd.end()) {
                return -1;
            }
            return it->second;
        }

        SPSCOEQueue& to_matching_engine;
        SPSCOEQueue& from_matching_engine;

        std::unordered_map<std::string, user_info>& users;
        std::shared_ptr<spdlog::logger> logger;

        std::unordered_map<int, session_info> sock_to_session_info;
        std::unordered_map<uint32_t, int> client_to_sock_fd;
        std::unordered_map<uint32_t, open_orders_map> client_to_open_orders;

        exch_orders_map exch_to_client_orders;

        std::random_device rd;
        std::mt19937 gen{rd()};
        std::uniform_int_distribution<std::uint64_t> dist{
            std::numeric_limits<std::uint64_t>::min(),
            std::numeric_limits<std::uint64_t>::max()};
};

} // namespace ndfex::oe