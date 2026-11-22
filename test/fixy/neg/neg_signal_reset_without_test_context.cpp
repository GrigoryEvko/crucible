// Negative compile test.  This file must fail to compile.
//
// reset_to_default changes a signal disposition in a child of the child
// door, and only a test calls it (the child reset of fixy/os/Signal.h).  A
// background context owns no Test capability, so the concept
// CtxFitsChildProcess refuses it at the call, also with the proof of a child.
//
// Expected diagnostic: reset_to_default has no viable candidate, and the
// note names the concept.

#include <fixy/os/Process.h>
#include <fixy/os/Signal.h>
#include <foundation/effects/Ctx.h>

namespace eff = foundation::effects;

namespace {

using BgDrainCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

[[maybe_unused]] void reset_in_background(BgDrainCtx const& background, fixy::process::ForkedChild const& child) {
    fixy::signal::reset_to_default(background, child, fixy::signal::FatalSignal::segmentation_fault);
}

}  // namespace

int main() { return 0; }
