#include "engine/types.hpp"
#include <gtest/gtest.h>

using namespace hft;

// ─── Fixed-point price arithmetic ────────────────────────────────────────────

TEST(PriceTest, ToPrice_BasicConversion) {
    EXPECT_EQ(to_price(189.42), 1'894'200);
    EXPECT_EQ(to_price(0.0001),        1);
    EXPECT_EQ(to_price(1.0),       10'000);
    EXPECT_EQ(to_price(100.0),  1'000'000);
}

TEST(PriceTest, FromPrice_BasicConversion) {
    EXPECT_DOUBLE_EQ(from_price(1'894'200), 189.42);
    EXPECT_DOUBLE_EQ(from_price(10'000),      1.0);
}

TEST(PriceTest, RoundTrip) {
    // to_price ∘ from_price should be the identity for 4-decimal prices.
    for (double p : {0.0001, 0.01, 1.2345, 99.9999, 12345.6789}) {
        EXPECT_NEAR(from_price(to_price(p)), p, 1e-4)
            << "Round-trip failed for price " << p;
    }
}

TEST(PriceTest, IntegerArithmetic_NoFloatError) {
    // Classic floating-point pitfall:
    //   0.1 + 0.2 != 0.3 in IEEE 754
    // With fixed-point integers this should be exact.
    Price a = to_price(0.1);
    Price b = to_price(0.2);
    Price c = to_price(0.3);
    EXPECT_EQ(a + b, c) << "0.1 + 0.2 should equal 0.3 in fixed-point";
}

// ─── Side helpers ─────────────────────────────────────────────────────────────

TEST(SideTest, Opposite) {
    EXPECT_EQ(opposite(Side::Buy),  Side::Sell);
    EXPECT_EQ(opposite(Side::Sell), Side::Buy);
}

TEST(SideTest, ToString) {
    EXPECT_STREQ(to_string(Side::Buy),  "BUY");
    EXPECT_STREQ(to_string(Side::Sell), "SELL");
}

// ─── OrderType helpers ────────────────────────────────────────────────────────

TEST(OrderTypeTest, ToString) {
    EXPECT_STREQ(to_string(OrderType::Limit),  "LIMIT");
    EXPECT_STREQ(to_string(OrderType::Market), "MARKET");
    EXPECT_STREQ(to_string(OrderType::IOC),    "IOC");
}
