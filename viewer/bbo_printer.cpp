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
                std::cout << "Trade summary" << std::endl;
                ndfex::md::trade_summary* trade_summary = reinterpret_cast<ndfex::md::trade_summary*>(const_cast<u_char*>(packet));
                std::cout << "Trade summary: symbol=" << trade_summary->symbol << ", last_price=" << trade_summary->last_price << ", total_quantity=" << trade_summary->total_quantity << std::endl;
                break;
            }
            case ndfex::md::MSG_TYPE::HEARTBEAT: {
                std::cout << "Heartbeat" << std::endl;
                break;
            }
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

    while ((packet = pcap_next(pcap, &header)) != nullptr) {
        // skip eth header
        packet += 14;
        header.caplen -= 14;

        // skip ip and udp headers
        packet += 28;
        header.caplen -= 28;

        while (header.caplen > 0) {
            ndfex::md::md_header* md_header = reinterpret_cast<ndfex::md::md_header*>(const_cast<u_char*>(packet));
            if (md_header->magic_number == ndfex::md::SNAPSHOT_MAGIC_NUMBER) {
                // skip
                packet += md_header->length;
                header.caplen -= md_header->length;
                continue;
            }

            if (md_header->magic_number != ndfex::md::MAGIC_NUMBER) {
                break;
            }

            if (md_header->seq_num != last_seq_num + 1) {
                packet += md_header->length;
                header.caplen -= md_header->length;

                // start over from the beginning
                last_seq_num = 0;
                continue;
            }

            if (md_header->seq_num == 1) {
                std::cout << "Found start of sequence" << header.ts.tv_sec << std::endl;
            }

            last_seq_num = md_header->seq_num;

            parse_and_apply_live_message(packet, header.caplen, order_books, order_to_symbol, async_logger);

            std::cout << "caplen = " << header.caplen << std::endl;
        }
    }

    pcap_close(pcap);
    return 0;
}