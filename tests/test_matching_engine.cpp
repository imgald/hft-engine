#include "engine/matching_engine.hpp"
#include <gtest/gtest.h>

using namespace hft;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static uint64_t next_id() {
    static uint64_t id = 1;
    return id++;
}

static Order* limit(Side side, double price, Quantity qty) {
    return new Order(Order::make_limit(next_id(), side, to_price(price), qty));
}

static Order* market(Side side, Quantity qty) {
    return new Order(Order::make_market(next_id(), side, qty));
}

static Order* ioc(Side side, double price, Quantity qty) {
    return new Order(Order::make_ioc(next_id(), side, to_price(price), qty));
}

// ─── Fixture ─────────────────────────────────────────────────────────────────

class MatchingEngineTest : public ::testing::Test {
protected:
    MatchingEngine engine_{"AAPL"};
};

// ─── No match: orders rest on book ───────────────────────────────────────────

TEST_F(MatchingEngineTest, LimitBuy_NoAsk_RestsOnBook) {
    auto result = engine_.process_order(limit(Side::Buy, 100.0, 200));
    EXPECT_TRUE(result.trades.empty());
    EXPECT_TRUE(result.resting);
    EXPECT_FALSE(result.cancelled);
    EXPECT_EQ(engine_.book().best_bid(), to_price(100.0));
}

TEST_F(MatchingEngineTest, LimitSell_NoBid_RestsOnBook) {
    auto result = engine_.process_order(limit(Side::Sell, 101.0, 100));
    EXPECT_TRUE(result.trades.empty());
    EXPECT_TRUE(result.resting);
    EXPECT_EQ(engine_.book().best_ask(), to_price(101.0));
}

TEST_F(MatchingEngineTest, LimitBuy_BelowAsk_RestsOnBook) {
    engine_.process_order(limit(Side::Sell, 101.0, 100));
    // Buy at 100 < Ask at 101 → no cross → rests
    auto result = engine_.process_order(limit(Side::Buy, 100.0, 100));
    EXPECT_TRUE(result.trades.empty());
    EXPECT_TRUE(result.resting);
}

// ─── Full fill ────────────────────────────────────────────────────────────────

TEST_F(MatchingEngineTest, FullFill_ExactQty) {
    engine_.process_order(limit(Side::Sell, 100.0, 200));
    auto result = engine_.process_order(limit(Side::Buy, 100.0, 200));

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].price,    to_price(100.0));
    EXPECT_EQ(result.trades[0].quantity, 200);
    EXPECT_FALSE(result.resting);

    // Book should be empty after full fill
    EXPECT_FALSE(engine_.book().best_bid().has_value());
    EXPECT_FALSE(engine_.book().best_ask().has_value());
}

TEST_F(MatchingEngineTest, FullFill_ExecutionAtPassivePrice) {
    // Passive ask at 100.0, aggressive buy at 101.0
    // Execution should happen at passive price (100.0), not aggressor price
    engine_.process_order(limit(Side::Sell, 100.0, 100));
    auto result = engine_.process_order(limit(Side::Buy, 101.0, 100));

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].price, to_price(100.0))
        << "Execution must happen at passive (resting) order price";
}

// ─── Partial fill ────────────────────────────────────────────────────────────

TEST_F(MatchingEngineTest, PartialFill_AggressorLarger) {
    // Ask 100 shares, Buy 300 shares → fill 100, rest 200
    engine_.process_order(limit(Side::Sell, 100.0, 100));
    auto result = engine_.process_order(limit(Side::Buy, 100.0, 300));

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].quantity, 100);
    EXPECT_TRUE(result.resting);   // 200 remaining should rest on book
    EXPECT_EQ(engine_.book().best_bid(), to_price(100.0));
    EXPECT_EQ(engine_.book().qty_at(Side::Buy, to_price(100.0)), 200);
}

TEST_F(MatchingEngineTest, PartialFill_PassiveLarger) {
    // Ask 300 shares, Buy 100 shares → fill 100, passive has 200 remaining
    engine_.process_order(limit(Side::Sell, 100.0, 300));
    auto result = engine_.process_order(limit(Side::Buy, 100.0, 100));

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].quantity, 100);
    // Remaining 200 on ask side should still be there
    EXPECT_EQ(engine_.book().qty_at(Side::Sell, to_price(100.0)), 200);
}

// ─── Multi-level fill ─────────────────────────────────────────────────────────

TEST_F(MatchingEngineTest, MultiLevel_BuySweeepsMultipleAsks) {
    // Book:
    //   ask 100.00 → 100 shares
    //   ask 100.50 → 150 shares
    //   ask 101.00 → 200 shares
    engine_.process_order(limit(Side::Sell, 101.0, 200));
    engine_.process_order(limit(Side::Sell, 100.5, 150));
    engine_.process_order(limit(Side::Sell, 100.0, 100));

    // Buy 300 @ 100.50 → fills 100 @ 100.00, then 150 @ 100.50 (but only need 200 more)
    // Actually: fills 100 @ 100.00, then 150 @ 100.50 → total 250, but we want 300
    // So: fills 100 @ 100.00, 150 @ 100.50 = 250. Still need 50 more but 101.00 > 100.50 limit
    auto result = engine_.process_order(limit(Side::Buy, 100.5, 300));

    ASSERT_EQ(result.trades.size(), 2u);
    EXPECT_EQ(result.trades[0].price, to_price(100.0));
    EXPECT_EQ(result.trades[0].quantity, 100);
    EXPECT_EQ(result.trades[1].price, to_price(100.5));
    EXPECT_EQ(result.trades[1].quantity, 150);
    // 50 shares unfilled should rest at 100.50
    EXPECT_TRUE(result.resting);
}

// ─── FIFO ordering within a price level ──────────────────────────────────────

TEST_F(MatchingEngineTest, FIFO_OlderOrderFilledFirst) {
    // Two asks at same price, different orders
    auto* ask1 = limit(Side::Sell, 100.0, 100);
    auto* ask2 = limit(Side::Sell, 100.0, 100);
    engine_.process_order(ask1);
    engine_.process_order(ask2);

    // Buy 100 — should match ask1 (the older one)
    auto result = engine_.process_order(limit(Side::Buy, 100.0, 100));

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].passive_id, ask1->order_id)
        << "FIFO: older order must be filled first";
}

// ─── Market orders ────────────────────────────────────────────────────────────

TEST_F(MatchingEngineTest, MarketBuy_FillsAtAnyPrice) {
    engine_.process_order(limit(Side::Sell, 105.0, 100));
    auto result = engine_.process_order(market(Side::Buy, 100));

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].quantity, 100);
    EXPECT_FALSE(result.resting);
}

TEST_F(MatchingEngineTest, MarketOrder_NoLiquidity_Cancelled) {
    // Empty book — market order has nothing to match against
    auto result = engine_.process_order(market(Side::Buy, 100));

    EXPECT_TRUE(result.trades.empty());
    EXPECT_FALSE(result.resting);
    EXPECT_TRUE(result.cancelled);
}

TEST_F(MatchingEngineTest, MarketOrder_PartialFill_RemainderCancelled) {
    // Only 50 shares available, market order wants 100
    engine_.process_order(limit(Side::Sell, 100.0, 50));
    auto result = engine_.process_order(market(Side::Buy, 100));

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].quantity, 50);
    EXPECT_TRUE(result.cancelled);   // remainder cancelled, not resting
    EXPECT_FALSE(engine_.book().best_bid().has_value());
}

// ─── IOC orders ──────────────────────────────────────────────────────────────

TEST_F(MatchingEngineTest, IOC_FullFill) {
    engine_.process_order(limit(Side::Sell, 100.0, 200));
    auto result = engine_.process_order(ioc(Side::Buy, 100.0, 200));

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].quantity, 200);
    EXPECT_FALSE(result.resting);
    EXPECT_FALSE(result.cancelled);
}

TEST_F(MatchingEngineTest, IOC_PartialFill_RemainderCancelled) {
    engine_.process_order(limit(Side::Sell, 100.0, 50));
    auto result = engine_.process_order(ioc(Side::Buy, 100.0, 200));

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].quantity, 50);
    EXPECT_FALSE(result.resting);    // IOC never rests
    EXPECT_TRUE(result.cancelled);   // remainder cancelled
}

TEST_F(MatchingEngineTest, IOC_NoMatch_Cancelled) {
    auto result = engine_.process_order(ioc(Side::Buy, 100.0, 100));
    EXPECT_TRUE(result.trades.empty());
    EXPECT_FALSE(result.resting);
    EXPECT_TRUE(result.cancelled);
}

// ─── Cancel ───────────────────────────────────────────────────────────────────

TEST_F(MatchingEngineTest, Cancel_RestingOrder) {
    auto* o = limit(Side::Buy, 100.0, 200);
    engine_.process_order(o);
    EXPECT_TRUE(engine_.book().best_bid().has_value());

    bool cancelled = engine_.cancel_order(o->order_id);
    EXPECT_TRUE(cancelled);
    EXPECT_FALSE(engine_.book().best_bid().has_value());
}

// ─── Trade fields ────────────────────────────────────────────────────────────

TEST_F(MatchingEngineTest, Trade_FieldsCorrect) {
    auto* ask = limit(Side::Sell, 100.0, 100);
    engine_.process_order(ask);
    auto* buy = limit(Side::Buy, 100.0, 100);
    auto result = engine_.process_order(buy);

    ASSERT_EQ(result.trades.size(), 1u);
    const Trade& t = result.trades[0];
    EXPECT_EQ(t.aggressor_id,   buy->order_id);
    EXPECT_EQ(t.passive_id,     ask->order_id);
    EXPECT_EQ(t.price,          to_price(100.0));
    EXPECT_EQ(t.quantity,       100);
    EXPECT_EQ(t.aggressor_side, Side::Buy);
    EXPECT_GT(t.timestamp_ns,   0);
    EXPECT_GE(t.trade_id,       1u);
}

// ─── Volume tracking ──────────────────────────────────────────────────────────

TEST_F(MatchingEngineTest, TotalVolume_AccumulatesAcrossTrades) {
    engine_.process_order(limit(Side::Sell, 100.0, 100));
    engine_.process_order(limit(Side::Sell, 100.0, 200));
    engine_.process_order(limit(Side::Buy,  100.0, 300));

    EXPECT_EQ(engine_.book().total_volume(), 300u);
    EXPECT_EQ(engine_.book().trade_count(),  2u);
}
