// Every capability gate asks row_contains whether the row of a context
// holds an effect.  This file tries to give every row every effect: it
// writes an explicit specialization of row_contains.  row_contains is a
// function at namespace scope that is not a template, so no
// specialization matches it.

#include <foundation/effects/Row.h>

#include <meta>

template <>
consteval bool foundation::effects::row_contains(std::meta::info, foundation::effects::Effect) {
    return true;
}

int main() { return 0; }
