#pragma once

#include <cstdint>

namespace ndfex::bots {

class FairValue {

public:

    virtual int32_t process(uint64_t ts) = 0;

};

}