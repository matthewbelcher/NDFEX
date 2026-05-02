#include <bots/md_client.H>

#include "clearing_client.H"

#include <iostream>
#include <chrono>
#include <string>
#include <set>
#include <thread>
#include <deque>
#include <mutex>

#include <spdlog/async.h>
#include <spdlog/sinks/daily_file_sink.h>

#include "adjustment_log_reader.H"

#define ASIO_STANDALONE
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <sstream>  // Add this include for std::ostringstream

typedef websocketpp::server<websocketpp::config::asio> web_server;
typedef std::set<websocketpp::connection_hdl,std::owner_less<websocketpp::connection_hdl>> connections;

// Trade summary storage for homework validation
struct TradeRecord {
    uint64_t timestamp;
    uint32_t seq_num;
    uint32_t symbol;
    uint32_t quantity;
    int32_t price;
    uint8_t aggressor_side;
};

class TradeCollector : public ndfex::bots::MDClient::TradeSummaryListener {
public:
    static constexpr size_t MAX_TRADES = 1000;

    void on_trade_summary(uint32_t seq_num, uint32_t symbol, uint32_t quantity, int32_t price, ndfex::md::SIDE aggressor_side) override {
        std::lock_guard<std::mutex> lock(mutex);
        trades.push_back({
            static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()),
            seq_num,
            symbol,
            quantity,
            price,
            static_cast<uint8_t>(aggressor_side)
        });
        if (trades.size() > MAX_TRADES) {
            trades.pop_front();
        }
    }

    std::deque<TradeRecord> get_recent_trades() {
        std::lock_guard<std::mutex> lock(mutex);
        return trades;
    }

private:
    std::deque<TradeRecord> trades;
    std::mutex mutex;
};


// Symbol ids the web_data serializer reports on. Must match the matching
// engine's symbol list.
//   1      GOLD
//   2      BLUE
//   3..12  UNDY ETF dorm components (KNAN, STED, FISH, DILN, SORN,
//          RYAN, LYON, WLSH, LEWI, BDIN)
//   13     UNDY ETF
static constexpr int EXPECTED_SYMBOLS[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13
};

void jsonify_snapshot(const ndfex::bots::MDClient& md_client, ndfex::clearing::ClearingClient& clearing,
                      TradeCollector& trade_collector, std::string& json) {

    // get positions and P&L from clearing client
    auto positions = clearing.get_positions();
    auto raw_pnl = clearing.get_raw_pnl();
    auto get_volume = clearing.get_volume();
    auto pos_breached = clearing.get_position_limit_breached();
    auto pnl_breached = clearing.get_pnl_breached();
    auto frozen_pnl = clearing.get_frozen_pnl();

    // Use string stream instead of directly manipulating string
    std::ostringstream json_stream;

    // Start JSON object
    json_stream << "{ \"seq_num\": " << md_client.get_last_seq_num() << ",";
    json_stream << " \"timestamp\": " << std::chrono::high_resolution_clock::now().time_since_epoch().count() << ",";
    json_stream << " \"snapshot\": [";

    bool first_symbol = true;
    for (const auto& symbol : EXPECTED_SYMBOLS) {
        if (!first_symbol) {
            json_stream << ",";
        }
        first_symbol = false;

        auto best_ask = md_client.get_best_ask(symbol);
        auto best_bid = md_client.get_best_bid(symbol);
        json_stream << "{\"symbol\": " << symbol
                    << ", \"best_bid\": " << best_bid.price
                    << ", \"best_bid_qty\": " << best_bid.quantity
                    << ", \"best_ask\": " << best_ask.price
                    << ", \"best_ask_qty\": " << best_ask.quantity << "}";
    }

    json_stream << "], \"positions\": [";

    // First pass: compute per-client per-symbol PnL, apply position breach clamping,
    // then check aggregate PnL for min PnL breach.
    // We need to iterate clients first to compute totals, then emit JSON.

    // Collect all client IDs
    std::set<uint32_t> client_ids;
    for (const auto& client : positions) {
        client_ids.insert(client.first);
    }

    // Per-client per-symbol PnL and breach info. unclamped_pnl preserves the
    // pre-clamp mark-to-market PnL so the frontend can show "what it would
    // have been" alongside the locked value on breached symbols.
    struct SymbolEntry {
        int32_t position;
        double pnl;
        double unclamped_pnl;
        uint32_t vol;
        bool pos_breach;
    };
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, SymbolEntry>> client_entries;

    for (auto client_id : client_ids) {
        bool is_exempt = ndfex::clearing::EXEMPT_CLIENTS.count(client_id) > 0;

        double total_pnl = 0.0;

        for (const auto& symbol : EXPECTED_SYMBOLS) {
            auto best_ask = md_client.get_best_ask(symbol);
            auto best_bid = md_client.get_best_bid(symbol);

            auto pos_it = positions.at(client_id).find(symbol);
            int32_t pos = 0;
            double pnl = 0.0;
            uint32_t vol = 0;
            bool sym_pos_breach = false;

            if (pos_it != positions.at(client_id).end()) {
                pos = pos_it->second;
                if (pos >= 0) {
                    pnl = raw_pnl.at(client_id).at(symbol) + (best_bid.price * pos);
                } else {
                    pnl = raw_pnl.at(client_id).at(symbol) + (best_ask.price * pos);
                }
                vol = get_volume.at(client_id).at(symbol);
                pnl -= vol * 0.05;
            }

            double unclamped_pnl = pnl;

            // Check position breach and clamp PnL if breached (non-exempt only)
            if (!is_exempt) {
                auto pb_client_it = pos_breached.find(client_id);
                if (pb_client_it != pos_breached.end()) {
                    auto pb_sym_it = pb_client_it->second.find(symbol);
                    if (pb_sym_it != pb_client_it->second.end() && pb_sym_it->second) {
                        sym_pos_breach = true;
                        // Per competition rule: penalized_pnl = min(0, symbol_pnl).
                        // Positive PnL is wiped on a breached symbol; losses are
                        // kept. The clamp prevents teams from profiting on a
                        // symbol where they breached the position limit.
                        if (pnl > 0.0) {
                            pnl = 0.0;
                        }
                    }
                }
            }

            client_entries[client_id][symbol] = {pos, pnl, unclamped_pnl, vol, sym_pos_breach};
            total_pnl += pnl;
        }

        // Check min PnL breach (non-exempt only)
        if (!is_exempt && !pnl_breached[client_id] && total_pnl < ndfex::clearing::MIN_PNL) {
            clearing.set_pnl_breached(client_id, total_pnl);
            pnl_breached[client_id] = true;
            frozen_pnl[client_id] = total_pnl;
        }
    }

    // Second pass: emit JSON, applying PnL freeze if breached
    bool first_position = true;
    for (const auto& symbol : EXPECTED_SYMBOLS) {
        for (auto client_id : client_ids) {
            if (!first_position) {
                json_stream << ",";
            }
            first_position = false;

            bool is_exempt = ndfex::clearing::EXEMPT_CLIENTS.count(client_id) > 0;
            const auto& entry = client_entries[client_id][symbol];
            bool client_pnl_breached = !is_exempt && pnl_breached.count(client_id) && pnl_breached[client_id];

            json_stream << "{\"client_id\": " << client_id
                        << ", \"symbol\": " << symbol
                        << ", \"position\": " << entry.position
                        << ", \"pnl\": " << entry.pnl
                        << ", \"volume\": " << entry.vol
                        << ", \"position_breach\": " << (entry.pos_breach ? "true" : "false")
                        << ", \"pnl_breach\": " << (client_pnl_breached ? "true" : "false");

            // Include the unclamped (would-be) PnL only when the position
            // limit has been breached on this symbol — that's when the
            // frontend wants to show "0 (X)" with the unclamped value beside
            // the locked one.
            if (entry.pos_breach) {
                json_stream << ", \"unclamped_pnl\": " << entry.unclamped_pnl;
            }

            // If PnL breached, override pnl with frozen value distributed proportionally
            // Actually, the frontend sums per-symbol PnL for the total.
            // When pnl_breached, we want the total to show frozen_pnl.
            // Simplest: include frozen_total in each row so frontend can use it.
            if (client_pnl_breached) {
                json_stream << ", \"frozen_pnl\": " << frozen_pnl[client_id];
            }

            json_stream << "}";
        }
    }
    json_stream << "], \"trades\": [";

    // Add recent trades for homework validation
    auto recent_trades = trade_collector.get_recent_trades();
    bool first_trade = true;
    for (const auto& trade : recent_trades) {
        if (!first_trade) {
            json_stream << ",";
        }
        first_trade = false;
        json_stream << "{\"timestamp\": " << trade.timestamp
                    << ", \"seq_num\": " << trade.seq_num
                    << ", \"symbol\": " << trade.symbol
                    << ", \"aggressor_side\": " << static_cast<int>(trade.aggressor_side)
                    << ", \"quantity\": " << trade.quantity
                    << ", \"price\": " << trade.price << "}";
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
    // Protects `connections` against concurrent mutation by the websocketpp
    // server thread (open/close handlers) and reads from the main broadcast
    // loop. Without this, the red-black tree can be restructured mid-iteration
    // and the broadcast loop walks a freed node (SIGSEGV in _Rb_tree_increment).
    std::mutex connections_mutex;

    web_server server;
    server.init_asio();

    server.set_open_handler([&connections, &connections_mutex](websocketpp::connection_hdl hdl) {
        std::cout << "New connection opened" << std::endl;
        std::lock_guard<std::mutex> lock(connections_mutex);
        connections.insert(hdl);
    });

    server.set_close_handler([&connections, &connections_mutex](websocketpp::connection_hdl hdl) {
        std::cout << "Connection closed" << std::endl;
        std::lock_guard<std::mutex> lock(connections_mutex);
        connections.erase(hdl);
    });

    server.set_message_handler([](websocketpp::connection_hdl, web_server::message_ptr msg) {
        std::cout << "Received message: " << msg->get_payload() << std::endl;
        // Handle incoming messages
    });

    server.set_validate_handler([](websocketpp::connection_hdl /*hdl*/) {
        // Validate the connection
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

    // Create trade collector for homework validation
    TradeCollector trade_collector;
    md_client.register_trade_summary_listener(&trade_collector);

    md_client.wait_for_snapshot();

    // Tail etf_service's create/redeem log so leaderboard positions reflect
    // ETF adjustments. Constructor replays the file on startup so we recover
    // every prior adjustment across web_data restarts.
    ndfex::clearing::AdjustmentLogReader adjustment_log_reader(
        "../logs/etf_adjustments.log", clearing_client, logger);

    std::chrono::steady_clock::time_point last_published_ts = std::chrono::steady_clock::now();

    std::cout << "Starting MDClient" << std::endl;
    while (true) {
        md_client.process();
        clearing_client.process();
        adjustment_log_reader.process();

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_published_ts).count();
        if (elapsed > 100) {
            std::string json;
            jsonify_snapshot(md_client, clearing_client, trade_collector, json);

            // Snapshot the connection set under the lock, then iterate the
            // copy lock-free. server.send() can synchronously invoke error
            // callbacks which re-enter close_handler → erase() on this set,
            // so holding the lock across send() would deadlock.
            decltype(connections) connections_copy;
            {
                std::lock_guard<std::mutex> lock(connections_mutex);
                connections_copy = connections;
            }

            for (const auto& hdl : connections_copy) {
                try {
                    if (server.get_con_from_hdl(hdl)->get_state() == websocketpp::session::state::open) {
                        server.send(hdl, json, websocketpp::frame::opcode::text);
                    }
                } catch (const websocketpp::exception& e) {
                    std::cerr << "Error sending to connection: " << e.what() << std::endl;
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