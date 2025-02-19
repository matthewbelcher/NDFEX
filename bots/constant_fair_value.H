#pragma once

#include "fair_value.H"

#include <cstdint>

namespace ndfex::bots {

class ConstantFairValue
    : public FairValue {

public:

    ConstantFairValue(int32_t start_price);

    virtual int32_t process(uint64_t ts) override final;

private:

    int32_t fair_value{0};
};

} // namespace ndfex::bots