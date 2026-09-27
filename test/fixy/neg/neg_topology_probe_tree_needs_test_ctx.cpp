// NEGATIVE-COMPILE TEST.  This file must not compile.
//
// Topology::probe_tree reads a sysfs tree under another root.  It is a
// door for a test that builds the tree it wants, so only a context that
// owns the Test capability reaches it.  A background context owns Bg and
// no Test, and is refused.

#include <fixy/concurrent/Topology.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace eff = foundation::effects;

int main() {
    const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> ctx{eff::testing::bg()};
    [[maybe_unused]] const auto topo = fixy::concurrent::Topology::probe_tree(ctx, "/tmp");
    return 0;
}
