#include "snapshot_client.H"
#include "snapshot_writer.H"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/async.h>
#include <iostream>

#include <thread>
#include <atomic>

void set_cpu_affinity(int cpu) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) == -1) {
        std::cerr << "Failed to set cpu affinity" << std::endl;
    }
}


int main(int argc, char** argv) {

    ndfex::SPSCMDQueue queue(80000);
    auto logger = spdlog::daily_logger_mt<spdlog::async_factory>("async_logger", "logs/SNAPSHOT");

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
        set_cpu_affinity(3); // Set affinity to target core
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