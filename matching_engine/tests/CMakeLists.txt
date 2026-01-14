# Matching Engine Tests

find_package(GTest REQUIRED)

# Test Order Ladder
add_executable(test_order_ladder
    test_order_ladder.cpp
)
target_include_directories(test_order_ladder PRIVATE
    ${CMAKE_SOURCE_DIR}/matching_engine
)
target_link_libraries(test_order_ladder
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)
add_test(NAME test_order_ladder COMMAND test_order_ladder)

# Test MD Mcast
add_executable(test_md_mcast
    test_md_mcast.cpp
    ${CMAKE_SOURCE_DIR}/matching_engine/md_mcast.cpp
    ${CMAKE_SOURCE_DIR}/matching_engine/utils.cpp
)
target_include_directories(test_md_mcast PRIVATE
    ${CMAKE_SOURCE_DIR}/matching_engine
)
target_link_libraries(test_md_mcast
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)
add_test(NAME test_md_mcast COMMAND test_md_mcast)

