#pragma once

#include "oe_protocol.H"
#include "oe_validator.H"
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

    user_info(std::string username, std::string password, uint32_t client_id)
        : username(username), password(password), client_id(client_id) {}

    user_info() = default;
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
            OrderEntryValidator& validator,
            SPSCOEQueue& to_matching_engine,
            SPSCOEQueue& from_matching_engine,
            std::unordered_map<std::string, user_info>& users, std::shared_ptr<spdlog::logger> logger);

        void on_login(int sock_fd, const login& msg);
        void on_new_order(int sock_fd, const new_order& msg);
        void on_delete_order(int sock_fd, const delete_order& msg);
        void on_modify_order(int sock_fd, const modify_order& msg);
        void on_socket_closed(int sock_fd);
        void process();

    private:

        template <typename Msg>
        void write_msg_to_client(uint32_t client_id, uint32_t last_seq_num, Msg& msg);

        template <typename Msg>
        ssize_t write_msg(int sock_fd, Msg& msg);

        void update_last_seq_num(int sock_fd, uint32_t seq_num);
        void log_network_send_error(int sock_fd, ssize_t err);
        void send_order_reject(int sock_fd, uint32_t seq_num, uint32_t client_id, uint8_t reason_code, uint64_t order_id);
        void cancel_all_client_orders(uint32_t client_id);
        bool validate_session(int sock_fd, uint64_t session_id, uint32_t client_id);
        int get_sock_fd(uint32_t client_id);

        OrderEntryValidator& validator;

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