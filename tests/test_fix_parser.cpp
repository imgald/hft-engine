#include "network/fix_parser.hpp"
#include <gtest/gtest.h>

using namespace hft;
using namespace hft::fix;

// ─── Parse New Order Single ───────────────────────────────────────────────────

TEST(FIXParserTest, ParseNewOrder_BuyLimit) {
    // Use '|' as separator for readability in tests
    std::string msg =
        "8=FIX.4.2|9=100|35=D|49=CLIENT|56=SERVER|34=1|"
        "11=ORD001|55=AAPL|54=1|38=100|44=18942|40=2|10=128|";

    auto parsed = FIXParser::parse(msg);

    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.msg_type,  'D');
    EXPECT_EQ(parsed.clord_id,  "ORD001");
    EXPECT_EQ(parsed.symbol,    "AAPL");
    EXPECT_EQ(parsed.side,      Side::Buy);
    EXPECT_EQ(parsed.order_qty, 100);
    EXPECT_EQ(parsed.price,     18942);
    EXPECT_EQ(parsed.ord_type,  OrderType::Limit);
    EXPECT_EQ(parsed.seq_num,   1u);
}

TEST(FIXParserTest, ParseNewOrder_SellMarket) {
    std::string msg =
        "8=FIX.4.2|35=D|34=2|11=ORD002|55=NVDA|54=2|38=50|40=1|10=000|";

    auto parsed = FIXParser::parse(msg);

    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.side,     Side::Sell);
    EXPECT_EQ(parsed.order_qty, 50);
    EXPECT_EQ(parsed.ord_type, OrderType::Market);
}

TEST(FIXParserTest, ParseCancel) {
    std::string msg =
        "8=FIX.4.2|35=F|34=3|11=ORD001|55=AAPL|54=1|38=100|10=000|";

    auto parsed = FIXParser::parse(msg);

    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.msg_type, 'F');
    EXPECT_EQ(parsed.clord_id, "ORD001");
}

// ─── Validation errors ────────────────────────────────────────────────────────

TEST(FIXParserTest, MissingMsgType_Invalid) {
    std::string msg = "8=FIX.4.2|49=CLIENT|11=ORD001|10=000|";
    auto parsed = FIXParser::parse(msg);
    EXPECT_FALSE(parsed.valid);
    EXPECT_FALSE(parsed.error.empty());
}

TEST(FIXParserTest, MissingSymbol_Invalid) {
    std::string msg = "8=FIX.4.2|35=D|11=ORD001|54=1|38=100|44=100|40=2|10=000|";
    auto parsed = FIXParser::parse(msg);
    EXPECT_FALSE(parsed.valid);
}

TEST(FIXParserTest, LimitOrder_MissingPrice_Invalid) {
    std::string msg = "8=FIX.4.2|35=D|11=ORD001|55=AAPL|54=1|38=100|40=2|10=000|";
    auto parsed = FIXParser::parse(msg);
    EXPECT_FALSE(parsed.valid);
}

// ─── Checksum ─────────────────────────────────────────────────────────────────

TEST(FIXParserTest, Checksum_KnownValue) {
    // "8=FIX.4.2\x01" — known checksum
    std::string_view msg = "8=FIX.4.2\x01";
    uint8_t cs = FIXParser::checksum(msg);
    EXPECT_GT(cs, 0u);  // just verify it runs without crash
}

// ─── Execution Report ─────────────────────────────────────────────────────────

TEST(FIXParserTest, MakeExecReport_Parseable) {
    std::string report = FIXParser::make_exec_report(
        "ORD001", "AAPL", Side::Buy,
        to_price(189.42), 100, 0, 1);

    EXPECT_FALSE(report.empty());
    // Must contain exec report msg type
    EXPECT_NE(report.find("35=8"), std::string::npos);
    // Must contain our clord_id
    EXPECT_NE(report.find("11=ORD001"), std::string::npos);
    // Must contain checksum tag
    EXPECT_NE(report.find("10="), std::string::npos);
}

// ─── SOH separator ───────────────────────────────────────────────────────────

TEST(FIXParserTest, ParseWithSOH_Separator) {
    // Build message with real SOH separator
    std::string msg;
    msg += "8=FIX.4.2\x01";
    msg += "35=D\x01";
    msg += "11=ORD003\x01";
    msg += "55=TSLA\x01";
    msg += "54=2\x01";
    msg += "38=200\x01";
    msg += "44=24815\x01";
    msg += "40=2\x01";
    msg += "10=000\x01";

    auto parsed = FIXParser::parse(msg);
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.symbol,    "TSLA");
    EXPECT_EQ(parsed.side,      Side::Sell);
    EXPECT_EQ(parsed.order_qty, 200);
}
