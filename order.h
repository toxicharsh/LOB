#pragma once

#include <cstdint>

// everything is in ticks (integer) to avoid floating point comparison nightmares.
// real exchanges work in ticks anyway -- you'd convert from dollars in the feed handler.
using Price     = int64_t;
using Qty       = int64_t;
using OrderId   = uint64_t;
using Timestamp = uint64_t;

enum class Side { Buy, Sell };

// ============================================================
// Order -- single order sitting in the book
// ============================================================
// Uses intrusive doubly-linked list pointers so we can yank an order
// out of its price level in O(1) when we already have the pointer
// (which we always do, because of the hash index).
// std::list would work but the extra indirection and allocator overhead
// adds up when you're doing millions of events.

struct Order {
    OrderId   id;
    Side      side;
    Price     price;
    Qty       qty;
    Qty       filled_qty;
    Timestamp timestamp;

    Order* prev = nullptr;
    Order* next = nullptr;

    Qty remaining() const { return qty - filled_qty; }
};

// ============================================================
// Trade -- one fill between a maker and taker
// ============================================================

struct Trade {
    OrderId   maker_id;
    OrderId   taker_id;
    Side      taker_side;
    Price     price;
    Qty       qty;
    Timestamp timestamp;
};

// ============================================================
// PriceLevel -- FIFO queue of orders at a single price
// ============================================================
// We track aggregate qty and count here so callers can query
// book depth without walking the whole list every time.

struct PriceLevel {
    Price price      = 0;
    Qty   total_qty  = 0;
    int   order_count = 0;
    Order* head = nullptr;
    Order* tail = nullptr;

    void push_back(Order* o) {
        o->prev = tail;
        o->next = nullptr;
        if (tail) tail->next = o;
        else      head = o;
        tail = o;
        total_qty += o->remaining();
        order_count++;
    }

    void remove(Order* o) {
        if (o->prev) o->prev->next = o->next;
        else         head = o->next;

        if (o->next) o->next->prev = o->prev;
        else         tail = o->prev;

        total_qty -= o->remaining();
        order_count--;
        o->prev = o->next = nullptr;
    }

    bool empty() const { return order_count == 0; }
};
