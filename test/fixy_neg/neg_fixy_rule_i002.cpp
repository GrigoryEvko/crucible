// neg_fixy_rule_i002 — R001 = I002_ClassifiedFailPayload
//
// classified × Fail(E) with non-secret error payload.  All-default
// Fn<TypeI002> has SecLevel::Classified.  Specialising marks_fail<F> =
// true (without flipping marks_fail_error_secret) tips classified_v ∧
// has_fail_v ∧ !fail_error_secret_v → I002 fires inside validate().
//
// Per-fixture TypeI002 carrier — keeps the resolved Fn<TypeI002>
// instantiation distinct from safety/Fn.h's `Fn<int>` self-test so
// the marker specialisation lands before primary instantiation.

#include "_fixy_neg_rule_pack.h"

using namespace fixy_neg_rule_detail;

struct TypeI002 {};

namespace probe { using F = sfn::Fn<TypeI002>; }

namespace crucible::safety::fn::collision {
template <> struct marks_fail<probe::F> : std::true_type {};
}  // namespace crucible::safety::fn::collision

using Witness = fixy::fn<TypeI002,
    strict<D::Refinement>, strict<D::Usage>,    strict<D::Effect>,
    strict<D::Security>,   strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>, strict<D::Trust>,    strict<D::Representation>,
    strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
    strict<D::Space>,      strict<D::Overflow>, strict<D::Mutation>,
    strict<D::Reentrancy>, strict<D::Size>,     strict<D::Version>,
    strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>>;

int main() { return static_cast<int>(sizeof(Witness)); }
