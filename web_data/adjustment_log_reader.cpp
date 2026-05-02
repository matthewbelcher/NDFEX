#include "adjustment_log_reader.H"

#include <cstdlib>
#include <sstream>

namespace ndfex::clearing {

AdjustmentLogReader::AdjustmentLogReader(std::string path, ClearingClient& clearing,
                                         std::shared_ptr<spdlog::logger> logger)
    : path(std::move(path)), clearing(clearing), logger(logger) {
    // Open + seek to EOF — we only apply adjustments that arrive AFTER
    // web_data starts. Replaying prior adjustments would mismatch our
    // clearing state (which only includes fills since startup), producing
    // nonsensical positions. The persistent log is for offline replay
    // (replay_pnl.py); live state resets on web_data restart, matching
    // the existing clearing-multicast semantics.
    if (ensure_open()) {
        fh.seekg(0, std::ios::end);
        auto pos = fh.tellg();
        if (pos > 0) {
            logger->info("AdjustmentLogReader: skipping {} bytes of pre-startup history in {}",
                         static_cast<long long>(pos), this->path);
        }
    }
}

bool AdjustmentLogReader::ensure_open() {
    if (fh.is_open()) {
        return true;
    }
    fh.open(path, std::ios::in | std::ios::binary);
    if (!fh.is_open()) {
        if (!open_failed_warned) {
            logger->warn("AdjustmentLogReader: {} not present yet — will retry", path);
            open_failed_warned = true;
        }
        return false;
    }
    // First-time open during runtime (after the file appears post-startup):
    // start at end-of-file too, for the same consistency reason.
    fh.seekg(0, std::ios::end);
    return true;
}

void AdjustmentLogReader::apply_line(const std::string& line) {
    // Format: <unix_ns> <op> <client_id> <symbol> <delta>
    // Any malformed line is skipped with an error log; we never want a
    // single bad write from etf_service to take down the leaderboard.
    std::istringstream ss(line);
    uint64_t ts_ns;
    std::string op;
    int64_t client_id, symbol, delta;
    if (!(ss >> ts_ns >> op >> client_id >> symbol >> delta)) {
        logger->error("AdjustmentLogReader: malformed line: '{}'", line);
        return;
    }
    if (client_id < 0 || symbol < 0) {
        logger->error("AdjustmentLogReader: negative id/symbol in '{}'", line);
        return;
    }
    clearing.apply_etf_adjustment(static_cast<uint32_t>(client_id),
                                  static_cast<uint32_t>(symbol),
                                  static_cast<int32_t>(delta));
    ++applied_count;
}

void AdjustmentLogReader::process() {
    if (!ensure_open()) {
        return;
    }
    // ifstream sets failbit at EOF on getline; clear it before each call.
    fh.clear();

    char chunk[4096];
    while (fh.read(chunk, sizeof(chunk)) || fh.gcount() > 0) {
        partial.append(chunk, static_cast<size_t>(fh.gcount()));
    }
    fh.clear();

    // Drain whole lines from the partial buffer; leave any trailing
    // unterminated bytes for the next call.
    size_t pos = 0;
    while (true) {
        size_t nl = partial.find('\n', pos);
        if (nl == std::string::npos) break;
        std::string line = partial.substr(pos, nl - pos);
        if (!line.empty()) {
            apply_line(line);
        }
        pos = nl + 1;
    }
    if (pos > 0) {
        partial.erase(0, pos);
    }
}

} // namespace ndfex::clearing
