// W001: hot x a kernel wait, stated as Block on the Effect axis.
//
// W001 reads the row that the whole pack lifts to.  This pack names no
// wait strategy and no system call.  It states with<Block>, and that puts
// Block in the row.  A body that states Block can wait in the kernel.
// The scheduler bounds that wait at 1-5 us, against a hot budget of 10-40
// ns.
//
// The cost and refinement atoms are in the pack so that the fixture trips
// W001 alone.  The row holds Block and nothing else, so H003, which reads
// Alloc and IO, stands down.

#include <fixy/Fn.h>

#include <foundation/effects/Effect.h>

// The tag has external linkage.  An atom whose argument has internal
// linkage has no stable identity, and the gate refuses it at tier 2.
namespace fixture {

struct ring_depth_proved final {};

}  // namespace fixture

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::regime::hot,
                                ::fixy::atom::with<::foundation::effects::Effect::Block>, ::fixy::atom::cost_constant,
                                ::fixy::atom::refined_with<fixture::ring_depth_proved>> refused{};
    return 0;
}
