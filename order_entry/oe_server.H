#pragma once

#include "oe_stream_parser.H"
#include "oe_client_handler.H"

#include <cstdint>
#include <sys/epoll.h>

namespace ndfex::oe {

    class EpollServer {
    public:
        EpollServer(uint16_t port);
        ~EpollServer();

        void run();

    private:

        int epoll_fd;
        int listen_fd;
        struct epoll_event events[16];

        StreamParser<ClientHandler> parser;
        char buf[1500];
    };

} // namespace ndfex::oe