// ── test_fixy_arg_promote — FIXY-G5 positive test ─────────────────────
//
// Exercise mint_arg + call_typed against a function-pointer-typed
// binding:
//   * Identity promote: raw int → int (no wrapper, no conversion).
//   * Convertible promote: int → long via std::is_constructible.
//   * call_typed forwards mint_arg-wrapped args to the underlying
//     callable.

#include <crucible/fixy/Fixy.h>

#include <cstdio>
#include <type_traits>

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

// Compile-time invariants.
static_assert(std::is_same_v<cf::arg_grade_t<AdderFn, 0>, int>);
static_assert(std::is_same_v<cf::arg_grade_t<AdderFn, 1>, int>);
static_assert(cf::ArgPromotable<AdderFn, 0, int>);
static_assert(cf::ArgPromotable<AdderFn, 0, long>);  // int<-long constructible
static_assert(cf::ArgPromotable<AdderFn, 1, short>); // int<-short constructible

}  // namespace

int main() {
    AdderFn bound{&adder};

    // Identity promote.
    int r1 = cf::mint_arg<AdderFn, 0>(42);
    if (r1 != 42) { std::fprintf(stderr, "mint_arg<0>(42) -> %d\n", r1); return 1; }

    // Convertible promote (long → int).
    int r2 = cf::mint_arg<AdderFn, 0>(static_cast<long>(7));
    if (r2 != 7) { std::fprintf(stderr, "mint_arg<0>(long 7) -> %d\n", r2); return 2; }

    // call_typed end-to-end — pass shorts, get them promoted into ints.
    int r3 = cf::call_typed<AdderFn>(bound, short{5}, short{6});
    if (r3 != 11) { std::fprintf(stderr, "call_typed(5,6) -> %d\n", r3); return 3; }

    std::fputs("test_fixy_arg_promote: OK\n", stdout);
    return 0;
}
