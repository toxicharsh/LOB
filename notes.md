1. What exactly is a Limit Order Book?

Imagine two crowds facing each other in a marketplace.
One crowd wants to buy, the other wants to sell.

Each person yells:

“I want to buy 10 units at price 100!”

“I want to sell 5 units at price 102!”

“I want to sell 20 units at price 101!”

This shouting is the order flow.
The “book” is simply a record of all these buy and sell wishes, organized neatly.

The book has two sides:

Bids (buyers)

Asks (sellers)

Buyers compete with each other:
higher price wins.

Sellers compete with each other:
lower price wins.

So:

Best bid = highest buy price

Best ask = lowest sell price

The difference = the spread.

These are the atoms of the financial universe.




2. What is a Limit Order?

A limit order is simple:

“I want to buy X units, but only if the price is ≤ Y.”

“I want to sell X units, but only if the price is ≥ Y.”

These orders go into the book.

And they sit there until:

they get matched

or cancelled

or expire

Which brings us to matching.


3. Price–Time Priority (The Golden Rule)

The exchange is fair.
If two people want to buy at the same price:

The one who arrived first gets filled first.

The next waits behind them.

And so on.

This is important.
It means each price level is like a queue.

So the book looks like:

Price: 101 → [order1, order8, order12]
Price: 100 → [order4, order7]
Price: 99  → [order2]


Each price has its own FIFO line.



4. What is Matching?

If a new market order arrives:

“Buy 50 units at the best available price!”

The simulator should:

Walk down through the best ask prices

Fill orders one by one

Reduce quantities

Remove fully filled orders

Update partially filled orders

This is the beating heart of the system.

You’ll build this.