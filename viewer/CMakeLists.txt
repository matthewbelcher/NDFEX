# Viewer Tools (requires FTXUI and libpcap)

# Find FTXUI - check if it's been built locally
if(EXISTS ${CMAKE_SOURCE_DIR}/3rdparty/FTXUI/build)
    set(FTXUI_BUILD_DIR ${CMAKE_SOURCE_DIR}/3rdparty/FTXUI/build)
    set(FTXUI_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/3rdparty/FTXUI/include)
    
    # Link directories for FTXUI libraries
    link_directories(${FTXUI_BUILD_DIR})
endif()

# Find libpcap
find_library(PCAP_LIBRARY pcap)

# Common sources for viewer tools
set(VIEWER_COMMON_SOURCES
    ${CMAKE_SOURCE_DIR}/bots/md_client.cpp
    ${CMAKE_SOURCE_DIR}/bots/order_book.cpp
)

# MD Viewer (terminal UI)
add_executable(md_viewer
    md_viewer.cpp
    ${VIEWER_COMMON_SOURCES}
)
target_include_directories(md_viewer PRIVATE
    ${FTXUI_INCLUDE_DIR}
)
target_link_libraries(md_viewer
    Threads::Threads
    ftxui-dom
    ftxui-screen
    ftxui-component
)

# PCAP Printer
add_executable(pcap_printer
    pcap_printer.cpp
    ${VIEWER_COMMON_SOURCES}
)
target_include_directories(pcap_printer PRIVATE
    ${FTXUI_INCLUDE_DIR}
)
target_link_libraries(pcap_printer
    Threads::Threads
    ftxui-dom
    ftxui-screen
    ftxui-component
    ${PCAP_LIBRARY}
)

# BBO Printer
add_executable(bbo_printer
    bbo_printer.cpp
    ${VIEWER_COMMON_SOURCES}
)
target_include_directories(bbo_printer PRIVATE
    ${FTXUI_INCLUDE_DIR}
)
target_link_libraries(bbo_printer
    Threads::Threads
    ftxui-dom
    ftxui-screen
    ftxui-component
)

