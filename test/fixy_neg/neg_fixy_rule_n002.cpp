// neg_fixy_rule_n002 — R008 = N002_DecimalOverflowWrap
//
// Decimal × OverflowMode::Wrap.  We mark int as an exact-decimal
// carrier via `is_exact_decimal<int>` and force OverflowMode::Wrap via
// gr::overflow_wrap.

#include "_fixy_neg_rule_pack.h"

using namespace fixy_neg_rule_detail;

struct TypeN002 {};

namespace crucible::safety::fn::collision {
template <> struct is_exact_decimal<TypeN002> : std::true_type {};
}  // namespace crucible::safety::fn::collision

using Witness = fixy::fn<TypeN002,
    strict<D::Refinement>, strict<D::Usage>,    strict<D::Effect>,
    strict<D::Security>,   strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>, strict<D::Trust>,    strict<D::Representation>,
    strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
    strict<D::Space>,
    gr::overflow_wrap,                          // Overflow = Wrap
    strict<D::Mutation>,
    strict<D::Reentrancy>, strict<D::Size>,     strict<D::Version>,
    strict<D::Staleness>>;

int main() { return static_cast<int>(sizeof(Witness)); }
