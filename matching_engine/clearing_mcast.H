#include "clearing_protocol.H"

#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>
#include <arpa/inet.h>

#include "utils.H"

#include <spdlog/spdlog.h>

namespace ndfex::clearing {

    class ClearingPublisher {
    public:
        ClearingPublisher(const std::string& mcastGroup,
                          const std::uint16_t mcastPort,
                          const std::string& localMcastIface,
                          std::shared_ptr<spdlog::logger> logger);

        void publish_fill(uint64_t order_id, uint32_t client_id, uint32_t symbol, uint32_t quantity, int32_t price, md::SIDE side);

        void publish_heartbeat();

    private:

        std::shared_ptr<spdlog::logger> logger;
        int multicast_fd;
        struct sockaddr_in mcast_addr;

        fill fill_msg;
    };

} // namespace ndfex
