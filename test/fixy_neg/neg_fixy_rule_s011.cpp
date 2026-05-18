// neg_fixy_rule_s011 — R012 = S011_CapabilityReplay
//
// Usage::Capability × marks_replay_required × !marks_replay_stable.
// Push Usage off its strict default via gr::capability_usage; specialise
// marks_replay_required while leaving marks_replay_stable at default
// false.

#include "_fixy_neg_rule_pack.h"

using namespace fixy_neg_rule_detail;

struct TypeS011 {};

namespace probe {
    using F = sfn::Fn<TypeS011,
        sfn::pred::True, sfn::UsageMode::Capability>;
}

namespace crucible::safety::fn::collision {
template <> struct marks_replay_required<probe::F> : std::true_type {};
}  // namespace crucible::safety::fn::collision

using Witness = fixy::fn<TypeS011,
    strict<D::Refinement>, gr::capability_usage, strict<D::Effect>,
    strict<D::Security>,   strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>, strict<D::Trust>,    strict<D::Representation>,
    strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
    strict<D::Space>,      strict<D::Overflow>, strict<D::Mutation>,
    strict<D::Reentrancy>, strict<D::Size>,     strict<D::Version>,
    strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>>;

int main() { return static_cast<int>(sizeof(Witness)); }
