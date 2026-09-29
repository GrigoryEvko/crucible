// H003: hot x Alloc in the row of the binding.
//
// The binding states a constant cost, and H003 refuses it all the same.
// An allocation on the hot path is refused with a bounded cost, because
// the allocator does not bound a call in nanoseconds.  CLAUDE.md VIII
// bans an allocation on the hot path: a hot path points into an arena or
// a memory plan that a cold path sized first.
//
// The cost and refinement atoms are in the pack so that the fixture trips
// H003 alone.  The row holds Alloc and nothing else, so W001 stands down,
// and no corpus entry reads Alloc on a classified binding.

#include <fixy/Fn.h>

#include <foundation/effects/Effect.h>

// The tag has external linkage.  An atom whose argument has internal
// linkage has no stable identity, and the gate refuses it at tier 2.
namespace fixture {

struct arena_capacity_proved final {};

}  // namespace fixture

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::regime::hot,
                                ::fixy::atom::with<::foundation::effects::Effect::Alloc>, ::fixy::atom::cost_constant,
                                ::fixy::atom::refined_with<fixture::arena_capacity_proved>> refused{};
    return 0;
}
