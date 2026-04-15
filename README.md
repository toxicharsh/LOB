# LOB — Limit Order Book Engine

C++ limit order book with price–time matching, a small Level-2 style replay harness, and throughput benchmarks.

## Build

Requires a C++17 compiler (GCC or Clang).

```bash
make
```

Sanitized debug build (AddressSanitizer):

```bash
make debug
```

## Run

Runs built-in checks, benchmarks, then replays `data/sample_events.csv`:

```bash
make run
# or: ./orderbook
```

Replay a different CSV (same columns as the sample):

```bash
./orderbook path/to/events.csv
```

CSV format: `timestamp,type,side,price,qty,order_id`  
Types: `LIMIT`, `MARKET`, `CANCEL`, `MODIFY`. Side: `BUY` or `SELL`.

## What’s in here

| Piece | Role |
|-------|------|
| `order.h` | `Order`, `Trade`, `PriceLevel` (FIFO per price) |
| `order_book.*` | Two-sided book, matching, cancel/modify, order pool |
| `replay_engine.*` | Load CSV events, replay, VWAP / slippage-style stats |
| `main.cpp` | Tests, benchmarks, demo entry point |
| `data/sample_events.csv` | Example event stream |

## Design notes (short)

- **Price levels**: `std::map` so best bid/ask iteration is cheap and ordered.
- **Order lookup**: `std::unordered_map` from order id to `Order*` for cancel/modify.
- **FIFO at a price**: intrusive doubly-linked list on `Order` (no `std::list` per level).
- **Allocations**: slab-style `OrderPool` to recycle `Order` nodes instead of hammering the heap on every event.

## License

No license file included — add one if you want this to be clearly open source.
