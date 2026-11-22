// Negative-compile test.  This file must fail to compile.
//
// build_trace_from writes the scratch buffers of the build stage.  A caller
// outside the pipeline gives the proof that no pipeline runs
// (prove_quiescence), so a build with no proof does not compile, and a build
// while a stage writes the scratch ends the process at the proof.

#include <crucible/BackgroundThread.h>

#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

int main() {
    crucible::BackgroundThread background{::foundation::effects::testing::test().alloc};
    ::fixy::BumpArena scratch{size_t{1} << 16};
    auto built = background.build_trace_from(::foundation::effects::testing::test().alloc, scratch, {}, {}, {}, {}, {},
                                             background.numerics());
    return built.is_ok() ? 0 : 1;
}
