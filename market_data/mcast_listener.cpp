#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>

int main(int argc, char* argv[]) {

    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <multicast_ip> <port>" << std::endl;
        return 1;
    }

    int mcast_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (mcast_fd < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return 1;
    }

    struct sockaddr_in mcast_addr;
    memset(&mcast_addr, 0, sizeof(mcast_addr));
    mcast_addr.sin_family = AF_INET;
    mcast_addr.sin_port = htons(atoi(argv[2]));
    mcast_addr.sin_addr.s_addr = inet_addr(argv[1]);

    if (bind(mcast_fd, (struct sockaddr*)&mcast_addr, sizeof(mcast_addr)) < 0) {
        std::cerr << "Failed to bind socket: " << strerror(errno) << std::endl;
        return 1;
    }

    // add group membership
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(argv[1]);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(mcast_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        std::cerr << "Failed to add group membership: " << strerror(errno) << std::endl;
        return 1;
    }

    int ret = read(mcast_fd, NULL, 0);


}