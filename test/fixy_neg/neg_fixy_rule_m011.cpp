// neg_fixy_rule_m011 — R010 = M011_LinearFailNoCleanup
//
// Usage::Linear × fail × uncleaned-fail.  Strict default Usage is
// Linear; we specialise marks_fail + marks_linear_uncleaned_fail.
// Also flip marks_fail_error_secret to sidestep I002 (which would
// otherwise also fire on Classified default + fail).

#include "_fixy_neg_rule_pack.h"

using namespace fixy_neg_rule_detail;

struct TypeM011 {};

namespace probe { using F = sfn::Fn<TypeM011>; }

namespace crucible::safety::fn::collision {
template <> struct marks_fail<probe::F>                  : std::true_type {};
template <> struct marks_linear_uncleaned_fail<probe::F> : std::true_type {};
template <> struct marks_fail_error_secret<probe::F>     : std::true_type {};
}  // namespace crucible::safety::fn::collision

using Witness = fixy::fn<TypeM011,
    strict<D::Refinement>, strict<D::Usage>,    strict<D::Effect>,
    strict<D::Security>,   strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>, strict<D::Trust>,    strict<D::Representation>,
    strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
    strict<D::Space>,      strict<D::Overflow>, strict<D::Mutation>,
    strict<D::Reentrancy>, strict<D::Size>,     strict<D::Version>,
    strict<D::Staleness>, strict<D::Synchronization>>;

int main() { return static_cast<int>(sizeof(Witness)); }
