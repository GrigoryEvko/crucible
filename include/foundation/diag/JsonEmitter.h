#pragma once

// The record this emits is read by editor tooling, so the field names,
// their nesting and the version number are an external contract. Adding
// a field is safe. Renaming or removing one is not.
//
// The emitter writes through stdio, so it is not async-signal-safe.  It
// keeps its stack frame small: a record goes to the stream through a
// buffer of a few hundred bytes, so a thread with a small stack can emit
// one.

#include <foundation/Platform.h>
#include <foundation/diag/Catalog.h>
#include <foundation/diag/RowMismatch.h>

#include <array>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <meta>
#include <string_view>
#include <system_error>

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
// build failure.
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
        std::define_static_array(std::meta::nonstatic_data_members_of(^^Record, std::meta::access_context::current()));
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

static_assert(record_fields_are<SourcePosition>(source_position_fields),
              "SourcePosition gained, lost or renamed a field.  The nested source_position object in "
              "write_json_record writes each field by name, so a new one is absent from the output until "
              "it is written there and added to source_position_fields.");

static_assert(record_fields_are<JsonDiagnosticRecord>(json_record_fields),
              "JsonDiagnosticRecord gained, lost or renamed a field.  write_json_record writes each field "
              "by name, so a new one is absent from the output until it is written there and added to "
              "json_record_fields.  Renaming or removing one changes an external contract and needs a "
              "format version.");

// The roster answers no for each way it and its record can disagree.
// Without these, a record_fields_are that answered yes to everything
// would leave both assertions above green and pin nothing.
namespace record_roster_self_test {

struct Probe {
    int first = 0;
    int second = 0;
};

inline constexpr std::array<std::string_view, 2> correct{"first", "second"};
static_assert(record_fields_are<Probe>(correct));

inline constexpr std::array<std::string_view, 2> renamed{"first", "deuxieme"};
static_assert(!record_fields_are<Probe>(renamed), "A renamed field must be caught.");

inline constexpr std::array<std::string_view, 2> reordered{"second", "first"};
static_assert(!record_fields_are<Probe>(reordered), "A reordered roster must be caught, because the writer "
                                                    "emits in declaration order.");

inline constexpr std::array<std::string_view, 1> too_short{"first"};
static_assert(!record_fields_are<Probe>(too_short), "A field the roster does not name must be caught.");

inline constexpr std::array<std::string_view, 3> too_long{"first", "second", "third"};
static_assert(!record_fields_are<Probe>(too_long), "A roster entry no field answers to must be caught.");

}  // namespace record_roster_self_test

[[nodiscard]] inline bool parse_u32(std::string_view s, std::uint_least32_t& out) noexcept {
    if (s.empty()) return false;
    unsigned long value = 0;
    const auto* begin = s.data();
    const auto* end = s.data() + s.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) return false;
    if (value > std::numeric_limits<std::uint_least32_t>::max()) {
        return false;
    }
    out = static_cast<std::uint_least32_t>(value);
    return true;
}

[[nodiscard]] inline bool all_decimal_digits(std::string_view s) noexcept {
    if (s.empty()) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

// A sink takes the bytes of a record: the counter below, or a stream
// through file_json_sink.  Each answers false when it refuses a byte, and
// the writer stops at the first false.  One writer then serves the count
// and the output, so an escape the format reserves is spelled once.
template <class S>
concept JsonSink = requires(S& sink, std::string_view text, char c) {
    { sink.append(text) } -> std::same_as<bool>;
    { sink.push(c) } -> std::same_as<bool>;
};

// Counts the bytes of a record and writes none.  It refuses the first
// byte past json_record_max_bytes, so a record over the bound is refused
// before any of it is written.
class counting_json_sink {
public:
    [[nodiscard]] bool append(std::string_view text) noexcept {
        if (text.size() > json_record_max_bytes - bytes_) return false;
        bytes_ += text.size();
        return true;
    }

    [[nodiscard]] bool push(char /*byte*/) noexcept { return append(std::string_view{" ", 1}); }

private:
    // bytes_ <= json_record_max_bytes is an invariant, so the subtraction
    // in append cannot wrap.
    std::size_t bytes_ = 0;
};

// Writes a record to a stream through a small buffer on the stack.  A full
// buffer goes to the stream in one fwrite.  The caller holds the lock of
// the stream for the whole record, so the records of two threads do not
// mix.
class file_json_sink {
public:
    explicit file_json_sink(FILE* out) noexcept : out_{out} {}
    file_json_sink(const file_json_sink&) = delete("a sink owns the unwritten bytes of one record");
    file_json_sink& operator=(const file_json_sink&) = delete("a sink owns the unwritten bytes of one record");

    [[nodiscard]] bool append(std::string_view text) noexcept {
        for (const char byte : text) {
            if (!push(byte)) return false;
        }
        return true;
    }

    [[nodiscard]] bool push(char byte) noexcept {
        if (!ok_ || (length_ == buffer_.size() && !flush())) return false;
        buffer_[length_] = byte;
        ++length_;
        return true;
    }

    // Writes the bytes that the buffer holds.  A failed write poisons the
    // sink, so each later push and flush answers false.
    [[nodiscard]] bool flush() noexcept {
        if (length_ > 0 && ok_) ok_ = std::fwrite(buffer_.data(), 1, length_, out_) == length_;
        length_ = 0;
        return ok_;
    }

private:
    FILE* out_ = nullptr;
    std::array<char, 512> buffer_{};
    std::size_t length_ = 0;
    bool ok_ = true;
};

// Writes s with every byte JSON reserves escaped: the quote, the
// backslash, the five named controls by their short form, and every
// other byte below 0x20 as \u00XX with uppercase hex.
template <JsonSink S>
[[nodiscard]] bool append_json_escaped(S& sink, std::string_view s) noexcept {
    static constexpr char hex[] = "0123456789ABCDEF";
    for (char raw : s) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"':
                if (!sink.append("\\\"")) return false;
                break;
            case '\\':
                if (!sink.append("\\\\")) return false;
                break;
            case '\b':
                if (!sink.append("\\b")) return false;
                break;
            case '\f':
                if (!sink.append("\\f")) return false;
                break;
            case '\n':
                if (!sink.append("\\n")) return false;
                break;
            case '\r':
                if (!sink.append("\\r")) return false;
                break;
            case '\t':
                if (!sink.append("\\t")) return false;
                break;
            default:
                if (c < 0x20) {
                    char escaped[6] = {
                        '\\', 'u', '0', '0', hex[(c >> 4) & 0x0F], hex[c & 0x0F],
                    };
                    if (!sink.append({escaped, sizeof(escaped)})) return false;
                } else if (!sink.push(static_cast<char>(c))) {
                    return false;
                }
                break;
        }
    }
    return true;
}

// Writes `"key":"value"` and, unless the field is the last of its
// object, the comma after it.
template <JsonSink S>
[[nodiscard]] bool append_json_string_field(S& sink, std::string_view key, std::string_view value,
                                            bool comma = true) noexcept {
    return sink.push('"') && append_json_escaped(sink, key) && sink.append("\":\"") && append_json_escaped(sink, value)
        && sink.push('"') && (!comma || sink.push(','));
}

template <JsonSink S>
[[nodiscard]] bool append_json_uint(S& sink, unsigned long value) noexcept {
    char digits[32];
    const auto [ptr, ec] = std::to_chars(digits, digits + sizeof(digits), value);
    if (ec != std::errc{}) return false;
    return sink.append({digits, static_cast<std::size_t>(ptr - digits)});
}

// The one writer of a record, for the count and for the output.
template <JsonSink S>
[[nodiscard]] bool write_json_record(S& sink, JsonDiagnosticRecord const& rec) noexcept {
    const std::string_view code = rec.error_code.empty() ? name_of(rec.category) : rec.error_code;
    const std::string_view goal = rec.goal.empty() ? description_of(rec.category) : rec.goal;
    const std::string_view suggestion = rec.suggestion.empty() ? remediation_of(rec.category) : rec.suggestion;

    if (!sink.append("{\"format_version\":")) return false;
    if (!append_json_uint(sink, static_cast<unsigned long>(CRUCIBLE_DIAG_FORMAT_VERSION))) return false;
    if (!sink.append(",\"source_position\":{")) return false;
    if (!append_json_string_field(sink, "file", rec.source.file)) return false;
    if (!sink.append("\"line\":")) return false;
    if (!append_json_uint(sink, static_cast<unsigned long>(rec.source.line))) return false;
    if (!sink.append(",\"column\":")) return false;
    if (!append_json_uint(sink, static_cast<unsigned long>(rec.source.column))) return false;
    if (!sink.push(',')) return false;
    if (!append_json_string_field(sink, "function", rec.source.function, false)) return false;
    if (!sink.append("},")) return false;
    if (!append_json_string_field(sink, "error_code", code)) return false;
    if (!append_json_string_field(sink, "goal", goal)) return false;
    if (!append_json_string_field(sink, "have", rec.have)) return false;
    if (!append_json_string_field(sink, "gap", rec.gap)) return false;
    if (!append_json_string_field(sink, "suggestion", suggestion)) return false;
    if (!sink.append("\"related_snippets\":[")) return false;
    if (!rec.related_snippet.empty()) {
        if (!sink.push('"') || !append_json_escaped(sink, rec.related_snippet) || !sink.push('"')) {
            return false;
        }
    }
    return sink.append("]}\n");
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
        loc = context.substr(0, at);
        pos.function = context.substr(at + 1);
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
    if (!detail::parse_u32(loc.substr(last_colon + 1), tail)) {
        pos.file = loc;
        return pos;
    }

    const std::size_t prev_colon = last_colon == 0 ? std::string_view::npos : loc.rfind(':', last_colon - 1);
    if (prev_colon != std::string_view::npos) {
        std::uint_least32_t parsed_line = 0;
        const std::string_view line_field = loc.substr(prev_colon + 1, last_colon - prev_colon - 1);
        if (detail::parse_u32(line_field, parsed_line)) {
            pos.file = loc.substr(0, prev_colon);
            pos.line = parsed_line;
            pos.column = tail;
            return pos;
        }
        if (detail::all_decimal_digits(line_field)) {
            pos.file = loc;
            return pos;
        }
    }

    pos.file = loc.substr(0, last_colon);
    pos.line = tail;
    return pos;
}

[[nodiscard]] inline JsonDiagnosticRecord record_from_violation(Category cat, std::string_view context,
                                                                std::string_view detail) noexcept {
    const SourcePosition source = parse_source_position(context);
    return JsonDiagnosticRecord{
        .category = cat,
        .source = source,
        .error_code = name_of(cat),
        .goal = description_of(cat),
        .have = source.function.empty() ? context : source.function,
        .gap = detail,
        .suggestion = remediation_of(cat),
        .related_snippet = {},
    };
}

// Two passes over one writer.  The first counts the bytes and refuses a
// record past json_record_max_bytes, so an over-long record leaves the
// stream untouched.  The second writes the record under the lock of the
// stream.  Complexity: linear in the length of the record, twice.
[[nodiscard]] inline bool emit_json_record(FILE* out, JsonDiagnosticRecord const& rec) noexcept {
    if (out == nullptr) return false;
    detail::counting_json_sink counter;
    if (!detail::write_json_record(counter, rec)) return false;
    ::flockfile(out);
    detail::file_json_sink sink{out};
    const bool written = detail::write_json_record(sink, rec) && sink.flush();
    ::funlockfile(out);
    return written;
}

[[nodiscard]] inline bool emit_json_violation(FILE* out, Category cat, std::string_view context,
                                              std::string_view detail) noexcept {
    return emit_json_record(out, record_from_violation(cat, context, detail));
}

[[nodiscard]] inline bool emit_legacy_text_violation(FILE* out, Category cat, std::string_view fn,
                                                     std::string_view detail) noexcept {
    if (out == nullptr) return false;
    constexpr int max_field_chars = 4096;
    const std::string_view cat_name = name_of(cat);
    const int cat_n = cat_name.size() > max_field_chars ? max_field_chars : static_cast<int>(cat_name.size());
    const int fn_n = fn.size() > max_field_chars ? max_field_chars : static_cast<int>(fn.size());
    const int dt_n = detail.size() > max_field_chars ? max_field_chars : static_cast<int>(detail.size());

    return std::fprintf(out, "crucible-violation: category=%.*s fn=%.*s detail=%.*s\n", cat_n, cat_name.data(), fn_n,
                        fn.data(), dt_n, detail.data())
        >= 0;
}

}  // namespace foundation::diag
