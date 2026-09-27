// A deliberate leak needs a leak atom, and IsLeakAtom says which types
// are one.  This file tries to add a class of its own to that set.  It
// specializes a variable template of the name a trait of the leak atom
// would have, because a gate that reads a trait admits each class that a
// specialization marks.  IsLeakAtom is one reflection query inside the
// concept and reads no trait, so the specialization has nothing to name.

#include <fixy/atoms/Os.h>

namespace {

struct Fake final {};

}  // namespace

template <>
inline constexpr bool fixy::atom::is_leak_atom_v<Fake> = true;

int main() {
    return 0;
}
