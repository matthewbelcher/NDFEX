#pragma once

#include <cstdint>

namespace ndfex::bots {

class FairValue {

public:

    virtual ~FairValue() = default;

    virtual int32_t process(uint64_t ts) = 0;

    // Side-aware fair-value queries. Default both to the symmetric mid
    // returned by process(); subclasses (notably BasketFairValue) override
    // these to return a side-specific "no-arb" valuation derived from the
    // component bid/ask touches rather than mids. The market maker uses
    // bid_value() − W/2 for its bid quote and ask_value() + W/2 for its
    // ask quote, so a basket implementation that returns sum(component_bids)
    // and sum(component_asks) prevents a static round-trip arb.
    virtual int32_t bid_value(uint64_t ts) { return process(ts); }
    virtual int32_t ask_value(uint64_t ts) { return process(ts); }

    // Multiplicative jump applied by the thin-market event; default no-op so
    // existing FairValue implementations don't have to opt in.
    virtual void apply_jump(double /*multiplier*/) {}

    // Directional-drift regime hooks (used by the "bear regime" event).
    // While active, each FV reading is shifted by `units_per_min * elapsed`
    // since the regime started. end_directional_drift bakes the cumulative
    // drift into the underlying state so prices don't snap back when the
    // regime ends. Default no-op for FVs that don't carry persistent state
    // (e.g. BasketFairValue, which follows components through their own
    // RandomWalk drift automatically).
    virtual void start_directional_drift(int32_t /*units_per_min*/, uint64_t /*now_ns*/) {}
    virtual void end_directional_drift(uint64_t /*now_ns*/) {}

};

}