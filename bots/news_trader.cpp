#include "news_trader.H"
#include "matching_engine/utils.H"

namespace ndfex::bots {

NewsTrader::NewsTrader(OEClient& oe, MDClient& md,
                        std::vector<symbol_definition> symbols,
                        std::string headlines_dir,
                        uint32_t& last_order_id,
                        std::shared_ptr<spdlog::logger> logger)
    : oe(oe), symbols(symbols), headlines_dir(headlines_dir),
      last_order_id(last_order_id), logger(logger), gen(rd()),
      reaction_dist(0.7, 1.3) {
    
    // Initialize symbol name to ID map
    symbol_name_map["GOLD"] = 1;
    symbol_name_map["BLUE"] = 2;
    
    // Set up the headlines monitor
    latest_file = headlines_dir + "/headlines_latest.json";
    
    // Initialize base prices and widths based on current market
    for (const auto& symbol : symbols) {
        OrderBook::PriceLevel best_bid = md.get_best_bid(symbol.symbol);
        OrderBook::PriceLevel best_ask = md.get_best_ask(symbol.symbol);
        
        // If we have market data, set base price to midpoint
        if (best_bid.price > 0 && best_ask.price > 0) {
            base_prices[symbol.symbol] = (best_bid.price + best_ask.price) / 2;
            price_widths[symbol.symbol] = (best_ask.price - best_bid.price) / 2;
        } else {
            // Default values if no market data
            base_prices[symbol.symbol] = symbol.min_price + (symbol.max_price - symbol.min_price) / 2;
            price_widths[symbol.symbol] = 5 * symbol.tick_size;
        }
        
        logger->info("NewsTrader initialized for symbol {}: Base price={}, Width={}",
                    symbol.symbol, base_prices[symbol.symbol], price_widths[symbol.symbol]);
    }
    
    // Check if headlines directory exists
    if (!fs::exists(headlines_dir)) {
        logger->warn("Headlines directory {} does not exist! Creating it...", headlines_dir);
        fs::create_directories(headlines_dir);
    }
    
    logger->info("NewsTrader initialized. Monitoring headlines from: {}", latest_file);
}

void NewsTrader::process() {
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
                    logger->info("Symbol: {}", headline.symbol_name);
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
}

std::string NewsTrader::extract_json_value(const std::string& json, const std::string& key) {
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

HeadlineData NewsTrader::parse_headline(const std::string& json_str) {
    HeadlineData headline;
    
    headline.timestamp = extract_json_value(json_str, "timestamp");
    headline.symbol_name = extract_json_value(json_str, "symbol");
    headline.headline = extract_json_value(json_str, "headline");
    
    // Parse sentiment as int
    std::string sentiment_str = extract_json_value(json_str, "sentiment");
    headline.sentiment = sentiment_str.empty() ? 0 : std::stoi(sentiment_str);
    
    // Parse impact as int
    std::string impact_str = extract_json_value(json_str, "impact");
    headline.impact = impact_str.empty() ? 0 : std::stoi(impact_str);
    
    // Get numeric symbol ID from name
    headline.symbol_id = get_symbol_id(headline.symbol_name);
    
    return headline;
}

uint32_t NewsTrader::get_symbol_id(const std::string& symbol_name) {
    // Check if we have a mapping for this symbol name
    auto it = symbol_name_map.find(symbol_name);
    if (it != symbol_name_map.end()) {
        return it->second;
    }
    
    // Try direct conversion for numeric symbols
    try {
        return std::stoi(symbol_name);
    } catch (...) {
        // Not a number
    }
    
    logger->warn("Unknown symbol name: {}", symbol_name);
    return 0; // Invalid symbol ID
}

void NewsTrader::on_headline(const HeadlineData& headline) {
    logger->info("Processing headline for symbol {}: {}", 
                headline.symbol_name, headline.headline);
    
    // Check if this is a symbol we're trading
    if (headline.symbol_id == 0) {
        logger->warn("Symbol {} not in trading universe, ignoring", headline.symbol_name);
        return;
    }
    
    // Find symbol definition
    symbol_definition symbol_def;
    bool found = false;
    for (const auto& sym : symbols) {
        if (sym.symbol == headline.symbol_id) {
            symbol_def = sym;
            found = true;
            break;
        }
    }
    
    if (!found) {
        logger->warn("Symbol {} not found in symbol definitions, ignoring", headline.symbol_id);
        return;
    }
    
    // Analyze headline sentiment
    analyze_headline(headline);
    
    // Determine price adjustment
    double reaction_factor = reaction_dist(gen);
    double raw_impact = headline.impact * reaction_factor;
    int tick_size = symbol_def.tick_size;
    
    int32_t impact_adjustment = static_cast<int32_t>(raw_impact) * tick_size;
    
    logger->info("Original base price for symbol {}: {}", 
                headline.symbol_id, base_prices[headline.symbol_id]);
    logger->info("Headline impact: {}, Reaction factor: {:.2f}, Adjustment: {}",
                headline.impact, reaction_factor, impact_adjustment);
    
    // Adjust base price
    base_prices[headline.symbol_id] += impact_adjustment;
    
    // Ensure price is within min/max bounds and a multiple of tick size
    if (base_prices[headline.symbol_id] < static_cast<int32_t>(symbol_def.min_price)) {
        base_prices[headline.symbol_id] = static_cast<int32_t>(symbol_def.min_price);
    } else if (base_prices[headline.symbol_id] > static_cast<int32_t>(symbol_def.max_price)) {
        base_prices[headline.symbol_id] = static_cast<int32_t>(symbol_def.max_price);
    }
    
    // Make sure it's a multiple of tick size
    base_prices[headline.symbol_id] = (base_prices[headline.symbol_id] / tick_size) * tick_size;
    
    logger->info("New base price for symbol {}: {}", 
                headline.symbol_id, base_prices[headline.symbol_id]);
    
    // Update our orders
    cancel_existing_orders(headline.symbol_id);
    place_new_orders(headline.symbol_id, symbol_def);
}

double NewsTrader::analyze_headline(const HeadlineData& headline) {
    // Simple sentiment analysis based on the headline
    // In a real system, this could use a more sophisticated NLP approach
    
    std::string text = headline.headline;
    std::transform(text.begin(), text.end(), text.begin(), ::tolower);
    
    double score = 0.0;
    
    // Positive keywords
    if (text.find("increase") != std::string::npos) score += 0.2;
    if (text.find("rise") != std::string::npos) score += 0.2;
    if (text.find("growth") != std::string::npos) score += 0.3;
    if (text.find("higher") != std::string::npos) score += 0.2;
    if (text.find("gain") != std::string::npos) score += 0.2;
    if (text.find("profit") != std::string::npos) score += 0.3;
    if (text.find("positive") != std::string::npos) score += 0.2;
    
    // Negative keywords
    if (text.find("decrease") != std::string::npos) score -= 0.2;
    if (text.find("fall") != std::string::npos) score -= 0.2;
    if (text.find("drop") != std::string::npos) score -= 0.2;
    if (text.find("decline") != std::string::npos) score -= 0.3;
    if (text.find("loss") != std::string::npos) score -= 0.3;
    if (text.find("lower") != std::string::npos) score -= 0.2;
    if (text.find("negative") != std::string::npos) score -= 0.2;
    
    // Clamp score to [-1, 1]
    score = std::max(-1.0, std::min(1.0, score));
    
    logger->info("Headline sentiment analysis score: {:.2f}", score);
    logger->info("Provided sentiment: {}", headline.sentiment);
    
    return score;
}

void NewsTrader::cancel_existing_orders(uint32_t symbol) {
    // Cancel existing bid for this symbol
    auto bid_it = active_bids.find(symbol);
    if (bid_it != active_bids.end()) {
        logger->info("Cancelling bid order {} for symbol {}", bid_it->second, symbol);
        oe.cancel_order(bid_it->second);
        active_bids.erase(bid_it);
    }
    
    // Cancel existing ask for this symbol
    auto ask_it = active_asks.find(symbol);
    if (ask_it != active_asks.end()) {
        logger->info("Cancelling ask order {} for symbol {}", ask_it->second, symbol);
        oe.cancel_order(ask_it->second);
        active_asks.erase(ask_it);
    }
}

void NewsTrader::place_new_orders(uint32_t symbol, const symbol_definition& symbol_def) {
    int32_t base_price = base_prices[symbol];
    int32_t width = price_widths[symbol];
    
    // Calculate bid and ask prices
    int32_t bid_price = std::max(base_price - width, static_cast<int32_t>(symbol_def.min_price));
    int32_t ask_price = std::min(base_price + width, static_cast<int32_t>(symbol_def.max_price));
    
    // Ensure prices are multiples of tick size
    bid_price = (bid_price / symbol_def.tick_size) * symbol_def.tick_size;
    ask_price = ((ask_price + symbol_def.tick_size - 1) / symbol_def.tick_size) * symbol_def.tick_size;
    
    // Define quantity for orders
    uint32_t quantity = symbol_def.min_lot_size * 5; // 5x min lot size
    
    // Place bid order
    uint64_t bid_order_id = last_order_id++;
    logger->info("Placing bid order for symbol {}: id={}, price={}, quantity={}",
                symbol, bid_order_id, bid_price, quantity);
    oe.send_order(symbol, bid_order_id, md::SIDE::BUY, quantity, bid_price, 0);
    active_bids[symbol] = bid_order_id;
    
    // Place ask order
    uint64_t ask_order_id = last_order_id++;
    logger->info("Placing ask order for symbol {}: id={}, price={}, quantity={}",
                symbol, ask_order_id, ask_price, quantity);
    oe.send_order(symbol, ask_order_id, md::SIDE::SELL, quantity, ask_price, 0);
    active_asks[symbol] = ask_order_id;
}

} // namespace ndfex::bots
