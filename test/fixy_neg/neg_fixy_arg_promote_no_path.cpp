// ── neg_fixy_arg_promote_no_path (FIXY-G5 HS14) ───────────────────────
//
// Promoting a raw `void*` to a typed `int` argument has no promote
// path; mint_arg's static_assert fires with the PromoteImpossible
// diagnostic naming F, N, and RawType.

#include <crucible/fixy/Fixy.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;

namespace {

using AdderPtr = int(*)(int, int);

int adder(int a, int b) noexcept { return a + b; }

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
    void* p = nullptr;
    // No promote path from void* to int — mint_arg's unsatisfied-concept
    // overload fires with PromoteImpossible<AdderFn, 0, void*>.
    auto r = cf::mint_arg<AdderFn, 0>(p);
    (void)r;
    return 0;
}
