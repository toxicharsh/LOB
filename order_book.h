#pragma once

#include "order.h"
#include <map>
#include <unordered_map>
#include <vector>
#include <memory>
#include <iostream>

// ============================================================
// OrderPool -- slab allocator for Order objects
// ============================================================
// Instead of hitting malloc/free on every add/cancel, we hand out
// slots from pre-allocated blocks. Cancelled/filled orders go onto
// a free list and get recycled.
//
// This makes a real difference when you're processing millions of
// events -- scattered heap allocations destroy cache locality and
// the allocator overhead itself shows up in profiles.

class OrderPool {
public:
    static constexpr size_t BLOCK_SIZE = 4096;

    Order* allocate() {
        if (!free_list_.empty()) {
            Order* o = free_list_.back();
            free_list_.pop_back();
            return o;
        }
        if (blocks_.empty() || next_slot_ >= BLOCK_SIZE) {
            blocks_.emplace_back(new Order[BLOCK_SIZE]);
            next_slot_ = 0;
        }
        return &blocks_.back()[next_slot_++];
    }

    void release(Order* o) {
        // wipe it so stale pointers don't silently work during development
        *o = Order{};
        free_list_.push_back(o);
    }

private:
    std::vector<std::unique_ptr<Order[]>> blocks_;
    std::vector<Order*> free_list_;
    size_t next_slot_ = 0;
};

// ============================================================
// OrderBook -- the full two-sided limit order book
// ============================================================

class OrderBook {
public:
    OrderBook() = default;

    // --- core operations: limit, market, cancel, modify ---
    OrderId add_limit_order(Side side, Price price, Qty qty, OrderId id = 0);
    OrderId add_market_order(Side side, Qty qty);
    bool    cancel_order(OrderId id);
    bool    modify_order(OrderId id, Qty new_qty);

    // --- book state queries ---
    Price best_bid() const;
    Price best_ask() const;
    Price spread() const;
    int   queue_position(OrderId id) const;

    void print_book(int depth = 5) const;

    const std::vector<Trade>& get_trades() const { return trades_; }
    void  clear_trades() { trades_.clear(); }
    size_t bid_levels()  const { return bids_.size(); }
    size_t ask_levels()  const { return asks_.size(); }
    size_t order_count() const { return order_index_.size(); }

private:
    // the matching hot path
    Qty match_order(Order* aggressor, std::map<Price, PriceLevel>& passive, bool is_buy);

    void   record_trade(OrderId maker, OrderId taker, Side taker_side, Price px, Qty qty);
    Order* alloc_order(OrderId id, Side side, Price price, Qty qty);
    void   free_order(Order* o);

    // ordered maps give us sorted price levels -- iterate begin() for
    // lowest (best ask) or rbegin() for highest (best bid)
    std::map<Price, PriceLevel> bids_;
    std::map<Price, PriceLevel> asks_;

    // hash-based index: O(1) lookup by order ID for cancel/modify
    std::unordered_map<OrderId, Order*> order_index_;

    OrderPool pool_;
    std::vector<Trade> trades_;

    OrderId   next_id_ = 1;
    Timestamp clock_   = 0;
};
