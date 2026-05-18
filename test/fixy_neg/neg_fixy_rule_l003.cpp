// neg_fixy_rule_l003 — R009 = L003_BorrowUnscopedSpawn
//
// Borrow capture × unscoped spawn — captures cannot outlive their
// scope.  Usage::Borrow via gr::borrow, marks_unscoped_spawn flipped.

#include "_fixy_neg_rule_pack.h"

using namespace fixy_neg_rule_detail;

struct TypeL003 {};

namespace probe {
    using F = sfn::Fn<TypeL003,
        sfn::pred::True, sfn::UsageMode::Borrow>;
}

namespace crucible::safety::fn::collision {
template <> struct marks_unscoped_spawn<probe::F> : std::true_type {};
}  // namespace crucible::safety::fn::collision

using Witness = fixy::fn<TypeL003,
    strict<D::Refinement>, gr::borrow,          strict<D::Effect>,
    strict<D::Security>,   strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>, strict<D::Trust>,    strict<D::Representation>,
    strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
    strict<D::Space>,      strict<D::Overflow>, strict<D::Mutation>,
    strict<D::Reentrancy>, strict<D::Size>,     strict<D::Version>,
    strict<D::Staleness>, strict<D::Synchronization>>;

int main() { return static_cast<int>(sizeof(Witness)); }
