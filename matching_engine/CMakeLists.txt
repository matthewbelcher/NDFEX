# Matching Engine executable

add_executable(matching_engine
    matching_engine.cpp
    md_mcast.cpp
    clearing_mcast.cpp
    utils.cpp
    ${CMAKE_SOURCE_DIR}/order_entry/oe_server.cpp
    ${CMAKE_SOURCE_DIR}/order_entry/oe_validator.cpp
    ${CMAKE_SOURCE_DIR}/order_entry/oe_client_handler.cpp
)

target_link_libraries(matching_engine
    Threads::Threads
)

target_include_directories(matching_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

