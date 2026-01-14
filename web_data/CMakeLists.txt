# Web Data Server (WebSocket server for web clients)

# Find websocketpp (header-only library)
# Users should have it installed or set WEBSOCKETPP_DIR
find_path(WEBSOCKETPP_INCLUDE_DIR websocketpp/server.hpp
    HINTS
        ${CMAKE_SOURCE_DIR}/3rdparty/websocketpp
        /usr/local/include
        /usr/include
)

if(NOT WEBSOCKETPP_INCLUDE_DIR)
    message(WARNING "websocketpp not found. web_data will not be built.")
    return()
endif()

add_executable(web_data
    web_data.cpp
    clearing_client.cpp
    ${CMAKE_SOURCE_DIR}/bots/md_client.cpp
    ${CMAKE_SOURCE_DIR}/bots/order_book.cpp
)

target_include_directories(web_data PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${WEBSOCKETPP_INCLUDE_DIR}
)

target_link_libraries(web_data
    Threads::Threads
)

