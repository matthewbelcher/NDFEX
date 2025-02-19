#include "random_walk_fair_value.H"

namespace ndfex::bots {

static uint32_t calculate_dist(uint32_t tick_size) {
    if (tick_size < 10) {
        return 1;
    }
    return tick_size / 5;
}

RandomWalkFairValue::RandomWalkFairValue(int32_t start_price, symbol_definition symbol)
    : fair_value(start_price), min_price(symbol.min_price), max_price(symbol.max_price),
      tick_size(symbol.tick_size),
      walk_value(-calculate_dist(tick_size), calculate_dist(tick_size)),
      time_dist(1000000, 100000000) {}

int32_t RandomWalkFairValue::process(uint64_t ts) {
    if (last_ts == 0) {
        last_ts = ts + time_dist(gen);
        return fair_value;
    }

    if (ts <= last_ts) {
        return fair_value;
    }

    int32_t change = walk_value(gen);
    fair_value += change;
    if (fair_value < min_price + 5 * tick_size) {
        fair_value = min_price + 5 * tick_size;
    } else if (fair_value > max_price - 5 * tick_size) {
        fair_value = max_price - 5 * tick_size;
    }

    // step forward a random amount of time
    last_ts = ts + time_dist(gen);
    return fair_value;
}

} // namespace ndfex::bots