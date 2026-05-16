// neg_fixy_rule_m012 — R005 = M012_MonotonicConcurrentNoAtomic
//
// Mutation::Monotonic × concurrent (effect row contains Bg) × Repr !=
// Atomic.  We push Mutation off its strict default via gr::mut_monotonic
// and the Effect row via gr::with<Effect::Bg>.  Repr stays at its
// strict default Opaque (≠ Atomic).

#include "_fixy_neg_rule_pack.h"

using namespace fixy_neg_rule_detail;

struct TypeM012 {};

using Witness = fixy::fn<TypeM012,
    strict<D::Refinement>, strict<D::Usage>,
    gr::with<eff::Effect::Bg>,                  // Effect row contains Bg
    strict<D::Security>,   strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>, strict<D::Trust>,    strict<D::Representation>,
    strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
    strict<D::Space>,      strict<D::Overflow>,
    gr::mut_monotonic,                          // Mutation = Monotonic
    strict<D::Reentrancy>, strict<D::Size>,     strict<D::Version>,
    strict<D::Staleness>>;

int main() { return static_cast<int>(sizeof(Witness)); }
