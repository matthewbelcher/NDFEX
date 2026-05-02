#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>

namespace ndfex::bots {

/**
 * Simple line-based TCP admin port for the LP bot process.
 *
 * Bound to <bind_addr>:<port>, plain-text protocol, one command per line.
 * The handler is called on the listener thread for each line and returns
 * a response string (newline appended automatically if missing).
 *
 * Designed for low-rate operator traffic — single-threaded accept loop,
 * one client at a time. Polls listen_fd with a short timeout so stop()
 * can shut down cleanly.
 */
class CommandPort {
public:
    using Handler = std::function<std::string(const std::string&)>;

    CommandPort(std::string bind_addr, uint16_t port, Handler handler,
                std::shared_ptr<spdlog::logger> logger);
    ~CommandPort();

    CommandPort(const CommandPort&) = delete;
    CommandPort& operator=(const CommandPort&) = delete;

    void start();
    void stop();

private:
    void run();
    void serve_client(int client_fd);

    std::string bind_addr;
    uint16_t port;
    Handler handler;
    std::shared_ptr<spdlog::logger> logger;

    std::atomic<bool> stop_flag{false};
    std::thread thread;
    int listen_fd{-1};
};

} // namespace ndfex::bots
