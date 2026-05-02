#pragma once

#include "fair_value.H"
#include "matching_engine/symbol_definition.H"

#include <cstdint>
#include <random>

namespace ndfex::bots {

/**
 * Simulates a fair value as a random walk.
 */

class RandomWalkFairValue
    : public FairValue {
public:

    RandomWalkFairValue(int32_t start_price, symbol_definition symbol);

    virtual int32_t process(uint64_t ts) override final;

    void apply_jump(double multiplier) override final;

    void start_directional_drift(int32_t units_per_min, uint64_t now_ns) override final;
    void end_directional_drift(uint64_t now_ns) override final;

    int32_t current_value() const { return fair_value; }

private:

    int32_t fair_value{0};
    uint64_t last_ts{0};

    const int32_t min_price;
    const int32_t max_price;
    const uint32_t tick_size;

    // Directional-drift regime state. drift_per_min == 0 means inactive.
    // When active, each call to process() returns fair_value + cumulative
    // drift since regime_start_ns; end_directional_drift bakes the drift
    // into fair_value so prices don't snap back.
    int32_t drift_per_min{0};
    uint64_t regime_start_ns{0};

    // initialize random device
    std::random_device rd;
    std::mt19937 gen{rd()};
    std::uniform_int_distribution<int32_t> walk_value;
    std::uniform_int_distribution<uint64_t> time_dist;

};

} // namespace ndfex::bots