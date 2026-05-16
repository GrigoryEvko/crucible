// neg_fixy_rule_e044 — R003 = E044_ConstantTimeAsync
//
// CT × Async — async scheduling defeats the constant-time guarantee.

#include "_fixy_neg_rule_pack.h"

using namespace fixy_neg_rule_detail;

struct TypeE044 {};

namespace probe { using F = sfn::Fn<TypeE044>; }

namespace crucible::safety::fn::collision {
template <> struct marks_ct<probe::F>    : std::true_type {};
template <> struct marks_async<probe::F> : std::true_type {};
}  // namespace crucible::safety::fn::collision

using Witness = fixy::fn<TypeE044,
    strict<D::Refinement>, strict<D::Usage>,    strict<D::Effect>,
    strict<D::Security>,   strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>, strict<D::Trust>,    strict<D::Representation>,
    strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
    strict<D::Space>,      strict<D::Overflow>, strict<D::Mutation>,
    strict<D::Reentrancy>, strict<D::Size>,     strict<D::Version>,
    strict<D::Staleness>>;

int main() { return static_cast<int>(sizeof(Witness)); }
