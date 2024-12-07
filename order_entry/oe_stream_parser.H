/**
 * Takes a stream of bytes from the epoll server and parses it into order entry messages.
 */

#pragma once

#include "oe_protocol.H"

#include <cstdint>
#include <cstddef>
#include <unistd.h>
#include <sys/socket.h>

#include <unordered_map>
#include <iostream>
#include <vector>
#include <cstring>

namespace ndfex::oe {

template<typename Handler>
class oe_stream_parser {

public:

    oe_stream_parser(Handler& handler) : handler(handler) {}
    ~oe_stream_parser() = default;

    void parse(int sock_fd, const char* buf, size_t len) {

        // If we have a partial message for this socket, append the new data to it
        auto& partial_message = sock_to_partial_messages[sock_fd];
        partial_message.insert(partial_message.end(), buf, buf + len);

        while (partial_message.size() >= sizeof(oe_header)) {

            oe_header* header = reinterpret_cast<oe_header*>(partial_message.data());
            if (partial_message.size() < header->length) {
                // not enough bytes to parse the full message
                return;
            }

            switch (header->msg_type) {
                case static_cast<uint8_t>(MSG_TYPE::NEW_ORDER): {
                    new_order* msg = reinterpret_cast<new_order*>(partial_message.data());
                    handler.on_new_order(*msg);
                    break;
                }
                case static_cast<uint8_t>(MSG_TYPE::DELETE_ORDER): {
                    delete_order* msg = reinterpret_cast<delete_order*>(partial_message.data());
                    handler.on_delete_order(*msg);
                    break;
                }
                case static_cast<uint8_t>(MSG_TYPE::MODIFY_ORDER): {
                    modify_order* msg = reinterpret_cast<modify_order*>(partial_message.data());
                    handler.on_modify_order(*msg);
                    break;
                }
                case static_cast<uint8_t>(MSG_TYPE::LOGIN): {
                    login* msg = reinterpret_cast<login*>(partial_message.data());
                    handler.on_login(*msg);
                    break;
                }
                default:
                    // unknown message type
                    // send an error message back to the client and close the socket
                    error_message err_msg;
                    err_msg.header.length = sizeof(error_message);
                    err_msg.header.msg_type = static_cast<uint8_t>(MSG_TYPE::ERROR);
                    err_msg.header.seq_num = header->seq_num;
                    err_msg.header.client_id = header->client_id;
                    snprintf(reinterpret_cast<char*>(err_msg.error_message), sizeof(err_msg.error_message),
                         "Unknown message type: %d", header->msg_type);
                    err_msg.error_message_length = strlen(reinterpret_cast<char*>(err_msg.error_message));

                    write(sock_fd, &err_msg, sizeof(err_msg)); // ignore return value because we are closing the socket anyway
                    shutdown(sock_fd, SHUT_RDWR);
                    close(sock_fd);
                    break;
            }

            // remove the parsed message from the buffer
            partial_message.erase(partial_message.begin(), partial_message.begin() + header->length);
        }
    }

    void socket_closed(int sock_fd) {
        sock_to_partial_messages.erase(sock_fd);
    }

private:

    Handler& handler;
    std::unordered_map<int, std::vector<char>> sock_to_partial_messages;

};
} // namespace ndfex::oe