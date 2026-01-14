cmake_minimum_required(VERSION 3.16)
project(NDFEX VERSION 1.0.0 LANGUAGES CXX)

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Werror -Wno-interference-size)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Common include directories (available to all targets)
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/3rdparty/spdlog/include
    ${CMAKE_SOURCE_DIR}/3rdparty/SPSCQueue/include
    ${CMAKE_SOURCE_DIR}/order_entry
)

# Threading
find_package(Threads REQUIRED)

# Build components
add_subdirectory(matching_engine)
add_subdirectory(bots)
add_subdirectory(market_data)

# Optional components
option(BUILD_VIEWER "Build the terminal viewer (requires FTXUI)" OFF)
option(BUILD_WEB_DATA "Build the web data server (requires websocketpp)" OFF)

if(BUILD_VIEWER)
    add_subdirectory(viewer)
endif()

if(BUILD_WEB_DATA)
    add_subdirectory(web_data)
endif()

# Tests (optional)
option(BUILD_TESTS "Build tests (requires GoogleTest)" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(matching_engine/tests)
    add_subdirectory(order_entry/tests)
endif()

