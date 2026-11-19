// The payload order asks refinement_drops whether a refinement drops to
// its value type.  This file tries to drop a sealed refinement too: it
// writes an explicit specialization of refinement_drops.  refinement_drops
// is a function at namespace scope that is not a template, so no
// specialization matches it.

#include <fixy/session/Subtype.h>

#include <meta>

template <>
consteval bool fixy::session::detail::payload_order::refinement_drops(std::meta::info) {
    return true;
}

int main() { return 0; }
