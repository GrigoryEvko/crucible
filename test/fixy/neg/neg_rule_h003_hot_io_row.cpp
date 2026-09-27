// H003: hot x IO in the row of the binding.
//
// The binding states IO on the Effect axis and a constant cost.  An I/O
// call enters the kernel, and the kernel does not bound a call in
// nanoseconds, so H003 refuses the binding whatever cost it states.
//
// The cost and refinement atoms are in the pack so that the fixture trips
// H003 alone.  as_public is in the pack so that the corpus entry
// classified_io_without_declassify has no classified value to refuse.
// The row holds IO without Block, so W001 stands down.

#include <fixy/Fn.h>

#include <foundation/effects/Effect.h>

// The tag has external linkage.  An atom whose argument has internal
// linkage has no stable identity, and the gate refuses it at tier 2.
namespace fixture {

struct record_size_proved final {};

}  // namespace fixture

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::regime::hot,
                                ::fixy::atom::with<::foundation::effects::Effect::IO>, ::fixy::atom::as_public,
                                ::fixy::atom::cost_constant, ::fixy::atom::refined_with<fixture::record_size_proved>>
        refused{};
    return 0;
}
