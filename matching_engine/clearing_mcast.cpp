#include "clearing_mcast.H"

namespace ndfex::clearing {

    ClearingPublisher::ClearingPublisher(const std::string& mcastGroup,
                                         const std::uint16_t mcastPort,
                                         const std::string& localMcastIface,
                                         std::shared_ptr<spdlog::logger> logger)
        : logger(logger) {

            multicast_fd = socket(AF_INET, SOCK_DGRAM, 0);
            if (multicast_fd < 0) {
                logger->error("Failed to create multicast socket: {}", strerror(errno));
                throw std::runtime_error("Failed to create multicast socket " + std::string(strerror(errno)));
            }

            mcast_addr.sin_family = AF_INET;
            mcast_addr.sin_port = htons(mcastPort);
            mcast_addr.sin_addr.s_addr = inet_addr(mcastGroup.c_str());

            struct in_addr localInterface;
            localInterface.s_addr = inet_addr(localMcastIface.c_str());
            int rc = setsockopt(multicast_fd, IPPROTO_IP, IP_MULTICAST_IF, (char*)&localInterface, sizeof(localInterface));
            if (rc < 0) {
                logger->error("Failed to bind to multicast address: {}", strerror(errno));
                throw std::runtime_error("Failed to bind to multicast address " + std::string(strerror(errno)));
            }

            // set mcast socket to non-blocking
            int flags = fcntl(multicast_fd, F_GETFL, 0);
            if (flags == -1) {
                logger->error("Failed to get socket flags: {}", strerror(errno));
                throw std::runtime_error("Failed to get socket flags " + std::string(strerror(errno)));
            }

            rc = fcntl(multicast_fd, F_SETFL, flags | O_NONBLOCK);
            if (rc == -1) {
                logger->error("Failed to set socket non-blocking: {}", strerror(errno));
                throw std::runtime_error("Failed to set socket non-blocking " + std::string(strerror(errno)));
            }

            char loopch = 1;
            if (setsockopt(multicast_fd, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&loopch, sizeof(loopch)) < 0)
            {
                throw std::runtime_error("Failed to enable multicast loopback");
            }

            fill_msg.header.magic_number = MAGIC_NUMBER;
            fill_msg.header.length = sizeof(fill);
            fill_msg.header.msg_type = MSG_TYPE::FILL;
            fill_msg.header.seq_num = 0;

            fill_msg.client_id = 0;
            fill_msg.symbol = 0;
            fill_msg.quantity = 0;
            fill_msg.price = 0;
            fill_msg.side = md::SIDE::BUY;
    }

    void ClearingPublisher::publish_fill(uint64_t order_id, uint32_t client_id, uint32_t symbol, uint32_t quantity, int32_t price, md::SIDE side) {
        fill_msg.header.length = sizeof(fill);
        fill_msg.header.msg_type = MSG_TYPE::FILL;
        fill_msg.header.seq_num++;
        fill_msg.client_id = client_id;
        fill_msg.symbol = symbol;
        fill_msg.quantity = quantity;
        fill_msg.price = price;
        fill_msg.side = side;

        int rc = sendto(multicast_fd, &fill_msg, sizeof(fill), 0, reinterpret_cast<struct sockaddr*>(&mcast_addr), sizeof(mcast_addr));
        if (rc < 0) {
            logger->error("Failed to send payload to multicast group: {}", strerror(errno));
            throw std::runtime_error("Failed to send payload to multicast group " + std::string(strerror(errno)));
        }
    }

    void ClearingPublisher::publish_heartbeat() {
        // heartbeats are empty messages
        fill_msg.header.length = sizeof(clearing_header);
        fill_msg.header.msg_type = MSG_TYPE::HEARTBEAT;
        fill_msg.header.seq_num++;
        fill_msg.client_id = 0;
        fill_msg.symbol = 0;
        fill_msg.quantity = 0;
        fill_msg.price = 0;

        int rc = sendto(multicast_fd, &fill_msg, sizeof(clearing_header), 0, reinterpret_cast<struct sockaddr*>(&mcast_addr), sizeof(mcast_addr));
        if (rc < 0) {
            logger->error("Failed to send payload to multicast group: {}", strerror(errno));
            throw std::runtime_error("Failed to send payload to multicast group " + std::string(strerror(errno)));
        }
    }
}

