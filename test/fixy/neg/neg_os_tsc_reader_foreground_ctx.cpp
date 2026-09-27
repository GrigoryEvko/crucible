// A TSC value differs from machine to machine, so a read on the
// replay-bound foreground path makes replay diverge.  The TSC mint asks
// for the gate of the clock readers, which admits a context that owns Bg,
// Init or Test, and the foreground context owns none of them.

#include <fixy/Ctx.h>
#include <fixy/os/Time.h>

#include <utility>

namespace ml = foundation::algebra::lattices;

namespace {

using Pin = fixy::CpuPinned<ml::AffinityMask::single(0), fixy::PinningPosture::PinnedExplicit, int>;

// The pin arrives as a parameter.  Only mint_affinity builds one.
[[maybe_unused]] void attempt(fixy::HotFgCtx const& ctx, Pin&& pin) {
    [[maybe_unused]] auto reader = fixy::time::mint_tsc_reader<fixy::time::TscMode::Raw>(ctx, std::move(pin));
}

}  // namespace

int main() { return 0; }
