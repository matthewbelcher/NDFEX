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

private:

    int32_t fair_value{0};
    uint64_t last_ts{0};

    const int32_t min_price;
    const int32_t max_price;
    const uint32_t tick_size;

    // initialize random device
    std::random_device rd;
    std::mt19937 gen{rd()};
    std::uniform_int_distribution<int32_t> walk_value;
    std::uniform_int_distribution<uint64_t> time_dist;

};

} // namespace ndfex::bots