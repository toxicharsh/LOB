#!/usr/bin/env python3

# -----------------------------
# ORDER OBJECT
# -----------------------------
class Order:
    def __init__(self, order_id, side, price, quantity, timestamp):
        # Unique ID for every order in the book
        self.order_id = order_id

        # Either "buy" or "sell"
        self.side = side

        # Limit price (None for market orders)
        self.price = price

        # How many units remain unfilled
        self.quantity = quantity

        # Timestamp used for FIFO priority at the same price level
        self.timestamp = timestamp

    def __repr__(self):
        return (
            f"order_id:{self.order_id}, side:{self.side}, price:{self.price}, "
            f"quantity:{self.quantity}, timestamp:{self.timestamp}"
        )


# -----------------------------
# ORDER BOOK SIDE (BID OR ASK)
# -----------------------------
class OrderBookSide:
    """
    This class stores ONE SIDE of the book:
        - All BUY orders (bids), sorted high→low
        - All SELL orders (asks), sorted low→high

    It maintains:
        - a price → list[orders] mapping
        - a sorted list of prices
    """

    def __init__(self, side):
        self.side = side
        self.levels = {}    # price → list of Order objects (FIFO queue)
        self.prices = []    # sorted list of all active prices on this side

    # -----------------------------
    # Insert a price into sorted list
    # -----------------------------
    def insertPrice(self, price):
        """
        Maintains sorted price list:
            For BUY side: highest price first
            For SELL side: lowest price first
        """
        indx = 0

        if self.side == "buy":
            # For bids we sort in DESCENDING order (best price = highest)
            while indx < len(self.prices) and self.prices[indx] > price:
                indx += 1
        else:
            # For asks we sort in ASCENDING order (best price = lowest)
            while indx < len(self.prices) and self.prices[indx] < price:
                indx += 1

        self.prices.insert(indx, price)

    # -----------------------------
    # Add limit order into this side
    # -----------------------------
    def addOrder(self, order):
        price = order.price

        # If price level does not exist, create it and insert into sorted list
        if price not in self.levels:
            self.levels[price] = []
            self.insertPrice(price)

        # Append to FIFO queue at this price level
        self.levels[price].append(order)

    # -----------------------------
    # Cancel order by ID
    # -----------------------------
    def cancelOrder(self, order_id):
        """
        Slow but simple: search through all price levels to find the order.
        This is okay for Phase 1; later we'll optimize.
        """
        for price in list(self.prices):
            queue = self.levels[price]

            for inx, o in enumerate(queue):
                if o.order_id == order_id:
                    # Remove from this price level
                    del queue[inx]

                    # If price level becomes empty → remove it entirely
                    if not queue:
                        del self.levels[price]
                        self.prices.remove(price)
                    return True

        return False

    # -----------------------------
    # Match aggressive order
    # -----------------------------
    def matchOrder(self, quantity):
        """
        Consume liquidity from THIS side's best prices.

        Example:
            If this is ASK side → BUY market order eats here.
            If this is BID side → SELL market order eats here.

        The function loops through price levels until:
            - quantity is 0 (fully filled), OR
            - no more prices left on this side
        """
        total_filled = 0
        trades = []

        while quantity > 0 and self.prices:
            # Best price on this side (index 0 because prices are sorted)
            best_price = self.prices[0]
            queue = self.levels[best_price]

            # FIFO: earliest order has priority
            best_order = queue[0]

            # How much can we fill?
            fill_qty = min(quantity, best_order.quantity)

            # Record trade event
            trades.append((best_order.order_id, fill_qty, best_price))

            # Update quantities
            best_order.quantity -= fill_qty
            quantity -= fill_qty
            total_filled += fill_qty

            # If the maker order is fully filled → remove from queue
            if best_order.quantity == 0:
                queue.pop(0)

                # If the entire price level is empty → remove it
                if not queue:
                    del self.levels[best_price]
                    self.prices.pop(0)

        return total_filled, trades


# ===========================================================
# FULL LIMIT ORDER BOOK (both sides + order routing logic)
# ===========================================================
class LimitOrderBook:
    def __init__(self):
        # Two separate sides
        self.bids = OrderBookSide("buy")
        self.asks = OrderBookSide("sell")

        # Fast lookup order_id → order object
        self.order_map = {}

        # Internal counters
        self.timestamp = 0
        self.next_id = 1

        # Trade history
        self.trades = []

    # -----------------------------
    # Generate unique order ID
    # -----------------------------
    def gen_id(self):
        oid = f"o{self.next_id}"
        self.next_id += 1
        return oid

    # -----------------------------
    # Generate timestamp for FIFO
    # -----------------------------
    def gen_ts(self):
        self.timestamp += 1
        return self.timestamp

    # -----------------------------
    # Add a limit order
    # -----------------------------
    def addLimitOrder(self, side, price, quantity, order_id=None):
        """
        Handles THREE cases:

        1. Limit order that does NOT cross -> add as resting liquidity
        2. Limit order that crosses -> match first, rest goes on book
        3. Limit order that fully crosses -> nothing left to add
        """
        if order_id is None:
            order_id = self.gen_id()

        ts = self.gen_ts()
        incoming = Order(order_id, side, price, quantity, ts)

        # -----------------------------
        # Determine if order crosses the opposite best price
        # -----------------------------
        if side == "buy":
            best_ask = self.asks.prices[0] if self.asks.prices else None

            # BUY crosses if price >= best ask
            if best_ask is not None and price >= best_ask:
                filled, trades = self.asks.matchOrder(incoming.quantity)
                incoming.quantity -= filled
                self.recordTrades(trades, incoming, "buy")

        else:  # side == "sell"
            best_bid = self.bids.prices[0] if self.bids.prices else None

            # SELL crosses if price <= best bid
            if best_bid is not None and price <= best_bid:
                filled, trades = self.bids.matchOrder(incoming.quantity)
                incoming.quantity -= filled
                self.recordTrades(trades, incoming, "sell")

        # -----------------------------
        # If leftover exists, it becomes a resting order
        # -----------------------------
        if incoming.quantity > 0:
            if side == "buy":
                self.bids.addOrder(incoming)
            else:
                self.asks.addOrder(incoming)

            # Track this order for cancellation later
            self.order_map[incoming.order_id] = incoming

        return incoming.order_id

    # -----------------------------
    # Add a market order
    # -----------------------------
    def addMarketOrder(self, side, quantity):
        """
        Market order ALWAYS crosses.

        BUY → hits ASK side  
        SELL → hits BID side
        """
        order_id = self.gen_id()
        ts = self.gen_ts()
        incoming = Order(order_id, side, None, quantity, ts)

        if side == "buy":
            filled, trades = self.asks.matchOrder(quantity)
            self.recordTrades(trades, incoming, "buy")
        else:
            filled, trades = self.bids.matchOrder(quantity)
            self.recordTrades(trades, incoming, "sell")

        return order_id

    # -----------------------------
    # Store trades + cleanup finished orders
    # -----------------------------
    def recordTrades(self, trades, taker_order, taker_side):
        """
        Each trade is recorded with:
            - maker_id (resting order)
            - taker_id (incoming aggressive)
            - qty
            - price
            - side (buy or sell)
        """
        for maker_id, qty, price in trades:
            trade = {
                "maker_id": maker_id,
                "taker_id": taker_order.order_id,
                "qty": qty,
                "price": price,
                "side": taker_side
            }
            self.trades.append(trade)

            # Remove maker if fully filled
            maker = self.order_map.get(maker_id)
            if maker and maker.quantity == 0:
                del self.order_map[maker_id]

    # -----------------------------
    # Cancel an existing order
    # -----------------------------
    def cancelOrder(self, order_id):
        order_obj = self.order_map.get(order_id)
        if not order_obj:
            return False

        if order_obj.side == "buy":
            removed = self.bids.cancelOrder(order_id)
        else:
            removed = self.asks.cancelOrder(order_id)

        if removed:
            del self.order_map[order_id]

        return removed

    # -----------------------------
    # Pretty print the book
    # -----------------------------
    def printBook(self):
        print("\n--- ORDER BOOK ---")
        print("ASKS:")
        # Reverse so lowest ask appears at the bottom like real LOB displays
        for p in reversed(self.asks.prices):
            qty = sum(o.quantity for o in self.asks.levels[p])
            print(f"  {p}: {qty}")

        print("BIDS:")
        for p in self.bids.prices:
            qty = sum(o.quantity for o in self.bids.levels[p])
            print(f"  {p}: {qty}")
        print("------------------\n")


# -----------------------------
# FULL DEMO / TEST
# -----------------------------
if __name__ == "__main__":
    lob = LimitOrderBook()

    print("\nADDING MANY BIDS AND ASKS...\n")

    # Add multiple BUY (bid) orders — from best to worst
    lob.addLimitOrder("buy", 105, 10)
    lob.addLimitOrder("buy", 104, 15)
    lob.addLimitOrder("buy", 103, 20)
    lob.addLimitOrder("buy", 102, 8)
    lob.addLimitOrder("buy", 101, 12)
    lob.addLimitOrder("buy", 100, 25)

    # Add multiple SELL (ask) orders — from best to worst
    lob.addLimitOrder("sell", 106, 10)
    lob.addLimitOrder("sell", 107, 5)
    lob.addLimitOrder("sell", 108, 8)
    lob.addLimitOrder("sell", 109, 12)
    lob.addLimitOrder("sell", 110, 20)

    lob.printBook()

    print("\nNOW LET’S ADD SOME MARKET ORDERS\n")

    # Market buy — should eat from lowest asks (106, 107,…)
    lob.addMarketOrder("buy", 18)

    # Market sell — should eat from highest bids (105, 104,…)
    lob.addMarketOrder("sell", 30)

    lob.printBook()

    print("\n--- TRADES EXECUTED ---")
    for t in lob.trades:
        print(t)
