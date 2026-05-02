#pragma once

#include "clearing_client.H"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>

namespace ndfex::clearing {

/**
 * Tails the etf_service adjustment log (logs/etf_adjustments.log) and applies
 * each new line to a ClearingClient as a non-trading position adjustment.
 *
 * The log is written by etf_service one line per (client_id, symbol, delta)
 * pair on every successful create/redeem. Format:
 *     <unix_ns> <op> <client_id> <symbol> <delta>
 *
 * On startup we read the entire existing file (so positions reflect prior
 * state across web_data restarts), then poll for new bytes each tick. The
 * file may not exist yet when web_data starts before etf_service — the
 * reader retries the open on each process() call until it succeeds.
 */
class AdjustmentLogReader {
public:
    AdjustmentLogReader(std::string path, ClearingClient& clearing,
                        std::shared_ptr<spdlog::logger> logger);

    // Read any new complete lines and apply them to clearing. Cheap to call
    // every main-loop tick — only does I/O when the file has grown.
    void process();

private:
    bool ensure_open();
    void apply_line(const std::string& line);

    std::string path;
    ClearingClient& clearing;
    std::shared_ptr<spdlog::logger> logger;

    std::ifstream fh;
    std::string partial;       // bytes read but not yet terminated by '\n'
    bool open_failed_warned{false};
    uint64_t applied_count{0}; // for one-time startup-replay log line
};

} // namespace ndfex::clearing
