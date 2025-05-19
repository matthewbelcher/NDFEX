#include "fair_value_stack_mm.H"

namespace ndfex::bots {

    FairValueStackingMarketMaker::FairValueStackingMarketMaker(
        OEClient& oe, MDClient& md,
        symbol_definition symbol,
        uint32_t width_in_ticks,
        uint32_t quantity,
        uint32_t& last_order_id,
        std::shared_ptr<spdlog::logger> logger)
            : oe(oe), md(md),
              symbol(symbol),
              stack_manager(oe, symbol, width_in_ticks, quantity, 10, last_order_id, logger),
              logger(logger) {

    }

    void FairValueStackingMarketMaker::process() {
        (void) (oe);
        // get the best bid and ask prices
        auto best_bid = md.get_best_bid(symbol.symbol);
        auto best_ask = md.get_best_ask(symbol.symbol);

        // check if the best bid and ask prices are valid
        if (best_bid.price == 0 || best_ask.price == 0) {
            logger->warn("Best bid or ask price is zero");
            return;
        }

        if (best_bid.price >= best_ask.price) {
            logger->warn("Best bid price is greater than or equal to best ask price");
            return;
        }

        if (best_bid.quantity == 0 || best_ask.quantity == 0) {
            logger->warn("Best bid or ask quantity is zero");
            return;
        }

        // calculate the weighted mid price
        double wmid_price = (best_bid.price * best_ask.quantity + best_ask.price * best_bid.quantity)
                / (best_bid.quantity + best_ask.quantity);

        int32_t wmid_rounded = static_cast<int32_t>(wmid_price / symbol.tick_size) * symbol.tick_size;

        // process the stack manager
        stack_manager.process(wmid_rounded, best_bid.price, best_ask.price);
    }

} // namespace ndfex::bots