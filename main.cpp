#include "order_book.h"
#include "replay_engine.h"

#include <iostream>
#include <chrono>
#include <random>
#include <iomanip>
#include <cassert>
#include <string>

// ============================================================
// sanity checks -- not exhaustive, just enough to make sure
// the basic mechanics work before we throw a million events at it
// ============================================================

void run_tests() {
    std::cout << "--- running basic tests ---\n";

    OrderBook book;

    // simple limit orders, no crossing
    book.add_limit_order(Side::Buy,  100, 10);
    book.add_limit_order(Side::Buy,   99, 15);
    book.add_limit_order(Side::Sell, 102, 10);
    book.add_limit_order(Side::Sell, 103,  5);

    assert(book.best_bid() == 100);
    assert(book.best_ask() == 102);
    assert(book.spread()   == 2);
    std::cout << "  [pass] basic limit orders\n";

    // market buy eats from the ask side
    book.add_market_order(Side::Buy, 7);
    // should have eaten 7 of the 10 at price 102, leaving 3
    assert(book.best_ask() == 102);
    std::cout << "  [pass] partial fill via market order\n";

    // cancel
    OrderId oid = book.add_limit_order(Side::Buy, 98, 20);
    assert(book.cancel_order(oid) == true);
    assert(book.cancel_order(oid) == false); // double cancel = nope
    std::cout << "  [pass] cancel order\n";

    // modify: increase (loses priority), decrease (keeps priority)
    OrderId m1 = book.add_limit_order(Side::Buy, 97, 10);
    assert(book.modify_order(m1, 15));
    assert(book.modify_order(m1, 12));
    std::cout << "  [pass] modify order\n";

    // crossing limit order generates a trade
    book.add_limit_order(Side::Sell, 105, 10);
    book.add_limit_order(Side::Buy,  105,  4);
    assert(!book.get_trades().empty());
    std::cout << "  [pass] crossing limit order\n";

    // queue position tracking
    OrderId q1 = book.add_limit_order(Side::Buy, 90, 5);
    OrderId q2 = book.add_limit_order(Side::Buy, 90, 5);
    OrderId q3 = book.add_limit_order(Side::Buy, 90, 5);
    assert(book.queue_position(q1) == 0);
    assert(book.queue_position(q2) == 1);
    assert(book.queue_position(q3) == 2);
    std::cout << "  [pass] queue position tracking\n";

    // partial fill should update remaining qty correctly
    {
        OrderBook b2;
        b2.add_limit_order(Side::Sell, 50, 100);
        b2.add_market_order(Side::Buy, 30);
        // the resting sell should now have 70 remaining
        assert(b2.best_ask() == 50);
        const auto& trades = b2.get_trades();
        assert(trades.size() == 1);
        assert(trades[0].qty == 30);
        std::cout << "  [pass] partial fill qty tracking\n";
    }

    // market order eating through multiple price levels
    {
        OrderBook b3;
        b3.add_limit_order(Side::Sell, 100, 10);
        b3.add_limit_order(Side::Sell, 101, 10);
        b3.add_limit_order(Side::Sell, 102, 10);
        b3.add_market_order(Side::Buy, 25);
        // should have eaten all of 100 (10), all of 101 (10), and 5 of 102
        assert(b3.best_ask() == 102);
        assert(b3.get_trades().size() == 3);
        std::cout << "  [pass] multi-level matching\n";
    }

    book.print_book();
    std::cout << "--- all tests passed ---\n\n";
}

// ============================================================
// benchmarks -- measure event processing throughput for
// different workload profiles
// ============================================================

void run_benchmarks() {
    std::cout << "===== BENCHMARKS =====\n\n";

    std::mt19937 rng(42);
    const int N = 1'000'000;

    // --- benchmark 1: pure add, no crossing ---
    // this isolates the cost of inserting into the map + pool allocation
    {
        OrderBook book;
        std::uniform_int_distribution<Price> px(900, 1100);
        std::uniform_int_distribution<Qty>   qt(1, 100);
        std::uniform_int_distribution<int>   sd(0, 1);

        auto t0 = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < N; i++) {
            Side s = sd(rng) ? Side::Buy : Side::Sell;
            Price p = px(rng);
            // keep sides separated so nothing crosses
            if (s == Side::Buy) p = std::min(p, Price(999));
            else                p = std::max(p, Price(1001));
            book.add_limit_order(s, p, qt(rng));
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "  add (no cross):   " << std::fixed << std::setprecision(1)
                  << ms << " ms  |  " << std::setprecision(0)
                  << (N / (ms / 1000.0)) << " events/sec\n";
    }

    // --- benchmark 2: add + cancel mix (70/30) ---
    // typical order flow: lots of adds, frequent cancels
    {
        OrderBook book;
        std::uniform_int_distribution<Price> px(900, 1100);
        std::uniform_int_distribution<Qty>   qt(1, 50);
        std::uniform_int_distribution<int>   sd(0, 1);
        std::vector<OrderId> live;
        live.reserve(N);

        auto t0 = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < N; i++) {
            if (live.empty() || (rng() % 10) < 7) {
                Side s = sd(rng) ? Side::Buy : Side::Sell;
                Price p = px(rng);
                if (s == Side::Buy) p = std::min(p, Price(999));
                else                p = std::max(p, Price(1001));
                live.push_back(book.add_limit_order(s, p, qt(rng)));
            } else {
                std::uniform_int_distribution<size_t> idx(0, live.size() - 1);
                size_t pick = idx(rng);
                book.cancel_order(live[pick]);
                live[pick] = live.back();
                live.pop_back();
            }
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "  add+cancel mix:   " << std::fixed << std::setprecision(1)
                  << ms << " ms  |  " << std::setprecision(0)
                  << (N / (ms / 1000.0)) << " events/sec\n";
    }

    // --- benchmark 3: heavy matching workload ---
    // seed the book then blast crossing orders at it, replenishing periodically
    {
        OrderBook book;
        std::uniform_int_distribution<Qty> qt(1, 20);
        std::uniform_int_distribution<int> sd(0, 1);

        // seed initial liquidity
        for (int i = 0; i < 10000; i++) {
            book.add_limit_order(Side::Buy,  990 + (i % 10), qt(rng));
            book.add_limit_order(Side::Sell, 1001 + (i % 10), qt(rng));
        }

        int match_n = 500'000;

        auto t0 = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < match_n; i++) {
            Side s = sd(rng) ? Side::Buy : Side::Sell;
            Price p = (s == Side::Buy) ? 1015 : 985;
            book.add_limit_order(s, p, qt(rng));

            // drip in new liquidity so the book doesn't drain completely
            if (i % 50 == 0) {
                for (int j = 0; j < 10; j++) {
                    book.add_limit_order(Side::Buy,  990 + (j % 10), qt(rng));
                    book.add_limit_order(Side::Sell, 1001 + (j % 10), qt(rng));
                }
            }
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "  matching load:    " << std::fixed << std::setprecision(1)
                  << ms << " ms  |  " << std::setprecision(0)
                  << (match_n / (ms / 1000.0)) << " events/sec"
                  << "  (" << book.get_trades().size() << " trades)\n";
    }

    // --- benchmark 4: realistic mixed workload ---
    // 50% limit adds, 30% cancels, 10% market orders, 5% modifies, 5% crossing limits
    {
        OrderBook book;
        std::uniform_int_distribution<Price> px(950, 1050);
        std::uniform_int_distribution<Qty>   qt(1, 50);
        std::uniform_int_distribution<int>   act(0, 99);
        std::vector<OrderId> live;
        live.reserve(N);

        auto t0 = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < N; i++) {
            int a = act(rng);

            if (a < 50) {
                // non-crossing limit
                Side s = (a < 25) ? Side::Buy : Side::Sell;
                Price p = px(rng);
                if (s == Side::Buy) p = std::min(p, Price(999));
                else                p = std::max(p, Price(1001));
                live.push_back(book.add_limit_order(s, p, qt(rng)));

            } else if (a < 80 && !live.empty()) {
                // cancel
                std::uniform_int_distribution<size_t> idx(0, live.size() - 1);
                size_t pick = idx(rng);
                book.cancel_order(live[pick]);
                live[pick] = live.back();
                live.pop_back();

            } else if (a < 90) {
                // market order
                Side s = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
                book.add_market_order(s, qt(rng));

            } else if (a < 95 && !live.empty()) {
                // modify
                std::uniform_int_distribution<size_t> idx(0, live.size() - 1);
                book.modify_order(live[idx(rng)], qt(rng));

            } else {
                // crossing limit
                Side s = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
                Price p = (s == Side::Buy) ? 1050 : 950;
                live.push_back(book.add_limit_order(s, p, qt(rng)));
            }
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "  mixed workload:   " << std::fixed << std::setprecision(1)
                  << ms << " ms  |  " << std::setprecision(0)
                  << (N / (ms / 1000.0)) << " events/sec"
                  << "  (" << book.get_trades().size() << " trades, "
                  << book.order_count() << " resting)\n";

        book.print_book();
    }

    std::cout << "======================\n\n";
}

// ============================================================
// replay a historical L2 event stream and print quality metrics
// ============================================================

void run_replay(const std::string& filepath) {
    std::cout << "===== L2 EVENT REPLAY =====\n";

    ReplayEngine engine;
    if (!engine.load_events(filepath)) {
        std::cerr << "skipping replay -- couldn't load " << filepath << "\n\n";
        return;
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    ReplayMetrics metrics = engine.run();
    auto t1 = std::chrono::high_resolution_clock::now();

    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    engine.print_summary(metrics);
    std::cout << "  replay time:  " << std::fixed << std::setprecision(2)
              << ms << " ms\n";
    std::cout << "  throughput:   " << std::setprecision(0)
              << (metrics.total_events / (ms / 1000.0)) << " events/sec\n\n";

    engine.book().print_book();
}

// ============================================================

int main(int argc, char* argv[]) {
    run_tests();
    run_benchmarks();

    std::string event_file = "data/sample_events.csv";
    if (argc > 1) event_file = argv[1];
    run_replay(event_file);

    return 0;
}
