#include "oe_server.H"

#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <bits/fcntl-linux.h>
#include <fcntl.h>

namespace ndfex::oe {

EpollServer::EpollServer(uint16_t port) {

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        throw std::runtime_error("Failed to create epoll instance: " + std::string(strerror(errno)));
    }

    listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd == -1) {
        throw std::runtime_error("Failed to create socket: " + std::string(strerror(errno)));
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        throw std::runtime_error("Failed to set socket options: " + std::string(strerror(errno)));
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd, (struct sockaddr*) &addr, sizeof(addr)) == -1) {
        throw std::runtime_error("Failed to bind socket: " + std::string(strerror(errno)));
    }

    if (listen(listen_fd, 64) == -1) {
        throw std::runtime_error("Failed to listen on socket: " + std::string(strerror(errno)));
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    ev.data.fd = listen_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
        throw std::runtime_error("Failed to add listen socket to epoll: " + std::string(strerror(errno)));
    }
}

EpollServer::~EpollServer() {
    close(epoll_fd);
    close(listen_fd);
}

void EpollServer::run() {
    while (true) {
        int nfds = epoll_wait(epoll_fd, events, 16, -1);
        if (nfds == -1) {
            throw std::runtime_error("Failed to wait on epoll: " + std::string(strerror(errno)));
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == listen_fd) {
                struct sockaddr_in addr;
                socklen_t addr_len = sizeof(addr);
                int conn_fd = accept(listen_fd, (struct sockaddr*) &addr, &addr_len);
                if (conn_fd == -1) {
                    throw std::runtime_error("Failed to accept connection: " + std::string(strerror(errno)));
                }

                // make the connection non-blocking
                int flags = fcntl(conn_fd, F_GETFL, 0);
                if (flags == -1) {
                    throw std::runtime_error("Failed to get socket flags: " + std::string(strerror(errno)));
                }

                if (fcntl(conn_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
                    throw std::runtime_error("Failed to set socket flags: " + std::string(strerror(errno)));
                }

                struct epoll_event ev;
                ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
                ev.data.fd = conn_fd;

                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &ev) == -1) {
                    throw std::runtime_error("Failed to add connection to epoll: " + std::string(strerror(errno)));
                }
            } else {
                // read from the socket until EWAIT is returned
                while (true) {
                    ssize_t len = read(events[i].data.fd, buf, sizeof(buf));
                    if (len == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        } else {
                            throw std::runtime_error("Failed to read from socket: " + std::string(strerror(errno)));
                        }
                    } else if (len == 0) {
                        close(events[i].data.fd);
                        parser.socket_closed(events[i].data.fd);
                        break;
                    } else {
                        parser.parse(events[i].data.fd, buf, len);
                    }
                }
            }
        }
    }
}

} // namespace ndfex::oe