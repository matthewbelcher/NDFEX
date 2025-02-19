#include "market_data/md_protocol.H"

#include <bots/order_book.H>
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>


#include <pcap/pcap.h>
#include <iostream>

static void parse_and_apply_live_message(const u_char*& packet, uint32_t& len,
     std::unordered_map<uint32_t, ndfex::bots::OrderBook*>& order_books,
     std::unordered_map<uint64_t, uint32_t>& order_to_symbol,
     std::shared_ptr<spdlog::logger>& async_logger) {

        ndfex::md::md_header* md_header = reinterpret_cast<ndfex::md::md_header*>(const_cast<u_char*>(packet));
        if (md_header->magic_number != ndfex::md::MAGIC_NUMBER)
        {
            std::cerr << "Invalid magic number: " << md_header->magic_number << std::endl;
            return;
        }

        switch(md_header->msg_type) {
            case ndfex::md::MSG_TYPE::NEW_ORDER: {
                if (len < sizeof(ndfex::md::new_order)) {
                    std::cerr << "Message too short for new order: " << len << std::endl;
                    return;
                }

                ndfex::md::new_order* new_order = reinterpret_cast<ndfex::md::new_order*>(const_cast<u_char*>(packet));
                if (order_books.find(new_order->symbol) == order_books.end()) {
                    order_books[new_order->symbol] = new ndfex::bots::OrderBook(async_logger);
                }
                order_books[new_order->symbol]->new_order(new_order->order_id, new_order->side, new_order->quantity, new_order->price, new_order->flags);
                order_to_symbol[new_order->order_id] = new_order->symbol;
                break;
            }

            case ndfex::md::MSG_TYPE::DELETE_ORDER: {
                if (len < sizeof(ndfex::md::delete_order)) {
                    std::cerr << "Message too short for delete order: " << len << std::endl;
                    return;
                }

                ndfex::md::delete_order* delete_order = reinterpret_cast<ndfex::md::delete_order*>(const_cast<u_char*>(packet));
                auto it = order_to_symbol.find(delete_order->order_id);
                if (it == order_to_symbol.end()) {
                    std::cerr << "Order not found: " << delete_order->order_id << std::endl;
                    break;
                }
                uint32_t symbol = it->second;
                if (order_books.find(symbol) == order_books.end()) {
                    std::cerr << "Order book not found: " << symbol << std::endl;
                    break;
                }
                order_books[symbol]->delete_order(delete_order->order_id);
                order_to_symbol.erase(it);
                break;
            }
            case ndfex::md::MSG_TYPE::MODIFY_ORDER: {
                if (len < sizeof(ndfex::md::modify_order)) {
                    std::cerr << "Message too short for modify order: " << len << std::endl;
                    return;
                }

                ndfex::md::modify_order* modify_order = reinterpret_cast<ndfex::md::modify_order*>(const_cast<u_char*>(packet));
                auto it = order_to_symbol.find(modify_order->order_id);
                if (it == order_to_symbol.end()) {
                    std::cerr << "Order not found: " << modify_order->order_id << std::endl;
                    break;
                }
                uint32_t symbol = it->second;
                if (order_books.find(symbol) == order_books.end()) {
                    std::cerr << "Order book not found: " << symbol << std::endl;
                    break;
                }
                order_books[symbol]->modify_order(modify_order->order_id, modify_order->side, modify_order->quantity, modify_order->price);
                break;
            }
            case ndfex::md::MSG_TYPE::TRADE: {
                if (len < sizeof(ndfex::md::trade)) {
                    std::cerr << "Message too short for trade: " << len << std::endl;
                    return;
                }

                ndfex::md::trade* trade = reinterpret_cast<ndfex::md::trade*>(const_cast<u_char*>(packet));
                auto it = order_to_symbol.find(trade->order_id);
                if (it == order_to_symbol.end()) {
                    std::cerr << "Order not found: " << trade->order_id << std::endl;
                    break;
                }
                uint32_t symbol = it->second;
                if (order_books.find(symbol) == order_books.end()) {
                    std::cerr << "Order book not found: " << symbol << std::endl;
                    break;
                }
                order_books[symbol]->order_trade(trade->order_id, trade->quantity, trade->price);
                break;
            }
            case ndfex::md::MSG_TYPE::TRADE_SUMMARY: {
                if (len < sizeof(ndfex::md::trade_summary)) {
                    std::cerr << "Message too short for trade summary: " << len << std::endl;
                    return;
                }
                break;
            }
            case ndfex::md::MSG_TYPE::HEARTBEAT:
            case ndfex::md::MSG_TYPE::SNAPSHOT_INFO: {
                break;
            }

        }

        len -= md_header->length;
        packet += md_header->length;

     }

int main(int argc, char* argv[]) {

    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <pcap file>" << std::endl;
        return 1;
    }

    pcap_t* pcap = pcap_open_offline(argv[1], nullptr);
    if (pcap == nullptr) {
        std::cerr << "Failed to open pcap file: " << argv[1] << std::endl;
        return 1;
    }

    // create async spdlogger
    std::shared_ptr<spdlog::logger> async_logger = spdlog::basic_logger_mt<spdlog::async_factory>("async_logger", "logs/async.txt");

    std::unordered_map<uint64_t, uint32_t> order_to_symbol;
    std::unordered_map<uint32_t, ndfex::bots::OrderBook*> order_books;

    pcap_pkthdr header;
    const u_char* packet;

    uint32_t last_seq_num = 0;
    std::unordered_map<uint32_t, bool> snapshot_complete;
    snapshot_complete[1] = false;
    snapshot_complete[2] = false;

    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> snapshot_order_count;

    auto done_snapshot = [&snapshot_complete]() {
        return snapshot_complete[1] && snapshot_complete[2];
    };

    std::vector<u_char> live_buffer;

    auto get_first_live_seq_num = [&live_buffer]() {
        if (live_buffer.empty()) {
            return UINT32_MAX;
        }

        ndfex::md::md_header* header = reinterpret_cast<ndfex::md::md_header*>(live_buffer.data());
        return header->seq_num;
    };

    while ((packet = pcap_next(pcap, &header)) != nullptr) {
        // skip eth header
        packet += 14;
        header.caplen -= 14;

        // skip ip and udp headers
        packet += 28;
        header.caplen -= 28;

        while (header.caplen > 0) {
            ndfex::md::md_header* md_header = reinterpret_cast<ndfex::md::md_header*>(const_cast<u_char*>(packet));
            if (md_header->magic_number != ndfex::md::MAGIC_NUMBER &&
                md_header->magic_number != ndfex::md::SNAPSHOT_MAGIC_NUMBER) {
                break;
            }

            if (!done_snapshot() && md_header->magic_number == ndfex::md::MAGIC_NUMBER) {
                std::cout << "Live message stored" << " " << md_header->seq_num << " " << md_header->length << std::endl;
                live_buffer.insert(live_buffer.end(), packet, packet + md_header->length);
                header.caplen -= md_header->length;
                packet += md_header->length;
                continue;
            }

            if (!done_snapshot() && md_header->magic_number == ndfex::md::SNAPSHOT_MAGIC_NUMBER &&
                                    md_header->msg_type == ndfex::md::MSG_TYPE::SNAPSHOT_INFO) {
                std::cout << "Snapshot message" << " " << md_header->seq_num << std::endl;

                ndfex::md::snapshot_info* snapshot_info = reinterpret_cast<ndfex::md::snapshot_info*>(const_cast<u_char*>(packet));
                if (snapshot_info->symbol != 1 && snapshot_info->symbol != 2) {
                    break;
                }

                if (snapshot_info->last_md_seq_num <= get_first_live_seq_num()) {
                    break;
                }

                last_seq_num = snapshot_info->last_md_seq_num;
                std::cout << "Snapshot info: symbol=" << snapshot_info->symbol << ", last_md_seq_num=" << snapshot_info->last_md_seq_num << ", bid_count=" << snapshot_info->bid_count << ", ask_count=" << snapshot_info->ask_count << std::endl;

                snapshot_order_count[snapshot_info->symbol] = {snapshot_info->bid_count, snapshot_info->ask_count};

            }

            if (done_snapshot() && md_header->magic_number == ndfex::md::SNAPSHOT_MAGIC_NUMBER) {
                break;
            }

            if (done_snapshot() && md_header->seq_num != last_seq_num + 1) {
                std::cerr << "Missing sequence number: " << last_seq_num << " -> " << md_header->seq_num << std::endl;
            }

            if (done_snapshot()) {
                last_seq_num = md_header->seq_num;
                parse_and_apply_live_message(packet, header.caplen, order_books, order_to_symbol, async_logger);

                // print the bbo for symbol 1 and 2
                for (auto& order_book : order_books) {
                    std::cout << (header.ts.tv_sec + header.ts.tv_usec / 1000000) << "," << md_header->seq_num << "," << order_book.first <<
                        "," << order_book.second->get_best_bid().price << "," << order_book.second->get_best_bid().quantity <<
                        "," << order_book.second->get_best_ask().price << "," << order_book.second->get_best_ask().quantity << std::endl;
                }
                continue;
            }

            switch (md_header->msg_type) {
                case ndfex::md::MSG_TYPE::NEW_ORDER: {
                    if (header.caplen < sizeof(ndfex::md::new_order)) {
                        std::cerr << "Message too short for new order: " << header.len << std::endl;
                        continue;
                    }

                    ndfex::md::new_order* new_order = reinterpret_cast<ndfex::md::new_order*>(const_cast<u_char*>(packet));
                    uint32_t symbol = new_order->symbol;
                    if (md_header->magic_number == ndfex::md::SNAPSHOT_MAGIC_NUMBER) {
                        if (snapshot_order_count.find(symbol) == snapshot_order_count.end()) {
                            std::cerr << "Snapshot order count not found: " << symbol << std::endl;
                            break;
                        }

                        std::cout << "Snapshot new order: symbol=" << symbol << ", side=" << static_cast<uint8_t>(new_order->side) << ", quantity=" << new_order->quantity << ", price=" << new_order->price << std::endl;
                        snapshot_order_count[symbol].first -= new_order->side == ndfex::md::SIDE::BUY ? 1 : 0;
                        snapshot_order_count[symbol].second -= new_order->side == ndfex::md::SIDE::SELL ? 1 : 0;

                        std::cout << "Snapshot order count: symbol=" << symbol << ", bid_count=" << snapshot_order_count[symbol].first << ", ask_count=" << snapshot_order_count[symbol].second << std::endl;
                        if (snapshot_order_count[symbol].first == 0 && snapshot_order_count[symbol].second == 0) {
                            snapshot_complete[symbol] = true;
                            // apply live buffer messages to the order book

                            if (done_snapshot()) {
                                std::cout << "Snapshot complete " << last_seq_num << std::endl;
                                uint32_t len = live_buffer.size();

                                for (const u_char* live_packet = live_buffer.data(); live_packet < live_buffer.data() + live_buffer.size();) {
                                    auto hdr = reinterpret_cast<ndfex::md::md_header*>(const_cast<u_char*>(live_packet));
                                    std::cout << "live message:" " " << hdr->seq_num << std::endl;
                                    if (hdr->seq_num > last_seq_num) {
                                        std::cout << "playing live message: " << hdr->seq_num << " " << len << std::endl;
                                        parse_and_apply_live_message(live_packet, len, order_books, order_to_symbol, async_logger);
                                    } else {
                                        live_packet += hdr->length;
                                        len -= hdr->length;
                                    }
                                    std::cout << "len = " << len << std::endl;
                                }
                                std::cout << "done playing live messages" << std::endl;
                                live_buffer.clear();
                            }
                        }
                    }
                    break;
                }
                case ndfex::md::MSG_TYPE::SNAPSHOT_INFO: {
                    break;
                }
                case ndfex::md::MSG_TYPE::DELETE_ORDER:
                case ndfex::md::MSG_TYPE::MODIFY_ORDER:
                case ndfex::md::MSG_TYPE::TRADE:
                case ndfex::md::MSG_TYPE::TRADE_SUMMARY:
                case ndfex::md::MSG_TYPE::HEARTBEAT:
                    break;
                default:
                    std::cerr << "Unknown message type: " << static_cast<uint8_t>(md_header->msg_type) << std::endl;
                    break;
            }
            header.caplen -= md_header->length;
            packet += md_header->length;
        }
    }

    pcap_close(pcap);
    return 0;
}