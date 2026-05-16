// ── test_fixy_value_t — FIXY-G2 positive test ─────────────────────────
//
// Pin value_t<F> + is_value_aligned + .call(args...) ergonomics for:
//   * all-strict binding (value_t = int; sizeof matches)
//   * function-pointer binding (call() forwards arg pack to value())
//   * is_value_aligned holds for every shipped binding

#include <crucible/fixy/Fixy.h>

#include <cstdio>
#include <type_traits>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;

namespace {

// Plain-value binding.
using IntFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
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

// Function-pointer binding — supports .call(args...).
using IntAdderPtr = int(*)(int, int);

int adder_impl(int a, int b) noexcept { return a + b; }

using AdderFn = cf::fn<IntAdderPtr,
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

// ── Compile-time invariants ─────────────────────────────────────────

// value_t<IntFn> is byte-equivalent to int.
static_assert(cf::is_value_aligned<IntFn>);
static_assert(sizeof(cf::value_t<IntFn>) == sizeof(int));

// value_t<AdderFn> is byte-equivalent to the function pointer.
static_assert(cf::is_value_aligned<AdderFn>);
static_assert(sizeof(cf::value_t<AdderFn>) == sizeof(IntAdderPtr));

// fixy::fn EBO collapse still holds — no overhead from the .call()
// member added in Phase G2.
static_assert(sizeof(IntFn) == sizeof(int));
static_assert(sizeof(AdderFn) == sizeof(IntAdderPtr));

}  // namespace

int main() {
    AdderFn adder{&adder_impl};

    int via_call = adder.call(7, 35);
    if (via_call != 42) {
        std::fprintf(stderr, "call(7,35) returned %d, expected 42\n", via_call);
        return 1;
    }

    // Backwards-compat: .value()() direct invocation also works.
    int via_value = adder.value()(7, 35);
    if (via_value != 42) {
        std::fprintf(stderr, "value()(7,35) returned %d, expected 42\n", via_value);
        return 2;
    }

    std::fputs("test_fixy_value_t: OK\n", stdout);
    return 0;
}
