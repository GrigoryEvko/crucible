// CtxAdmits and the permission gates ask is_subrow whether a row fits in
// the row of a context.  This file tries to fit every row in every
// context: it writes an explicit specialization of is_subrow.  is_subrow
// is a function at namespace scope that is not a template, so no
// specialization matches it.

#include <foundation/effects/Row.h>

#include <meta>

template <>
consteval bool foundation::effects::is_subrow(std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
