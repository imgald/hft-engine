#include "network/fix_parser.hpp"
#include <charconv>
#include <sstream>
#include <iomanip>

namespace hft::fix {

// ─── checksum ────────────────────────────────────────────────────────────────
uint8_t FIXParser::checksum(std::string_view msg) {
    uint32_t sum = 0;
    for (unsigned char c : msg) sum += c;
    return static_cast<uint8_t>(sum % 256);
}

// ─── extract_fields ──────────────────────────────────────────────────────────
//
// Scan the raw message byte by byte, splitting on sep (SOH or '|').
// Each token is "tag=value".  We parse the tag as an integer and store
// the value as a string_view into the original buffer.
//
// No heap allocation: we use string_view throughout and only copy into
// the map (which is returned to the caller and used once).
//
std::unordered_map<int, std::string>
FIXParser::extract_fields(std::string_view raw, char sep) {
    std::unordered_map<int, std::string> fields;
    fields.reserve(16);

    size_t pos = 0;
    while (pos < raw.size()) {
        // Find end of this field
        size_t end = raw.find(sep, pos);
        if (end == std::string_view::npos) end = raw.size();

        std::string_view field = raw.substr(pos, end - pos);

        // Split on '='
        size_t eq = field.find('=');
        if (eq != std::string_view::npos) {
            std::string_view tag_str = field.substr(0, eq);
            std::string_view val_str = field.substr(eq + 1);

            // Parse tag as integer
            int tag = 0;
            auto [ptr, ec] = std::from_chars(
                tag_str.data(), tag_str.data() + tag_str.size(), tag);
            if (ec == std::errc{}) {
                fields.emplace(tag, std::string(val_str));
            }
        }

        pos = end + 1;
    }
    return fields;
}

// ─── parse ───────────────────────────────────────────────────────────────────
ParsedMessage FIXParser::parse(std::string_view raw) {
    ParsedMessage msg;

    // Detect separator: prefer SOH, fall back to '|' for testing
    char sep = (raw.find(SOH) != std::string_view::npos) ? SOH : '|';

    auto fields = extract_fields(raw, sep);

    // ── MsgType (required) ────────────────────────────────────
    auto it = fields.find(TAG_MSG_TYPE);
    if (it == fields.end() || it->second.empty()) {
        msg.error = "missing tag 35 (MsgType)";
        return msg;
    }
    msg.msg_type = it->second[0];

    // ── SeqNum ────────────────────────────────────────────────
    if (auto s = fields.find(TAG_MSG_SEQ_NUM); s != fields.end()) {
        std::from_chars(s->second.data(),
                        s->second.data() + s->second.size(),
                        msg.seq_num);
    }

    // For non-order messages we don't need further parsing
    if (msg.msg_type == MSG_CANCEL) {
        // Cancel only needs clord_id
        if (auto s = fields.find(TAG_CLORD_ID); s != fields.end()) {
            msg.clord_id = s->second;
            msg.valid = true;
        } else {
            msg.error = "missing tag 11 (ClOrdID)";
        }
        return msg;
    }

    if (msg.msg_type != MSG_NEW_ORDER) {
        msg.valid = true;
        return msg;
    }

    // ── ClOrdID (required for orders) ─────────────────────────
    if (auto s = fields.find(TAG_CLORD_ID); s != fields.end()) {
        msg.clord_id = s->second;
    } else {
        msg.error = "missing tag 11 (ClOrdID)";
        return msg;
    }

    // ── Symbol ────────────────────────────────────────────────
    if (auto s = fields.find(TAG_SYMBOL); s != fields.end()) {
        msg.symbol = s->second;
    } else {
        msg.error = "missing tag 55 (Symbol)";
        return msg;
    }

    // ── Side (1=Buy, 2=Sell) ──────────────────────────────────
    if (auto s = fields.find(TAG_SIDE); s != fields.end()) {
        msg.side = (s->second == "1") ? Side::Buy : Side::Sell;
    } else {
        msg.error = "missing tag 54 (Side)";
        return msg;
    }

    // ── OrderQty ──────────────────────────────────────────────
    if (auto s = fields.find(TAG_ORDER_QTY); s != fields.end()) {
        std::from_chars(s->second.data(),
                        s->second.data() + s->second.size(),
                        msg.order_qty);
    } else {
        msg.error = "missing tag 38 (OrderQty)";
        return msg;
    }

    // ── OrdType (1=Market, 2=Limit) ───────────────────────────
    if (auto s = fields.find(TAG_ORD_TYPE); s != fields.end()) {
        msg.ord_type = (s->second == "1") ? OrderType::Market : OrderType::Limit;
    }

    // ── Price (only required for Limit orders) ────────────────
    // Price is stored as fixed-point integer in FIX messages.
    // e.g. tag 44=18942 means $189.42 with PRICE_SCALE=100
    // We store in our internal PRICE_SCALE=10000 format.
    if (auto s = fields.find(TAG_PRICE); s != fields.end()) {
        // Try parsing as integer first (already scaled)
        int64_t raw_price = 0;
        auto [ptr, ec] = std::from_chars(
            s->second.data(), s->second.data() + s->second.size(), raw_price);
        if (ec == std::errc{}) {
            msg.price = raw_price;
        } else {
            // Fall back: parse as double and convert
            msg.price = to_price(std::stod(s->second));
        }
    } else if (msg.ord_type == OrderType::Limit) {
        msg.error = "missing tag 44 (Price) for limit order";
        return msg;
    }

    msg.valid = true;
    return msg;
}

// ─── make_exec_report ────────────────────────────────────────────────────────
//
// Build a FIX 4.2 Execution Report (35=8).
// Fields included:
//   8=FIX.4.2  35=8  49=SERVER  56=CLIENT
//   11=<clord_id>  55=<symbol>  54=<side>
//   32=<exec_qty>  31=<exec_price>  151=<leaves_qty>
//   10=<checksum>
//
std::string FIXParser::make_exec_report(
    const std::string& clord_id,
    const std::string& symbol,
    Side               side,
    Price              exec_price,
    Quantity           exec_qty,
    Quantity           leaves_qty,
    uint32_t           seq_num)
{
    std::ostringstream ss;
    // Build body first (needed for BodyLength tag 9)
    std::ostringstream body;
    body << TAG_MSG_TYPE    << "=8"         << SOH
         << TAG_SENDER_COMP << "=SERVER"    << SOH
         << TAG_TARGET_COMP << "=CLIENT"    << SOH
         << TAG_MSG_SEQ_NUM << "=" << seq_num << SOH
         << TAG_CLORD_ID    << "=" << clord_id << SOH
         << TAG_SYMBOL      << "=" << symbol  << SOH
         << TAG_SIDE        << "=" << (side == Side::Buy ? "1" : "2") << SOH
         << 32 << "=" << exec_qty           << SOH   // LastShares
         << 31 << "=" << exec_price         << SOH   // LastPx
         << 151 << "=" << leaves_qty        << SOH;  // LeavesQty

    std::string body_str = body.str();

    // Header
    ss << TAG_BEGIN_STRING << "=FIX.4.2" << SOH
       << TAG_BODY_LENGTH  << "=" << body_str.size() << SOH
       << body_str;

    // Checksum
    std::string partial = ss.str();
    uint8_t cs = checksum(partial);
    ss << TAG_CHECKSUM << "=" << std::setw(3) << std::setfill('0')
       << static_cast<int>(cs) << SOH;

    return ss.str();
}

} // namespace hft::fix
