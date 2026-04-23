#include "engine/order_book.hpp"
#include <gtest/gtest.h>

using namespace hft;

static Order* make_limit(OrderId id, Side side, double price, Quantity qty) {
    return new Order(Order::make_limit(id, side, to_price(price), qty));
}

class OrderBookTest : public ::testing::Test {
protected:
    OrderBook book_{"AAPL"};
};

// ─── Empty book ───────────────────────────────────────────────────────────────

TEST_F(OrderBookTest, EmptyBook_NoBestBidOrAsk) {
    EXPECT_FALSE(book_.best_bid().has_value());
    EXPECT_FALSE(book_.best_ask().has_value());
    EXPECT_FALSE(book_.spread().has_value());
}

// ─── Best bid / ask ───────────────────────────────────────────────────────────

TEST_F(OrderBookTest, BestBid_IsHighestBidPrice) {
    auto* o1 = make_limit(1, Side::Buy, 100.0, 100);
    auto* o2 = make_limit(2, Side::Buy, 101.0, 100);
    auto* o3 = make_limit(3, Side::Buy,  99.0, 100);
    book_.add_order(o1);
    book_.add_order(o2);
    book_.add_order(o3);
    EXPECT_EQ(book_.best_bid(), to_price(101.0));
}

TEST_F(OrderBookTest, BestAsk_IsLowestAskPrice) {
    auto* o1 = make_limit(1, Side::Sell, 102.0, 100);
    auto* o2 = make_limit(2, Side::Sell, 101.0, 100);
    auto* o3 = make_limit(3, Side::Sell, 103.0, 100);
    book_.add_order(o1);
    book_.add_order(o2);
    book_.add_order(o3);
    EXPECT_EQ(book_.best_ask(), to_price(101.0));
}

// ─── Spread ───────────────────────────────────────────────────────────────────

TEST_F(OrderBookTest, Spread_BidAskDifference) {
    book_.add_order(make_limit(1, Side::Buy,  100.0, 100));
    book_.add_order(make_limit(2, Side::Sell, 100.5, 100));
    auto spread = book_.spread();
    ASSERT_TRUE(spread.has_value());
    EXPECT_EQ(*spread, to_price(100.5) - to_price(100.0));
}

// ─── Cancel ───────────────────────────────────────────────────────────────────

TEST_F(OrderBookTest, Cancel_RemovesOrder) {
    auto* o = make_limit(1, Side::Buy, 100.0, 500);
    book_.add_order(o);
    EXPECT_TRUE(book_.has_order(1));
    EXPECT_TRUE(book_.cancel_order(1));
    EXPECT_FALSE(book_.has_order(1));
}

TEST_F(OrderBookTest, Cancel_LastOrderAtPrice_RemovesLevel) {
    auto* o = make_limit(1, Side::Buy, 100.0, 100);
    book_.add_order(o);
    book_.cancel_order(1);
    // After cancelling the only bid, best_bid should be gone.
    EXPECT_FALSE(book_.best_bid().has_value());
}

TEST_F(OrderBookTest, Cancel_NonExistent_ReturnsFalse) {
    EXPECT_FALSE(book_.cancel_order(999));
}

// ─── Depth snapshot ───────────────────────────────────────────────────────────

TEST_F(OrderBookTest, BidLevels_OrderedHighToLow) {
    book_.add_order(make_limit(1, Side::Buy, 99.0, 100));
    book_.add_order(make_limit(2, Side::Buy, 101.0, 200));
    book_.add_order(make_limit(3, Side::Buy, 100.0, 150));

    auto levels = book_.bid_levels(3);
    ASSERT_EQ(levels.size(), 3u);
    EXPECT_EQ(levels[0].price, to_price(101.0));
    EXPECT_EQ(levels[1].price, to_price(100.0));
    EXPECT_EQ(levels[2].price, to_price(99.0));
}

TEST_F(OrderBookTest, AskLevels_OrderedLowToHigh) {
    book_.add_order(make_limit(1, Side::Sell, 103.0, 100));
    book_.add_order(make_limit(2, Side::Sell, 101.0, 100));
    book_.add_order(make_limit(3, Side::Sell, 102.0, 100));

    auto levels = book_.ask_levels(3);
    ASSERT_EQ(levels.size(), 3u);
    EXPECT_EQ(levels[0].price, to_price(101.0));
    EXPECT_EQ(levels[1].price, to_price(102.0));
    EXPECT_EQ(levels[2].price, to_price(103.0));
}

TEST_F(OrderBookTest, DepthSnapshot_RespectsNLimit) {
    for (int i = 1; i <= 5; ++i)
        book_.add_order(make_limit(i, Side::Buy, 100.0 - i, 100));
    auto levels = book_.bid_levels(3);
    EXPECT_EQ(levels.size(), 3u);
}

// ─── qty_at ───────────────────────────────────────────────────────────────────

TEST_F(OrderBookTest, QtyAt_AggregatesMultipleOrdersAtSamePrice) {
    book_.add_order(make_limit(1, Side::Buy, 100.0, 100));
    book_.add_order(make_limit(2, Side::Buy, 100.0, 200));
    EXPECT_EQ(book_.qty_at(Side::Buy, to_price(100.0)), 300);
}
