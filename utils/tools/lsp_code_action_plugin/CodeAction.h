#pragma once

// Tiny JSON-diagnostic -> LSP CodeAction bridge.
//
// The input format is the single-line record emitted by
// foundation/diag/JsonEmitter.h.  This header intentionally parses only the
// stable Crucible fields it consumes; it is not a general JSON library.
//
// A line holds at most kMaxLineCharacters characters, so a decoded field
// holds at most as many, and an action at most kMaxActionCharacters: six
// fields of at most two characters for each character of the line, and the
// fixed text of the action.  Each text grows on the heap up to its maximum,
// so an append that does not fit is a defect of these bounds and ends the
// process.

#include <fixy/Core.h>
#include <foundation/core/Heap.h>

#include <cstddef>
#include <utility>

namespace crucible::tools::lsp_code_action {

// The longest line that the tool reads.
inline constexpr std::size_t kMaxLineCharacters = std::size_t{1} << 20;

// The longest action that one line gives.
inline constexpr std::size_t kMaxActionCharacters = 12 * kMaxLineCharacters + 1024;

struct CrucibleDiagnostic {
    ::fixy::GrowText file = ::fixy::mint_grow_text(kMaxLineCharacters);
    ::fixy::GrowText function = ::fixy::mint_grow_text(kMaxLineCharacters);
    ::fixy::GrowText error_code = ::fixy::mint_grow_text(kMaxLineCharacters);
    ::fixy::GrowText goal = ::fixy::mint_grow_text(kMaxLineCharacters);
    ::fixy::GrowText gap = ::fixy::mint_grow_text(kMaxLineCharacters);
    ::fixy::GrowText suggestion = ::fixy::mint_grow_text(kMaxLineCharacters);
    unsigned line = 0;
    unsigned column = 0;
};

namespace detail {

// Puts text after the last character of out.  The bounds of this header
// make each append fit.
inline void append_text(::foundation::core::Alloc allocation, ::fixy::GrowText& out, ::fixy::TextView text) noexcept {
    out.append(allocation, text).expect("the text fits the maximum of its owner");
}

// The characters of text after its first character.  The text is not empty.
[[nodiscard]] inline ::fixy::TextView rest_of(::fixy::TextView text) noexcept {
    return text.window(1, text.size() - 1).expect("a text that is not empty has a rest");
}

// The first character of text, which is not empty.
[[nodiscard]] inline char first_of(::fixy::TextView text) noexcept { return *text.begin(); }

// The characters of one JSON line, and the reads of its fields.
class JsonLine final {
    ::fixy::TextView json_{};

public:
    explicit JsonLine(::fixy::TextView json) noexcept : json_{json} {}

    // The characters after the colon that follows the first key, with its
    // quotes, or no value when the line holds no such key or no colon after
    // it.
    [[nodiscard]] ::fixy::Option<::fixy::TextView> value_of(::fixy::TextView key) const noexcept {
        using Value = ::fixy::Option<::fixy::TextView>;
        ::fixy::FixedText<64> needle{};
        bool const fits = needle.append('"').is_ok() && needle.append(key).is_ok() && needle.append('"').is_ok();
        if (!fits) return Value{::fixy::none};
        // A match of a position gives a position, and the window after it is
        // taken outside the match, so no result of a match holds a borrow.
        ::fixy::Option<std::size_t> key_pos = ::fixy::find(json_, needle.view());
        if (key_pos.is_none()) return Value{::fixy::none};
        std::size_t const key_at = std::move(key_pos).value_or(0);
        ::fixy::TextView const from_key =
            json_.window(key_at, json_.size() - key_at).expect("the key is inside the line");
        ::fixy::Option<std::size_t> colon = ::fixy::find(from_key, ':');
        if (colon.is_none()) return Value{::fixy::none};
        std::size_t const colon_at = std::move(colon).value_or(0);
        return from_key.window(colon_at + 1, from_key.size() - colon_at - 1);
    }

    // The decoded string of the first key, put into out.  A key that the
    // line does not hold, or that has no string, puts nothing.  A string
    // with no closing quote puts the characters up to the end of the line.
    void extract_json_string(::foundation::core::Alloc allocation, ::fixy::TextView key,
                             ::fixy::GrowText& out) const noexcept {
        ::fixy::TextView rest{};
        bool has_string = false;
        value_of(key).match(
            [&](::fixy::TextView value) noexcept {
                ::fixy::find(value, '"')
                    .match(
                        [&](std::size_t quote) noexcept {
                            rest =
                                value.window(quote + 1, value.size() - quote - 1).expect("the quote is in the value");
                            has_string = true;
                        },
                        []() noexcept {});
            },
            []() noexcept {});
        if (!has_string) return;
        while (rest.size() > 0) {
            char const c = first_of(rest);
            rest = rest_of(rest);
            if (c == '"') return;
            if (c != '\\') {
                out.append(allocation, c).expect("the character fits the maximum of its owner");
                continue;
            }
            if (rest.size() == 0) break;
            char const escaped = first_of(rest);
            rest = rest_of(rest);
            switch (escaped) {
                case 'b':
                    append_text(allocation, out, ::fixy::TextView{"\b"});
                    break;
                case 'f':
                    append_text(allocation, out, ::fixy::TextView{"\f"});
                    break;
                case 'n':
                    append_text(allocation, out, ::fixy::TextView{"\n"});
                    break;
                case 'r':
                    append_text(allocation, out, ::fixy::TextView{"\r"});
                    break;
                case 't':
                    append_text(allocation, out, ::fixy::TextView{"\t"});
                    break;
                case 'u': {
                    // The four digits of the code point give one '?'.
                    std::size_t const skipped = ::fixy::min(rest.size(), std::size_t{4});
                    rest = rest.window(skipped, rest.size() - skipped).expect("the digits are in the rest");
                    append_text(allocation, out, ::fixy::TextView{"?"});
                    break;
                }
                default:
                    // A quote, a backslash, a slash and each other character
                    // stand for themselves.
                    out.append(allocation, escaped).expect("the character fits the maximum of its owner");
                    break;
            }
        }
    }

    // The unsigned number of the first key, or 0 when the line holds no
    // such key, or no digits after its colon and its spaces, or a number
    // that does not fit.
    [[nodiscard]] unsigned extract_json_uint(::fixy::TextView key) const noexcept {
        return value_of(key).match(
            [](::fixy::TextView value) noexcept {
                ::fixy::TextView rest = value;
                while (rest.size() > 0 && first_of(rest) == ' ')
                    rest = rest_of(rest);
                std::size_t count = 0;
                for (char const c : rest) {
                    if (c < '0' || c > '9') break;
                    ++count;
                }
                ::fixy::TextView const digits = rest.window(0, count).expect("the digits are a window of the rest");
                return ::fixy::parse<unsigned>(digits).match([](unsigned number) noexcept { return number; },
                                                             [](::fixy::ParseError) noexcept { return 0U; });
            },
            []() noexcept { return 0U; });
    }
};

// Puts s after the last character of out as a JSON string: in quotes, with
// an escape for each quote, backslash and named control character, and with
// no other control character.
inline void append_json_string(::foundation::core::Alloc allocation, ::fixy::GrowText& out,
                               ::fixy::TextView s) noexcept {
    append_text(allocation, out, ::fixy::TextView{"\""});
    for (char const raw : s) {
        auto const c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"':
                append_text(allocation, out, ::fixy::TextView{"\\\""});
                break;
            case '\\':
                append_text(allocation, out, ::fixy::TextView{"\\\\"});
                break;
            case '\b':
                append_text(allocation, out, ::fixy::TextView{"\\b"});
                break;
            case '\f':
                append_text(allocation, out, ::fixy::TextView{"\\f"});
                break;
            case '\n':
                append_text(allocation, out, ::fixy::TextView{"\\n"});
                break;
            case '\r':
                append_text(allocation, out, ::fixy::TextView{"\\r"});
                break;
            case '\t':
                append_text(allocation, out, ::fixy::TextView{"\\t"});
                break;
            default:
                if (c >= 0x20) out.append(allocation, raw).expect("the character fits the maximum of its owner");
                break;
        }
    }
    append_text(allocation, out, ::fixy::TextView{"\""});
}

// Puts the JSON string of a field after the last character of out.
inline void append_json_field(::foundation::core::Alloc allocation, ::fixy::GrowText& out,
                              ::fixy::GrowText const& field) noexcept {
    field.with_view([&](::fixy::TextView text) noexcept { append_json_string(allocation, out, text); });
}

// Puts the title of the action after the last character of out: the
// suggestion, or the error code when there is no suggestion, cut to 96
// characters.
inline void append_title(::foundation::core::Alloc allocation, ::fixy::GrowText& out,
                         CrucibleDiagnostic const& d) noexcept {
    constexpr std::size_t max_title_payload = 96;
    ::fixy::GrowText const& source = d.suggestion.size() == 0 ? d.error_code : d.suggestion;
    append_text(allocation, out, ::fixy::TextView{"Crucible: "});
    source.with_view([&](::fixy::TextView text) noexcept {
        std::size_t const count = ::fixy::min(text.size(), max_title_payload);
        append_text(allocation, out, text.window(0, count).expect("the payload is a window of the source"));
    });
}

}  // namespace detail

inline void parse_diagnostic_json(::foundation::core::Alloc allocation, ::fixy::TextView json,
                                  CrucibleDiagnostic& d) noexcept {
    detail::JsonLine const line{json};
    line.extract_json_string(allocation, ::fixy::TextView{"file"}, d.file);
    line.extract_json_string(allocation, ::fixy::TextView{"function"}, d.function);
    line.extract_json_string(allocation, ::fixy::TextView{"error_code"}, d.error_code);
    line.extract_json_string(allocation, ::fixy::TextView{"goal"}, d.goal);
    line.extract_json_string(allocation, ::fixy::TextView{"gap"}, d.gap);
    line.extract_json_string(allocation, ::fixy::TextView{"suggestion"}, d.suggestion);
    d.line = line.extract_json_uint(::fixy::TextView{"line"});
    d.column = line.extract_json_uint(::fixy::TextView{"column"});
}

// Puts the LSP CodeAction of one diagnostic line after the last character
// of out.
inline void append_lsp_code_action(::foundation::core::Alloc allocation, ::fixy::TextView diagnostic_json,
                                   ::fixy::GrowText& out) noexcept {
    CrucibleDiagnostic d{};
    parse_diagnostic_json(allocation, diagnostic_json, d);
    const unsigned lsp_line = d.line == 0 ? 0 : d.line - 1;
    const unsigned lsp_column = d.column == 0 ? 0 : d.column - 1;
    ::fixy::GrowText title = ::fixy::mint_grow_text(kMaxLineCharacters);
    detail::append_title(allocation, title, d);

    detail::append_text(allocation, out, ::fixy::TextView{"{\"title\":"});
    detail::append_json_field(allocation, out, title);
    detail::append_text(allocation, out,
                        ::fixy::TextView{",\"kind\":\"quickfix\",\"diagnostics\":[{\"source\":\"crucible\","});
    detail::append_text(allocation, out, ::fixy::TextView{"\"code\":"});
    detail::append_json_field(allocation, out, d.error_code);
    detail::append_text(allocation, out, ::fixy::TextView{",\"message\":"});
    detail::append_json_field(allocation, out, d.gap);
    ::fixy::FixedText<128> range{};
    detail::append_text(allocation, out,
                        ::fixy::format(range,
                                       ",\"range\":{{\"start\":{{\"line\":{},\"character\":{}}},\"end\":{{\"line\":{},"
                                       "\"character\":{}}}}}}}],\"data\":{{\"file\":",
                                       lsp_line, lsp_column, lsp_line, lsp_column)
                            .expect("the range of four numbers fits 128 characters"));
    detail::append_json_field(allocation, out, d.file);
    detail::append_text(allocation, out, ::fixy::TextView{",\"function\":"});
    detail::append_json_field(allocation, out, d.function);
    detail::append_text(allocation, out, ::fixy::TextView{",\"goal\":"});
    detail::append_json_field(allocation, out, d.goal);
    detail::append_text(allocation, out, ::fixy::TextView{",\"suggestion\":"});
    detail::append_json_field(allocation, out, d.suggestion);
    detail::append_text(allocation, out, ::fixy::TextView{"}}"});
}

// Writes the LSP CodeAction of one diagnostic line and a newline to
// standard output.
inline void write_lsp_code_action(::foundation::core::Alloc allocation, ::fixy::TextView diagnostic_json) noexcept {
    ::fixy::GrowText action = ::fixy::mint_grow_text(kMaxActionCharacters);
    append_lsp_code_action(allocation, diagnostic_json, action);
    action.with_view([](::fixy::TextView text) noexcept { ::fixy::report(::fixy::Sink::Out, "{}\n", text); });
}

}  // namespace crucible::tools::lsp_code_action
