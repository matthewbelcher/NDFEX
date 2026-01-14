# Market Data Snapshot Service

add_executable(md_snapshots
    md_snapshots.cpp
    snapshot_client.cpp
    snapshot_writer.cpp
    ${CMAKE_SOURCE_DIR}/matching_engine/utils.cpp
)

target_link_libraries(md_snapshots
    Threads::Threads
)

target_include_directories(md_snapshots PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

