// The witnessed mint reads the row of an execution context.  A capability
// source has no row, so it witnesses no claim, although the background
// source permits Bg.
//
// Expected diagnostic: mint_computation_in_ctx has no candidate, because
// the background source is not an execution context.

#include <foundation/effects/Computation.h>

int main() {
    namespace fe = ::foundation::effects;
    auto const source = fe::testing::bg();
    [[maybe_unused]] auto claimed = fe::Computation<fe::Row<>, int>::mint_computation_in_ctx<fe::Effect::Bg>(source, 1);
    return 0;
}
