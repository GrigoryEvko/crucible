// fixy_neg: direct fixy::fn<T, ...> value construction is rejected —
// closes fixy-A4-018.
//
// HS14 fixture 1/2.  Pre-A4-018, `fixy::fn<int, ...>::fn(Type v)` was
// public, allowing `fixy::fn<int, all-strict-pack>{42}` to construct a
// value-carrying binding WITHOUT routing through `mint_fn` — diluting
// the §XXI "single grep target" discipline.  Reviewers grep-scanning
// for `mint_fn` would miss every direct-ctor binding.
//
// Tightened form: `fn(Type v)` is private.  Only the three §XXI mint
// factories (`mint_fn`, `mint_fn_for<UnaryStance>`,
// `mint_fn_for<BinaryStance>`) are friended to construct.  Direct
// construction at any other call site fails with a "private within
// this context" diagnostic.
//
// Distinct from neg_fixy_fn_stance_ctor_bypass.cpp: that fixture
// witnesses bypass via a STANCE ALIAS spelling
// (`fixy::stance::PureLinear<int>{42}`); this fixture witnesses
// bypass via the RAW `fixy::fn<T, all-strict-grants>{42}` spelling.
// Both flow through the SAME private-ctor rejection but cover
// orthogonal call-site shapes the reviewer might see in production.
//
// Expected diagnostic: "private" (gcc emits "is private within this
// context" or "declared private here").

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // Direct value construction with all-strict-pack — must reject.
    // The Grants pack is well-formed (all strict) so tier-2 and
    // tier-3 of fn's class-body static_assert chain pass; the
    // failure is purely the private-ctor inaccessibility.
    auto bad = fixy::fn<int,
        strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
        strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
        strict<D::Provenance>, strict<D::Trust>,
        strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
        strict<D::Synchronization>, strict<D::Regime>>{42};
    (void)bad;
    return 0;
}
