#include "engine/order.hpp"
#include <gtest/gtest.h>
#include <cstddef>

using namespace hft;

TEST(OrderTest, SizeIs128Bytes) {
    EXPECT_EQ(sizeof(Order), 128u);
}

TEST(OrderTest, AlignedTo64Bytes) {
    EXPECT_EQ(alignof(Order), 64u);
}

TEST(OrderTest, HotFieldsInFirstCacheLine) {
    EXPECT_LT(offsetof(Order, order_id),      64u);
    EXPECT_LT(offsetof(Order, price),         64u);
    EXPECT_LT(offsetof(Order, quantity),      64u);
    EXPECT_LT(offsetof(Order, remaining_qty), 64u);
    EXPECT_LT(offsetof(Order, side),          64u);
    EXPECT_LT(offsetof(Order, type),          64u);
}

TEST(OrderTest, MakeLimit_FieldsCorrect) {
    auto o = Order::make_limit(42, Side::Buy, to_price(100.0), 500);
    EXPECT_EQ(o.order_id,      42u);
    EXPECT_EQ(o.price,         to_price(100.0));
    EXPECT_EQ(o.quantity,      500);
    EXPECT_EQ(o.remaining_qty, 500);
    EXPECT_EQ(o.side,          Side::Buy);
    EXPECT_EQ(o.type,          OrderType::Limit);
    EXPECT_EQ(o.status,        OrderStatus::New);
    EXPECT_GT(o.timestamp_ns,  0);
}

TEST(OrderTest, MakeMarket_BuySentinelPrice) {
    auto o = Order::make_market(1, Side::Buy, 100);
    EXPECT_EQ(o.price, INT64_MAX);
}

TEST(OrderTest, MakeMarket_SellSentinelPrice) {
    auto o = Order::make_market(2, Side::Sell, 100);
    EXPECT_EQ(o.price, 0);
}

TEST(OrderTest, MakeIOC_TypeIsIOC) {
    auto o = Order::make_ioc(99, Side::Sell, to_price(50.0), 200);
    EXPECT_EQ(o.type, OrderType::IOC);
    EXPECT_EQ(o.side, Side::Sell);
}

TEST(OrderTest, IsFilled_WhenRemainingIsZero) {
    auto o = Order::make_limit(1, Side::Buy, to_price(10.0), 100);
    EXPECT_FALSE(o.is_filled());
    o.remaining_qty = 0;
    EXPECT_TRUE(o.is_filled());
}

TEST(OrderTest, IsActive_NewAndPartiallyFilled) {
    auto o = Order::make_limit(1, Side::Buy, to_price(10.0), 100);
    EXPECT_TRUE(o.is_active());
    o.status = OrderStatus::PartiallyFilled;
    EXPECT_TRUE(o.is_active());
    o.status = OrderStatus::Filled;
    EXPECT_FALSE(o.is_active());
    o.status = OrderStatus::Cancelled;
    EXPECT_FALSE(o.is_active());
}
