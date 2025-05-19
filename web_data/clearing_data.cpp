#include <bots/md_client.H>

#include <iostream>
#include <chrono>
#include <string>
#include <set>
#include <thread>

#include <spdlog/async.h>
#include <spdlog/sinks/daily_file_sink.h>

#define ASIO_STANDALONE
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

typedef websocketpp::server<websocketpp::config::asio> web_server;
typedef std::set<websocketpp::connection_hdl,std::owner_less<websocketpp::connection_hdl>> connections;



int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << "<mcast ip address> <snapshot ip address> <mcast bind ip>" << std::endl;
        return 1;
    }

    std::string mcast_ip = argv[1];
    std::string snapshot_ip = argv[2];
    std::string mcast_bind_ip = argv[3];

    auto logger = spdlog::daily_logger_mt<spdlog::async_factory>("async_logger", "logs/web_data");

    connections connections;

    web_server server;
    server.init_asio();

    server.set_open_handler([&connections](websocketpp::connection_hdl hdl) {
        std::cout << "New connection opened" << std::endl;
        // Send snapshot data to the new connection
        connections.insert(hdl);
    });

    server.set_close_handler([&connections](websocketpp::connection_hdl hdl) {
        std::cout << "Connection closed" << std::endl;
        // Handle connection close
        connections.erase(hdl);
    });

    server.set_message_handler([](websocketpp::connection_hdl, web_server::message_ptr msg) {
        std::cout << "Received message: " << msg->get_payload() << std::endl;
        // Handle incoming messages
    });

    server.set_validate_handler([](websocketpp::connection_hdl hdl) {
        // Validate the connection
        return true; // Accept all connections for simplicity
    });

    server.set_reuse_addr(true);
    server.listen(9002);
    server.start_accept();

    std::cout << "WebSocket server started on port 9002" << std::endl;

    // Start the server's event loop
    std::thread server_thread([&server]() {
        server.run();
    });

    ndfex::bots::MDClient md_client(mcast_ip, 12345, snapshot_ip, 12345, mcast_bind_ip, logger);
    md_client.wait_for_snapshot();

    std::chrono::steady_clock::time_point last_published_ts = std::chrono::steady_clock::now();

    std::cout << "Starting MDClient" << std::endl;
    while (true) {
        md_client.process();

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_published_ts).count();
        if (elapsed > 100) {
            std::string json;
            jsonify_snapshot(md_client, json);
//            std::cout << "Sending snapshot: " << json << std::endl;
            for (const auto& hdl : connections) {
                // Send snapshot data to all connected clients
                server.send(hdl, json, websocketpp::frame::opcode::text);
            }

            last_published_ts = now;
        }
    }
    server.stop_listening();
    server.stop();

    server_thread.join();
    std::cout << "WebSocket server stopped" << std::endl;
    return 0;



}