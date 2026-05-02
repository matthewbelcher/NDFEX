"""
Append-only structured log of ETF create/redeem position adjustments.

Format (one line per affected (client_id, symbol) pair):
    <unix_ns> <op> <client_id> <symbol> <delta>

Examples:
    1761594321000000000 create 4 3 -10
    1761594321000000000 create 4 13 10
    1761594398000000000 redeem 4 13 -5

Why a flat text file (vs. SQLite, JSON, etc.):
- web_data is C++ and tails this file each tick to apply position adjustments
  to its leaderboard view; a fixed-shape line format is trivial to parse there.
- Append-only writes are atomic for short lines on Linux, so a concurrent
  reader never sees a partial line.
- etf_service replays the file on startup to rebuild _etf_adjustments, giving
  it free persistence across restarts.

Single-writer (etf_service); multi-reader (etf_service-on-startup, web_data).
"""

from __future__ import annotations

import os
import threading
import time
from pathlib import Path
from typing import Callable, Iterator, Tuple


class AdjustmentLog:
    """Append-only log of (op, client_id, symbol, delta) tuples."""

    def __init__(self, path: Path):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        # Open in append+binary mode and write our own newlines so we control
        # exactly when bytes hit the kernel. line_buffering doesn't apply in
        # binary mode, so we flush after every write.
        self._fh = open(self.path, "ab", buffering=0)
        self._lock = threading.Lock()

    def replay(self) -> Iterator[Tuple[int, str, int, int, int]]:
        """Yield every (ts_ns, op, client_id, symbol, delta) currently in the file.

        Used at startup to rebuild in-memory state. Skips malformed lines
        (logged via print to stderr) so a partial-write at process death
        doesn't break recovery.
        """
        if not self.path.exists():
            return
        with open(self.path, "r") as fh:
            for line in fh:
                line = line.strip()
                if not line:
                    continue
                parts = line.split()
                if len(parts) != 5:
                    print(f"adjustment_log: skipping malformed line: {line!r}")
                    continue
                try:
                    ts_ns = int(parts[0])
                    op = parts[1]
                    client_id = int(parts[2])
                    symbol = int(parts[3])
                    delta = int(parts[4])
                except ValueError:
                    print(f"adjustment_log: skipping unparseable line: {line!r}")
                    continue
                yield ts_ns, op, client_id, symbol, delta

    def append(self, op: str, client_id: int, symbol: int, delta: int) -> None:
        """Append one (op, client_id, symbol, delta) record."""
        line = f"{time.time_ns()} {op} {client_id} {symbol} {delta}\n".encode("ascii")
        with self._lock:
            self._fh.write(line)
            # buffering=0 above means the write went straight to the kernel,
            # but explicit fsync would be needed for true durability. For
            # leaderboard purposes (web_data reads from page cache) the
            # default is fine.

    def close(self) -> None:
        with self._lock:
            try:
                self._fh.close()
            except Exception:
                pass
