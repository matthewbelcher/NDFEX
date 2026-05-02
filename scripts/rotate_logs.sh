#!/bin/bash
#
# Gzip dated spdlog files from days prior to today.
#
# Targets files matching *_YYYY-MM-DD (spdlog daily_logger format) in the
# NDFEX log directories. Skips today's file (still being written by the live
# process) and anything already gzipped.
#
# Safe to re-run: already-compressed files are skipped.
# Intended to run daily via cron.

set -euo pipefail

TODAY=$(date +%Y-%m-%d)
LOG_DIRS=(
    /home/matt/NDFEX/bots/logs
    /home/matt/NDFEX/logs
)

for dir in "${LOG_DIRS[@]}"; do
    [[ -d "$dir" ]] || continue

    # Dated spdlog files: any name ending in _YYYY-MM-DD with no extension.
    # Exclude today's file (still open for append) and anything already .gz.
    while IFS= read -r -d '' f; do
        base=$(basename "$f")
        # Skip today's file
        if [[ "$base" == *"_${TODAY}" ]]; then
            continue
        fi
        # -9 for best compression; logs are highly redundant text
        gzip -9 "$f"
    done < <(find "$dir" -maxdepth 1 -type f \
        -regextype posix-extended \
        -regex '.*_[0-9]{4}-[0-9]{2}-[0-9]{2}$' \
        -print0)
done
