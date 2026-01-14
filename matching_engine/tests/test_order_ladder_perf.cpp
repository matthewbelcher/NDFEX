#include "order_ladder.H"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

class PerfSubscriber {
public:
    void onNewOrder(uint64_t, uint32_t, ndfex::md::SIDE, uint32_t, int32_t, uint8_t) {}
    void onDeleteOrder(uint64_t, bool = true) {}
    void onModifyOrder(uint64_t, uint32_t, ndfex::md::SIDE, uint32_t, int32_t) {}
    void onTrade(uint64_t, uint32_t, int32_t) {}
    void onFill(uint64_t, uint32_t, ndfex::md::SIDE, uint32_t, int32_t, uint8_t, bool) {}
    void onCancelReject(uint64_t) {}
};

static std::size_t read_env_size(const char* name, std::size_t fallback) {
    const char* raw = std::getenv(name);
    if (!raw || *raw == '\0') {
        return fallback;
    }
    try {
        return static_cast<std::size_t>(std::stoull(raw));
    } catch (...) {
        return fallback;
    }
}

template <typename Func>
static double time_ms(Func&& func) {
    auto start = std::chrono::steady_clock::now();
    func();
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

int main() {
    constexpr uint32_t symbol = 1337;
    const std::size_t order_count = read_env_size("PERF_ORDER_COUNT", 200000);
    const std::size_t modify_count = read_env_size("PERF_MODIFY_COUNT", order_count);
    const std::size_t match_count = read_env_size("PERF_MATCH_COUNT", order_count);

    PerfSubscriber subscriber;

    {
        ndfex::OrderLadder<PerfSubscriber> ladder(&subscriber, symbol);
        double ms = time_ms([&]() {
            for (std::size_t i = 0; i < order_count; ++i) {
                ladder.new_order(static_cast<uint64_t>(i + 1),
                                 ndfex::md::SIDE::BUY,
                                 10,
                                 static_cast<int32_t>(100 + (i % 100)),
                                 0);
            }
        });

        std::cout << "new_order: " << order_count << " orders in "
                  << ms << " ms (" << (order_count / ms) << " orders/ms)\n";
    }

    {
        ndfex::OrderLadder<PerfSubscriber> ladder(&subscriber, symbol);
        for (std::size_t i = 0; i < order_count; ++i) {
            ladder.new_order(static_cast<uint64_t>(i + 1),
                             ndfex::md::SIDE::BUY,
                             10,
                             static_cast<int32_t>(100 + (i % 100)),
                             0);
        }

        double ms = time_ms([&]() {
            for (std::size_t i = 0; i < modify_count; ++i) {
                ladder.modify_order(static_cast<uint64_t>(i + 1),
                                    ndfex::md::SIDE::BUY,
                                    10,
                                    static_cast<int32_t>(200 + (i % 100)));
            }
        });

        std::cout << "modify_order: " << modify_count << " orders in "
                  << ms << " ms (" << (modify_count / ms) << " orders/ms)\n";
    }

    {
        ndfex::OrderLadder<PerfSubscriber> ladder(&subscriber, symbol);
        for (std::size_t i = 0; i < match_count; ++i) {
            ladder.new_order(static_cast<uint64_t>(i + 1),
                             ndfex::md::SIDE::BUY,
                             10,
                             100,
                             0);
        }

        double ms = time_ms([&]() {
            ladder.new_order(static_cast<uint64_t>(match_count + 1),
                             ndfex::md::SIDE::SELL,
                             static_cast<uint32_t>(match_count * 10),
                             100,
                             0);
        });

        std::cout << "match: " << match_count << " resting orders in "
                  << ms << " ms (" << (match_count / ms) << " orders/ms)\n";
    }

    return 0;
}
