#pragma once

#include "oe_client.H"
#include "md_client.H"
#include "fair_value.H"
#include "matching_engine/symbol_definition.H"

#include <spdlog/spdlog.h>
#include <unordered_map>
#include <vector>
#include <random>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace ndfex::bots {

/**
 * Trading bot that monitors news headlines and reacts to them by 
 * adjusting its trading strategy.
 */
struct HeadlineData {
    std::string timestamp;
    std::string symbol_name;
    std::string headline;
    int sentiment;
    int impact;
    uint32_t symbol_id;
};

class NewsTrader {

public:
    NewsTrader(OEClient& oe, MDClient& md,
               std::vector<symbol_definition> symbols,
               std::string headlines_dir,
               uint32_t& last_order_id,
               std::shared_ptr<spdlog::logger> logger);

    void process();

private:
    OEClient& oe;
    std::vector<symbol_definition> symbols;
    std::string headlines_dir;
    std::string latest_file;
    std::string last_timestamp;

    uint32_t& last_order_id;
    std::shared_ptr<spdlog::logger> logger;

    // Mapping between symbol strings (GOLD, BLUE) and symbol IDs (1, 2)
    std::unordered_map<std::string, uint32_t> symbol_name_map;
    
    // Base prices for each symbol
    std::unordered_map<uint32_t, int32_t> base_prices;
    std::unordered_map<uint32_t, int32_t> price_widths;
    
    // Active orders
    std::unordered_map<uint32_t, uint64_t> active_bids;
    std::unordered_map<uint32_t, uint64_t> active_asks;
    
    // Random number generation
    std::random_device rd;
    std::mt19937 gen;
    std::uniform_real_distribution<> reaction_dist;

    // Helper functions
    std::string extract_json_value(const std::string& json, const std::string& key);
    HeadlineData parse_headline(const std::string& json_str);
    void on_headline(const HeadlineData& headline);
    void cancel_existing_orders(uint32_t symbol);
    void place_new_orders(uint32_t symbol, const symbol_definition& symbol_def);
    double analyze_headline(const HeadlineData& headline);
    uint32_t get_symbol_id(const std::string& symbol_name);
};

} // namespace ndfex::bots

