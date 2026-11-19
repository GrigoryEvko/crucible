// The payload order asks tier_weakens whether a numerical tier weakens to
// another.  This file tries to make a loose tier stand for a bit-exact one:
// it writes an explicit specialization of tier_weakens.  tier_weakens is
// a function at namespace scope that is not a template, so no
// specialization matches it.

#include <fixy/session/Subtype.h>

#include <meta>

template <>
consteval bool fixy::session::detail::payload_order::tier_weakens(std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
