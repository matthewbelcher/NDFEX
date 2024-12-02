#pragma once

#include <cstdint>
#include <sys/epoll.h>
#include <oe_stream_parser.H>

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

        oe_stream_parser parser;
        char buf[1500];
    };

} // namespace ndfex::oe