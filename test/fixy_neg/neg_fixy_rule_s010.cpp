// neg_fixy_rule_s010 — R011 = S010_StalenessConstantTime
//
// CT × Staleness != Fresh — runtime freshness checks defeat CT.  Push
// Staleness off Fresh via gr::stale_to<TauMax>; specialise marks_ct.

#include "_fixy_neg_rule_pack.h"

using namespace fixy_neg_rule_detail;

struct TypeS010 {};

namespace probe {
    using F = sfn::Fn<TypeS010,
        sfn::pred::True, sfn::UsageMode::Linear,
        eff::Row<>, sfn::SecLevel::Classified,
        sfn::proto::None, sfn::lifetime::Static,
        crucible::safety::source::FromInternal,
        crucible::safety::trust::Verified,
        sfn::ReprKind::Opaque,
        sfn::cost::Unstated, sfn::precision::Exact,
        sfn::space::Zero, sfn::OverflowMode::Trap,
        sfn::MutationMode::Immutable, sfn::ReentrancyMode::NonReentrant,
        sfn::size_pol::Unstated, 1u,
        sfn::stale::Stale<10>>;
}

namespace crucible::safety::fn::collision {
template <> struct marks_ct<probe::F> : std::true_type {};
}  // namespace crucible::safety::fn::collision

using Witness = fixy::fn<TypeS010,
    strict<D::Refinement>, strict<D::Usage>,    strict<D::Effect>,
    strict<D::Security>,   strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>, strict<D::Trust>,    strict<D::Representation>,
    strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
    strict<D::Space>,      strict<D::Overflow>, strict<D::Mutation>,
    strict<D::Reentrancy>, strict<D::Size>,     strict<D::Version>,
    gr::stale_to<10>>;                          // Staleness ≠ Fresh

int main() { return static_cast<int>(sizeof(Witness)); }
