// neg_fixy_rule_i003 — R004 = I003_ConstantTimeFailOnSecret
//
// CT × Fail × fail-on-secret.  Three markers fire together; we also
// flip marks_fail_error_secret to sidestep I002 (classified default +
// fail without secret-error payload), keeping the focused diagnostic
// on I003.

#include "_fixy_neg_rule_pack.h"

using namespace fixy_neg_rule_detail;

struct TypeI003 {};

namespace probe { using F = sfn::Fn<TypeI003>; }

namespace crucible::safety::fn::collision {
template <> struct marks_ct<probe::F>                 : std::true_type {};
template <> struct marks_fail<probe::F>               : std::true_type {};
template <> struct marks_fail_on_secret<probe::F>     : std::true_type {};
template <> struct marks_fail_error_secret<probe::F>  : std::true_type {};
}  // namespace crucible::safety::fn::collision

using Witness = fixy::fn<TypeI003,
    strict<D::Refinement>, strict<D::Usage>,    strict<D::Effect>,
    strict<D::Security>,   strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>, strict<D::Trust>,    strict<D::Representation>,
    strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
    strict<D::Space>,      strict<D::Overflow>, strict<D::Mutation>,
    strict<D::Reentrancy>, strict<D::Size>,     strict<D::Version>,
    strict<D::Staleness>>;

int main() { return static_cast<int>(sizeof(Witness)); }
