#include "engine/object_pool.hpp"
#include "engine/order.hpp"
#include <gtest/gtest.h>

using namespace hft;

// ─── Basic allocate / free ────────────────────────────────────────────────────

TEST(ObjectPoolTest, InitialState) {
    ObjectPool<Order, 64> pool;
    EXPECT_EQ(pool.capacity(),  64u);
    EXPECT_EQ(pool.available(), 64u);
    EXPECT_EQ(pool.in_use(),     0u);
    EXPECT_TRUE(pool.empty());
    EXPECT_FALSE(pool.full());
}

TEST(ObjectPoolTest, Allocate_ReturnsNonNull) {
    ObjectPool<Order, 8> pool;
    Order* p = pool.allocate();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(pool.available(), 7u);
    EXPECT_EQ(pool.in_use(),    1u);
}

TEST(ObjectPoolTest, Allocate_ExhaustsPool_ReturnsNullptr) {
    ObjectPool<Order, 4> pool;
    for (int i = 0; i < 4; ++i) ASSERT_NE(pool.allocate(), nullptr);
    EXPECT_TRUE(pool.full());
    EXPECT_EQ(pool.allocate(), nullptr);  // pool exhausted
}

TEST(ObjectPoolTest, Free_RestoresAvailability) {
    ObjectPool<Order, 4> pool;
    Order* p = pool.allocate();
    EXPECT_EQ(pool.available(), 3u);
    pool.free(p);
    EXPECT_EQ(pool.available(), 4u);
}

TEST(ObjectPoolTest, AllocateFree_Cycle) {
    ObjectPool<Order, 4> pool;
    Order* p1 = pool.allocate();
    Order* p2 = pool.allocate();
    pool.free(p1);
    Order* p3 = pool.allocate();   // should reuse p1's slot
    EXPECT_EQ(pool.in_use(), 2u);
    pool.free(p2);
    pool.free(p3);
    EXPECT_TRUE(pool.empty());
}

// ─── make / destroy ───────────────────────────────────────────────────────────

TEST(ObjectPoolTest, Make_ConstructsObject) {
    ObjectPool<Order, 8> pool;
    Order* o = pool.make(Order::make_limit(1, Side::Buy, to_price(100.0), 500));
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(o->order_id,  1u);
    EXPECT_EQ(o->price,     to_price(100.0));
    EXPECT_EQ(o->quantity,  500);
    EXPECT_EQ(o->side,      Side::Buy);
    pool.destroy(o);
    EXPECT_TRUE(pool.empty());
}

// ─── owns ─────────────────────────────────────────────────────────────────────

TEST(ObjectPoolTest, Owns_CorrectlyIdentifiesPoolPointers) {
    ObjectPool<Order, 8> pool;
    Order* p = pool.allocate();
    EXPECT_TRUE(pool.owns(p));

    Order external = Order::make_limit(99, Side::Sell, to_price(50.0), 100);
    EXPECT_FALSE(pool.owns(&external));
    pool.free(p);
}

// ─── Reuse correctness ────────────────────────────────────────────────────────

TEST(ObjectPoolTest, Reuse_SlotDataIsOverwritten) {
    ObjectPool<Order, 4> pool;

    // Write order A into slot
    Order* a = pool.make(Order::make_limit(1, Side::Buy, to_price(100.0), 100));
    pool.destroy(a);

    // Reuse same slot for order B
    Order* b = pool.make(Order::make_limit(2, Side::Sell, to_price(200.0), 999));
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->order_id, 2u);
    EXPECT_EQ(b->price,    to_price(200.0));
    EXPECT_EQ(b->quantity, 999u);
    pool.destroy(b);
}

// ─── Full allocate-free cycle at capacity ─────────────────────────────────────

TEST(ObjectPoolTest, FullCycle_AllSlotsUsedAndReturned) {
    constexpr size_t CAP = 16;
    ObjectPool<Order, CAP> pool;

    std::vector<Order*> ptrs;
    for (size_t i = 0; i < CAP; ++i)
        ptrs.push_back(pool.make(Order::make_limit(i, Side::Buy, to_price(100.0), 100)));

    EXPECT_TRUE(pool.full());
    for (auto* p : ptrs) pool.destroy(p);
    EXPECT_TRUE(pool.empty());
    EXPECT_EQ(pool.available(), CAP);
}
