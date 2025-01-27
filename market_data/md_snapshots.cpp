#include "snapshot_client.H"
#include "snapshot_writer.H"

#include <spdlog/spdlog.h>
#include <iostream>

#include <thread>
#include <atomic>

int main(int argc, char** argv) {

    ndfex::SPSCMDQueue queue(1000);
    auto logger = spdlog::default_logger();

    if (argc != 6) {
        std::cerr << "Usage: " << argv[0] << " <ip address> <port> <snapshot mcast ip address> <mcast port> <mcast bind ip>" << std::endl;
        return 1;
    }

    std::string ip = argv[1];
    uint16_t listen_port = std::stoi(argv[2]);
    std::string snapshot_mcast_ip = argv[3];
    uint16_t out_port = std::stoi(argv[4]);
    std::string mcast_bind_ip = argv[5];

    std::cout << "Starting snapshot client" << std::endl;
    ndfex::md::SnapshotClient client(ip, listen_port, mcast_bind_ip, logger, queue);

    std::cout << "Starting snapshot writer" << std::endl;
    ndfex::md::SnapshotWriter writer(snapshot_mcast_ip, out_port, mcast_bind_ip, logger, queue);

    std::cout << "Starting snapshot client thread" << std::endl;
    std::atomic<bool> running = true;
    std::thread client_thread([&client, &running, logger]() {
        try {
            while (running) {
                client.process();
            }
        } catch (const std::exception& e) {
            logger->error("Snapshot client failed: {}", e.what());
        }
    });

    try {
        while (true) {
            writer.process();
        }
    } catch (const std::exception& e) {
        logger->error("Snapshot writer failed: {}", e.what());
    }

    running = false;
    client_thread.join();
    spdlog::shutdown();
    return 0;
}