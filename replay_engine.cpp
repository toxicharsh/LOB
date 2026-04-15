#include "replay_engine.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cmath>

bool ReplayEngine::load_events(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "couldn't open event file: " << filepath << "\n";
        return false;
    }

    std::string line;
    std::getline(file, line); // skip the CSV header

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        events_.push_back(parse_line(line));
    }

    std::cout << "loaded " << events_.size() << " events from " << filepath << "\n";
    return !events_.empty();
}

// format: timestamp,type,side,price,qty,order_id
L2Event ReplayEngine::parse_line(const std::string& line) const {
    L2Event ev{};
    std::stringstream ss(line);
    std::string tok;

    std::getline(ss, tok, ',');
    ev.timestamp = std::stoull(tok);

    std::getline(ss, tok, ',');
    ev.type = tok;

    std::getline(ss, tok, ',');
    ev.side = (tok == "BUY") ? Side::Buy : Side::Sell;

    std::getline(ss, tok, ',');
    ev.price = std::stoll(tok);

    std::getline(ss, tok, ',');
    ev.qty = std::stoll(tok);

    std::getline(ss, tok, ',');
    ev.order_id = std::stoull(tok);

    return ev;
}

void ReplayEngine::process_event(const L2Event& ev, ReplayMetrics& metrics) {
    // snapshot the mid-price before we do anything, for slippage calculation
    Price mid = 0;
    Price bb = book_.best_bid();
    Price ba = book_.best_ask();
    if (bb > 0 && ba > 0) {
        mid = (bb + ba) / 2;
    }

    size_t trades_before = book_.get_trades().size();

    if (ev.type == "LIMIT") {
        book_.add_limit_order(ev.side, ev.price, ev.qty, ev.order_id);
    } else if (ev.type == "MARKET") {
        book_.add_market_order(ev.side, ev.qty);
    } else if (ev.type == "CANCEL") {
        book_.cancel_order(ev.order_id);
        metrics.total_cancels++;
    } else if (ev.type == "MODIFY") {
        book_.modify_order(ev.order_id, ev.qty);
        metrics.total_modifies++;
    }

    metrics.total_events++;

    // accumulate metrics for any trades this event produced
    const auto& all_trades = book_.get_trades();
    for (size_t i = trades_before; i < all_trades.size(); i++) {
        const Trade& t = all_trades[i];
        metrics.total_trades++;

        double fill_value = static_cast<double>(t.price) * t.qty;

        if (t.taker_side == Side::Buy) {
            metrics.total_buy_fill_value += fill_value;
            metrics.total_buy_fill_qty   += t.qty;
        } else {
            metrics.total_sell_fill_value += fill_value;
            metrics.total_sell_fill_qty   += t.qty;
        }

        // slippage: how far was the fill from the mid-price?
        if (mid > 0) {
            double slip = std::abs(static_cast<double>(t.price) - static_cast<double>(mid));
            metrics.total_slippage += slip;
            metrics.slippage_samples++;
        }
    }
}

ReplayMetrics ReplayEngine::run() {
    ReplayMetrics metrics;
    for (const auto& ev : events_) {
        process_event(ev, metrics);
    }
    return metrics;
}

void ReplayEngine::print_summary(const ReplayMetrics& m) const {
    std::cout << "\n===== REPLAY SUMMARY =====\n";
    std::cout << "  total events processed: " << m.total_events  << "\n";
    std::cout << "  trades executed:        " << m.total_trades   << "\n";
    std::cout << "  cancels:                " << m.total_cancels  << "\n";
    std::cout << "  modifies:               " << m.total_modifies << "\n";

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  buy VWAP:               " << m.buy_vwap()      << "\n";
    std::cout << "  sell VWAP:              " << m.sell_vwap()     << "\n";
    std::cout << "  avg slippage:           " << m.avg_slippage()  << " ticks\n";

    double fill_rate = (m.total_events > 0)
        ? (100.0 * m.total_trades / m.total_events) : 0.0;
    std::cout << "  fill rate:              " << fill_rate << "%\n";
    std::cout << "==========================\n\n";
}
