/**
 * Takes a stream of bytes from the epoll server and parses it into order entry messages.
 */

#pragma once

#include "oe_protocol.H"
#include <spdlog/logger.h>

#include <cstdint>
#include <cstddef>
#include <unistd.h>
#include <sys/socket.h>

#include <unordered_map>
#include <iostream>
#include <vector>
#include <memory>
#include <cstring>


namespace ndfex::oe {

template<typename Handler>
class StreamParser {

public:

    StreamParser(Handler& handler, std::shared_ptr<spdlog::logger> logger) : handler(handler), logger(logger) {}
    ~StreamParser() = default;

    void parse(int sock_fd, const char* buf, size_t len) {

        // If we have a partial message for this socket, append the new data to it
        auto& partial_message = sock_to_partial_messages[sock_fd];
        partial_message.insert(partial_message.end(), buf, buf + len);

        while (partial_message.size() >= sizeof(oe_request_header)) {

            oe_request_header* header = reinterpret_cast<oe_request_header*>(partial_message.data());
            if (header->version != OE_PROTOCOL_VERSION) {
                // unknown protocol version
                // send an error message back to the client and close the socket
                logger->warn("Unknown protocol version: {} from client sock {}", header->version, sock_fd);
                logger->flush();

                send_error_msg(sock_fd, header->seq_num, header->client_id, REJECT_REASON::INVALID_ORDER,
                               "Unknown protocol version");
                return;
            }

            if (header->length > MAX_OE_MESSAGE_SIZE) {
                // message too large
                // send an error message back to the client and close the socket
                logger->warn("Message too large: {} from client sock {}", header->length, sock_fd);
                logger->flush();

                send_error_msg(sock_fd, header->seq_num, header->client_id, REJECT_REASON::INVALID_ORDER, "Message too large");
                return;
            }

            if (partial_message.size() < header->length) {
                // not enough bytes to parse the full message
                return;
            }

            switch (header->msg_type) {
                case static_cast<uint8_t>(MSG_TYPE::NEW_ORDER): {
                    new_order* msg = reinterpret_cast<new_order*>(partial_message.data());
                    handler.on_new_order(sock_fd, *msg);
                    break;
                }
                case static_cast<uint8_t>(MSG_TYPE::DELETE_ORDER): {
                    delete_order* msg = reinterpret_cast<delete_order*>(partial_message.data());
                    handler.on_delete_order(sock_fd, *msg);
                    break;
                }
                case static_cast<uint8_t>(MSG_TYPE::MODIFY_ORDER): {
                    modify_order* msg = reinterpret_cast<modify_order*>(partial_message.data());
                    handler.on_modify_order(sock_fd, *msg);
                    break;
                }
                case static_cast<uint8_t>(MSG_TYPE::LOGIN): {
                    login* msg = reinterpret_cast<login*>(partial_message.data());
                    handler.on_login(sock_fd, *msg);
                    break;
                }
                default: {
                    // unknown message type
                    // send an error message back to the client and close the socket
                    logger->warn("Unknown message type: {} from client sock {}", header->msg_type, sock_fd);
                    logger->flush();

                    send_error_msg(sock_fd, header->seq_num, header->client_id, REJECT_REASON::INVALID_ORDER,
                                   "Unknown message type");
                    return;
                }
            }

            // remove the parsed message from the buffer
            partial_message.erase(partial_message.begin(), partial_message.begin() + header->length);
        }
        return;
    }

    void send_error_msg(int sock_fd, uint32_t seq_num, uint32_t client_id, REJECT_REASON reason_code, const char* error_msg) {
        error_message err_msg;
        err_msg.header.length = sizeof(error_message);
        err_msg.header.msg_type = static_cast<uint8_t>(MSG_TYPE::ERROR);
        err_msg.header.seq_num = seq_num;
        err_msg.header.client_id = client_id;
        err_msg.error_code = static_cast<uint8_t>(reason_code);
        strncpy(reinterpret_cast<char*>(err_msg.error_message), error_msg, sizeof(err_msg.error_message));
        err_msg.error_message_length = strlen(reinterpret_cast<char*>(err_msg.error_message));

        write(sock_fd, reinterpret_cast<const char*>(&err_msg), sizeof(err_msg));
        shutdown(sock_fd, SHUT_RDWR);
        close(sock_fd);
    }

    void socket_closed(int sock_fd) {
        logger->info("parser Socket {} closed", sock_fd);
        logger->flush();

        sock_to_partial_messages.erase(sock_fd);
        handler.on_socket_closed(sock_fd);
    }

    void new_socket(int sock_fd) {
        sock_to_partial_messages[sock_fd] = {};
    }

private:

    Handler& handler;
    std::shared_ptr<spdlog::logger> logger;
    std::unordered_map<int, std::vector<char>> sock_to_partial_messages;

};
} // namespace ndfex::oe