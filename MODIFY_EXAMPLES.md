# Modify Order Examples

Examples of OE protocol messages from the client's perspective during modify order scenarios.

## Response Types

| Msg Type | Code | Description |
|----------|------|-------------|
| ACK      | 101  | Order accepted/modified |
| FILL     | 103  | Trade execution |
| CLOSE    | 104  | Order removed from book |
| REJECT   | 102  | Request rejected |

## Scenario 1: Simple modify (same price, no fills in flight)

Client has a resting BUY 10 @ 50. They modify to BUY 15 @ 50.

```
Client sends:    MODIFY_ORDER  order_id=1, side=BUY, qty=15, price=50

Client receives: ACK           order_id=1, qty=15, price=50
```

Since the price didn't change, queue priority is preserved. The ACK quantity is `quantity - filled_quantity` = `15 - 0` = 15.

## Scenario 2: Modify after partial fill (same price)

Client has a BUY 10 @ 50. A sell comes in and fills 3. Client then modifies to BUY 8 @ 50.

```
Client receives: FILL          order_id=1, qty=3, price=50, flags=PARTIAL_FILL

Client sends:    MODIFY_ORDER  order_id=1, side=BUY, qty=8, price=50

Client receives: ACK           order_id=1, qty=5, price=50
```

The ACK shows remaining quantity: `8 - 3` = 5. The client asked for total qty 8 but 3 are already filled, so 5 remain on the book.

## Scenario 3: Modify after partial fill -- new qty <= filled qty (auto-close)

Client has a BUY 10 @ 50. 7 get filled. Client modifies to BUY 5 @ 50 (but 7 are already filled).

```
Client receives: FILL          order_id=1, qty=7, price=50, flags=PARTIAL_FILL

Client sends:    MODIFY_ORDER  order_id=1, side=BUY, qty=5, price=50

Client receives: CLOSE         order_id=1
```

Since `filled_quantity (7) >= new quantity (5)`, the engine auto-closes the order. No ACK is sent -- just the CLOSE. The modify is effectively converted into a cancel.

## Scenario 4: Modify with price change (cancel-replace)

Client has a BUY 10 @ 50. They modify to BUY 10 @ 52.

```
Client sends:    MODIFY_ORDER  order_id=1, side=BUY, qty=10, price=52

Client receives: ACK           order_id=1, qty=10, price=52
```

Internally the order was deleted and re-inserted at the new price (losing queue priority), and matching ran against the opposite side. Nothing matched here, so the client just sees an ACK.

## Scenario 5: Modify with price change that triggers a trade

Client has a BUY 10 @ 50. There's a resting SELL 4 @ 52 from another user. Client modifies to BUY 10 @ 52.

```
Client sends:    MODIFY_ORDER  order_id=1, side=BUY, qty=10, price=52

Client receives: FILL          order_id=1, qty=4, price=52, flags=PARTIAL_FILL
Client receives: ACK           order_id=1, qty=6, price=52
```

The engine deletes the old order, re-inserts at 52 which triggers matching against the sell. The FILL comes first because `handle_new_order` runs `match()` before the `onModifyOrder` callback. The ACK shows qty=6 remaining.

## Scenario 6: Modify with price change that fully fills

Same setup as Scenario 5 but SELL is 10 @ 52.

```
Client sends:    MODIFY_ORDER  order_id=1, side=BUY, qty=10, price=52

Client receives: FILL          order_id=1, qty=10, price=52, flags=CLOSED
Client receives: CLOSE         order_id=1
```

The order fully matches so `quantity` becomes 0, and the engine sends a CLOSE instead of an ACK.

## Scenario 7: Modify on an unknown/already-filled order

Client sends a modify for an order that was fully filled and removed before the modify arrived.

```
Client sends:    MODIFY_ORDER  order_id=1, side=BUY, qty=10, price=50

Client receives: REJECT        order_id=1
```

The order is not found in the order map, so a REJECT is sent.

## Key Takeaway

The client never gets a "modify rejected due to partial fill" message. Instead the engine forces a valid state: it either adjusts the remaining quantity (ACK with a smaller qty than requested), or auto-closes the order (CLOSE) if the fills already exceed the requested new quantity. The `quantity` field in the ACK always reflects the **remaining** quantity on the book after accounting for fills, not the total the client originally requested.
