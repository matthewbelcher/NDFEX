#pragma once

#include <stdexcept>

namespace ndfex {

class OrderNotFoundException : public std::runtime_error {
    public:
        explicit OrderNotFoundException(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace ndfex
