#include "imbalance_taker.H"

#include "matching_engine/utils.H"

namespace ndfex::bots {

ImbalanceTaker::ImbalanceTaker(OEClient& oe, MDClient& md,
                                std::vector<symbol_definition> symbols,
                                uint32_t quantity,
                                std::vector<int32_t> imbalance_thresholds,
                                uint32_t& last_order_id,
                                std::shared_ptr<spdlog::logger> logger)
    : oe(oe), symbols(symbols), quantity(quantity), imbalance_thresholds(imbalance_thresholds), last_order_id(last_order_id), logger(logger) {

        md.register_trade_summary_listener(this);
        for (const auto& symbol : symbols) {
            imbalance_quantities[symbol.symbol] = 0;
        }
}

void ImbalanceTaker::process() {

    // process the orders
    if (last_order_send_ts == 0 || ((nanotime() - last_order_send_ts) > 100000000)) {
        // create a new order
        for (const auto& symbol : symbols) {
            auto it = imbalance_quantities.find(symbol.symbol);
            if (it == imbalance_quantities.end()) {
                continue;
            }

            int32_t imbalance_quantity = it->second;
            int32_t imbalance_threshold = imbalance_thresholds[symbol.symbol - 1];

            if (imbalance_quantity > imbalance_threshold) {
                logger->info("ImbalanceTaker: Imbalance quantity for symbol {} {} is greater than threshold {}", symbol.symbol, imbalance_quantity, imbalance_threshold);
                oe.send_order(symbol.symbol, last_order_id++, md::SIDE::BUY, quantity, symbol.max_price, 1);
                last_order_send_ts = nanotime();
                imbalance_quantities[symbol.symbol] = 0;
            } else if (imbalance_quantity < -imbalance_threshold) {
                logger->info("ImbalanceTaker: Imbalance quantity for symbol {} {} is less than threshold {}", symbol.symbol, imbalance_quantity, -imbalance_threshold);
                oe.send_order(symbol.symbol, last_order_id++, md::SIDE::SELL, quantity, symbol.min_price, 1);
                last_order_send_ts = nanotime();
                imbalance_quantities[symbol.symbol] = 0;
            }
        }
    }
}

void ImbalanceTaker::on_trade_summary(uint32_t symbol, uint32_t quantity, int32_t price, md::SIDE aggressor_side) {
    (void)price;
    if (aggressor_side == md::SIDE::BUY) {
        imbalance_quantities[symbol] += quantity;
    } else if (aggressor_side == md::SIDE::SELL) {
        imbalance_quantities[symbol] -= quantity;
    }
}

} // namespace ndfex::bots