#include "matching_engine.H"
#include "md_mcast.H"
#include "spsc_md_queue.H"
#include "spsc_subscriber.H"
#include "order_ladder.H"

#include "order_entry/oe_server.H"
#include "order_entry/oe_client_handler.H"

#include <spdlog/spdlog.h>
#include <spdlog/async.h>

#include <iostream>
#include <spdlog/sinks/daily_file_sink.h>

void set_cpu_affinity(int cpu) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) == -1) {
        std::cerr << "Failed to set cpu affinity" << std::endl;
    }
}

struct symbol_info {
    uint32_t symbol;
    uint32_t tick_size;
    uint32_t min_lot_size;
    uint32_t max_lot_size;

    int32_t max_price;
    int32_t min_price;
};

using namespace ndfex;

int main(int argc, char** argv) {

    std::vector<symbol_info> symbols = {
        {1, 10, 1, 1000, 10000000, 0}, // GOLD
        {2, 5, 1, 1000, 10000000, 0}, // BLUE
    };

    constexpr int logging_core = 1;
    auto on_thread_start = []() {
        set_cpu_affinity(logging_core); // Set affinity to target core
    };

    spdlog::init_thread_pool(8192, 1, on_thread_start);
    auto async_file = spdlog::daily_logger_mt<spdlog::async_factory>("async_logger", "logs/ME");

    async_file->info("Hello from async logger");
    async_file->flush();

    // create spsc md queue for as many symbols as we have
    std::vector<std::unique_ptr<SPSCMDQueue>> queues;
    for (ssize_t i = 0; i < symbols.size(); ++i) {
        queues.push_back(std::make_unique<SPSCMDQueue>(20000));
    }

    // create market data publisher
    MarketDataPublisher publisher(queues, "239.1.3.37", 12345, "129.74.247.7", async_file);

    typedef OrderLadder<SPSC_Subscriber> OrderLadder;

    // create an order ladder for each symbol
    std::vector<std::unique_ptr<OrderLadder>> ladders;
    for (const auto& symbol : symbols) {
        ladders.push_back(std::make_unique<OrderLadder>(new SPSC_Subscriber(*queues[symbol.symbol - 1]), symbol.symbol));
    }

    // create oe_server and client handlers
    std::unordered_map<std::string, oe::user_info> users;
    users["good"] = {"good", "password", 1};

    oe::ClientHandler<OrderLadder> client_handler(users, async_file);
    client_handler.set_matching_engine(1, ladders[0].get());
    client_handler.set_matching_engine(2, ladders[1].get());

    oe::EpollServer<oe::ClientHandler<OrderLadder>> oe_server(client_handler, 1234, async_file);


    std::thread t([&publisher, async_file] {
        set_cpu_affinity(2); // Set affinity to target core
        try {
            while (true) publisher.process();
        } catch (const std::exception& e) {
            async_file->error("Exception in publisher thread: {}", e.what());
        }
    });

    try {
        oe_server.run();
    } catch (const std::exception& e) {
        async_file->error("Exception in main thread: {}", e.what());
        t.join();
        async_file->flush();
        spdlog::shutdown();
        return 1;
    }

    t.join();
    async_file->flush();
    spdlog::shutdown();
    return 0;
}