// IsEffectRow asks is_effect_row whether a type is a row.  This file tries
// to make each type a row: it writes an explicit specialization of
// is_effect_row.  is_effect_row is a function at namespace scope that is
// not a template, so no specialization matches it.

#include <foundation/effects/Row.h>

#include <meta>

template <>
consteval bool foundation::effects::is_effect_row(std::meta::info) {
    return true;
}

int main() { return 0; }
