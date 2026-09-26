// A gate of one brand handed the foreground context of another brand.
// Each context came from the claim of its own state, so both callers hold
// a producer claim, but only the claim of the gate's own state passes.
//
// The brands are in a named namespace: a claim keys its brand by a stable
// identity, which a type with internal linkage does not have.

#include <foundation/effects/Ctx.h>

namespace other_brand_probe {
namespace fe = ::foundation::effects;

struct Guarded {
    fe::host::ProducerClaim<Guarded> claim;
};
struct Stranger {
    fe::host::ProducerClaim<Stranger> claim;
};

inline void gate_of_guarded(const fe::ExecCtx<fe::ctx_cap::BrandedFg<Guarded>, fe::Row<>>&) noexcept {}
}  // namespace other_brand_probe

int main() {
    other_brand_probe::Stranger stranger;
    other_brand_probe::gate_of_guarded(stranger.claim.mint_producer_context());
    return 0;
}
