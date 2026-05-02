#include "command_port.H"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace ndfex::bots {

CommandPort::CommandPort(std::string bind_addr, uint16_t port, Handler handler,
                         std::shared_ptr<spdlog::logger> logger)
    : bind_addr(std::move(bind_addr)), port(port),
      handler(std::move(handler)), logger(std::move(logger)) {}

CommandPort::~CommandPort() { stop(); }

void CommandPort::start() {
    listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        throw std::runtime_error(std::string("CommandPort socket: ") + std::strerror(errno));
    }
    int one = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, bind_addr.c_str(), &addr.sin_addr) != 1) {
        ::close(listen_fd);
        listen_fd = -1;
        throw std::runtime_error("CommandPort: invalid bind address " + bind_addr);
    }
    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const std::string err = std::strerror(errno);
        ::close(listen_fd);
        listen_fd = -1;
        throw std::runtime_error("CommandPort bind: " + err);
    }
    if (::listen(listen_fd, 4) < 0) {
        const std::string err = std::strerror(errno);
        ::close(listen_fd);
        listen_fd = -1;
        throw std::runtime_error("CommandPort listen: " + err);
    }

    logger->info("[command_port] listening on {}:{}", bind_addr, port);
    thread = std::thread([this] { run(); });
}

void CommandPort::stop() {
    stop_flag.store(true);
    if (listen_fd >= 0) {
        ::shutdown(listen_fd, SHUT_RDWR);
        ::close(listen_fd);
        listen_fd = -1;
    }
    if (thread.joinable()) thread.join();
}

void CommandPort::run() {
    while (!stop_flag.load()) {
        pollfd pfd{listen_fd, POLLIN, 0};
        int pr = ::poll(&pfd, 1, 250);
        if (pr <= 0) continue;

        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (stop_flag.load()) break;
            logger->warn("[command_port] accept: {}", std::strerror(errno));
            continue;
        }
        serve_client(client_fd);
        ::close(client_fd);
    }
}

void CommandPort::serve_client(int client_fd) {
    std::string buf;
    char chunk[256];
    while (true) {
        ssize_t n = ::recv(client_fd, chunk, sizeof(chunk), 0);
        if (n <= 0) return;
        buf.append(chunk, chunk + n);

        std::string::size_type nl;
        while ((nl = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();

            std::string response;
            try {
                response = handler(line);
            } catch (const std::exception& e) {
                response = std::string("ERR exception: ") + e.what();
            }
            if (response.empty() || response.back() != '\n') response.push_back('\n');
            if (::send(client_fd, response.data(), response.size(), MSG_NOSIGNAL) < 0) {
                return;
            }
        }
    }
}

} // namespace ndfex::bots
