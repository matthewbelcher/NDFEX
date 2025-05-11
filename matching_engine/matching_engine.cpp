#include "matching_engine.H"
#include "md_mcast.H"
#include "spsc_oe_queue.H"
#include "me_broker.H"
#include "order_ladder.H"
#include "symbol_definition.H"

#include "order_entry/oe_server.H"
#include "order_entry/oe_client_handler.H"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/async.h>

#include <iostream>
#include <fstream>


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

    // get ip address from command line
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <ip address> <mcast ip>" << std::endl;
        return 1;
    }

    std::string bind_ip = argv[1];
    std::string mcast_ip = argv[2];

    std::vector<symbol_definition> symbols = {
        {1, 10, 1, 1000, 10000000, 0}, // GOLD
        {2, 5, 1, 1000, 10000000, 0}, // BLUE
        //define UNDY & components
        {3, 5, 1, 1000, 10000000, 0}, //KNAN
        {4, 5, 1, 1000, 10000000, 0}, //STED
        {5, 5, 1, 1000, 10000000, 0}, //RYAN
        {6, 5, 1, 1000, 10000000, 0}, //SHAG
        {7, 5, 1, 1000, 10000000, 0}, //DBRT
        {8, 10, 1, 1000, 10000000, 0}, //UNDY
    };

    constexpr int logging_core = 1;
    auto on_thread_start = []() {
        set_cpu_affinity(logging_core); // Set affinity to target core
    };

    spdlog::init_thread_pool(8192, 1, on_thread_start);
    auto async_file = spdlog::daily_logger_mt<spdlog::async_factory>("async_logger", "logs/ME");

    async_file->info("Starting matching engine");
    async_file->flush();

    // create market data publisher
    MarketDataPublisher publisher(mcast_ip, 12345, bind_ip, async_file);

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

    // read users from file
    std::ifstream user_file("users.txt");
    if (!user_file.is_open()) {
        std::cerr << "Failed to open users file" << std::endl;
        return 1;
    }

    std::string line;
    while (std::getline(user_file, line)) {
        std::cout << line << std::endl;
        std::stringstream ss(line);
        std::string client_id, username, password;
        ss >> client_id >> username >> password;
        users[username] = {username, password, (uint32_t) std::stoi(client_id)};
        std::cout << "Added user: " << username << " " << password << " " << client_id << std::endl;
    }


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

    std::atomic<bool> running = true;
    std::thread t([&broker, async_file, &running]() {
        set_cpu_affinity(2); // Set affinity to target core
        try {
            while (running) {
                broker.process();
                async_file->flush();
            }
        } catch (const std::exception& e) {
            async_file->error("Exception in publisher thread: {}", e.what());
            std::cout << "Exception in publisher thread: " << e.what() << std::endl;
        }
    });

    try {
        oe_server.run();
    } catch (const std::exception& e) {
        async_file->error("Exception in main thread: {}", e.what());
        std::cout << "Exception in main thread: " << e.what() << std::endl;
        running = false;
        t.join();
        async_file->flush();
        spdlog::shutdown();
        return 1;
    }

    running = false;
    t.join();
    async_file->flush();
    spdlog::shutdown();
    return 0;
}