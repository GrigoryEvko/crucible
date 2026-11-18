// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A wait policy that parks spins for its budget first, and the spin reads the
// counter of the core, a source of nondeterminism.  A background context that
// owns Block and no Nondet can block, but it cannot read the counter, so
// wait_for refuses the policy SpinThenPark and names the gate.
//
// Expected diagnostic: no candidate of wait_for, with a note on the gate of
// the wait policy.

#include <fixy/concurrent/EventCount.h>
#include <foundation/effects/Ctx.h>

namespace {

namespace eff = ::foundation::effects;

using BlockOnlyCtx =
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

}  // namespace

int main() {
    BlockOnlyCtx const ctx{eff::testing::bg()};
    ::fixy::concurrent::EventCount events{};
    ::fixy::concurrent::wait_for<::fixy::concurrent::SpinThenPark<1000>>(ctx, events, []() noexcept { return true; });
    return 0;
}
