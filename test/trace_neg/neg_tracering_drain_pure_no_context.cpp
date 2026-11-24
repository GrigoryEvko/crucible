// Negative-compile test.  This file must fail to compile.
//
// A caller that holds the context of the drain stage of the background
// thread, whose row holds Bg and Alloc, calls TraceRing::drain_pure and names
// no row.  A gate whose row has a default admits such a caller, because the
// default row is empty whatever the row of the caller is.  The gate takes
// its row from the context, and no form with no context exists.
//
// Expected diagnostic: no drain_pure takes the output alone.

#include <crucible/TraceRing.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

#include <utility>

namespace eff = ::foundation::effects;

namespace {

void drain_from_an_allocating_caller(::fixy::BgDrainCtx const&, crucible::TraceRing& ring) {
    ::fixy::FixedArray<crucible::TraceRing::Entry, 1> out{};
    out.with_mut([&ring](auto run) noexcept -> void { (void)ring.drain_pure(std::move(run)); });
}

}  // namespace

int main() {
    const ::fixy::BgDrainCtx bg{eff::testing::bg()};
    crucible::TraceRing ring;
    drain_from_an_allocating_caller(bg, ring);
    return 0;
}
