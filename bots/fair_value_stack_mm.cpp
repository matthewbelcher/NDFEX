#include "fair_value_stack_mm.H"
#include "matching_engine/utils.H"

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
              fv(nullptr),
              width_in_ticks(width_in_ticks),
              stack_manager(oe, symbol, width_in_ticks, quantity, 10, last_order_id, logger),
              logger(logger) {

    }

    FairValueStackingMarketMaker::FairValueStackingMarketMaker(
        OEClient& oe, MDClient& md,
        symbol_definition symbol,
        FairValue* fv,
        uint32_t width_in_ticks,
        uint32_t quantity,
        uint32_t& last_order_id,
        std::shared_ptr<spdlog::logger> logger)
            : oe(oe), md(md),
              symbol(symbol),
              fv(fv),
              width_in_ticks(width_in_ticks),
              stack_manager(oe, symbol, width_in_ticks, quantity, 10, last_order_id, logger),
              logger(logger) {

    }

    void FairValueStackingMarketMaker::process() {
        (void) (oe);

        int32_t bid_anchor;
        int32_t ask_anchor;
        int32_t fair_value;

        if (fv != nullptr) {
            // Basket-aware path: stack against the structurally-safe touch
            // values, not the symbol's own BBO. We pre-deduct width*tick from
            // the bid anchor and add it to the ask anchor so that the
            // StackManager's `improve` step (which always shifts the anchor
            // toward more aggressive prices by width ticks) lands at exactly
            // bid_value/ask_value — the no-arb edge. Stacked depth fills out
            // strictly *below* that on the bid side and strictly *above* on
            // the ask side, keeping every level structurally arb-free
            // (modulo the rare BasketFairValue::PRICING_ERROR_RATE leak,
            // which is the only intended way for the LP to be hittable).
            const uint64_t now = nanotime();
            const int32_t bid_v = fv->bid_value(now);
            const int32_t ask_v = fv->ask_value(now);
            const int32_t offset = static_cast<int32_t>(width_in_ticks) * static_cast<int32_t>(symbol.tick_size);
            bid_anchor = bid_v - offset;
            ask_anchor = ask_v + offset;
            fair_value = (bid_v + ask_v) / 2;
        } else {
            // Legacy path: anchor on the symbol's own market BBO.
            auto best_bid = md.get_best_bid(symbol.symbol);
            auto best_ask = md.get_best_ask(symbol.symbol);

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

            bid_anchor = best_bid.price;
            ask_anchor = best_ask.price;
            fair_value = static_cast<int32_t>(wmid_price);
        }

        int32_t fair_value_rounded = (fair_value / static_cast<int32_t>(symbol.tick_size))
                                   * static_cast<int32_t>(symbol.tick_size);

        // process the stack manager
        stack_manager.process(fair_value_rounded, bid_anchor, ask_anchor);
    }

} // namespace ndfex::bots
