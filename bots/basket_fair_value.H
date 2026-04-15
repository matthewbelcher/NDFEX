#pragma once

#include "fair_value.H"
#include "md_client.H"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace ndfex::bots {

/**
 * Fair value for an ETF / basket product computed from the market mid-prices
 * of its underlying components. Each component is specified as a
 * (symbol_id, multiplier) pair; the fair value is
 *     sum_i multiplier_i * mid_i
 * where mid_i is the midpoint of the best bid and best ask of component i as
 * reported by the MDClient.
 *
 * The bot using this fair value deliberately does NOT share the underlying
 * components' random-walk FV objects — it only sees the market book, the way
 * a real ETF arbitrageur would.
 *
 * If any component is missing a bid or ask when process() is called, the last
 * successfully computed value is returned instead (seeded with initial_fv on
 * the first call).
 */
class BasketFairValue : public FairValue {
public:
    BasketFairValue(MDClient& md,
                    std::vector<std::pair<uint32_t, int32_t>> components,
                    int32_t initial_fv,
                    std::shared_ptr<spdlog::logger> logger);

    int32_t process(uint64_t ts) override final;

private:
    MDClient& md;
    std::vector<std::pair<uint32_t, int32_t>> components;
    int32_t last_fv;
    std::shared_ptr<spdlog::logger> logger;
};

} // namespace ndfex::bots
