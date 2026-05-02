#!/usr/bin/env python3
"""
Quick per-team position lookup from the matching-engine log.

Use during the competition when a team's bot crashes (or otherwise misses
fills via OE) and they need to know their authoritative position before
deciding whether to flatten or resume. The matching engine log is the
source of truth — every fill is recorded with the responsible client_id.
ETF create/redeem adjustments from logs/etf_adjustments.log are also
folded in for the current day.

Typical run takes 5-15 seconds against a 10+ GB live log.

Usage:
    scripts/team_position.py team1
    scripts/team_position.py 7                 # by client_id
    scripts/team_position.py team4 --log matching_engine/logs/ME_2026-04-27

    # During an in-session check, restrict to the official competition window
    # (matches how replay_pnl.py is invoked for end-of-day scoring):
    scripts/team_position.py team1 --from 11:00 --to 15:00
"""

from __future__ import annotations

import argparse
import datetime
import glob
import re
import subprocess
import sys
from pathlib import Path
from typing import Dict, Optional

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_USERS = REPO_ROOT / "matching_engine" / "users.txt"
DEFAULT_ADJUSTMENT_LOG = REPO_ROOT / "logs" / "etf_adjustments.log"
ME_LOG_GLOB = str(REPO_ROOT / "matching_engine" / "logs" / "ME_*")

SYMBOL_NAMES = {
    1: "GOLD", 2: "BLUE", 3: "KNAN", 4: "STED", 5: "FISH", 6: "DILN",
    7: "SORN", 8: "RYAN", 9: "LYON", 10: "WLSH", 11: "LEWI", 12: "BDIN",
    13: "UNDY",
}
POSITION_LIMIT = 10            # see docs/competition_rules.py §3
MIN_VOLUME = 2000              # prize-eligibility floor; same doc §2
EXEMPT_CLIENTS = {98, 99}      # LP/test bots — competition rules don't apply

# AUTH: Fill for order id <oid> client id <cid> symbol <s> quantity <q> price <p> side <1|2> flags <f>
FILL_RE = re.compile(
    r"AUTH: Fill for order id \d+ client id (?P<cid>\d+) symbol (?P<sym>\d+) "
    r"quantity (?P<qty>\d+) price (?P<price>-?\d+) side (?P<side>\d+)"
)


def parse_time_of_day(s: str) -> str:
    """'HH:MM' or 'HH:MM:SS' -> normalized 'HH:MM:SS' (lex-comparable)."""
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


def latest_me_log() -> Optional[Path]:
    files = sorted(glob.glob(ME_LOG_GLOB))
    return Path(files[-1]) if files else None


def load_users(path: Path) -> Dict[int, str]:
    users: Dict[int, str] = {}
    if not path.exists():
        return users
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) >= 2:
            try:
                users[int(parts[0])] = parts[1]
            except ValueError:
                pass
    return users


def resolve_cid(token: str, users: Dict[int, str]) -> int:
    if token.isdigit():
        return int(token)
    for cid, name in users.items():
        if name == token:
            return cid
    print(f"team_position: '{token}' is not a client_id or known username "
          f"(see {DEFAULT_USERS})", file=sys.stderr)
    sys.exit(2)


def replay_fills(me_log: Path, target_cid: int,
                 time_from: Optional[str] = None,
                 time_to: Optional[str] = None):
    """Pre-filter via grep on the exact 'client id N symbol' substring (the
    Fill-line shape), then parse the small remainder in Python. If
    time_from / time_to are set ('HH:MM:SS'), only fills with
    `time_from <= time-of-day < time_to` are counted.
    """
    needle = f"client id {target_cid} symbol"
    pos: Dict[int, int] = {}
    vol: Dict[int, int] = {}
    last_price: Dict[int, int] = {}
    last_ts: Optional[str] = None
    fills = 0
    skipped = 0

    cmd = ["grep", "-F", needle, str(me_log)]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True, errors="replace")
    assert proc.stdout is not None
    for line in proc.stdout:
        # Time-of-day filter: spdlog "[YYYY-MM-DD HH:MM:SS.fff] ..." → HH:MM:SS at line[12:20]
        if (time_from or time_to) and len(line) >= 20 and line[0] == "[":
            hms = line[12:20]
            if time_from and hms < time_from:
                skipped += 1
                continue
            if time_to and hms >= time_to:
                skipped += 1
                continue
        m = FILL_RE.search(line)
        if not m:
            continue
        if int(m["cid"]) != target_cid:
            continue
        sym = int(m["sym"])
        qty = int(m["qty"])
        price = int(m["price"])
        side = int(m["side"])
        if side == 1:
            pos[sym] = pos.get(sym, 0) + qty
        elif side == 2:
            pos[sym] = pos.get(sym, 0) - qty
        else:
            continue
        vol[sym] = vol.get(sym, 0) + qty
        last_price[sym] = price
        # spdlog format: "[YYYY-MM-DD HH:MM:SS.fff] ..."
        if line.startswith("[") and len(line) > 24:
            last_ts = line[1:24]
        fills += 1
    proc.wait()
    return pos, vol, last_price, last_ts, fills, skipped


def apply_today_adjustments(adj_log: Path, target_cid: int, today: str,
                            pos: Dict[int, int],
                            time_from: Optional[str] = None,
                            time_to: Optional[str] = None) -> int:
    """Fold in same-day ETF create/redeem deltas for this client.

    When `time_from`/`time_to` are set, only adjustments whose local time-of-day
    is in `[time_from, time_to)` are applied — same convention as the fill
    filter so an in-session check excludes pre-open/post-close ETF activity.
    """
    if not adj_log.exists():
        return 0
    count = 0
    with adj_log.open(errors="replace") as fh:
        for raw in fh:
            parts = raw.split()
            if len(parts) != 5:
                continue
            try:
                ts_ns = int(parts[0])
                cid = int(parts[2])
                sym = int(parts[3])
                delta = int(parts[4])
            except ValueError:
                continue
            if cid != target_cid:
                continue
            ts_local = datetime.datetime.fromtimestamp(ts_ns / 1e9)
            if ts_local.strftime("%Y-%m-%d") != today:
                continue
            if time_from or time_to:
                hms = ts_local.strftime("%H:%M:%S")
                if time_from and hms < time_from:
                    continue
                if time_to and hms >= time_to:
                    continue
            pos[sym] = pos.get(sym, 0) + delta
            count += 1
    return count


def session_date(name: str) -> Optional[str]:
    m = re.search(r"(\d{4}-\d{2}-\d{2})", name)
    return m.group(1) if m else None


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("team", help="client_id (e.g. 7) or username from users.txt (e.g. team4)")
    ap.add_argument("--log", type=Path, default=None,
                    help="ME log file (default: most recent matching_engine/logs/ME_*)")
    ap.add_argument("--users", type=Path, default=DEFAULT_USERS,
                    help=f"users.txt path (default: {DEFAULT_USERS})")
    ap.add_argument("--adjustment-log", type=Path, default=DEFAULT_ADJUSTMENT_LOG,
                    help="etf_adjustments.log path (set to /dev/null to skip)")
    ap.add_argument("--from", dest="time_from", type=parse_time_of_day, default=None,
                    metavar="HH:MM",
                    help="only count fills/adjustments at or after this local "
                         "time-of-day (e.g. 11:00 to exclude pre-session activity)")
    ap.add_argument("--to", dest="time_to", type=parse_time_of_day, default=None,
                    metavar="HH:MM",
                    help="only count fills/adjustments strictly before this "
                         "local time-of-day (e.g. 15:00)")
    args = ap.parse_args()

    users = load_users(args.users)
    cid = resolve_cid(args.team, users)
    name = users.get(cid, f"client_{cid}")

    me_log = args.log or latest_me_log()
    if me_log is None or not me_log.exists():
        print("team_position: no ME log found", file=sys.stderr)
        return 1

    pos, vol, last_price, last_ts, fills, skipped = replay_fills(
        me_log, cid, args.time_from, args.time_to,
    )

    log_date = session_date(me_log.name) or datetime.date.today().strftime("%Y-%m-%d")
    adj_count = apply_today_adjustments(
        args.adjustment_log, cid, log_date, pos,
        args.time_from, args.time_to,
    )

    summary_extras = []
    if adj_count:
        summary_extras.append(f"{adj_count} ETF adjustment(s)")
    if skipped:
        summary_extras.append(f"{skipped:,} fills skipped outside window")
    extras_str = (" + " + ", ".join(summary_extras)) if summary_extras else ""

    print(f"{name} (client_id={cid})")
    print(f"  source:    {me_log.name}  —  {fills:,} fills{extras_str}")
    if args.time_from or args.time_to:
        print(f"  window:    [{args.time_from or '00:00:00'}, "
              f"{args.time_to or '24:00:00'})")
    if last_ts:
        print(f"  last fill: {last_ts}")

    print()
    header = f"  {'sym':<3}  {'ticker':<6}  {'position':>9}  {'volume':>8}  {'last_px':>8}"
    print(header)
    print(f"  {'-'*3}  {'-'*6}  {'-'*9}  {'-'*8}  {'-'*8}")

    is_exempt = cid in EXEMPT_CLIENTS

    total_abs_pos = 0
    total_vol = 0
    breaches = []
    for sym in sorted(set(list(pos.keys()) + list(vol.keys()))):
        p = pos.get(sym, 0)
        v = vol.get(sym, 0)
        lp = last_price.get(sym, 0)
        if p == 0 and v == 0:
            continue
        over = abs(p) > POSITION_LIMIT and not is_exempt
        flag = "  ⚠ over limit" if over else ""
        if over:
            breaches.append(SYMBOL_NAMES.get(sym, str(sym)))
        ticker = SYMBOL_NAMES.get(sym, "?")
        print(f"  {sym:<3}  {ticker:<6}  {p:>+9d}  {v:>8d}  {lp:>8d}{flag}")
        total_abs_pos += abs(p)
        total_vol += v

    print(f"  {'-'*3}  {'-'*6}  {'-'*9}  {'-'*8}  {'-'*8}")
    print(f"  {'':3}  {'TOTAL':<6}  {'|' + str(total_abs_pos) + '|':>9}  {total_vol:>8}")
    if is_exempt:
        print()
        print(f"  ℹ {name} is on the exempt list — position-limit and volume rules do not apply.")
        return 0
    if breaches:
        print()
        print(f"  ⚠ position limit (|pos| > {POSITION_LIMIT}) breached on: {', '.join(breaches)}")
        print(f"    these symbols' positive PnL is permanently clamped to 0 per the rules.")
    if total_vol < MIN_VOLUME:
        print()
        print(f"  ℹ volume {total_vol} is below the {MIN_VOLUME}-lot prize-eligibility floor.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
