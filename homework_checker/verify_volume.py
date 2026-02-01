#!/usr/bin/env python3
"""
Independent volume verification script.

Connects to the web_data WebSocket and tracks trades with proper deduplication,
then compares against the homework checker's /volume endpoint.
"""

import asyncio
import json
import urllib.request
import aiohttp

HOMEWORK_CHECKER_URL = "http://localhost:8080"
WS_URL = "ws://localhost:9002"


async def main():
    # Independent volume tracking with proper dedup
    # Track trades by (seq_num, symbol, aggressor_side, quantity, price) to avoid double-counting
    seen_trades = set()
    volume_by_symbol = {}  # symbol -> [(seq_num, cumulative_volume)]
    message_count = 0

    print("Connecting to web_data WebSocket...")
    async with aiohttp.ClientSession() as session:
        async with session.ws_connect(WS_URL) as ws:
            async for msg in ws:
                if msg.type != aiohttp.WSMsgType.TEXT:
                    continue

                data = json.loads(msg.data)
                seq_num = data.get("seq_num", 0)
                trades = data.get("trades", [])
                message_count += 1

                new_trade_count = 0
                for trade in trades:
                    trade_key = (
                        trade["seq_num"],
                        trade["symbol"],
                        trade["aggressor_side"],
                        trade["quantity"],
                        trade["price"],
                        trade["timestamp"],  # unique per trade
                    )
                    if trade_key not in seen_trades:
                        seen_trades.add(trade_key)
                        new_trade_count += 1

                        symbol = trade["symbol"]
                        trade_seq = trade["seq_num"]
                        if symbol not in volume_by_symbol:
                            volume_by_symbol[symbol] = [(0, 0)]  # sentinel

                        history = volume_by_symbol[symbol]
                        prev_cumulative = history[-1][1]
                        new_cumulative = prev_cumulative + trade["quantity"]

                        if not history or history[-1][0] < trade_seq:
                            history.append((trade_seq, new_cumulative))
                        elif history[-1][0] == trade_seq:
                            history[-1] = (trade_seq, new_cumulative)

                # Compare against homework checker every 50 messages (~5 seconds)
                if message_count % 50 == 0 and len(volume_by_symbol) > 0:
                    print(f"\n--- After {message_count} messages, {len(seen_trades)} unique trades ---")
                    print(f"Trades in latest message: {len(trades)}, new: {new_trade_count}")

                    # Get bounds from homework checker
                    try:
                        with urllib.request.urlopen(f"{HOMEWORK_CHECKER_URL}/volume/bounds", timeout=5) as resp:
                            bounds_data = json.loads(resp.read().decode())
                    except Exception as e:
                        print(f"Failed to get bounds: {e}")
                        continue

                    for symbol_str, bounds in bounds_data.get("volume_bounds", {}).items():
                        symbol = int(symbol_str)
                        if symbol not in volume_by_symbol:
                            continue

                        my_history = volume_by_symbol[symbol]
                        # Find a common range to compare
                        my_min_seq = my_history[1][0] if len(my_history) > 1 else 0
                        my_max_seq = my_history[-1][0]

                        checker_min = bounds["min_seq"]
                        checker_max = bounds["max_seq"]

                        # Use overlapping range
                        start = max(my_min_seq, checker_min)
                        end = min(my_max_seq, checker_max)

                        if end <= start + 100:
                            print(f"Symbol {symbol}: not enough overlap to compare (my: {my_min_seq}-{my_max_seq}, checker: {checker_min}-{checker_max})")
                            continue

                        # Move start inward a bit to avoid edge effects
                        start = start + 50
                        end = end - 50

                        # My volume calculation
                        import bisect
                        def my_volume_at(hist, s):
                            idx = bisect.bisect_right(hist, (s, float('inf'))) - 1
                            if idx < 0:
                                return 0
                            return hist[idx][1]

                        my_vol = my_volume_at(my_history, end) - my_volume_at(my_history, start - 1)

                        # Homework checker volume
                        try:
                            url = f"{HOMEWORK_CHECKER_URL}/volume?symbol={symbol}&start_seq={start}&end_seq={end}"
                            with urllib.request.urlopen(url, timeout=5) as resp:
                                vol_data = json.loads(resp.read().decode())
                            checker_vol = vol_data["volume"]
                        except Exception as e:
                            print(f"Symbol {symbol}: failed to query checker: {e}")
                            continue

                        match = "OK" if my_vol == checker_vol else "MISMATCH"
                        ratio = checker_vol / my_vol if my_vol > 0 else float('inf')
                        print(f"Symbol {symbol} range [{start}-{end}]: mine={my_vol}, checker={checker_vol}, ratio={ratio:.2f} [{match}]")


if __name__ == "__main__":
    asyncio.run(main())
