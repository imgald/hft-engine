#include "engine/price_level.hpp"
#include <gtest/gtest.h>
#include <vector>

using namespace hft;

// Helper: build a limit order on the heap (tests own the lifetime).
static Order* make(OrderId id, Side side, double price, Quantity qty) {
    auto* o = new Order(Order::make_limit(id, side, to_price(price), qty));
    return o;
}

class PriceLevelTest : public ::testing::Test {
protected:
    PriceLevel level_{to_price(100.0)};

    void TearDown() override {
        // In real engine, ObjectPool owns memory.  Here we just leak — tests are short-lived.
    }
};

// ─── Basic add / front ────────────────────────────────────────────────────────

TEST_F(PriceLevelTest, EmptyOnConstruction) {
    EXPECT_TRUE(level_.empty());
    EXPECT_EQ(level_.total_qty(), 0);
    EXPECT_EQ(level_.depth(), 0u);
}

TEST_F(PriceLevelTest, AddSingleOrder) {
    Order* o = make(1, Side::Buy, 100.0, 200);
    level_.add(o);
    EXPECT_FALSE(level_.empty());
    EXPECT_EQ(level_.total_qty(), 200);
    EXPECT_EQ(level_.depth(), 1u);
    EXPECT_EQ(level_.front(), o);
}

// ─── FIFO ordering ────────────────────────────────────────────────────────────

TEST_F(PriceLevelTest, FIFOOrder_FrontIsOldest) {
    Order* o1 = make(1, Side::Buy, 100.0, 100);
    Order* o2 = make(2, Side::Buy, 100.0, 200);
    Order* o3 = make(3, Side::Buy, 100.0, 150);
    level_.add(o1);
    level_.add(o2);
    level_.add(o3);
    // Front should be the first added.
    EXPECT_EQ(level_.front()->order_id, 1u);
    EXPECT_EQ(level_.total_qty(), 450);
}

// ─── reduce_front (partial / full fill) ──────────────────────────────────────

TEST_F(PriceLevelTest, ReduceFront_PartialFill) {
    Order* o = make(1, Side::Buy, 100.0, 300);
    level_.add(o);
    level_.reduce_front(100);          // fill 100 of 300
    EXPECT_EQ(level_.total_qty(), 200);
    EXPECT_EQ(o->remaining_qty, 200);
    EXPECT_FALSE(level_.empty());
}

TEST_F(PriceLevelTest, ReduceFront_FullFill_RemovesOrder) {
    Order* o1 = make(1, Side::Buy, 100.0, 100);
    Order* o2 = make(2, Side::Buy, 100.0, 50);
    level_.add(o1);
    level_.add(o2);
    level_.reduce_front(100);          // fully fill o1
    // o2 should now be at front
    EXPECT_EQ(level_.front()->order_id, 2u);
    EXPECT_EQ(level_.total_qty(), 50);
    EXPECT_EQ(level_.depth(), 1u);
}

// ─── O(1) cancel ─────────────────────────────────────────────────────────────

TEST_F(PriceLevelTest, Cancel_MiddleOrder) {
    Order* o1 = make(1, Side::Buy, 100.0, 100);
    Order* o2 = make(2, Side::Buy, 100.0, 200);
    Order* o3 = make(3, Side::Buy, 100.0, 150);
    level_.add(o1); level_.add(o2); level_.add(o3);

    bool removed = level_.remove(2);   // cancel middle order
    EXPECT_TRUE(removed);
    EXPECT_EQ(level_.total_qty(), 250);   // 100 + 150
    EXPECT_EQ(level_.depth(), 2u);
    EXPECT_FALSE(level_.contains(2));

    // FIFO should still be intact: o1 is first, then o3
    EXPECT_EQ(level_.front()->order_id, 1u);
    level_.reduce_front(100);
    EXPECT_EQ(level_.front()->order_id, 3u);
}

TEST_F(PriceLevelTest, Cancel_NonExistentId_ReturnsFalse) {
    EXPECT_FALSE(level_.remove(999));
}

TEST_F(PriceLevelTest, Cancel_LastOrder_LevelBecomesEmpty) {
    Order* o = make(1, Side::Buy, 100.0, 100);
    level_.add(o);
    level_.remove(1);
    EXPECT_TRUE(level_.empty());
    EXPECT_EQ(level_.total_qty(), 0);
}

// ─── contains ─────────────────────────────────────────────────────────────────

TEST_F(PriceLevelTest, Contains_AfterAddAndRemove) {
    Order* o = make(5, Side::Buy, 100.0, 100);
    EXPECT_FALSE(level_.contains(5));
    level_.add(o);
    EXPECT_TRUE(level_.contains(5));
    level_.remove(5);
    EXPECT_FALSE(level_.contains(5));
}
