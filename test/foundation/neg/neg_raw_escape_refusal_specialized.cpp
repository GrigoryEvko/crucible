// RawEscapesSanctioned asks raw_escape_refusal_text for the refusal of a
// type.  This file tries to sanction every type: it writes an explicit
// specialization of raw_escape_refusal_text.  raw_escape_refusal_text is
// a function at namespace scope that is not a template, so no
// specialization matches it.

#include <foundation/reflect/RawEscape.h>

#include <meta>
#include <string_view>

template <>
consteval std::string_view foundation::reflect::detail::raw_escape_refusal_text(std::meta::info) {
    return "";
}

int main() { return 0; }
