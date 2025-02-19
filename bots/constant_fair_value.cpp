#include "constant_fair_value.H"

namespace ndfex::bots {

ConstantFairValue::ConstantFairValue(int32_t start_price)
    : fair_value(start_price) {}

int32_t ConstantFairValue::process(uint64_t ts) {
    return fair_value;
}

} // namespace ndfex::bots