#include "basket_fair_value.H"

namespace ndfex::bots {

BasketFairValue::BasketFairValue(MDClient& md,
                                 std::vector<std::pair<uint32_t, int32_t>> components,
                                 int32_t initial_fv,
                                 std::shared_ptr<spdlog::logger> logger)
    : md(md), components(std::move(components)), last_fv(initial_fv), logger(logger) {}

int32_t BasketFairValue::process(uint64_t /*ts*/) {
    int32_t fv = 0;
    for (const auto& [sym, mult] : components) {
        auto bid = md.get_best_bid(sym);
        auto ask = md.get_best_ask(sym);
        if (bid.price == 0 || ask.price == 0) {
            // component book is incomplete; fall back to the last value we
            // could compute cleanly. On the first call this is initial_fv.
            return last_fv;
        }
        const int32_t mid = (bid.price + ask.price) / 2;
        fv += mult * mid;
    }
    last_fv = fv;
    return fv;
}

} // namespace ndfex::bots
