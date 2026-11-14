// The owner of the atoms builds Alloc, IO and Block, and no other type.  Its
// constraint refuses an integer, so a holder of an owner gets no other
// value through it.

#include <foundation/effects/Effect.h>

namespace {

[[maybe_unused]] int probe(::foundation::effects::host::AtomOwner const& owner) { return owner.mint_atom<int>(); }

}  // namespace

int main() { return 0; }
