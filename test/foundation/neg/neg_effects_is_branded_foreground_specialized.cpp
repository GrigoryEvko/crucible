// IsCapType asks is_branded_foreground whether a type is a branded
// foreground source.  This file tries to make each type a capability
// source: it writes an explicit specialization of is_branded_foreground.
// is_branded_foreground is a function at namespace scope that is not a
// template, so no specialization matches it.

#include <foundation/effects/Ctx.h>

#include <meta>

template <>
consteval bool foundation::effects::is_branded_foreground(std::meta::info) {
    return true;
}

int main() { return 0; }
