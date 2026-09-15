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
// Expected diagnostic: the private-ctor rejection named precisely —
// `crucible::fixy::fn<Type, Grants>::fn(Type) ... is private within
// this context`.  A bare "private" would also be satisfied by the
// tier-3 AllDimsEngaged failure that a short Grants pack produces,
// which is how this fixture came to assert two rejections while
// documenting one.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr = crucible::fixy::grant;
using D = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // Direct value construction with all-strict-pack — must reject.
    // The Grants pack is well-formed (all strict) so tier-2 and
    // tier-3 of fn's class-body static_assert chain pass; the
    // failure is purely the private-ctor inaccessibility.
    //
    // The pack engages all 32 grant-carrying axes.  `DimensionAxis`
    // holds 33 enumerators and `Type` is the `fn` value parameter
    // rather than a grant, so 32 is the full complement.  An
    // incomplete pack makes tier 3 (`AllDimsEngaged`) fire as well,
    // and the combined output still contains the word "private" —
    // so a short pack hides the second rejection behind the first
    // instead of reddening.  Keep this list exhaustive.
    auto bad =
        fixy::fn<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, strict<D::Security>,
                 strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                 strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                 strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>, strict<D::Size>,
                 strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
                 strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>,
                 strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>,
                 strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>{42};
    (void)bad;
    return 0;
}
