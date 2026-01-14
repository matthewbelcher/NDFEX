# Trading Bots

# Common bot sources
set(BOT_COMMON_SOURCES
    oe_client.cpp
    md_client.cpp
    order_book.cpp
    ${CMAKE_SOURCE_DIR}/matching_engine/utils.cpp
)

# Bot Runner (main bot with random walk fair value)
add_executable(bot_runner
    bot_runner.cpp
    ${BOT_COMMON_SOURCES}
    random_walk_fair_value.cpp
    fair_value_mm.cpp
    random_taker.cpp
    fair_value_stack_mm.cpp
    stack_manager.cpp
)
target_link_libraries(bot_runner Threads::Threads)

# Stable Bot Runner (constant fair value)
add_executable(stable_bot_runner
    stable_bot_runner.cpp
    ${BOT_COMMON_SOURCES}
    constant_fair_value.cpp
    fair_value_mm.cpp
    random_taker.cpp
)
target_link_libraries(stable_bot_runner Threads::Threads)

# Reject Bot (for testing rejections)
add_executable(reject_bot
    reject_bot.cpp
    ${BOT_COMMON_SOURCES}
    random_walk_fair_value.cpp
    fair_value_mm.cpp
    random_taker.cpp
)
target_link_libraries(reject_bot Threads::Threads)

# Smarter Bots (advanced strategies)
add_executable(smarter_bots
    smarter_bots.cpp
    ${BOT_COMMON_SOURCES}
    random_walk_fair_value.cpp
    fair_value_mm.cpp
    random_taker.cpp
    stack_manager.cpp
    fair_value_stack_mm.cpp
    imbalance_taker.cpp
    pressure_taker.cpp
    resting_orders.cpp
)
target_link_libraries(smarter_bots Threads::Threads)

# Print Snapshots utility
add_executable(print_snapshots
    print_snapshots.cpp
)
target_link_libraries(print_snapshots Threads::Threads)

