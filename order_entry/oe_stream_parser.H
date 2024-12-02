/**
 * Takes a stream of bytes from the epoll server and parses it into order entry messages.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <sstream>

#include <unordered_map>

namespace ndfex::oe {

class oe_stream_parser {
public:
    oe_stream_parser();
    ~oe_stream_parser();

    void parse(int sock_fd, const char* buf, size_t len);

    void socket_closed(int sock_fd);

private:

    std::unordered_map<int, std::stringbuf> sock_to_partial_messages;

};
} // namespace ndfex::oe