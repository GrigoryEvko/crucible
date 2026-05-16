// ── neg_fixy_call_args_mismatched_arity (FIXY-G2 HS14) ────────────────
//
// fixy::fn::call(args...) forwards to the underlying value's callable;
// passing the wrong arity is rejected by the compiler (substrate-level
// call-expression typecheck).

#include <crucible/fixy/Fixy.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;

namespace {

using AdderPtr = int(*)(int, int);

int two_arg_adder(int a, int b) noexcept { return a + b; }

using AdderFn = cf::fn<AdderPtr,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

int main() {
    AdderFn adder{&two_arg_adder};
    // Mismatched arity — passing one arg to a 2-arg function pointer.
    // The substrate's call-expression check produces a "no match" /
    // "cannot convert" / "mismatched" diagnostic; the neg-compile
    // driver regex matches any of them.
    return adder.call(1);
}
