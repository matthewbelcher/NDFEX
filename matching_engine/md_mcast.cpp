
#include "md_mcast.H"
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>

namespace ndfex {

MarketDataPublisher::MarketDataPublisher(std::vector<std::unique_ptr<SPSCMDQueue>>& queues,
    const std::string mcastGroup,
    const std::uint16_t mcastPort,
    const std::string localMcastIface) : queues(queues) {
    multicast_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (multicast_fd < 0) {
        throw std::runtime_error("Failed to create multicast socket " + std::string(strerror(errno)));
    }

    mcast_addr.sin_family = AF_INET;
    mcast_addr.sin_port = htons(mcastPort);
    mcast_addr.sin_addr.s_addr = inet_addr(mcastGroup.c_str());

    struct in_addr localInterface;
    localInterface.s_addr = inet_addr(localMcastIface.c_str());
    int rc = setsockopt(multicast_fd, IPPROTO_IP, IP_MULTICAST_IF, (char*)&localInterface, sizeof(localInterface));
    if (rc < 0) {
        throw std::runtime_error("Failed to bind to multicast address " + std::string(strerror(errno)));
    }
}

void MarketDataPublisher::process() {
    for (auto& queue : queues) {
        while (queue->front() && buffer_size + md::MAX_MSG_SIZE < sizeof(buffer)) {
            md_payload payload = *queue->front();

            md::md_header* header = reinterpret_cast<md::md_header*>(&buffer[buffer_size]);
            header->magic_number = md::MAGIC_NUMBER;
            header->seq_num = seq_num++;
            header->timestamp = 0; // TODO nanotime();
            header->msg_type = payload.msg_type;

            if (payload.msg_type == md::MSG_TYPE::NEW_ORDER) {
                md::new_order* msg = reinterpret_cast<md::new_order*>(&buffer[buffer_size]);
                msg->order_id = payload.order_id;
                msg->symbol = payload.symbol;
                msg->side = payload.side;
                msg->quantity = payload.quantity;
                msg->price = payload.price;
                msg->flags = payload.flags;
                header->length = sizeof(md::new_order);
                buffer_size += sizeof(md::new_order);

            } else if (payload.msg_type == md::MSG_TYPE::DELETE_ORDER) {
                md::delete_order* msg = reinterpret_cast<md::delete_order*>(&buffer[buffer_size]);
                msg->order_id = payload.order_id;
                header->length = sizeof(md::delete_order);
                buffer_size += sizeof(md::delete_order);

            } else if (payload.msg_type == md::MSG_TYPE::MODIFY_ORDER) {
                md::modify_order* msg = reinterpret_cast<md::modify_order*>(&buffer[buffer_size]);
                msg->order_id = payload.order_id;
                msg->side = payload.side;
                msg->quantity = payload.quantity;
                msg->price = payload.price;
                header->length = sizeof(md::modify_order);
                buffer_size += sizeof(md::modify_order);

            } else if (payload.msg_type == md::MSG_TYPE::TRADE) {
                md::trade* msg = reinterpret_cast<md::trade*>(&buffer[buffer_size]);
                msg->order_id = payload.order_id;
                msg->quantity = payload.quantity;
                msg->price = payload.price;
                header->length = sizeof(md::trade);
                buffer_size += sizeof(md::trade);

            } else if (payload.msg_type == md::MSG_TYPE::TRADE_SUMMARY) {
                md::trade_summary* msg = reinterpret_cast<md::trade_summary*>(&buffer[buffer_size]);
                msg->symbol = payload.symbol;
                msg->aggressor_side = payload.side;
                msg->total_quantity = payload.quantity;
                msg->last_price = payload.price;
                header->length = sizeof(md::trade_summary);
                buffer_size += sizeof(md::trade_summary);
            }
            queue->pop();
        }


        if (sendto(multicast_fd, &buffer, buffer_size, 0, reinterpret_cast<struct sockaddr*>(&mcast_addr), sizeof(mcast_addr)) < 0) {
            throw std::runtime_error("Failed to send payload to multicast group " + std::string(strerror(errno)));
        }
        buffer_size = 0;
    }
}

}  // namespace ndfex