// simplified_news_trader.cpp
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <filesystem>
#include <vector>
#include <map>
#include <random>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace ndfex {
    // Basic symbol definition
    struct symbol_definition {
        uint32_t symbol;
        uint32_t tick_size;
        uint32_t min_lot_size;
        uint32_t min_price;
        uint32_t max_price;
        uint32_t max_lot_size;
    };
    
    namespace md {
        enum class SIDE : uint8_t {
            BUY = 0,
            SELL = 1
        };
    }
}

// Simple headline data structure
struct HeadlineData {
    std::string timestamp;
    std::string symbol;
    std::string headline;
    int sentiment;
    int impact;
};

// Simple order structure to track orders
struct OrderInfo {
    uint64_t order_id;
    uint32_t symbol;
    ndfex::md::SIDE side;
    uint32_t quantity;
    int32_t price;
};

// Class to monitor headlines and simulate order placement
class NewsTrader {
public:
    NewsTrader(
        const std::string& headlines_dir,
        const std::vector<ndfex::symbol_definition>& symbols,
        uint32_t& last_order_id,
        std::shared_ptr<spdlog::logger> logger,
        std::string bot_id = "",
        double reaction_factor = 1.0
    ) : 
        headlines_dir(headlines_dir),
        symbols(symbols),
        last_order_id(last_order_id),
        logger(logger),
        reaction_factor(reaction_factor),
        gen(std::random_device()()),
        confidence_dist(0.7, 1.0),
        agree_dist(0.0, 1.0),
        impact_variation_dist(0.8, 1.2),
        disagree_variation_dist(0.3, 0.7) {
        
        if (bot_id.empty()) {
            // Generate a random bot ID if not provided
            this->bot_id = "NewsBot-" + std::to_string(gen() % 9000 + 1000);
        } else {
            this->bot_id = bot_id;
        }
        
        latest_file = headlines_dir + "/headlines_latest.json";
        
        // Initialize base prices
        for (const auto& symbol : symbols) {
            base_prices[symbol.symbol] = {
                static_cast<int32_t>(1000 * symbol.tick_size), 
                static_cast<int32_t>(5 * symbol.tick_size)
            };
        }
        
        std::cout << "=== " << this->bot_id << " INITIALIZED ===" << std::endl;
        std::cout << "Monitoring headlines from: " << latest_file << std::endl;
        std::cout << "Trading symbols:" << std::endl;
        for (const auto& symbol : symbols) {
            std::cout << "  Symbol " << symbol.symbol 
                     << ": Base price=" << base_prices[symbol.symbol].price 
                     << ", Width=" << base_prices[symbol.symbol].width << std::endl;
        }
    }
    
    void process() {
        try {
            // Check if the latest headline file exists
            if (fs::exists(latest_file)) {
                // Read the file
                std::ifstream file(latest_file);
                if (file.is_open()) {
                    // Read the entire file content
                    std::string json_content((std::istreambuf_iterator<char>(file)),
                                           std::istreambuf_iterator<char>());
                    file.close();
                    
                    // Extract timestamp to check if this is a new headline
                    std::string timestamp = extract_json_value(json_content, "timestamp");
                    
                    // Check if this is a new headline by comparing timestamps
                    if (timestamp != last_timestamp && !timestamp.empty()) {
                        logger->info("New headline detected!");
                        
                        // Parse the headline
                        HeadlineData headline = parse_headline(json_content);
                        last_timestamp = timestamp;
                        
                        // Log headline details
                        logger->info("=== HEADLINE ===");
                        logger->info("Symbol: {}", headline.symbol);
                        logger->info("Impact: {}", headline.impact);
                        logger->info("Sentiment: {}", (headline.sentiment > 0 ? "positive" : "negative"));
                        logger->info("Text: {}", headline.headline);
                        
                        // React to the headline
                        on_headline(headline);
                    }
                }
            }
        } catch (const std::exception& e) {
            logger->error("Error monitoring headlines: {}", e.what());
        }
        
        // Process pending orders (in a real system, this would connect to your order entry system)
        process_orders();
    }
    
private:
    std::string headlines_dir;
    std::string latest_file;
    std::string last_timestamp;
    std::vector<ndfex::symbol_definition> symbols;
    uint32_t& last_order_id;
    std::shared_ptr<spdlog::logger> logger;
    std::string bot_id;
    double reaction_factor;
    
    // Random number generation
    std::mt19937 gen;
    std::uniform_real_distribution<> confidence_dist;
    std::uniform_real_distribution<> agree_dist;
    std::uniform_real_distribution<> impact_variation_dist;
    std::uniform_real_distribution<> disagree_variation_dist;
    
    // Current order tracking
    std::map<uint32_t, OrderInfo> active_bids;
    std::map<uint32_t, OrderInfo> active_asks;
    std::vector<OrderInfo> pending_orders;
    std::vector<uint64_t> cancel_orders;
    
    // Base prices
    struct PriceInfo {
        int32_t price;
        int32_t width;
    };
    std::map<uint32_t, PriceInfo> base_prices;
    
    std::string extract_json_value(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    
    pos += search.length();
    // Skip whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    
    // Check if string or number
    if (pos < json.length()) {
        if (json[pos] == '"') {
            // String value
            pos++; // Skip the opening quote
            
            // Find the closing quote (with proper handling of escaped quotes)
            std::string value;
            while (pos < json.length()) {
                if (json[pos] == '"' && (pos == 0 || json[pos-1] != '\\')) {
                    // Unescaped quote marks the end
                    break;
                }
                value += json[pos];
                pos++;
            }
            
            // Remove outer quotes if present
            if (value.length() >= 2 && value[0] == '"' && value[value.length()-1] == '"') {
                value = value.substr(1, value.length() - 2);
            }
            
            return value;
        } else if ((json[pos] >= '0' && json[pos] <= '9') || json[pos] == '-') {
            // Number value
            size_t end = json.find_first_of(",}\n", pos);
            if (end != std::string::npos) {
                return json.substr(pos, end - pos);
            }
        }
    }
    return "";
}

    // Parse headline from JSON string
    HeadlineData parse_headline(const std::string& json_str) {
        HeadlineData headline;
        
        headline.timestamp = extract_json_value(json_str, "timestamp");
        headline.symbol = extract_json_value(json_str, "symbol");
        headline.headline = extract_json_value(json_str, "headline");
        
        // Parse sentiment as int
        std::string sentiment_str = extract_json_value(json_str, "sentiment");
        headline.sentiment = sentiment_str.empty() ? 0 : std::stoi(sentiment_str);
        
        // Parse impact as int
        std::string impact_str = extract_json_value(json_str, "impact");
        headline.impact = impact_str.empty() ? 0 : std::stoi(impact_str);
        
        return headline;
    }
    
    uint32_t symbol_str_to_id(const std::string& symbol_str) {
        // Try direct conversion from string to int
        try {
            return std::stoi(symbol_str);
        } catch (const std::exception&) {
            // If that fails, try to match by symbol name
            for (const auto& symbol : symbols) {
                if (std::to_string(symbol.symbol) == symbol_str) {
                    return symbol.symbol;
                }
            }
        }
        
        logger->warn("{}: Could not find symbol ID for '{}'", bot_id, symbol_str);
        return 0; // Return 0 for unknown symbol
    }
    
    void on_headline(const HeadlineData& headline) {
        logger->info("{}: New headline for {}: {}", bot_id, headline.symbol, headline.headline);
        logger->info("{}: Impact: {}, Sentiment: {}", bot_id, headline.impact, headline.sentiment);
        
        std::cout << "\n=== " << bot_id << " REACTING TO HEADLINE ===" << std::endl;
        std::cout << "HEADLINE: " << headline.headline << std::endl;
        std::cout << "SYMBOL: " << headline.symbol << ", IMPACT: " << headline.impact << std::endl;
        
        // Convert symbol string to ID
        uint32_t symbol_id = symbol_str_to_id(headline.symbol);
        if (symbol_id == 0) {
            logger->warn("{}: Symbol {} not in trading universe, ignoring", bot_id, headline.symbol);
            return;
        }
        
        // Check if we have this symbol in our universe
        bool found = false;
        ndfex::symbol_definition symbol_def;
        for (const auto& symbol : symbols) {
            if (symbol.symbol == symbol_id) {
                found = true;
                symbol_def = symbol;
                break;
            }
        }
        
        if (!found) {
            logger->warn("{}: Symbol {} not in trading universe, ignoring", bot_id, symbol_id);
            return;
        }
        
        // Analyze the headline
        double sentiment_score = analyze_headline(headline);
        
        // Determine reaction based on analysis and impact
        double reaction_confidence = confidence_dist(gen);
        double actual_impact = 0.0;
        
        // Sometimes disagree with the provided impact
        if (agree_dist(gen) < 0.2) {  // 20% chance to disagree
            actual_impact = -headline.impact * disagree_variation_dist(gen) * reaction_factor;
            logger->info("{}: DISAGREEING with headline sentiment", bot_id);
            logger->info("{}: Original impact: {}, My impact assessment: {:.2f}", 
                        bot_id, headline.impact, actual_impact);
            
            std::cout << bot_id << ": DISAGREEING with headline sentiment" << std::endl;
            std::cout << bot_id << ": Original impact: " << headline.impact 
                      << ", My impact assessment: " << actual_impact << std::endl;
        } else {
            // Agree but with some random variation
            actual_impact = headline.impact * impact_variation_dist(gen) * reaction_factor;
            logger->info("{}: AGREEING with headline sentiment (confidence: {:.2f})", 
                        bot_id, reaction_confidence);
            logger->info("{}: Original impact: {}, My impact assessment: {:.2f}", 
                        bot_id, headline.impact, actual_impact);
            
            std::cout << bot_id << ": AGREEING with headline sentiment (confidence: " 
                      << reaction_confidence << ")" << std::endl;
            std::cout << bot_id << ": Original impact: " << headline.impact 
                      << ", My impact assessment: " << actual_impact << std::endl;
        }
        
        // Log decision process
        int32_t old_price = base_prices[symbol_id].price;
        logger->info("{}: Current base price for {}: {}", bot_id, symbol_id, old_price);
        logger->info("{}: Adjusting by {:.2f} points", bot_id, actual_impact);
        
        std::cout << bot_id << ": Current base price for " << symbol_id << ": " 
                  << old_price << std::endl;
        std::cout << bot_id << ": Adjusting by " << actual_impact << " points" << std::endl;
        
        // Adjust our base price based on the headline impact
        base_prices[symbol_id].price += static_cast<int32_t>(actual_impact);
        int32_t new_price = base_prices[symbol_id].price;
        
        // Ensure price is within bounds
        if (new_price < static_cast<int32_t>(symbol_def.min_price)) {
            new_price = static_cast<int32_t>(symbol_def.min_price);
            base_prices[symbol_id].price = new_price;
        } else if (new_price > static_cast<int32_t>(symbol_def.max_price)) {
            new_price = static_cast<int32_t>(symbol_def.max_price);
            base_prices[symbol_id].price = new_price;
        }
        
        // Ensure price is a multiple of tick size
        int32_t remainder = new_price % symbol_def.tick_size;
        if (remainder != 0) {
            new_price = new_price - remainder + symbol_def.tick_size;
            base_prices[symbol_id].price = new_price;
        }
        
        logger->info("{}: New base price for {}: {} (change: {})", 
                    bot_id, symbol_id, new_price, new_price - old_price);
        
        std::cout << bot_id << ": New base price for " << symbol_id << ": " 
                  << new_price << " (change: " << (new_price - old_price) << ")" << std::endl;
        
        // Cancel existing orders and place new ones
        cancel_existing_orders(symbol_id);
        place_new_orders(symbol_id, symbol_def);
        
        std::cout << "=== " << bot_id << " REACTION COMPLETE ===\n" << std::endl;
    }
    
    double analyze_headline(const HeadlineData& headline) {
        // Simple sentiment analysis based on keywords
        logger->info("{}: Analyzing headline: {}", bot_id, headline.headline);
        
        // Mock analysis based on keywords
        double sentiment_score = 0.0;
        std::string text = headline.headline;
        
        // Convert to lowercase for keyword matching
        std::transform(text.begin(), text.end(), text.begin(), ::tolower);
        
        // Positive keywords
        if (text.find("rise") != std::string::npos) sentiment_score += 0.3;
        if (text.find("increase") != std::string::npos) sentiment_score += 0.3;
        if (text.find("growth") != std::string::npos) sentiment_score += 0.4;
        if (text.find("profit") != std::string::npos) sentiment_score += 0.5;
        if (text.find("positive") != std::string::npos) sentiment_score += 0.3;
        if (text.find("higher") != std::string::npos) sentiment_score += 0.3;
        if (text.find("gain") != std::string::npos) sentiment_score += 0.3;
        
        // Negative keywords
        if (text.find("fall") != std::string::npos) sentiment_score -= 0.3;
        if (text.find("decrease") != std::string::npos) sentiment_score -= 0.3;
        if (text.find("decline") != std::string::npos) sentiment_score -= 0.4;
        if (text.find("loss") != std::string::npos) sentiment_score -= 0.5;
        if (text.find("negative") != std::string::npos) sentiment_score -= 0.3;
        if (text.find("lower") != std::string::npos) sentiment_score -= 0.3;
        if (text.find("drop") != std::string::npos) sentiment_score -= 0.3;
        
        // Limit to range [-1, 1]
        if (sentiment_score > 1.0) sentiment_score = 1.0;
        if (sentiment_score < -1.0) sentiment_score = -1.0;
        
        logger->info("{}: Calculated sentiment score: {}", bot_id, sentiment_score);
        logger->info("{}: Provided sentiment: {}", bot_id, (headline.sentiment > 0 ? "positive" : "negative"));
        
        return sentiment_score;
    }
    
    void cancel_existing_orders(uint32_t symbol_id) {
        // Cancel existing orders for this symbol
        if (active_bids.find(symbol_id) != active_bids.end()) {
            logger->info("{}: Cancelling bid for symbol {}", bot_id, symbol_id);
            cancel_orders.push_back(active_bids[symbol_id].order_id);
            active_bids.erase(symbol_id);
        }
        
        if (active_asks.find(symbol_id) != active_asks.end()) {
            logger->info("{}: Cancelling ask for symbol {}", bot_id, symbol_id);
            cancel_orders.push_back(active_asks[symbol_id].order_id);
            active_asks.erase(symbol_id);
        }
    }
    
    void place_new_orders(uint32_t symbol_id, const ndfex::symbol_definition& symbol_def) {
        int32_t base_price = base_prices[symbol_id].price;
        int32_t width = base_prices[symbol_id].width;
        
        // Calculate bid and ask prices
        int32_t bid_price = std::max(base_price - width, static_cast<int32_t>(symbol_def.min_price));
        int32_t ask_price = std::min(base_price + width, static_cast<int32_t>(symbol_def.max_price));
        
        // Ensure prices are multiples of tick size
        bid_price = (bid_price / symbol_def.tick_size) * symbol_def.tick_size;
        ask_price = ((ask_price + symbol_def.tick_size - 1) / symbol_def.tick_size) * symbol_def.tick_size;
        
        logger->info("{}: Placing new orders for {}", bot_id, symbol_id);
        logger->info("{}: Base price: {}, Width: {}", bot_id, base_price, width);
        logger->info("{}: Bid price: {}, Ask price: {}", bot_id, bid_price, ask_price);
        
        // Define quantity for order (use minimum lot size or a multiple)
        uint32_t quantity = symbol_def.min_lot_size * 10;  // 10x minimum lot size
        
        // Place bid
        uint64_t bid_order_id = last_order_id++;
        OrderInfo bid_order = {
            bid_order_id,
            symbol_id,
            ndfex::md::SIDE::BUY,
            quantity,
            bid_price
        };
        pending_orders.push_back(bid_order);
        active_bids[symbol_id] = bid_order;
        logger->info("{}: Placing bid order {} at {}", bot_id, bid_order_id, bid_price);
        
        // Place ask
        uint64_t ask_order_id = last_order_id++;
        OrderInfo ask_order = {
            ask_order_id,
            symbol_id,
            ndfex::md::SIDE::SELL,
            quantity,
            ask_price
        };
        pending_orders.push_back(ask_order);
        active_asks[symbol_id] = ask_order;
        logger->info("{}: Placing ask order {} at {}", bot_id, ask_order_id, ask_price);
    }
    
    void process_orders() {
        // In a real implementation, this would send the orders to your exchange
        
        // Process cancellations
        for (uint64_t order_id : cancel_orders) {
            std::cout << "[SIMULATED] Cancelling order " << order_id << std::endl;
            // In real code: oe_client.cancel_order(order_id);
        }
        cancel_orders.clear();
        
        // Process new orders
        for (const OrderInfo& order : pending_orders) {
            std::string side_str = (order.side == ndfex::md::SIDE::BUY) ? "BUY" : "SELL";
            std::cout << "[SIMULATED] Placing " << side_str << " order for symbol " << order.symbol
                      << " - Order ID: " << order.order_id
                      << ", Price: " << order.price
                      << ", Quantity: " << order.quantity << std::endl;
            // In real code: oe_client.send_order(order.symbol, order.order_id, order.side, order.quantity, order.price, 0);
        }
        pending_orders.clear();
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <headlines_dir>" << std::endl;
        return 1;
    }
    
    std::string headlines_dir = argv[1];
    
    auto logger = spdlog::default_logger();
    
    // Initialize symbol definitions
    std::vector<ndfex::symbol_definition> symbols = {
        {1, 10, 1, 1000, 10000000, 0}, // GOLD
        {2, 5, 1, 1000, 10000000, 0},  // BLUE
    };
    
    // Initialize order IDs
    uint32_t last_order_id = 1;
    
    // Create news trader
    NewsTrader trader(headlines_dir, symbols, last_order_id, logger, "NewsTrader", 1.0);
    
    std::cout << "News trader started. Monitoring headlines in " << headlines_dir << std::endl;
    
    // Main loop
    while (true) {
        trader.process();
        
        // Sleep to avoid high CPU usage
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    return 0;
}
