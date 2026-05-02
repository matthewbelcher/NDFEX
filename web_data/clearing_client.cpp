#include "clearing_client.H"

#include <matching_engine/clearing_protocol.H>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>


namespace ndfex::clearing {


static int create_mcast_socket(const std::string& ip, uint16_t port, const std::string& bind_ip,
    std::shared_ptr<spdlog::logger> logger) {
    int mcast_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (mcast_fd < 0) {
        logger->error("Failed to create socket: {}", strerror(errno));
        throw std::runtime_error("Failed to create socket");
    }

    // set mcast socket to non-blocking
    int flags = fcntl(mcast_fd, F_GETFL, 0);
    if (flags == -1) {
        logger->error("Failed to get socket flags: {}", strerror(errno));
        throw std::runtime_error("Failed to get socket flags");
    }

    if (fcntl(mcast_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        logger->error("Failed to set socket to non-blocking: {}", strerror(errno));
        throw std::runtime_error("Failed to set socket to non-blocking");
    }

    // set SO_REUSEPORT
    int optval = 1;
    if (setsockopt(mcast_fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)) < 0) {
        logger->error("Failed to set SO_REUSEPORT: {}", strerror(errno));
        throw std::runtime_error("Failed to set SO_REUSEPORT");
    }

    // set receive buffer size to 16MB
    int rcvbuf_size = 16 * 1024 * 1024;
    if (setsockopt(mcast_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size)) < 0) {
        logger->error("Failed to set receive buffer size: {}", strerror(errno));
        throw std::runtime_error("Failed to set receive buffer size");
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    if (bind(mcast_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        logger->error("Failed to bind socket: {}", strerror(errno));
        throw std::runtime_error("Failed to bind socket");
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(ip.c_str());
    mreq.imr_interface.s_addr = inet_addr(bind_ip.c_str());

    if (setsockopt(mcast_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) < 0) {
        throw std::runtime_error("Failed to join multicast group");
    }

    logger->info("Listening on {}:{}", ip, port);
    return mcast_fd;
}

ClearingClient::ClearingClient(std::string live_ip, uint16_t live_port,
                                 std::string bind_ip, std::shared_ptr<spdlog::logger> logger)
    : logger(logger), bind_ip(bind_ip) {
    live_fd = create_mcast_socket(live_ip, live_port, bind_ip, logger);
}

ClearingClient::~ClearingClient() {
    close (live_fd);
}

void ClearingClient::process() {
    ssize_t len = recvfrom(live_fd, buf, sizeof(buf), 0, nullptr, nullptr);
    if (len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        logger->error("Failed to receive message: {}", strerror(errno));
        throw std::runtime_error("Failed to receive message");
    }

    clearing::clearing_header* header = reinterpret_cast<clearing::clearing_header*>(buf);
    if (header->magic_number != clearing::MAGIC_NUMBER) {
        logger->error("Invalid magic number: {}", header->magic_number);
        throw std::runtime_error("Invalid magic number");
    }

    logger->info("Message type: {}", static_cast<uint8_t>(header->msg_type));

    // check sequence number
    if (last_seq_num != 0 && header->seq_num != last_seq_num + 1) {
        logger->error("Invalid sequence number: {} (expected {})", header->seq_num, last_seq_num + 1);
        return;
    }
    last_seq_num = header->seq_num;

    switch (header->msg_type) {
        case MSG_TYPE::FILL: {
            if (len < sizeof(clearing::fill)) {
                logger->error("Message too short for fill: {}", len);
                return;
            }

            clearing::fill* fill = reinterpret_cast<clearing::fill*>(buf);
            logger->info("Fill: client_id={}, symbol={}, quantity={}, price={}, side={}", fill->client_id, fill->symbol, fill->quantity,
                 fill->price, static_cast<int>(fill->side));
            if (fill->side == md::SIDE::BUY) {
                positions[fill->client_id][fill->symbol] += fill->quantity;
                total_buy[fill->client_id][fill->symbol] += (fill->quantity * fill->price);
            } else if (fill->side == md::SIDE::SELL) {
                positions[fill->client_id][fill->symbol] -= fill->quantity;
                total_sell[fill->client_id][fill->symbol] += (fill->quantity * fill->price);
            } else {
                logger->error("Unknown side: {}", static_cast<int>(fill->side));
                return;
            }
            volume[fill->client_id][fill->symbol] += fill->quantity;
            raw_pnl[fill->client_id][fill->symbol] = total_sell[fill->client_id][fill->symbol] - total_buy[fill->client_id][fill->symbol];

            // Competition rule: track high-water mark of |position|; if it
            // ever exceeded POSITION_LIMIT we mark the symbol as breached
            // (permanent — even if the position later shrinks back below
            // 10). Exempt clients (test bots) are skipped.
            if (EXEMPT_CLIENTS.find(fill->client_id) == EXEMPT_CLIENTS.end()) {
                int32_t abs_pos = std::abs(positions[fill->client_id][fill->symbol]);
                auto& hwm = max_abs_position[fill->client_id][fill->symbol];
                if (abs_pos > hwm) {
                    hwm = abs_pos;
                }
                if (hwm > POSITION_LIMIT) {
                    position_limit_breached[fill->client_id][fill->symbol] = true;
                }
            }
            break;
        }
        case MSG_TYPE::HEARTBEAT: {
            logger->info("Heartbeat {}", header->seq_num);
            break;
        }
        default:
            logger->error("Unknown message type: {}", static_cast<uint8_t>(header->msg_type));
            return;
    }
}

const std::unordered_map<uint32_t,std::unordered_map<uint32_t,int32_t>>& ClearingClient::get_positions() const {
    return positions;
}

const std::unordered_map<uint32_t,std::unordered_map<uint32_t, int32_t>>& ClearingClient::get_raw_pnl() const {
    return raw_pnl;
}

const std::unordered_map<uint32_t,std::unordered_map<uint32_t, uint32_t>>& ClearingClient::get_volume() const {
    return volume;
}

const std::unordered_map<uint32_t, std::unordered_map<uint32_t, int32_t>>& ClearingClient::get_max_abs_position() const {
    return max_abs_position;
}

const std::unordered_map<uint32_t, std::unordered_map<uint32_t, bool>>& ClearingClient::get_position_limit_breached() const {
    return position_limit_breached;
}

const std::unordered_map<uint32_t, bool>& ClearingClient::get_pnl_breached() const {
    return pnl_breached;
}

const std::unordered_map<uint32_t, double>& ClearingClient::get_frozen_pnl() const {
    return frozen_pnl;
}

void ClearingClient::set_pnl_breached(uint32_t client_id, double frozen_pnl_value) {
    if (!pnl_breached[client_id]) {
        pnl_breached[client_id] = true;
        frozen_pnl[client_id] = frozen_pnl_value;
    }
}

void ClearingClient::apply_etf_adjustment(uint32_t client_id, uint32_t symbol, int32_t delta) {
    positions[client_id][symbol] += delta;
    // jsonify_snapshot reads raw_pnl/volume/total_* with at(), so any symbol
    // that has a position must also have entries in those maps. Touch them
    // with operator[] which inserts a zero if absent. Without this an
    // adjustment for a (client, symbol) pair that hasn't yet seen a fill
    // throws std::out_of_range on the next snapshot tick.
    raw_pnl[client_id][symbol];
    total_buy[client_id][symbol];
    total_sell[client_id][symbol];
    volume[client_id][symbol];

    // Position-limit rule applies to the effective position regardless of
    // how it was acquired. Track the high-water mark and flag breaches.
    if (EXEMPT_CLIENTS.find(client_id) == EXEMPT_CLIENTS.end()) {
        int32_t abs_pos = std::abs(positions[client_id][symbol]);
        auto& hwm = max_abs_position[client_id][symbol];
        if (abs_pos > hwm) {
            hwm = abs_pos;
        }
        if (hwm > POSITION_LIMIT) {
            position_limit_breached[client_id][symbol] = true;
        }
    }
}

} // namespace ndfex::clearing