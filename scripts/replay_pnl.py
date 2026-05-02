#!/usr/bin/env python3
"""
Replay matching engine logs to compute per-client positions and PnL.

The matching engine writes one `AUTH: Fill` line per (client, fill) to
matching_engine/logs/ME_<date>. Those lines are the authoritative record of
every match — this script reconstructs positions/PnL from them so the
competition leaderboard survives an exchange crash.

ETF create/redeem activity is read from logs/etf_adjustments.log (written by
etf_service). Each adjustment is applied to the matching session by date —
positions are updated; volume, fees, and notional are not touched, since
ETF conversions are zero-cash.

Each input file is treated as one independent session. Per-session results
are printed and saved to CSV; a final aggregate sums per-session PnLs across
all sessions.

Usage:
    scripts/replay_pnl.py                       # all ME_* files
    scripts/replay_pnl.py path/to/ME_2026-04-24 # single session
    scripts/replay_pnl.py ME_*.log --out-dir reports/

    # Official competition scoring window — only count fills/adjustments
    # made between 11:00 and 15:00 local time on each session day:
    scripts/replay_pnl.py --from 11:00 --to 15:00
"""

from __future__ import annotations

import argparse
import csv
import datetime
import glob
import os
import re
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_LOG_GLOB = str(REPO_ROOT / "matching_engine" / "logs" / "ME_*")
DEFAULT_USERS = REPO_ROOT / "matching_engine" / "users.txt"
DEFAULT_ADJUSTMENT_LOG = REPO_ROOT / "logs" / "etf_adjustments.log"
DEFAULT_FEE = 0.05  # per share traded
ETF_SYMBOL = 13     # UNDY ETF symbol id (basket of 10 dorm components)

# Competition rules — must match web_data/clearing_client.{H,cpp}.
# POSITION_LIMIT: per-symbol high-water-mark of |position|. If a non-exempt
# client ever exceeds this on any symbol, that symbol is permanently flagged
# and any positive P&L on it is clamped to zero (losses are kept).
# MIN_PNL:        on the first event that drives a non-exempt client's
# clamped total P&L below this value, the client is frozen at that value
# for the rest of the session.
# EXEMPT_CLIENTS: house bots (smarter_bots, LP) — rules don't apply.
POSITION_LIMIT = 10
MIN_PNL = -5000.0
EXEMPT_CLIENTS = {98, 99}

# Match the matching engine's authoritative fill line. We anchor on
# "AUTH: Fill" and pull every named field by keyword so the regex survives
# field-order changes in spdlog.
FILL_RE = re.compile(
    r"AUTH: Fill for order id (?P<oid>\d+) "
    r"client id (?P<cid>\d+) "
    r"symbol (?P<sym>\d+) "
    r"quantity (?P<qty>\d+) "
    r"price (?P<price>-?\d+) "
    r"side (?P<side>\d+) "
    r"flags (?P<flags>\d+)"
)

SIDE_BUY = 1
SIDE_SELL = 2


@dataclass
class SymbolBook:
    position: int = 0
    total_buy_notional: int = 0   # price * qty summed for buys
    total_sell_notional: int = 0  # price * qty summed for sells
    volume: int = 0               # shares traded (both sides count)
    # High-water mark of |position| seen during the session, including the
    # effect of ETF adjustments. Once this exceeds POSITION_LIMIT for a
    # non-exempt client, the symbol stays flagged for the rest of the run.
    max_abs_position: int = 0


@dataclass
class SessionResult:
    name: str
    path: Path
    fills: int = 0
    fills_skipped_outside_window: int = 0  # only counted when --from/--to set
    adjustments: int = 0  # ETF create/redeem entries applied this session
    last_price: Dict[int, int] = field(default_factory=dict)
    # client_id -> symbol -> SymbolBook
    books: Dict[int, Dict[int, SymbolBook]] = field(
        default_factory=lambda: defaultdict(lambda: defaultdict(SymbolBook))
    )
    # client_id -> P&L value at which the drawdown freeze fired. Presence in
    # this dict means the client is frozen for the rest of the session and
    # later activity does not change their official score.
    frozen_pnl: Dict[int, float] = field(default_factory=dict)

    def per_client_pnl(self, fee: float, apply_rules: bool = True) -> Dict[int, Tuple[float, int, int]]:
        """Returns client_id -> (total_pnl, total_volume, total_abs_position).

        When apply_rules is True (the default and what the leaderboard uses):
          * Per-symbol P&L is clamped to ≤ 0 on any (cid, sym) where the
            position high-water mark exceeded POSITION_LIMIT (non-exempt only).
          * If the client is in self.frozen_pnl, that frozen value is returned
            instead — drawdown freeze locks in the score at trigger time.

        Pass apply_rules=False to get the unrestricted P&L for comparison.
        """
        out: Dict[int, Tuple[float, int, int]] = {}
        for cid, by_sym in self.books.items():
            vol = sum(b.volume for b in by_sym.values())
            abs_pos = sum(abs(b.position) for b in by_sym.values())

            if apply_rules and cid in self.frozen_pnl:
                out[cid] = (self.frozen_pnl[cid], vol, abs_pos)
                continue

            pnl = 0.0
            for sym, b in by_sym.items():
                mark = self.last_price.get(sym, 0)
                sym_pnl = (
                    b.total_sell_notional
                    - b.total_buy_notional
                    + b.position * mark
                    - fee * b.volume
                )
                if (apply_rules
                        and cid not in EXEMPT_CLIENTS
                        and b.max_abs_position > POSITION_LIMIT
                        and sym_pnl > 0):
                    sym_pnl = 0.0
                pnl += sym_pnl
            out[cid] = (pnl, vol, abs_pos)
        return out


def load_adjustments_by_date(path: Path,
                             time_from: Optional[str] = None,
                             time_to: Optional[str] = None,
                             ) -> Dict[str, List[Tuple[int, int, int, int]]]:
    """Parse logs/etf_adjustments.log and group entries by local-date string.

    Each line is `<unix_ns> <op> <client_id> <symbol> <delta>`. Returns a
    map from "YYYY-MM-DD" (local time, matching the ME daily-rotation
    convention) to a list of (ts_ns, client_id, symbol, delta) tuples
    sorted by timestamp. Returns {} if the file is missing — the script
    then behaves exactly like before, with no ETF reconciliation.

    If `time_from` / `time_to` are set ('HH:MM:SS'), only adjustments
    whose local time-of-day is in `[time_from, time_to)` are returned.

    The timestamp is kept so that replay_session can interleave adjustments
    with fills in chronological order — required for accurate freeze timing
    when a redeem trips a position-limit breach.
    """
    out: Dict[str, List[Tuple[int, int, int, int]]] = defaultdict(list)
    if not path.exists():
        return out
    with path.open("r", errors="replace") as fh:
        for raw in fh:
            line = raw.strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) != 5:
                print(f"replay_pnl: skipping malformed adjustment: {line!r}",
                      file=sys.stderr)
                continue
            try:
                ts_ns = int(parts[0])
                # parts[1] is op (create/redeem) — informational only; the
                # delta sign already encodes direction.
                cid = int(parts[2])
                sym = int(parts[3])
                delta = int(parts[4])
            except ValueError:
                print(f"replay_pnl: skipping unparseable adjustment: {line!r}",
                      file=sys.stderr)
                continue
            ts_local = datetime.datetime.fromtimestamp(ts_ns / 1e9)
            if time_from or time_to:
                hms = ts_local.strftime("%H:%M:%S")
                if time_from and hms < time_from:
                    continue
                if time_to and hms >= time_to:
                    continue
            date_str = ts_local.strftime("%Y-%m-%d")
            out[date_str].append((ts_ns, cid, sym, delta))
    # Adjustments are written sequentially by the etf_service, so they're
    # already in ts order in the file. Sort defensively in case multiple
    # writers ever race.
    for v in out.values():
        v.sort(key=lambda x: x[0])
    return out


def session_date(name: str) -> Optional[str]:
    """Extract the YYYY-MM-DD piece from a 'ME_YYYY-MM-DD' filename, or None."""
    m = re.search(r"(\d{4}-\d{2}-\d{2})", name)
    return m.group(1) if m else None


def parse_time_of_day(s: str) -> str:
    """Normalize 'HH:MM' or 'HH:MM:SS' to 'HH:MM:SS' for lexicographic compare
    against the spdlog timestamp slice. Raises argparse-friendly ValueError on
    bad input."""
    parts = s.split(":")
    if len(parts) == 2:
        h, m = parts
        sec = "00"
    elif len(parts) == 3:
        h, m, sec = parts
    else:
        raise ValueError(f"time must be HH:MM or HH:MM:SS, got {s!r}")
    if not (h.isdigit() and m.isdigit() and sec.isdigit()):
        raise ValueError(f"non-numeric time component in {s!r}")
    if not (0 <= int(h) < 24 and 0 <= int(m) < 60 and 0 <= int(sec) < 60):
        raise ValueError(f"time out of range in {s!r}")
    return f"{int(h):02d}:{int(m):02d}:{int(sec):02d}"


def load_users(path: Path) -> Dict[int, str]:
    """Parse users.txt: each line is `<client_id> <name> <password>`."""
    users: Dict[int, str] = {}
    if not path.exists():
        return users
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        try:
            users[int(parts[0])] = parts[1]
        except ValueError:
            continue
    return users


def replay_session(path: Path,
                   adjustments: Optional[List[Tuple[int, int, int, int]]] = None,
                   time_from: Optional[str] = None,
                   time_to: Optional[str] = None,
                   fee: float = DEFAULT_FEE,
                   apply_rules: bool = True) -> SessionResult:
    """Stream-parse one ME log file. Files are ~16 GB so we never hold them in memory.

    `adjustments` is a sorted list of (ts_ns, client_id, symbol, delta) ETF
    create/redeem entries that occurred in this session's date window. They
    are interleaved with fills in chronological order so that position-limit
    breaches and the drawdown freeze fire at the correct moment — a redeem
    that pushes a dorm over the 10-share limit must trip the breach exactly
    when it happened, not at end-of-session.

    If `time_from` / `time_to` are set ('HH:MM:SS'), only fills with
    `time_from <= time-of-day < time_to` are counted. Lexicographic compare
    on the spdlog timestamp slice — fast, no datetime parsing needed.

    When `apply_rules` is True (default), the per-symbol position-limit
    clamp and the −$5,000 drawdown freeze are evaluated on every event,
    matching the live web_data leaderboard semantics. Pass False for raw
    P&L without those penalties.
    """
    result = SessionResult(name=path.name, path=path)
    skipped = 0

    # Sorted-adjustment cursor for chronological merge with fills.
    adj_iter = iter(adjustments or [])
    next_adj = next(adj_iter, None)

    def update_hwm(cid: int, sym: int) -> None:
        b = result.books[cid][sym]
        ap = abs(b.position)
        if ap > b.max_abs_position:
            b.max_abs_position = ap

    def check_freeze(cid: int) -> None:
        """If this client's running clamped P&L just dropped below MIN_PNL
        for the first time, lock it in. No-op for exempt or already-frozen
        clients, or when rules are disabled."""
        if not apply_rules or cid in EXEMPT_CLIENTS or cid in result.frozen_pnl:
            return
        total = 0.0
        for sym, b in result.books[cid].items():
            mark = result.last_price.get(sym, 0)
            sym_pnl = (
                b.total_sell_notional - b.total_buy_notional
                + b.position * mark - fee * b.volume
            )
            if b.max_abs_position > POSITION_LIMIT and sym_pnl > 0:
                sym_pnl = 0.0
            total += sym_pnl
        if total < MIN_PNL:
            result.frozen_pnl[cid] = total

    def apply_adjustment(adj: Tuple[int, int, int, int]) -> None:
        _ts_ns, cid, sym, delta = adj
        result.books[cid][sym].position += delta
        result.adjustments += 1
        update_hwm(cid, sym)
        check_freeze(cid)

    with path.open("r", errors="replace") as fh:
        for line in fh:
            # Cheap prefilter — most lines aren't fills, and substring check
            # is ~3x faster than running the full regex per line.
            if "AUTH: Fill" not in line:
                continue
            # Time-of-day filter: spdlog format is "[YYYY-MM-DD HH:MM:SS.fff] ..."
            # so HH:MM:SS lives at line[12:20]. Lex compare matches numeric
            # ordering for fixed-width times.
            if (time_from or time_to) and len(line) >= 20 and line[0] == "[":
                hms = line[12:20]
                if time_from and hms < time_from:
                    skipped += 1
                    continue
                if time_to and hms >= time_to:
                    skipped += 1
                    continue

            # Drain any adjustments older than this fill so the freeze fires
            # at the right moment. Only pay the strptime cost while there's
            # actually something to merge — once next_adj is None, skip it.
            if next_adj is not None and len(line) >= 24 and line[0] == "[":
                try:
                    fill_dt = datetime.datetime.strptime(line[1:20], "%Y-%m-%d %H:%M:%S")
                    millis = int(line[21:24])
                    fill_ts_ns = int(fill_dt.timestamp() * 1_000_000_000) + millis * 1_000_000
                except (ValueError, IndexError):
                    fill_ts_ns = None
                if fill_ts_ns is not None:
                    while next_adj is not None and next_adj[0] < fill_ts_ns:
                        apply_adjustment(next_adj)
                        next_adj = next(adj_iter, None)

            m = FILL_RE.search(line)
            if not m:
                continue
            cid = int(m["cid"])
            sym = int(m["sym"])
            qty = int(m["qty"])
            price = int(m["price"])
            side = int(m["side"])
            book = result.books[cid][sym]
            if side == SIDE_BUY:
                book.position += qty
                book.total_buy_notional += price * qty
            elif side == SIDE_SELL:
                book.position -= qty
                book.total_sell_notional += price * qty
            else:
                continue  # unknown side, skip
            book.volume += qty
            result.last_price[sym] = price
            result.fills += 1
            update_hwm(cid, sym)
            check_freeze(cid)

    # Apply any adjustments that arrived after the last fill (or all
    # adjustments if there were no fills at all).
    while next_adj is not None:
        apply_adjustment(next_adj)
        next_adj = next(adj_iter, None)

    result.fills_skipped_outside_window = skipped
    return result


def name_for(cid: int, users: Dict[int, str]) -> str:
    return users.get(cid, f"client_{cid}")


def print_session_table(result: SessionResult, users: Dict[int, str], fee: float,
                        apply_rules: bool = True) -> None:
    pnl_rows = result.per_client_pnl(fee, apply_rules=apply_rules)
    sorted_rows = sorted(pnl_rows.items(), key=lambda kv: kv[1][0], reverse=True)

    skipped_str = ""
    if result.fills_skipped_outside_window:
        skipped_str = f", {result.fills_skipped_outside_window:,} skipped outside window"
    rules_str = "rules: on" if apply_rules else "rules: off (raw P&L)"
    header = (f"=== Session: {result.name}  "
              f"({result.fills:,} fills{skipped_str}, "
              f"{result.adjustments:,} ETF adjustments, {rules_str}) ===")
    print()
    print(header)
    if not sorted_rows:
        print("  (no fills)")
        return
    print(f"  {'client':<6} {'name':<14} {'pnl':>12}  {'volume':>10}  {'|pos|':>6}  flags")
    print(f"  {'-'*6} {'-'*14} {'-'*12}  {'-'*10}  {'-'*6}  -----")
    for cid, (pnl, vol, abs_pos) in sorted_rows:
        flags: List[str] = []
        if apply_rules:
            if cid in result.frozen_pnl:
                flags.append("FROZEN")
            breached = sorted(s for s, b in result.books[cid].items()
                              if b.max_abs_position > POSITION_LIMIT
                              and cid not in EXEMPT_CLIENTS)
            if breached:
                flags.append("pos-breach=" + ",".join(str(s) for s in breached))
            if cid in EXEMPT_CLIENTS:
                flags.append("exempt")
        flag_str = " ".join(flags)
        print(f"  {cid:<6} {name_for(cid, users):<14} {pnl:>12,.2f}  {vol:>10,d}  {abs_pos:>6,d}  {flag_str}")

    # Mark prices used (helps reviewers understand the mark assumption).
    if result.last_price:
        marks = ", ".join(
            f"{sym}={price}" for sym, price in sorted(result.last_price.items())
        )
        print(f"  marks (last fill): {marks}")


def write_session_csv(result: SessionResult, users: Dict[int, str], fee: float, out_dir: Path,
                      apply_rules: bool = True) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    # ME_2026-04-24 -> pnl_session_2026-04-24.csv
    suffix = result.name.replace("ME_", "", 1) if result.name.startswith("ME_") else result.name
    out_path = out_dir / f"pnl_session_{suffix}.csv"
    with out_path.open("w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["client_id", "name", "symbol", "position", "max_abs_position",
                    "buy_notional", "sell_notional", "volume", "mark_price",
                    "raw_pnl", "clamped_pnl", "pos_breach"])
        for cid in sorted(result.books):
            is_exempt = cid in EXEMPT_CLIENTS
            for sym in sorted(result.books[cid]):
                b = result.books[cid][sym]
                mark = result.last_price.get(sym, 0)
                raw = (b.total_sell_notional - b.total_buy_notional
                       + b.position * mark - fee * b.volume)
                breached = (apply_rules and not is_exempt
                            and b.max_abs_position > POSITION_LIMIT)
                clamped = 0.0 if (breached and raw > 0) else raw
                w.writerow([cid, name_for(cid, users), sym, b.position,
                            b.max_abs_position, b.total_buy_notional,
                            b.total_sell_notional, b.volume, mark,
                            f"{raw:.2f}", f"{clamped:.2f}",
                            "true" if breached else "false"])
    return out_path


def print_aggregate(results: List[SessionResult], users: Dict[int, str], fee: float,
                    apply_rules: bool = True) -> Dict[int, Tuple[float, int]]:
    """Sum per-session PnLs per client and print a final scoreboard."""
    totals: Dict[int, List[float]] = defaultdict(lambda: [0.0, 0])  # [pnl, volume]
    sessions_played: Dict[int, int] = defaultdict(int)
    for r in results:
        for cid, (pnl, vol, _abs_pos) in r.per_client_pnl(fee, apply_rules=apply_rules).items():
            totals[cid][0] += pnl
            totals[cid][1] += vol
            sessions_played[cid] += 1

    print()
    print(f"=== Aggregate across {len(results)} session(s) ===")
    if not totals:
        print("  (no fills in any session)")
        return {}

    sorted_rows = sorted(totals.items(), key=lambda kv: kv[1][0], reverse=True)
    print(f"  {'client':<6} {'name':<14} {'total_pnl':>14}  {'total_vol':>11}  {'sessions':>8}")
    print(f"  {'-'*6} {'-'*14} {'-'*14}  {'-'*11}  {'-'*8}")
    out: Dict[int, Tuple[float, int]] = {}
    for cid, (pnl, vol) in sorted_rows:
        print(f"  {cid:<6} {name_for(cid, users):<14} {pnl:>14,.2f}  {vol:>11,d}  {sessions_played[cid]:>8}")
        out[cid] = (pnl, vol)
    return out


def write_aggregate_csv(totals: Dict[int, Tuple[float, int]],
                        sessions_played: Dict[int, int],
                        users: Dict[int, str],
                        out_dir: Path) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / "pnl_aggregate.csv"
    with out_path.open("w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["client_id", "name", "total_pnl", "total_volume", "sessions_played"])
        for cid in sorted(totals, key=lambda c: totals[c][0], reverse=True):
            pnl, vol = totals[cid]
            w.writerow([cid, name_for(cid, users), f"{pnl:.2f}", vol, sessions_played[cid]])
    return out_path


def resolve_inputs(paths: Iterable[str]) -> List[Path]:
    if not paths:
        expanded = sorted(glob.glob(DEFAULT_LOG_GLOB))
    else:
        expanded = []
        for p in paths:
            matches = sorted(glob.glob(p))
            expanded.extend(matches if matches else [p])
    out: List[Path] = []
    for p in expanded:
        path = Path(p)
        if not path.is_file():
            print(f"warning: skipping {p} (not a file)", file=sys.stderr)
            continue
        out.append(path)
    return out


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="*", help="ME log files (default: all matching_engine/logs/ME_*)")
    ap.add_argument("--users", type=Path, default=DEFAULT_USERS, help="users.txt for client_id -> name mapping")
    ap.add_argument("--fee", type=float, default=DEFAULT_FEE, help=f"per-share fee (default {DEFAULT_FEE})")
    ap.add_argument("--out-dir", type=Path, default=Path.cwd(), help="directory for CSV outputs")
    ap.add_argument("--no-csv", action="store_true", help="skip CSV output, print tables only")
    ap.add_argument("--adjustment-log", type=Path, default=DEFAULT_ADJUSTMENT_LOG,
                    help="ETF create/redeem log to fold into per-session positions "
                         "(set to /dev/null to skip)")
    ap.add_argument("--from", dest="time_from", type=parse_time_of_day, default=None,
                    metavar="HH:MM",
                    help="only count fills/adjustments at or after this local "
                         "time-of-day in each session (e.g. 11:00 for the "
                         "official 11am competition open)")
    ap.add_argument("--to", dest="time_to", type=parse_time_of_day, default=None,
                    metavar="HH:MM",
                    help="only count fills/adjustments strictly before this "
                         "local time-of-day in each session (e.g. 15:00)")
    ap.add_argument("--no-rules", dest="apply_rules", action="store_false",
                    default=True,
                    help="skip the position-limit clamp and the −$5,000 "
                         "drawdown freeze. Default is to apply them, "
                         "matching the live leaderboard scoring.")
    args = ap.parse_args(argv)

    inputs = resolve_inputs(args.logs)
    if not inputs:
        print("no input files found", file=sys.stderr)
        return 1

    users = load_users(args.users)
    adjustments_by_date = load_adjustments_by_date(
        args.adjustment_log, args.time_from, args.time_to,
    )
    total_adj = sum(len(v) for v in adjustments_by_date.values())
    window_str = ""
    if args.time_from or args.time_to:
        window_str = (f"; window=[{args.time_from or '00:00:00'},"
                      f"{args.time_to or '24:00:00'})")
    print(f"replaying {len(inputs)} session(s); fee={args.fee}/share; "
          f"users loaded: {len(users)}; adjustments loaded: {total_adj} "
          f"across {len(adjustments_by_date)} day(s){window_str}")

    results: List[SessionResult] = []
    for path in inputs:
        print(f"  parsing {path} ...", file=sys.stderr)
        date = session_date(path.name)
        session_adj = adjustments_by_date.get(date, []) if date else []
        results.append(replay_session(path, session_adj,
                                      time_from=args.time_from,
                                      time_to=args.time_to,
                                      fee=args.fee,
                                      apply_rules=args.apply_rules))

    sessions_played: Dict[int, int] = defaultdict(int)
    for r in results:
        print_session_table(r, users, args.fee, apply_rules=args.apply_rules)
        if not args.no_csv:
            csv_path = write_session_csv(r, users, args.fee, args.out_dir,
                                         apply_rules=args.apply_rules)
            print(f"  -> {csv_path}")
        for cid in r.books:
            sessions_played[cid] += 1

    totals: Dict[int, List[float]] = defaultdict(lambda: [0.0, 0])
    for r in results:
        for cid, (pnl, vol, _) in r.per_client_pnl(args.fee, apply_rules=args.apply_rules).items():
            totals[cid][0] += pnl
            totals[cid][1] += vol
    print_aggregate(results, users, args.fee, apply_rules=args.apply_rules)
    if not args.no_csv:
        agg_path = write_aggregate_csv(
            {c: (v[0], v[1]) for c, v in totals.items()},
            sessions_played, users, args.out_dir,
        )
        print(f"  -> {agg_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
