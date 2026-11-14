// An allocation tag built from nothing, under the name that the effect
// layer gives it.  The tag is an authority: its default constructor is
// private, and its one friend is the owner of the atoms, which only a
// context and extract_bare build.  So the spelling of the name changes
// nothing.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

int main() {
    auto numbers = ::foundation::core::mint_heap_array(::foundation::effects::cap::Alloc{}, 4, 1);
    return static_cast<int>(numbers.view().size());
}
