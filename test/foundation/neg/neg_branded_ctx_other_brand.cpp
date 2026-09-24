// A gate of one brand handed the foreground context of another brand.
// Each context came from the claim of its own state, so both callers hold
// a producer claim, but only the claim of the gate's own state passes.

#include <foundation/effects/Ctx.h>

namespace {
namespace fe = ::foundation::effects;

struct Guarded {
    fe::host::ProducerClaim<Guarded> claim;
};
struct Stranger {
    fe::host::ProducerClaim<Stranger> claim;
};

void gate_of_guarded(const fe::ExecCtx<fe::ctx_cap::BrandedFg<Guarded>, fe::Row<>>&) noexcept {}
}  // namespace

int main() {
    Stranger stranger;
    gate_of_guarded(stranger.claim.mint_producer_context());
    return 0;
}
