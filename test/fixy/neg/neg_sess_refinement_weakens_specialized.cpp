// The payload order asks refinement_weakens whether one refinement weakens
// to another.  This file tries to weaken every refinement to every other:
// it writes an explicit specialization of refinement_weakens.
// refinement_weakens is a function at namespace scope that is not a
// template, so no specialization matches it.

#include <fixy/session/Subtype.h>

#include <meta>

template <>
consteval bool fixy::session::detail::payload_order::refinement_weakens(std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
