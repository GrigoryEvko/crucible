#pragma once

// The record this emits is read by editor tooling, so the field names,
// their nesting and the version number are an external contract. Adding
// a field is safe. Renaming or removing one is not.
//
// THE OUTPUT
//   A record and a line go to a sink of the Report family (Sink::Out or
//   Sink::Err) through the sink write of foundation/core/Report.h, which
//   issues the raw write of foundation/os/Syscall.h.  The emitter calls no
//   stdio, takes no lock and allocates nothing, and it is async-signal-safe.
//   It keeps its stack frame small: a record goes to the sink through a
//   buffer of 512 bytes, and a thread with a small stack can emit one.  A
//   record longer than the buffer goes in more than one write.  The records
//   of two threads that emit at one time can then mix.  The format forms write a
//   record or a line into a FixedText of the caller, and write no byte to a
//   sink.

#include <foundation/Platform.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Report.h>
#include <foundation/core/Text.h>
#include <foundation/diag/Catalog.h>
#include <foundation/diag/RowMismatch.h>
#include <foundation/reflect/Anchor.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <vector>

namespace foundation::diag {

// The version consumers gate on is CRUCIBLE_DIAG_FORMAT_VERSION in
// RowMismatch.h, which ties it to the line count of the block.

struct SourcePosition {
    std::string_view file{};
    std::uint_least32_t line = 0;
    std::uint_least32_t column = 0;
    std::string_view function{};
};

struct JsonDiagnosticRecord {
    Category category = Category::EffectRowMismatch;
    SourcePosition source{};
    std::string_view error_code{};
    std::string_view goal{};
    std::string_view have{};
    std::string_view gap{};
    std::string_view suggestion{};
    std::string_view related_snippet{};
};

// A record longer than this is refused whole: nothing of it reaches the
// stream.
inline constexpr std::size_t json_record_max_bytes = 32768;

namespace detail {

// The writer further down names each field it emits.  A field added to
// either record above would be silently absent from the output, and a
// consumer would read a record that claims the current format version
// and is missing data.  These two rosters are what make that addition a
// build failure.  The check file of this header compares each record with
// its roster.
//
// Each roster is hand-written and never derived, because a derived one
// would agree with any addition and say nothing.  Extending it is the
// moment to decide how the new field is emitted, and whether
// CRUCIBLE_DIAG_FORMAT_VERSION has to rise for a consumer to notice.
//
// The names here are the members, not the JSON keys.  Two differ on
// purpose: `source` is emitted as the nested object "source_position",
// and `related_snippet` as the array "related_snippets".  `category` is
// emitted through no key of its own: it supplies the fallback text for
// error_code, goal and suggestion when those are empty.
inline constexpr std::array<std::string_view, 4> source_position_fields{"file", "line", "column", "function"};

inline constexpr std::array<std::string_view, 8> json_record_fields{
    "category", "source", "error_code", "goal", "have", "gap", "suggestion", "related_snippet"};

// The members of Record, in declaration order, are exactly `expected`.
template <typename Record, std::size_t N>
[[nodiscard]] consteval bool record_fields_are(std::array<std::string_view, N> const& expected) noexcept {
    static constexpr auto members =
        std::define_static_array(static_cast<::foundation::reflect::anchored_t<^^Record, std::vector<std::meta::info>>>(
            std::meta::nonstatic_data_members_of(^^Record, std::meta::access_context::current())));
    if (members.size() != N) return false;
    std::size_t index = 0;
    bool matched = true;
// An expansion statement unrolls into successive scopes that each
// declare the same induction variable, so -Wshadow fires once per
// iteration.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        matched = matched && std::meta::identifier_of(member) == expected[index];
        ++index;
    }
#pragma GCC diagnostic pop
    return matched;
}

// Reads s as a decimal number of std::uint_least32_t: one digit or more,
// and no other character.  A value past the largest value of the type gives
// false, and out does not change.
[[nodiscard]] inline bool parse_u32(std::string_view s, std::uint_least32_t& out) noexcept {
    if (s.empty()) return false;
    constexpr std::uint_least32_t largest = ~std::uint_least32_t{0};
    std::uint_least32_t value = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<std::uint_least32_t>(c - '0');
        if (value > (largest - digit) / 10U) return false;
        value = value * 10U + digit;
    }
    out = value;
    return true;
}

[[nodiscard]] inline bool all_decimal_digits(std::string_view s) noexcept {
    bool has_digit = false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        has_digit = true;
    }
    return has_digit;
}

// The part of a view from START, with LENGTH characters at the maximum.  A
// START past the end gives an empty part at the end.  std::string_view::substr
// throws std::out_of_range for such a START, and this function has no throw.
[[nodiscard]] constexpr std::string_view view_part(std::string_view view, std::size_t start,
                                                   std::size_t length = std::string_view::npos) noexcept {
    const std::size_t first = start < view.size() ? start : view.size();
    const std::size_t rest = view.size() - first;
    return std::string_view{view.data() + first, length < rest ? length : rest};
}

// A sink takes the bytes of a record one at a time: the counter below, a
// sink of the Report family through report_json_sink, or a FixedText through
// text_json_sink.  Each answers false when it refuses a byte, and the writer
// stops at the first false.  One writer then serves the count and the output,
// and the code spells each escape of the format one time.
template <class S>
concept JsonSink = requires(S& sink, char c) {
    { sink.push(c) } -> std::same_as<bool>;
};

// Counts the bytes of a record and writes none.  It refuses the first
// byte past json_record_max_bytes, so a record over the bound is refused
// before any of it is written.
class counting_json_sink {
public:
    [[nodiscard]] bool push(char /*byte*/) noexcept {
        if (bytes_ >= json_record_max_bytes) return false;
        ++bytes_;
        return true;
    }

private:
    // bytes_ <= json_record_max_bytes is an invariant.
    std::size_t bytes_ = 0;
};

// The bytes of the buffer of report_json_sink.
inline constexpr std::size_t report_buffer_bytes = 512;

// Writes a record to a sink of the Report family through a small buffer on
// the stack.  A full buffer goes to the sink in one sink write, and flush()
// writes the rest.  The sink write has no error road, because a report has
// no caller to tell.  For that reason, the sink refuses no byte.
class report_json_sink {
public:
    explicit report_json_sink(core::Sink sink) noexcept : sink_{sink} {}
    report_json_sink(const report_json_sink&) = delete("a sink owns the unwritten bytes of one record");
    report_json_sink& operator=(const report_json_sink&) = delete("a sink owns the unwritten bytes of one record");

    [[nodiscard]] bool push(char byte) noexcept {
        std::size_t count = buffer_.size();
        if (count >= report_buffer_bytes) {
            flush();
            count = 0;
        }
        core::detail::text_bytes_(buffer_)[count] = byte;
        core::detail::text_resize_(buffer_, count + 1);
        return true;
    }

    // Writes the bytes that the buffer holds.
    void flush() noexcept {
        std::size_t const count = buffer_.size();
        if (count > 0) core::detail::write_sink_(sink_, core::detail::text_bytes_(buffer_), count);
        core::detail::text_resize_(buffer_, 0);
    }

private:
    core::Sink sink_ = core::Sink::Err;
    core::FixedText<report_buffer_bytes> buffer_{};
};

// Writes a record into a FixedText.  It refuses the first byte past the
// capacity of the text, and each byte after that.
template <std::size_t Capacity>
class text_json_sink {
public:
    explicit text_json_sink(core::FixedText<Capacity>& out) noexcept : out_{&out} {
        core::detail::text_resize_(*out_, 0);
    }
    text_json_sink(const text_json_sink&) = delete("a sink writes one record into one text");
    text_json_sink& operator=(const text_json_sink&) = delete("a sink writes one record into one text");

    [[nodiscard]] bool push(char byte) noexcept {
        const std::size_t count = out_->size();
        if (count >= Capacity) return false;
        core::detail::text_bytes_(*out_)[count] = byte;
        core::detail::text_resize_(*out_, count + 1);
        return true;
    }

private:
    // The text of the caller, which outlives the sink.
    core::FixedText<Capacity>* out_;
};

// Writes each character of text to the sink, and stops at the first
// character that the sink refuses.  Complexity: linear in the length of
// text.
template <JsonSink S>
[[nodiscard]] bool append_text(S& sink, core::TextView text) noexcept {
    char const* const bytes = core::detail::text_first_(text);
    std::size_t const count = text.size();
    for (std::size_t index = 0; index < count; ++index) {
        if (!sink.push(bytes[index])) return false;
    }
    return true;
}

// Calls step with each byte of text, in order, and stops at the first byte
// for which step gives false.  It gives false when step stops it.  Text is
// a view of characters with no terminating zero: a character array would
// give its zero to step too.  Each writer of a field walks its text through
// this one loop.  Complexity: linear in the length of text.
template <class Text, class Step>
[[nodiscard]] CRUCIBLE_INLINE bool each_byte(Text text, Step step) noexcept {
    for (const char byte : text) {
        if (!step(byte)) return false;
    }
    return true;
}

// Writes s with every byte JSON reserves escaped: the quote, the
// backslash, the five named controls by their short form, and every
// other byte below 0x20 as \u00XX with uppercase hex.
template <JsonSink S>
[[nodiscard]] bool append_json_escaped(S& sink, std::string_view s) noexcept {
    static constexpr char hex[] = "0123456789ABCDEF";
    return each_byte(s, [&sink](char raw) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"':
                return append_text(sink, "\\\"");
            case '\\':
                return append_text(sink, "\\\\");
            case '\b':
                return append_text(sink, "\\b");
            case '\f':
                return append_text(sink, "\\f");
            case '\n':
                return append_text(sink, "\\n");
            case '\r':
                return append_text(sink, "\\r");
            case '\t':
                return append_text(sink, "\\t");
            default:
                if (c >= 0x20) return sink.push(static_cast<char>(c));
                const char escaped[6] = {
                    '\\', 'u', '0', '0', hex[(c >> 4) & 0x0F], hex[c & 0x0F],
                };
                for (const char byte : escaped) {
                    if (!sink.push(byte)) return false;
                }
                return true;
        }
    });
}

// Writes `"key":"value"` and, unless the field is the last of its
// object, the comma after it.
template <JsonSink S>
[[nodiscard]] bool append_json_string_field(S& sink, std::string_view key, std::string_view value,
                                            bool comma = true) noexcept {
    return sink.push('"') && append_json_escaped(sink, key) && append_text(sink, "\":\"")
        && append_json_escaped(sink, value) && sink.push('"') && (!comma || sink.push(','));
}

// Writes value in decimal.  An unsigned long has at most 20 digits.
template <JsonSink S>
[[nodiscard]] bool append_json_uint(S& sink, unsigned long value) noexcept {
    char digits[20] = {};
    std::size_t count = 0;
    do {
        digits[count] = static_cast<char>('0' + value % 10U);
        ++count;
        value /= 10U;
    } while (value != 0U);
    while (count > 0) {
        --count;
        if (!sink.push(digits[count])) return false;
    }
    return true;
}

// The one writer of a record, for the count and for the output.
//
// Each function of this header that reads the catalog is a template with
// the parameter Unused, and each catalog call names that parameter.  So
// only a translation unit that calls the function builds the tables of the
// catalog (CLAUDE.md §XV "Compile time", rule 2).
template <JsonSink S, class Unused = void>
[[nodiscard]] bool write_json_record(S& sink, JsonDiagnosticRecord const& rec) noexcept {
    const std::string_view code = rec.error_code.empty() ? name_of<Unused>(rec.category) : rec.error_code;
    const std::string_view goal = rec.goal.empty() ? description_of<Unused>(rec.category) : rec.goal;
    const std::string_view suggestion = rec.suggestion.empty() ? remediation_of<Unused>(rec.category) : rec.suggestion;

    if (!append_text(sink, "{\"format_version\":")) return false;
    if (!append_json_uint(sink, static_cast<unsigned long>(CRUCIBLE_DIAG_FORMAT_VERSION))) return false;
    if (!append_text(sink, ",\"source_position\":{")) return false;
    if (!append_json_string_field(sink, "file", rec.source.file)) return false;
    if (!append_text(sink, "\"line\":")) return false;
    if (!append_json_uint(sink, static_cast<unsigned long>(rec.source.line))) return false;
    if (!append_text(sink, ",\"column\":")) return false;
    if (!append_json_uint(sink, static_cast<unsigned long>(rec.source.column))) return false;
    if (!sink.push(',')) return false;
    if (!append_json_string_field(sink, "function", rec.source.function, false)) return false;
    if (!append_text(sink, "},")) return false;
    if (!append_json_string_field(sink, "error_code", code)) return false;
    if (!append_json_string_field(sink, "goal", goal)) return false;
    if (!append_json_string_field(sink, "have", rec.have)) return false;
    if (!append_json_string_field(sink, "gap", rec.gap)) return false;
    if (!append_json_string_field(sink, "suggestion", suggestion)) return false;
    if (!append_text(sink, "\"related_snippets\":[")) return false;
    if (!rec.related_snippet.empty()) {
        if (!sink.push('"') || !append_json_escaped(sink, rec.related_snippet) || !sink.push('"')) {
            return false;
        }
    }
    return append_text(sink, "]}\n");
}

}  // namespace detail

// The context is the composite the runtime emitter puts in its function
// field, "<file>:<line>:<column>@<function>". Every shorter or malformed
// form degrades rather than fails: whatever cannot be read as a position
// stays whole in one of the two string fields.
[[nodiscard]] inline SourcePosition parse_source_position(std::string_view context) noexcept {
    SourcePosition pos{};
    const std::size_t at = context.rfind('@');
    const bool has_function_delimiter = at != std::string_view::npos;
    std::string_view loc = context;
    if (has_function_delimiter) {
        loc = detail::view_part(context, 0, at);
        pos.function = detail::view_part(context, at + 1);
    } else {
        pos.function = context;
    }

    if (loc.empty()) return pos;

    const std::size_t last_colon = loc.rfind(':');
    if (last_colon == std::string_view::npos) {
        if (has_function_delimiter) pos.file = loc;
        return pos;
    }

    std::uint_least32_t tail = 0;
    if (!detail::parse_u32(detail::view_part(loc, last_colon + 1), tail)) {
        pos.file = loc;
        return pos;
    }

    const std::size_t prev_colon = last_colon == 0 ? std::string_view::npos : loc.rfind(':', last_colon - 1);
    if (prev_colon != std::string_view::npos) {
        std::uint_least32_t parsed_line = 0;
        const std::string_view line_field = detail::view_part(loc, prev_colon + 1, last_colon - prev_colon - 1);
        if (detail::parse_u32(line_field, parsed_line)) {
            pos.file = detail::view_part(loc, 0, prev_colon);
            pos.line = parsed_line;
            pos.column = tail;
            return pos;
        }
        if (detail::all_decimal_digits(line_field)) {
            pos.file = loc;
            return pos;
        }
    }

    pos.file = detail::view_part(loc, 0, last_colon);
    pos.line = tail;
    return pos;
}

template <class Unused = void>
[[nodiscard]] JsonDiagnosticRecord record_from_violation(Category cat, std::string_view context,
                                                         std::string_view detail) noexcept {
    const SourcePosition source = parse_source_position(context);
    return JsonDiagnosticRecord{
        .category = cat,
        .source = source,
        .error_code = name_of<Unused>(cat),
        .goal = description_of<Unused>(cat),
        .have = source.function.empty() ? context : source.function,
        .gap = detail,
        .suggestion = remediation_of<Unused>(cat),
        .related_snippet = {},
    };
}

// Two passes over one writer.  The first counts the bytes and refuses a
// record past json_record_max_bytes, so an over-long record leaves the
// sink untouched and the function gives false.  The second writes the
// record to the sink.  Complexity: linear in the length of the record, twice.
template <class Unused = void>
[[nodiscard]] bool emit_json_record(core::Sink out, JsonDiagnosticRecord const& rec) noexcept {
    detail::counting_json_sink counter;
    if (!detail::write_json_record<detail::counting_json_sink, Unused>(counter, rec)) return false;
    detail::report_json_sink sink{out};
    const bool written = detail::write_json_record<detail::report_json_sink, Unused>(sink, rec);
    sink.flush();
    return written;
}

// Writes the record into out, from its start, and gives the view of out.  A
// record longer than json_record_max_bytes or than Capacity gives no value,
// and out then holds no character.  Complexity: linear in the length of the
// record, twice.
template <std::size_t Capacity, class Unused = void>
[[nodiscard]] core::Option<core::TextView> format_json_record(core::FixedText<Capacity>& out,
                                                              JsonDiagnosticRecord const& rec) noexcept {
    detail::counting_json_sink counter;
    detail::text_json_sink<Capacity> sink{out};
    if (!detail::write_json_record<detail::counting_json_sink, Unused>(counter, rec)
        || !detail::write_json_record<detail::text_json_sink<Capacity>, Unused>(sink, rec)) {
        core::detail::text_resize_(out, 0);
        return core::none;
    }
    return core::Option<core::TextView>::some(out.view());
}

template <class Unused = void>
[[nodiscard]] bool emit_json_violation(core::Sink out, Category cat, std::string_view context,
                                       std::string_view detail) noexcept {
    return emit_json_record<Unused>(out, record_from_violation<Unused>(cat, context, detail));
}

namespace detail {

// The most characters of each field of the line of a violation.
inline constexpr std::size_t legacy_field_chars = 4096;

// Writes the characters of text up to its first zero byte, and at most
// legacy_field_chars of them, as the %.*s of printf does.  Text is a view of
// characters with no terminating zero, and the function takes a copy of it.
template <JsonSink S, class Text>
[[nodiscard]] bool append_legacy_field(S& sink, Text text) noexcept {
    std::size_t written = 0;
    bool is_refused = false;
    static_cast<void>(each_byte(text, [&](char byte) {
        if (written >= legacy_field_chars || byte == '\0') return false;
        is_refused = !sink.push(byte);
        ++written;
        return !is_refused;
    }));
    return !is_refused;
}

// The writer of the line of a violation: the name of the category, the
// function and the text of the violation.  Text is a view of characters with
// no terminating zero.
template <JsonSink S, class Text>
[[nodiscard]] bool write_legacy_text(S& sink, Text name, Text fn, Text detail) noexcept {
    return append_text(sink, "crucible-violation: category=") && append_legacy_field(sink, name)
        && append_text(sink, " fn=") && append_legacy_field(sink, fn) && append_text(sink, " detail=")
        && append_legacy_field(sink, detail) && sink.push('\n');
}

}  // namespace detail

// Writes the line of a violation to the sink:
//
//   crucible-violation: category=<Name> fn=<fn> detail=<detail>
//
// Each field is cut at 4096 characters and at its first zero byte.  The
// parameter detail hides the namespace detail.  For that reason, the body
// names diag::detail.
template <class Unused = void>
void emit_legacy_text_violation(core::Sink out, Category cat, std::string_view fn, std::string_view detail) noexcept {
    diag::detail::report_json_sink sink{out};
    static_cast<void>(diag::detail::write_legacy_text(sink, name_of<Unused>(cat), fn, detail));
    sink.flush();
}

}  // namespace foundation::diag
