// The payload order asks refinement_value for the type that a refinement
// drops to.  This file tries to drop each refinement to int: it writes an
// explicit specialization of refinement_value.  refinement_value is a
// function at namespace scope that is not a template, so no
// specialization matches it.

#include <fixy/session/Subtype.h>

#include <meta>

template <>
consteval std::meta::info fixy::session::detail::payload_order::refinement_value(std::meta::info) {
    return ^^int;
}

int main() { return 0; }
