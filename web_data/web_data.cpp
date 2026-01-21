#include <bots/md_client.H>

#include "clearing_client.H"

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

#include <sstream>  // Add this include for std::ostringstream

typedef websocketpp::server<websocketpp::config::asio> web_server;
typedef std::set<websocketpp::connection_hdl,std::owner_less<websocketpp::connection_hdl>> connections;


void jsonify_snapshot(const ndfex::bots::MDClient& md_client, const ndfex::clearing::ClearingClient& clearing, std::string& json) {

    // get positions and P&L from clearing client
    auto positions = clearing.get_positions();
    auto raw_pnl = clearing.get_raw_pnl();
    auto get_volume = clearing.get_volume();

    // Use string stream instead of directly manipulating string
    std::ostringstream json_stream;

    // Start JSON object
    json_stream << "{ \"timestamp\": " << std::chrono::high_resolution_clock::now().time_since_epoch().count() << ",";
    json_stream << " \"snapshot\": [";

    bool first_symbol = true;
    for (const auto& symbol : {1, 2}) {
        if (!first_symbol) {
            json_stream << ",";
        }
        first_symbol = false;

        auto best_ask = md_client.get_best_ask(symbol);
        auto best_bid = md_client.get_best_bid(symbol);
        json_stream << "{\"symbol\": " << symbol
                    << ", \"best_bid\": " << best_bid.price
                    << ", \"best_ask\": " << best_ask.price << "}";
    }

    json_stream << "], \"positions\": [";

    bool first_position = true;
    for (const auto& symbol : {1, 2}) {
        auto best_ask = md_client.get_best_ask(symbol);
        auto best_bid = md_client.get_best_bid(symbol);

        for (const auto& client : positions) {
            if (!first_position) {
                json_stream << ",";
            }
            first_position = false;

            auto client_id = client.first;
            auto position = client.second.find(symbol);

            if (position != client.second.end()) {
                if (position->second >= 0) {
                    // mark the position to the bid price
                    double pnl = raw_pnl.at(client_id).at(symbol) + (best_bid.price * position->second);
                    // adjust pnl for fees
                    pnl -= get_volume.at(client_id).at(symbol) * 0.05;

                    json_stream << "{\"client_id\": " << client_id
                                << ", \"symbol\": " << symbol
                                << ", \"position\": " << position->second
                                << ", \"pnl\": " << pnl
                                << ", \"volume\": " << get_volume.at(client_id).at(symbol) << "}";
                } else if (position->second < 0) {
                    // mark the position to the ask price
                    double pnl = raw_pnl.at(client_id).at(symbol) + (best_ask.price * position->second);

                    // adjust pnl for fees
                    pnl -= get_volume.at(client_id).at(symbol) * 0.05;

                    json_stream << "{\"client_id\": " << client_id
                                << ", \"symbol\": " << symbol
                                << ", \"position\": " << position->second
                                << ", \"pnl\": " << pnl
                                << ", \"volume\": " << get_volume.at(client_id).at(symbol) << "}";
                }
            } else {
                json_stream << "{\"client_id\": " << client_id
                            << ", \"symbol\": " << symbol
                            << ", \"position\": 0, \"pnl\": 0, \"volume\": 0}";
            }
        }
    }
    json_stream << "]}";

    // Get the final string
    json = json_stream.str();
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << "<market data mcast ip address> <snapshot ip address> <clearing data mcast ip> <mcast bind ip>" << std::endl;
        return 1;
    }

    std::string mcast_ip = argv[1];
    std::string snapshot_ip = argv[2];
    std::string clearing_mcast_ip = argv[3];
    std::string mcast_bind_ip = argv[4];

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
        (void) hdl;
        return true; // Accept all connections for simplicity
    });

    server.clear_access_channels(websocketpp::log::alevel::frame_header | websocketpp::log::alevel::frame_payload);
    server.set_reuse_addr(true);
    server.listen(9002);
    server.start_accept();

    std::cout << "WebSocket server started on port 9002" << std::endl;

    // Start the server's event loop
    std::thread server_thread([&server]() {
        server.run();
    });

    ndfex::clearing::ClearingClient clearing_client(clearing_mcast_ip, 12346, mcast_bind_ip, logger);

    ndfex::bots::MDClient md_client(mcast_ip, 12345, snapshot_ip, 12345, mcast_bind_ip, logger,
                                    true);
    md_client.wait_for_snapshot();

    std::chrono::steady_clock::time_point last_published_ts = std::chrono::steady_clock::now();

    std::cout << "Starting MDClient" << std::endl;
    while (true) {
        md_client.process();
        clearing_client.process();

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_published_ts).count();
        if (elapsed > 100) {
            std::string json;
            jsonify_snapshot(md_client, clearing_client, json);

            for (const auto& hdl : connections) {
                try {
                    // Check if connection is still open before sending
                    if (server.get_con_from_hdl(hdl)->get_state() == websocketpp::session::state::open) {
                        server.send(hdl, json, websocketpp::frame::opcode::text);
                    }
                } catch (const websocketpp::exception& e) {
                    // Log the exception and continue with other connections
                    std::cerr << "Error sending to connection: " << e.what() << std::endl;

                    // You may want to remove invalid connections here if appropriate
                    // connections.erase(hdl);
                }
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
