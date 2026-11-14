// The owner of the atoms builds Alloc, IO and Block, and no other type.  Its
// constraint refuses a context, so a holder of an owner does not build a
// background context through it.

#include <foundation/effects/Effect.h>

namespace {

[[maybe_unused]] void probe(::foundation::effects::host::AtomOwner const& owner) {
    static_cast<void>(owner.mint_atom<::foundation::effects::Bg>());
}

}  // namespace

int main() { return 0; }
