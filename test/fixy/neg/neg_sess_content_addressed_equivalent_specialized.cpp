// The payload order asks is_content_addressed_equivalent whether two
// payloads stand for the same content.  This file tries to make every pair
// of payloads equivalent: it writes an explicit specialization of
// is_content_addressed_equivalent.  The function stands at namespace scope
// and is not a template, so no specialization matches it.

#include <fixy/session/ContentAddressed.h>

#include <meta>

template <>
consteval bool fixy::session::is_content_addressed_equivalent(std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
