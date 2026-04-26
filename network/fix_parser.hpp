#pragma once

#include "engine/types.hpp"
#include <string>
#include <string_view>
#include <unordered_map>
#include <optional>

namespace hft {

// ─── FIX Protocol Overview ───────────────────────────────────────────────────
//
// FIX (Financial Information eXchange) is the industry standard protocol
// for electronic trading.  Messages are key=value pairs separated by SOH
// (ASCII 0x01, shown as | in documentation).
//
// Example New Order Single (35=D):
//   8=FIX.4.2|9=148|35=D|49=CLIENT|56=SERVER|34=1|52=20240101-12:00:00|
//   11=ORD001|55=AAPL|54=1|38=100|44=18942|40=2|10=128|
//
// Key tags we implement:
//   8  = BeginString    (FIX.4.2)
//   9  = BodyLength
//   10 = Checksum
//   11 = ClOrdID        (client order id)
//   34 = MsgSeqNum
//   35 = MsgType        (D=NewOrder, F=Cancel, 8=ExecutionReport)
//   38 = OrderQty
//   40 = OrdType        (1=Market, 2=Limit)
//   44 = Price          (in fixed-point: 18942 = $189.42)
//   49 = SenderCompID
//   54 = Side           (1=Buy, 2=Sell)
//   55 = Symbol
//   56 = TargetCompID
//
// ─── MsgType values ──────────────────────────────────────────────────────────
namespace fix {

constexpr char SOH          = '\x01';   // field separator
constexpr char MSG_NEW_ORDER  = 'D';
constexpr char MSG_CANCEL     = 'F';
constexpr char MSG_EXEC_REPORT = '8';

// Tag numbers
constexpr int TAG_BEGIN_STRING = 8;
constexpr int TAG_BODY_LENGTH  = 9;
constexpr int TAG_CHECKSUM     = 10;
constexpr int TAG_CLORD_ID     = 11;
constexpr int TAG_MSG_SEQ_NUM  = 34;
constexpr int TAG_MSG_TYPE     = 35;
constexpr int TAG_ORDER_QTY    = 38;
constexpr int TAG_ORD_TYPE     = 40;
constexpr int TAG_PRICE        = 44;
constexpr int TAG_SENDER_COMP  = 49;
constexpr int TAG_SIDE         = 54;
constexpr int TAG_SYMBOL       = 55;
constexpr int TAG_TARGET_COMP  = 56;

// ─── ParsedMessage ────────────────────────────────────────────────────────────
//
// Represents a decoded FIX message.
// We store only the fields we care about.
//
struct ParsedMessage {
    char        msg_type    = '\0';
    std::string clord_id;
    std::string symbol;
    Side        side        = Side::Buy;
    Quantity    order_qty   = 0;
    Price       price       = 0;      // fixed-point (PRICE_SCALE = 10000)
    OrderType   ord_type    = OrderType::Limit;
    uint32_t    seq_num     = 0;
    bool        valid       = false;  // false if parse failed
    std::string error;                // reason if !valid
};

// ─── FIXParser ────────────────────────────────────────────────────────────────
//
// Stateless parser: parse() takes a raw FIX message string and returns
// a ParsedMessage.  No heap allocation in the hot path.
//
class FIXParser {
public:
    // Parse a complete FIX message.
    // Input may use either SOH (0x01) or '|' as separator (| for testing).
    static ParsedMessage parse(std::string_view raw);

    // Build a FIX Execution Report (35=8) for a fill.
    // Returns the serialized message string.
    static std::string make_exec_report(
        const std::string& clord_id,
        const std::string& symbol,
        Side               side,
        Price              exec_price,
        Quantity           exec_qty,
        Quantity           leaves_qty,   // remaining unfilled
        uint32_t           seq_num
    );

    // Compute FIX checksum: sum of all bytes mod 256, formatted as 3 digits.
    static uint8_t checksum(std::string_view msg);

private:
    // Extract all tag=value pairs from a raw message.
    // Returns map of tag→value strings.
    static std::unordered_map<int, std::string>
    extract_fields(std::string_view raw, char sep);
};

} // namespace fix
} // namespace hft
