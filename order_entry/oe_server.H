#pragma once

#include "oe_stream_parser.H"
#include "oe_client_handler.H"

#include <cstdint>
#include <sys/epoll.h>
#include <memory>
#include <spdlog/spdlog.h>

namespace ndfex::oe {

    template <typename ClientHandler>
    class EpollServer {
    public:
        EpollServer(ClientHandler& handler, uint16_t port, std::shared_ptr<spdlog::logger> logger);
        ~EpollServer();

        void run();

    private:

        int epoll_fd;
        int listen_fd;
        struct epoll_event events[16];

        std::shared_ptr<spdlog::logger> logger;
        StreamParser<ClientHandler> parser;
        ClientHandler& handler;
        char buf[1500];

    };

} // namespace ndfex::oe