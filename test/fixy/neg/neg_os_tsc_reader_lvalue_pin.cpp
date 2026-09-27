// The TSC reader owns its pin, so the mint takes the pin from the caller.
// The mint took a forwarding reference, and an lvalue pin bound to it
// and was moved out, with no std::move at the call site.  The gate now
// refuses an lvalue pin, so the call site must name the move.

#include <fixy/os/Time.h>

namespace eff = foundation::effects;
namespace ml = foundation::algebra::lattices;

namespace {

using InitCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init>>;
using Pin = fixy::CpuPinned<ml::AffinityMask::single(0), fixy::PinningPosture::PinnedExplicit, int>;

// The pin arrives as a parameter.  Only mint_affinity builds one.
[[maybe_unused]] void attempt(InitCtx const& ctx, Pin& pin) {
    [[maybe_unused]] auto reader = fixy::time::mint_tsc_reader<fixy::time::TscMode::Raw>(ctx, pin);
}

}  // namespace

int main() { return 0; }
