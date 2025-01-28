#include "snapshot_client.H"

#include <spdlog/spdlog.h>

#include "spsc_md_queue.H"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ndfex::md {

SnapshotClient::SnapshotClient(std::string ip, uint16_t port, std::string bind_ip, std::shared_ptr<spdlog::logger> logger,
                               SPSCMDQueue &queue) :
    logger(logger), queue(queue) {

    mcast_fd = socket(AF_INET, SOCK_DGRAM, 0);
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
    int ret = fcntl(mcast_fd, F_SETFL, flags | O_NONBLOCK);
    if (ret == -1) {
        logger->error("Failed to set socket to non-blocking: {}", strerror(errno));
        throw std::runtime_error("Failed to set socket to non-blocking");
    }

        // set SO_REUSEPORT
    int optval = 1;
    if (setsockopt(mcast_fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)) < 0) {
        logger->error("Failed to set SO_REUSEPORT: {}", strerror(errno));
        throw std::runtime_error("Failed to set SO_REUSEPORT");
    }

    // bind mcast socket to bind_ip
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(mcast_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        logger->error("Failed to bind socket: {}", strerror(errno));
        throw std::runtime_error("Failed to bind socket");
    }

    // join multicast group
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(ip.c_str());
    mreq.imr_interface.s_addr = inet_addr(bind_ip.c_str());

    if (setsockopt(mcast_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) < 0) {
        throw std::runtime_error("Failed to join multicast group");
    }

    logger->info("Listening on {}:{}", ip, port);
}

SnapshotClient::~SnapshotClient() {
    close(mcast_fd);
}

void SnapshotClient::process() {

    ssize_t len = recvfrom(mcast_fd, buf, sizeof(buf), 0, nullptr, nullptr);
    if (len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        logger->error("Failed to receive message: {}", strerror(errno));
        throw std::runtime_error("Failed to receive message");
    }

    uint8_t* buf_ptr = buf;
    size_t buf_len = len;
    while (buf_len > 0) {
        md::md_header* header = reinterpret_cast<md::md_header*>(buf_ptr);
        if (header->magic_number != md::MAGIC_NUMBER) {
            logger->error("Invalid magic number: {}", header->magic_number);
            return;
        }

        uint32_t seq_num = header->seq_num;

        switch (header->msg_type) {
            case md::MSG_TYPE::NEW_ORDER: {
                if (buf_len < sizeof(md::new_order)) {
                    logger->error("Message too short for new order: {}", len);
                    return;
                }

                md::new_order* new_order = reinterpret_cast<md::new_order*>(buf_ptr);
                queue.emplace(md::MSG_TYPE::NEW_ORDER, seq_num, new_order->order_id, new_order->symbol, new_order->side, new_order->quantity, new_order->price, new_order->flags);
                break;
            }
            case md::MSG_TYPE::DELETE_ORDER: {
                if (buf_len < sizeof(md::delete_order)) {
                    logger->error("Message too short for delete order: {}", len);
                    return;
                }
                queue.emplace(md::MSG_TYPE::DELETE_ORDER, seq_num, reinterpret_cast<md::delete_order*>(buf_ptr)->order_id, 0, md::SIDE::BUY, 0, 0, 0);
                break;
            }
            case md::MSG_TYPE::MODIFY_ORDER: {
                if (buf_len < sizeof(md::modify_order)) {
                    logger->error("Message too short for modify order: {}", len);
                    return;
                }

                md::modify_order* modify_order = reinterpret_cast<md::modify_order*>(buf_ptr);
                queue.emplace(md::MSG_TYPE::MODIFY_ORDER, seq_num, modify_order->order_id, 0, modify_order->side, modify_order->quantity, modify_order->price, 0);
                break;
            }
            case md::MSG_TYPE::TRADE: {
                if (buf_len < sizeof(md::trade)) {
                    logger->error("Message too short for trade: {}", len);
                    return;
                }

                md::trade* trade = reinterpret_cast<md::trade*>(buf_ptr);
                queue.emplace(md::MSG_TYPE::TRADE, seq_num, trade->order_id, 0, md::SIDE::BUY, trade->quantity, trade->price, 0);
                break;
            }
            case md::MSG_TYPE::TRADE_SUMMARY: {
                if (buf_len < sizeof(md::trade_summary)) {
                    logger->error("Message too short for trade summary: {}", len);
                    return;
                }
                // trade summaries aren't used in the snapshot client
                break;
            }
            case md::MSG_TYPE::HEARTBEAT: {
                // just to update the sequence number
                queue.emplace(md::MSG_TYPE::HEARTBEAT, seq_num, 0, 0, md::SIDE::BUY, 0, 0, 0);
                break;
            }
            default:
                logger->error("Unknown message type: {}", static_cast<uint8_t>(header->msg_type));
                break;
        }

        buf_len -= header->length;
        buf_ptr += header->length;
    }

}


} // namespace ndfex::md