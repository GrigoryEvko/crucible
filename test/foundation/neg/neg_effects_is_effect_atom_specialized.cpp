// IsEffect asks is_effect_atom whether a value of the Effect type is one
// of the enumerators.  This file tries to admit a value that no enumerator
// holds: it writes an explicit specialization of is_effect_atom.
// is_effect_atom is a function at namespace scope that is not a template,
// so no specialization matches it.

#include <foundation/effects/Effect.h>

template <>
consteval bool foundation::effects::is_effect_atom(foundation::effects::Effect) {
    return true;
}

int main() { return 0; }
