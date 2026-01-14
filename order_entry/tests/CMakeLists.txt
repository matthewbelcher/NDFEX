# Order Entry Tests

find_package(GTest REQUIRED)

# Test OE Stream Parser
add_executable(test_oe_stream_parser
    test_oe_stream_parser.cpp
    ${CMAKE_SOURCE_DIR}/matching_engine/utils.cpp
    ${CMAKE_SOURCE_DIR}/order_entry/oe_validator.cpp
    ${CMAKE_SOURCE_DIR}/order_entry/oe_client_handler.cpp
)
target_include_directories(test_oe_stream_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/matching_engine
    ${CMAKE_SOURCE_DIR}/order_entry
)
target_link_libraries(test_oe_stream_parser
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)
add_test(NAME test_oe_stream_parser COMMAND test_oe_stream_parser)

# Test OE Client Handler
add_executable(test_oe_client_handler
    test_oe_client_handler.cpp
    ${CMAKE_SOURCE_DIR}/matching_engine/utils.cpp
    ${CMAKE_SOURCE_DIR}/order_entry/oe_validator.cpp
    ${CMAKE_SOURCE_DIR}/order_entry/oe_client_handler.cpp
)
target_include_directories(test_oe_client_handler PRIVATE
    ${CMAKE_SOURCE_DIR}/matching_engine
    ${CMAKE_SOURCE_DIR}/order_entry
)
target_link_libraries(test_oe_client_handler
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)
add_test(NAME test_oe_client_handler COMMAND test_oe_client_handler)

