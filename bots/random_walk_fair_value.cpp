#include "random_walk_fair_value.H"

#include <algorithm>
#include <cmath>

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

// Apply the active directional-drift overlay (no-op when inactive).
// Drift is computed deterministically from elapsed wall-time, so its rate
// is independent of how often process() is called or how the random walk
// happens to step. Result is clamped to the same legal-price band as the
// underlying walk.
static int32_t apply_drift_overlay(int32_t fv, int32_t drift_per_min,
                                   uint64_t regime_start_ns, uint64_t now_ns,
                                   int32_t min_price, int32_t max_price,
                                   uint32_t tick_size) {
    if (drift_per_min == 0 || now_ns < regime_start_ns) return fv;
    const int64_t elapsed_ns = static_cast<int64_t>(now_ns - regime_start_ns);
    const int64_t drift = (static_cast<int64_t>(drift_per_min) * elapsed_ns)
                          / 60'000'000'000LL;
    int32_t out = fv + static_cast<int32_t>(drift);
    const int32_t tick_step = static_cast<int32_t>(tick_size) * 5;
    if (out < min_price + tick_step) out = min_price + tick_step;
    else if (out > max_price - tick_step) out = max_price - tick_step;
    return out;
}

int32_t RandomWalkFairValue::process(uint64_t ts) {
    if (last_ts == 0) {
        last_ts = ts + time_dist(gen);
        return apply_drift_overlay(fair_value, drift_per_min, regime_start_ns, ts,
                                   min_price, max_price, tick_size);
    }

    if (ts <= last_ts) {
        return apply_drift_overlay(fair_value, drift_per_min, regime_start_ns, ts,
                                   min_price, max_price, tick_size);
    }

    int32_t change = walk_value(gen);
    fair_value += change;
    const int32_t tick_step = static_cast<int32_t>(tick_size) * 5;
    if (fair_value < min_price + tick_step) {
        fair_value = min_price + tick_step;
    } else if (fair_value > max_price - tick_step) {
        fair_value = max_price - tick_step;
    }

    // step forward a random amount of time
    last_ts = ts + time_dist(gen);
    return apply_drift_overlay(fair_value, drift_per_min, regime_start_ns, ts,
                               min_price, max_price, tick_size);
}

void RandomWalkFairValue::apply_jump(double multiplier) {
    int32_t v = static_cast<int32_t>(std::lround(fair_value * multiplier));
    // Snap to the symbol's tick grid and clamp into the legal price band, with
    // a tick_step margin on either edge to leave room for normal walking.
    const int32_t ts32 = static_cast<int32_t>(tick_size);
    v = (v / ts32) * ts32;
    const int32_t tick_step = ts32 * 5;
    v = std::clamp(v, min_price + tick_step, max_price - tick_step);
    fair_value = v;
}

void RandomWalkFairValue::start_directional_drift(int32_t units_per_min, uint64_t now_ns) {
    drift_per_min = units_per_min;
    regime_start_ns = now_ns;
}

void RandomWalkFairValue::end_directional_drift(uint64_t now_ns) {
    if (drift_per_min == 0) return;
    // Bake the cumulative drift into fair_value so prices don't snap back
    // when consumers call process() outside the regime.
    const int64_t elapsed_ns = (now_ns >= regime_start_ns)
                                 ? static_cast<int64_t>(now_ns - regime_start_ns) : 0;
    const int64_t drift = (static_cast<int64_t>(drift_per_min) * elapsed_ns) / 60'000'000'000LL;
    fair_value += static_cast<int32_t>(drift);
    const int32_t tick_step = static_cast<int32_t>(tick_size) * 5;
    fair_value = std::clamp(fair_value, min_price + tick_step, max_price - tick_step);
    drift_per_min = 0;
    regime_start_ns = 0;
}

} // namespace ndfex::bots