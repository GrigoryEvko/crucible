// neg_fixy_rule_l002 — R002 = L002_BorrowAsync
//
// Borrow × Async cannot bridge an await point.

#include "_fixy_neg_rule_pack.h"

using namespace fixy_neg_rule_detail;

struct TypeL002 {};

namespace probe {
    using F = sfn::Fn<TypeL002,
        sfn::pred::True, sfn::UsageMode::Borrow>;
}

namespace crucible::safety::fn::collision {
template <> struct marks_async<probe::F> : std::true_type {};
}  // namespace crucible::safety::fn::collision

using Witness = fixy::fn<TypeL002,
    strict<D::Refinement>, gr::borrow,        strict<D::Effect>,
    strict<D::Security>,   strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>, strict<D::Trust>,    strict<D::Representation>,
    strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
    strict<D::Space>,      strict<D::Overflow>, strict<D::Mutation>,
    strict<D::Reentrancy>, strict<D::Size>,     strict<D::Version>,
    strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>>;

int main() { return static_cast<int>(sizeof(Witness)); }
