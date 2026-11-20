// Two checks of the base hold under the ignore semantic.  The build gives
// this file the ignore semantic, where a CRUCIBLE_PRE or a CRUCIBLE_POST is
// only an assumption for the optimizer.  A false assumption is undefined
// behavior.  Each check below guards a fact that data or a caller can break
// at run time, so it is a fatal invariant:
//   - Lazy::get refuses a read before the build of the value.
//   - FractionalLattice refuses a sum or a product whose reduced
//     denominator is above the bound.
// Each violation runs in a child process through expect_dies of the Check
// family, which requires SIGABRT and the text of the failed invariant.  The
// live window of PermissionedMetaLog has its test in
// test_metalog_session_ignore, which builds test_metalog_session.cpp with the
// ignore semantic.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/handle/Once.h>
#include <foundation/algebra/lattices/FractionalLattice.h>
#include <foundation/effects/Effect.h>

namespace {

namespace fl = ::foundation::algebra::lattices;
using ::fixy::check::expect_dies;
using ::fixy::check::pass;

// The value comes back through a call that no optimization looks into, so
// each operation below runs on a run-time argument.
template <typename T>
[[nodiscard, gnu::noipa]] T opaque(T value) noexcept {
    return value;
}

void read_lazy_before_build() noexcept {
    ::fixy::handle::Lazy<int> lazy{};
    (void)opaque(lazy.get());
}

// The reduced denominator of 1/2^31 * 1/2^31 is 2^62.
void multiply_past_bound() noexcept {
    const fl::Rational least{1, opaque(fl::Rational::MAX_SAFE_MAGNITUDE)};
    (void)opaque(fl::FractionalLattice::mul(least, least));
}

// The reduced denominator of 1/(2^31 - 1) + 1/2^31 is (2^31 - 1) * 2^31.
void add_past_bound() noexcept {
    const fl::Rational least{1, opaque(fl::Rational::MAX_SAFE_MAGNITUDE)};
    const fl::Rational next_to_least{1, opaque(fl::Rational::MAX_SAFE_MAGNITUDE - 1)};
    (void)opaque(fl::FractionalLattice::add(next_to_least, least));
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    expect_dies(test, read_lazy_before_build, ::fixy::TextView{"fatal invariant failed: initialized()"});
    expect_dies(test, multiply_past_bound, ::fixy::TextView{"fatal invariant failed: product.is_well_formed()"});
    expect_dies(test, add_past_bound, ::fixy::TextView{"fatal invariant failed: sum.is_well_formed()"});

    // The controls: a read after the build, and a product and a sum at the
    // bound.
    ::fixy::handle::Lazy<int> lazy{};
    CRUCIBLE_TEST_ASSERT(test, lazy.get_or_init(test, [] noexcept { return 7; }) == 7 && lazy.get() == 7);
    const fl::Rational least{1, opaque(fl::Rational::MAX_SAFE_MAGNITUDE)};
    CRUCIBLE_TEST_ASSERT(test, fl::FractionalLattice::mul(least, fl::FractionalLattice::top()) == least);
    const fl::Rational half = opaque(fl::Rational{1, 2});
    CRUCIBLE_TEST_ASSERT(test, fl::FractionalLattice::add(half, half) == fl::FractionalLattice::top());

    pass(test, "test_fatal_invariants_under_ignore: PASSED\n");
    return 0;
}
