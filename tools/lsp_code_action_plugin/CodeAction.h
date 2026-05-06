#pragma once

// Tiny JSON-diagnostic -> LSP CodeAction bridge.
//
// The input format is the single-line record emitted by
// safety/diag/JsonEmitter.h.  This header intentionally parses only the
// stable Crucible fields it consumes; it is not a general JSON library.

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>

namespace crucible::tools::lsp_code_action {

struct CrucibleDiagnostic {
    std::string file{};
    std::string function{};
    std::string error_code{};
    std::string goal{};
    std::string gap{};
    std::string suggestion{};
    unsigned line = 0;
    unsigned column = 0;
};

namespace detail {

[[nodiscard]] inline std::size_t key_position(std::string_view json,
                                              std::string_view key) {
    std::string needle;
    needle.reserve(key.size() + 2);
    needle.push_back('"');
    needle.append(key);
    needle.push_back('"');
    return json.find(needle);
}

[[nodiscard]] inline std::string extract_json_string(std::string_view json,
                                                     std::string_view key) {
    const std::size_t key_pos = key_position(json, key);
    if (key_pos == std::string_view::npos) return {};
    const std::size_t colon = json.find(':', key_pos);
    if (colon == std::string_view::npos) return {};
    const std::size_t begin_quote = json.find('"', colon + 1);
    if (begin_quote == std::string_view::npos) return {};

    std::string out;
    for (std::size_t i = begin_quote + 1; i < json.size(); ++i) {
        const char c = json[i];
        if (c == '"') return out;
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (++i >= json.size()) break;
        switch (json[i]) {
        case '"':  out.push_back('"');  break;
        case '\\': out.push_back('\\'); break;
        case '/':  out.push_back('/');  break;
        case 'b':  out.push_back('\b'); break;
        case 'f':  out.push_back('\f'); break;
        case 'n':  out.push_back('\n'); break;
        case 'r':  out.push_back('\r'); break;
        case 't':  out.push_back('\t'); break;
        case 'u':
            i = std::min(json.size(), i + 4);
            out.push_back('?');
            break;
        default:
            out.push_back(json[i]);
            break;
        }
    }
    return out;
}

[[nodiscard]] inline unsigned extract_json_uint(std::string_view json,
                                                std::string_view key) {
    const std::size_t key_pos = key_position(json, key);
    if (key_pos == std::string_view::npos) return 0;
    const std::size_t colon = json.find(':', key_pos);
    if (colon == std::string_view::npos) return 0;
    std::size_t begin = colon + 1;
    while (begin < json.size() && json[begin] == ' ') ++begin;
    std::size_t end = begin;
    while (end < json.size() && json[end] >= '0' && json[end] <= '9') {
        ++end;
    }
    unsigned value = 0;
    const auto [ptr, ec] =
        std::from_chars(json.data() + begin, json.data() + end, value);
    if (ec != std::errc{} || ptr != json.data() + end) return 0;
    return value;
}

inline void append_json_string(std::string& out, std::string_view s) {
    out.push_back('"');
    for (char raw : s) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c >= 0x20) out.push_back(static_cast<char>(c));
            break;
        }
    }
    out.push_back('"');
}

[[nodiscard]] inline std::string title_from(CrucibleDiagnostic const& d) {
    std::string title = "Crucible: ";
    std::string_view source =
        d.suggestion.empty() ? std::string_view{d.error_code}
                             : std::string_view{d.suggestion};
    constexpr std::size_t max_title_payload = 96;
    if (source.size() > max_title_payload) {
        source = source.substr(0, max_title_payload);
    }
    title.append(source);
    return title;
}

}  // namespace detail

[[nodiscard]] inline CrucibleDiagnostic parse_diagnostic_json(
    std::string_view json) {
    CrucibleDiagnostic d{};
    d.file = detail::extract_json_string(json, "file");
    d.function = detail::extract_json_string(json, "function");
    d.error_code = detail::extract_json_string(json, "error_code");
    d.goal = detail::extract_json_string(json, "goal");
    d.gap = detail::extract_json_string(json, "gap");
    d.suggestion = detail::extract_json_string(json, "suggestion");
    d.line = detail::extract_json_uint(json, "line");
    d.column = detail::extract_json_uint(json, "column");
    return d;
}

[[nodiscard]] inline std::string to_lsp_code_action(
    std::string_view diagnostic_json) {
    const CrucibleDiagnostic d = parse_diagnostic_json(diagnostic_json);
    const unsigned lsp_line = d.line == 0 ? 0 : d.line - 1;
    const unsigned lsp_column = d.column == 0 ? 0 : d.column - 1;
    const std::string title = detail::title_from(d);

    std::string out;
    out.reserve(512 + d.error_code.size() + d.gap.size()
                + d.suggestion.size() + d.file.size());
    out += "{\"title\":";
    detail::append_json_string(out, title);
    out += ",\"kind\":\"quickfix\",\"diagnostics\":[{\"source\":\"crucible\",";
    out += "\"code\":";
    detail::append_json_string(out, d.error_code);
    out += ",\"message\":";
    detail::append_json_string(out, d.gap);
    out += ",\"range\":{\"start\":{\"line\":";
    out += std::to_string(lsp_line);
    out += ",\"character\":";
    out += std::to_string(lsp_column);
    out += "},\"end\":{\"line\":";
    out += std::to_string(lsp_line);
    out += ",\"character\":";
    out += std::to_string(lsp_column);
    out += "}}}],\"data\":{\"file\":";
    detail::append_json_string(out, d.file);
    out += ",\"function\":";
    detail::append_json_string(out, d.function);
    out += ",\"goal\":";
    detail::append_json_string(out, d.goal);
    out += ",\"suggestion\":";
    detail::append_json_string(out, d.suggestion);
    out += "}}";
    return out;
}

}  // namespace crucible::tools::lsp_code_action
