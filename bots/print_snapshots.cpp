#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>

#include <iostream>
#include <market_data/md_protocol.H>

using namespace ndfex::md;

int main(int argc, char* argv[]) {

    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <multicast_ip> <port> <local ip>" << std::endl;
        return 1;
    }

    int mcast_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (mcast_fd < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return 1;
    }

    // set mcast socket to non-blocking
    int flags = fcntl(mcast_fd, F_GETFL, 0);
    if (flags == -1) {
        std::cerr << "Failed to get socket flags: " << strerror(errno) << std::endl;
        return 1;
    }

    if (fcntl(mcast_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "Failed to set socket to non-blocking: " << strerror(errno) << std::endl;
        return 1;
    }

    // set SO_REUSEPORT
    int optval = 1;
    if (setsockopt(mcast_fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)) < 0) {
        std::cerr << "Failed to set SO_REUSEPORT: " << strerror(errno) << std::endl;
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[2]));
    addr.sin_addr.s_addr = inet_addr(argv[1]);

    if (bind(mcast_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind socket: " << strerror(errno) << std::endl;
        return 1;
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(argv[1]);
    mreq.imr_interface.s_addr = inet_addr(argv[3]);

    if (setsockopt(mcast_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) < 0) {
        std::cerr << "Failed to join multicast group: " << strerror(errno) << std::endl;
        return 1;
    }

    while (true) {
        uint8_t buf[4096];
        ssize_t len = recvfrom(mcast_fd, buf, sizeof(buf), 0, nullptr, nullptr);
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            std::cerr << "Failed to receive message: " << strerror(errno) << std::endl;
            return 1;
        }
        ssize_t buf_ptr = 0;
        while (len > 0) {
            if (len < sizeof(md_header)) {
                std::cerr << "Message too short: " << len << std::endl;
                return 1;
            }
            md_header* header = reinterpret_cast<md_header*>(buf + buf_ptr);
            if (header->magic_number != SNAPSHOT_MAGIC_NUMBER) {
                std::cerr << "Invalid magic number: " << header->magic_number << std::endl;
                return 1;
            }

            std::cout << "Message type: " << static_cast<int>(header->msg_type) << std::endl;
            if (header->msg_type == MSG_TYPE::SNAPSHOT_INFO) {
                snapshot_info* snapshot_info = reinterpret_cast<struct snapshot_info*>(buf + buf_ptr);
                std::cout << "Snapshot info: symbol=" << snapshot_info->symbol <<
                    ", last_seq_num=" << snapshot_info->last_md_seq_num <<
                    ", bid_count=" << snapshot_info->bid_count <<
                    ", ask_count=" << snapshot_info->ask_count << std::endl;
            } else if (header->msg_type == MSG_TYPE::NEW_ORDER) {
                new_order* new_order = reinterpret_cast<struct new_order*>(buf + buf_ptr);
                std::cout << "New order: order_id=" << new_order->order_id <<
                    ", symbol=" << new_order->symbol <<
                    ", side=" << static_cast<int>(new_order->side) <<
                    ", quantity=" << new_order->quantity <<
                    ", price=" << new_order->price << std::endl;
            }
            len -= header->length;
            buf_ptr += header->length;
        }
    }
}