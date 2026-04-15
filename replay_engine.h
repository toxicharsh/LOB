#pragma once

#include "order_book.h"
#include <string>
#include <vector>

// a single event from a historical Level-2 feed
struct L2Event {
    Timestamp   timestamp;
    std::string type;      // LIMIT, MARKET, CANCEL, MODIFY
    Side        side;
    Price       price;
    Qty         qty;
    OrderId     order_id;  // nonzero for LIMIT/CANCEL/MODIFY, 0 = auto-assign
};

// execution quality metrics accumulated across a replay session
struct ReplayMetrics {
    int    total_events   = 0;
    int    total_trades   = 0;
    int    total_cancels  = 0;
    int    total_modifies = 0;

    // volume-weighted average price tracking
    double total_buy_fill_value  = 0.0;
    Qty    total_buy_fill_qty    = 0;
    double total_sell_fill_value = 0.0;
    Qty    total_sell_fill_qty   = 0;

    // slippage = abs(fill_price - mid_price) at time of execution
    double total_slippage    = 0.0;
    int    slippage_samples  = 0;

    double buy_vwap() const {
        return (total_buy_fill_qty > 0)
            ? total_buy_fill_value / total_buy_fill_qty : 0.0;
    }
    double sell_vwap() const {
        return (total_sell_fill_qty > 0)
            ? total_sell_fill_value / total_sell_fill_qty : 0.0;
    }
    double avg_slippage() const {
        return (slippage_samples > 0)
            ? total_slippage / slippage_samples : 0.0;
    }
};

// replays a stream of L2 events through an OrderBook and
// collects execution quality metrics (VWAP, slippage, fill rate, etc.)
class ReplayEngine {
public:
    bool load_events(const std::string& filepath);
    ReplayMetrics run();
    void print_summary(const ReplayMetrics& m) const;

    const OrderBook& book() const { return book_; }

private:
    L2Event parse_line(const std::string& line) const;
    void    process_event(const L2Event& ev, ReplayMetrics& metrics);

    OrderBook book_;
    std::vector<L2Event> events_;
};
