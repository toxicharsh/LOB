#include "order_book.h"
#include <algorithm>
#include <cassert>
#include <iomanip>

// ============================================================
// internal helpers
// ============================================================

Order* OrderBook::alloc_order(OrderId id, Side side, Price price, Qty qty) {
    Order* o     = pool_.allocate();
    o->id        = id;
    o->side      = side;
    o->price     = price;
    o->qty       = qty;
    o->filled_qty = 0;
    o->timestamp = ++clock_;
    o->prev      = nullptr;
    o->next      = nullptr;
    return o;
}

void OrderBook::free_order(Order* o) {
    order_index_.erase(o->id);
    pool_.release(o);
}

void OrderBook::record_trade(OrderId maker, OrderId taker, Side taker_side,
                             Price px, Qty qty) {
    trades_.push_back({maker, taker, taker_side, px, qty, clock_});
}

// ============================================================
// match_order -- the critical path
// ============================================================
// Walks the passive side (asks for a buy, bids for a sell) eating
// liquidity in price-time priority until:
//   - the aggressor is fully filled, or
//   - no more resting orders cross the aggressor's price
//
// For limit orders, price acts as a ceiling (buy) or floor (sell).
// For market orders, price is 0 which means "take everything."

Qty OrderBook::match_order(Order* aggressor,
                           std::map<Price, PriceLevel>& passive,
                           bool is_buy) {
    Qty total_filled = 0;

    while (aggressor->remaining() > 0 && !passive.empty()) {
        // best price: lowest ask (begin) or highest bid (last element)
        auto it = is_buy ? passive.begin() : std::prev(passive.end());
        Price level_price = it->first;

        // price check -- market orders (price == 0) skip this
        if (aggressor->price != 0) {
            if (is_buy  && level_price > aggressor->price) break;
            if (!is_buy && level_price < aggressor->price) break;
        }

        PriceLevel& level = it->second;

        // walk the FIFO queue at this price
        while (aggressor->remaining() > 0 && level.head != nullptr) {
            Order* maker = level.head;
            Qty fill = std::min(aggressor->remaining(), maker->remaining());

            aggressor->filled_qty += fill;
            maker->filled_qty    += fill;
            level.total_qty      -= fill;
            total_filled         += fill;

            record_trade(maker->id, aggressor->id, aggressor->side,
                         level_price, fill);

            // maker fully filled -> pull it off the queue and recycle
            if (maker->remaining() == 0) {
                level.remove(maker);
                free_order(maker);
            }
        }

        if (level.empty()) {
            passive.erase(it);
        }
    }

    return total_filled;
}

// ============================================================
// public API
// ============================================================

OrderId OrderBook::add_limit_order(Side side, Price price, Qty qty, OrderId id) {
    if (id == 0) id = next_id_++;
    else if (id >= next_id_) next_id_ = id + 1;

    Order* o = alloc_order(id, side, price, qty);

    // check if this order crosses the book (aggressive limit)
    if (side == Side::Buy) {
        if (!asks_.empty() && asks_.begin()->first <= price) {
            match_order(o, asks_, true);
        }
    } else {
        if (!bids_.empty() && bids_.rbegin()->first >= price) {
            match_order(o, bids_, false);
        }
    }

    // whatever's left becomes a resting order
    if (o->remaining() > 0) {
        auto& side_map = (side == Side::Buy) ? bids_ : asks_;
        auto& level = side_map[price];
        level.price = price;
        level.push_back(o);
        order_index_[id] = o;
    } else {
        // fully filled on entry, nothing to rest
        pool_.release(o);
    }

    return id;
}

OrderId OrderBook::add_market_order(Side side, Qty qty) {
    OrderId id = next_id_++;
    // price = 0 tells the matcher "no limit, eat everything"
    Order* o = alloc_order(id, side, 0, qty);

    if (side == Side::Buy)  match_order(o, asks_, true);
    else                    match_order(o, bids_, false);

    Qty unfilled = o->remaining();
    pool_.release(o);

    if (unfilled > 0) {
        // in production you'd probably reject or partially fill + report
        std::cerr << "warning: market order " << id
                  << " left " << unfilled << " unfilled\n";
    }

    return id;
}

bool OrderBook::cancel_order(OrderId id) {
    auto it = order_index_.find(id);
    if (it == order_index_.end()) return false;

    Order* o = it->second;
    auto& side_map = (o->side == Side::Buy) ? bids_ : asks_;

    auto level_it = side_map.find(o->price);
    if (level_it == side_map.end()) return false;

    level_it->second.remove(o);
    if (level_it->second.empty()) {
        side_map.erase(level_it);
    }

    free_order(o);
    return true;
}

// Modify only changes quantity, not price.
// Real exchanges (CME, for example) work the same way -- if you want to
// change the price you cancel and re-add, because it's essentially a
// different order.
//
// Priority rules:
//   - Decrease qty: keep your queue position (you're making the book thinner, no advantage)
//   - Increase qty: lose priority, move to back of the queue at that level

bool OrderBook::modify_order(OrderId id, Qty new_qty) {
    auto it = order_index_.find(id);
    if (it == order_index_.end()) return false;

    Order* o = it->second;

    if (new_qty <= o->filled_qty) return false;

    auto& side_map = (o->side == Side::Buy) ? bids_ : asks_;
    auto level_it = side_map.find(o->price);
    if (level_it == side_map.end()) return false;

    Qty old_remaining = o->remaining();
    o->qty = new_qty;
    Qty new_remaining = o->remaining();

    level_it->second.total_qty += (new_remaining - old_remaining);

    if (new_remaining > old_remaining) {
        // increasing size -> lose time priority
        level_it->second.remove(o);
        level_it->second.push_back(o);
    }

    return true;
}

// ============================================================
// accessors
// ============================================================

Price OrderBook::best_bid() const {
    return bids_.empty() ? 0 : bids_.rbegin()->first;
}

Price OrderBook::best_ask() const {
    return asks_.empty() ? 0 : asks_.begin()->first;
}

Price OrderBook::spread() const {
    Price bb = best_bid(), ba = best_ask();
    if (bb == 0 || ba == 0) return 0;
    return ba - bb;
}

// walks the queue at the order's price level to find its FIFO position
int OrderBook::queue_position(OrderId id) const {
    auto it = order_index_.find(id);
    if (it == order_index_.end()) return -1;

    Order* target = it->second;
    const auto& side_map = (target->side == Side::Buy) ? bids_ : asks_;
    auto level_it = side_map.find(target->price);
    if (level_it == side_map.end()) return -1;

    int pos = 0;
    for (Order* cur = level_it->second.head; cur != nullptr; cur = cur->next) {
        if (cur->id == id) return pos;
        pos++;
    }
    return -1;
}

void OrderBook::print_book(int depth) const {
    std::cout << "\n========== ORDER BOOK ==========\n";

    // collect top N ask levels for display (lowest = best)
    std::vector<std::pair<Price, const PriceLevel*>> ask_snapshot;
    int count = 0;
    for (auto it = asks_.begin(); it != asks_.end() && count < depth; ++it, ++count) {
        ask_snapshot.push_back({it->first, &it->second});
    }

    // print asks top-down (highest at top, like a real trading screen)
    std::cout << "  ASKS:\n";
    for (int i = (int)ask_snapshot.size() - 1; i >= 0; --i) {
        std::cout << "    " << ask_snapshot[i].first
                  << "  |  qty: " << ask_snapshot[i].second->total_qty
                  << "  (" << ask_snapshot[i].second->order_count << " orders)\n";
    }

    std::cout << "  ----- spread: " << spread() << " -----\n";

    std::cout << "  BIDS:\n";
    count = 0;
    for (auto it = bids_.rbegin(); it != bids_.rend() && count < depth; ++it, ++count) {
        std::cout << "    " << it->first
                  << "  |  qty: " << it->second.total_qty
                  << "  (" << it->second.order_count << " orders)\n";
    }
    std::cout << "================================\n\n";
}
