// neg_fixy_rule_i004 — R007 = I004_ClassifiedAsyncSession
//
// classified × async × session_protocol × !ct.  Push Protocol off its
// strict default to a non-None protocol so session_protocol_v is true.

#include "_fixy_neg_rule_pack.h"

using namespace fixy_neg_rule_detail;

struct TypeI004 {};

// Minimal protocol marker — non-None so session_protocol_v is true.
struct ToyProto {};

namespace probe {
    using F = sfn::Fn<TypeI004,
        sfn::pred::True, sfn::UsageMode::Linear,
        eff::Row<>, sfn::SecLevel::Classified,
        ToyProto>;
}

namespace crucible::safety::fn::collision {
template <> struct marks_async<probe::F> : std::true_type {};
}  // namespace crucible::safety::fn::collision

using Witness = fixy::fn<TypeI004,
    strict<D::Refinement>, strict<D::Usage>,    strict<D::Effect>,
    strict<D::Security>,
    gr::protocol<ToyProto>,                     // Protocol ≠ None
    strict<D::Lifetime>,
    strict<D::Provenance>, strict<D::Trust>,    strict<D::Representation>,
    strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
    strict<D::Space>,      strict<D::Overflow>, strict<D::Mutation>,
    strict<D::Reentrancy>, strict<D::Size>,     strict<D::Version>,
    strict<D::Staleness>>;

int main() { return static_cast<int>(sizeof(Witness)); }
