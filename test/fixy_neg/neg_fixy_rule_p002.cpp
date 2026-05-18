// neg_fixy_rule_p002 — R006 = P002_GhostRuntimeUse
//
// Usage::Ghost × marks_runtime_ghost_use.  Ghost values are erased and
// cannot drive runtime branches.

#include "_fixy_neg_rule_pack.h"

using namespace fixy_neg_rule_detail;

struct TypeP002 {};

namespace probe {
    using F = sfn::Fn<TypeP002,
        sfn::pred::True, sfn::UsageMode::Ghost>;
}

namespace crucible::safety::fn::collision {
template <> struct marks_runtime_ghost_use<probe::F> : std::true_type {};
}  // namespace crucible::safety::fn::collision

using Witness = fixy::fn<TypeP002,
    strict<D::Refinement>, gr::ghost,           strict<D::Effect>,
    strict<D::Security>,   strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>, strict<D::Trust>,    strict<D::Representation>,
    strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
    strict<D::Space>,      strict<D::Overflow>, strict<D::Mutation>,
    strict<D::Reentrancy>, strict<D::Size>,     strict<D::Version>,
    strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>>;

int main() { return static_cast<int>(sizeof(Witness)); }
