#pragma once

// safety/diag/JsonEmitter.h — structured runtime diagnostic emission.
//
// Cold-path companion to Runtime.h.  The default runtime sink keeps the
// legacy single-line text format unless CRUCIBLE_DIAG_FORMAT=json is set;
// this header owns the JSON record layout used by IDE/LSP tooling.

#include <crucible/Platform.h>
#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/diag/RowMismatch.h>

#include <charconv>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <system_error>

namespace crucible::safety::diag {

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

namespace detail {

[[nodiscard]] inline bool write_all(FILE* out, std::string_view s) noexcept {
    if (out == nullptr) return false;
    if (s.empty()) return true;
    return std::fwrite(s.data(), 1, s.size(), out) == s.size();
}

[[nodiscard]] inline bool write_char(FILE* out, char c) noexcept {
    if (out == nullptr) return false;
    return std::fputc(static_cast<unsigned char>(c), out) != EOF;
}

[[nodiscard]] inline bool parse_u32(std::string_view s,
                                    std::uint_least32_t& out) noexcept {
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

template <std::size_t Capacity>
struct fixed_json_buffer {
    fixed_json_buffer() noexcept = default;

    char data[Capacity];
    std::size_t size = 0;
    bool ok = true;

    bool append(std::string_view s) noexcept {
        if (!ok || s.size() > Capacity - size) {
            ok = false;
            return false;
        }
        if (!s.empty()) {
            std::memcpy(data + size, s.data(), s.size());
            size += s.size();
        }
        return true;
    }

    bool push(char c) noexcept {
        if (!ok || size == Capacity) {
            ok = false;
            return false;
        }
        data[size++] = c;
        return true;
    }

    bool append_uint(unsigned long value) noexcept {
        char tmp[32];
        const auto [ptr, ec] =
            std::to_chars(tmp, tmp + sizeof(tmp), value);
        if (ec != std::errc{}) {
            ok = false;
            return false;
        }
        return append({tmp, static_cast<std::size_t>(ptr - tmp)});
    }

    bool append_escaped(std::string_view s) noexcept {
        static constexpr char hex[] = "0123456789ABCDEF";
        for (char raw : s) {
            const auto c = static_cast<unsigned char>(raw);
            switch (c) {
            case '"':
                if (!append("\\\"")) return false;
                break;
            case '\\':
                if (!append("\\\\")) return false;
                break;
            case '\b':
                if (!append("\\b")) return false;
                break;
            case '\f':
                if (!append("\\f")) return false;
                break;
            case '\n':
                if (!append("\\n")) return false;
                break;
            case '\r':
                if (!append("\\r")) return false;
                break;
            case '\t':
                if (!append("\\t")) return false;
                break;
            default:
                if (c < 0x20) {
                    char escaped[6] = {
                        '\\', 'u', '0', '0',
                        hex[(c >> 4) & 0x0F],
                        hex[c & 0x0F],
                    };
                    if (!append({escaped, sizeof(escaped)})) return false;
                } else if (!push(static_cast<char>(c))) {
                    return false;
                }
                break;
            }
        }
        return true;
    }

    bool string_field(std::string_view key,
                      std::string_view value,
                      bool comma = true) noexcept {
        return push('"')
            && append_escaped(key)
            && append("\":\"")
            && append_escaped(value)
            && push('"')
            && (!comma || push(','));
    }

    bool flush(FILE* out) noexcept {
        return ok
            && out != nullptr
            && std::fwrite(data, 1, size, out) == size;
    }
};

}  // namespace detail

[[nodiscard]] inline SourcePosition parse_source_position(
    std::string_view context) noexcept {
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

    const std::size_t prev_colon =
        last_colon == 0 ? std::string_view::npos
                        : loc.rfind(':', last_colon - 1);
    if (prev_colon != std::string_view::npos) {
        std::uint_least32_t parsed_line = 0;
        const std::string_view line_field =
            loc.substr(prev_colon + 1, last_colon - prev_colon - 1);
        if (detail::parse_u32(
                line_field, parsed_line)) {
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

[[nodiscard]] inline bool write_json_escaped(FILE* out,
                                             std::string_view s) noexcept {
    static constexpr char hex[] = "0123456789ABCDEF";
    for (char raw : s) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
        case '"':
            if (!detail::write_all(out, "\\\"")) return false;
            break;
        case '\\':
            if (!detail::write_all(out, "\\\\")) return false;
            break;
        case '\b':
            if (!detail::write_all(out, "\\b")) return false;
            break;
        case '\f':
            if (!detail::write_all(out, "\\f")) return false;
            break;
        case '\n':
            if (!detail::write_all(out, "\\n")) return false;
            break;
        case '\r':
            if (!detail::write_all(out, "\\r")) return false;
            break;
        case '\t':
            if (!detail::write_all(out, "\\t")) return false;
            break;
        default:
            if (c < 0x20) {
                char escaped[6] = {
                    '\\', 'u', '0', '0',
                    hex[(c >> 4) & 0x0F],
                    hex[c & 0x0F],
                };
                if (!detail::write_all(out, {escaped, sizeof(escaped)})) {
                    return false;
                }
            } else if (!detail::write_char(out, static_cast<char>(c))) {
                return false;
            }
            break;
        }
    }
    return true;
}

[[nodiscard]] inline bool write_json_string_field(
    FILE* out,
    std::string_view key,
    std::string_view value,
    bool comma = true) noexcept {
    return detail::write_char(out, '"')
        && write_json_escaped(out, key)
        && detail::write_all(out, "\":\"")
        && write_json_escaped(out, value)
        && detail::write_char(out, '"')
        && (!comma || detail::write_char(out, ','));
}

[[nodiscard]] inline JsonDiagnosticRecord record_from_violation(
    Category cat,
    std::string_view context,
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

[[nodiscard]] inline bool emit_json_record(
    FILE* out,
    JsonDiagnosticRecord const& rec) noexcept {
    if (out == nullptr) return false;
    detail::fixed_json_buffer<32768> buf;
    const std::string_view code =
        rec.error_code.empty() ? name_of(rec.category) : rec.error_code;
    const std::string_view goal =
        rec.goal.empty() ? description_of(rec.category) : rec.goal;
    const std::string_view suggestion =
        rec.suggestion.empty() ? remediation_of(rec.category) : rec.suggestion;

    if (!buf.append("{\"format_version\":")) return false;
    if (!buf.append_uint(static_cast<unsigned long>(
            CRUCIBLE_DIAG_FORMAT_VERSION))) return false;
    if (!buf.append(",\"source_position\":{")) return false;
    if (!buf.string_field("file", rec.source.file)) return false;
    if (!buf.append("\"line\":")) return false;
    if (!buf.append_uint(static_cast<unsigned long>(rec.source.line))) {
        return false;
    }
    if (!buf.append(",\"column\":")) return false;
    if (!buf.append_uint(static_cast<unsigned long>(rec.source.column))) {
        return false;
    }
    if (!buf.push(',')) return false;
    if (!buf.string_field("function", rec.source.function, false)) return false;
    if (!buf.append("},")) return false;
    if (!buf.string_field("error_code", code)) return false;
    if (!buf.string_field("goal", goal)) return false;
    if (!buf.string_field("have", rec.have)) return false;
    if (!buf.string_field("gap", rec.gap)) return false;
    if (!buf.string_field("suggestion", suggestion)) return false;
    if (!buf.append("\"related_snippets\":[")) return false;
    if (!rec.related_snippet.empty()) {
        if (!buf.push('"')
            || !buf.append_escaped(rec.related_snippet)
            || !buf.push('"')) {
            return false;
        }
    }
    return buf.append("]}\n") && buf.flush(out);
}

[[nodiscard]] inline bool emit_json_violation(
    FILE* out,
    Category cat,
    std::string_view context,
    std::string_view detail) noexcept {
    return emit_json_record(out, record_from_violation(cat, context, detail));
}

[[nodiscard]] inline bool emit_legacy_text_violation(
    FILE* out,
    Category cat,
    std::string_view fn,
    std::string_view detail) noexcept {
    if (out == nullptr) return false;
    constexpr int max_field_chars = 4096;
    const std::string_view cat_name = name_of(cat);
    const int cat_n =
        cat_name.size() > max_field_chars ? max_field_chars
                                          : static_cast<int>(cat_name.size());
    const int fn_n =
        fn.size() > max_field_chars ? max_field_chars
                                    : static_cast<int>(fn.size());
    const int dt_n =
        detail.size() > max_field_chars ? max_field_chars
                                        : static_cast<int>(detail.size());

    return std::fprintf(
        out,
        "crucible-violation: category=%.*s fn=%.*s detail=%.*s\n",
        cat_n, cat_name.data(),
        fn_n, fn.data(),
        dt_n, detail.data()) >= 0;
}

inline void json_violation_sink(Category cat,
                                std::string_view fn,
                                std::string_view detail) noexcept {
    (void)emit_json_violation(stderr, cat, fn, detail);
}

}  // namespace crucible::safety::diag
