#include "matching_engine.H"
#include "md_mcast.H"
#include "spsc_oe_queue.H"
#include "me_broker.H"
#include "order_ladder.H"
#include "symbol_definition.H"

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

using namespace ndfex;

int main(int argc, char** argv) {

    std::vector<symbol_definition> symbols = {
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


    // create market data publisher
    MarketDataPublisher publisher("239.1.3.37", 12345, "129.74.247.7", async_file);


    // create broker queues
    SPSCOEQueue to_client(1000);
    SPSCOEQueue from_client(1000);

    // create matching engine broker
    MatchingEngineBroker broker(to_client, from_client, publisher, async_file);

    // add symbols to broker
    for (const auto& symbol : symbols) {
        broker.add_symbol(symbol.symbol);
    }

    // create oe_server and client handlers
    std::unordered_map<std::string, oe::user_info> users;
    users["good"] = {"good", "password", 1};

    auto symbols_map = [] (const std::vector<symbol_definition>& symbols) {
        std::unordered_map<uint32_t, symbol_definition> map;
        for (const auto& symbol : symbols) {
            map[symbol.symbol] = symbol;
        }
        return map;
    }(symbols);

    oe::OrderEntryValidator validator(symbols_map, async_file);
    oe::ClientHandler client_handler(validator, from_client, to_client, users, async_file);

    oe::EpollServer<oe::ClientHandler> oe_server(client_handler, 1234, async_file);


    std::thread t([&broker, async_file] {
        set_cpu_affinity(2); // Set affinity to target core
        try {
            while (true) broker.process();
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