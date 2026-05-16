// ── test_fixy_fn — Phase B sentinel for the universal integration ─
//
// Pulls fixy/Fn.h into a TU compiled under project warning flags so
// the header's static_asserts + smoke-test assertions execute.  Adds
// a small set of runtime witnesses on top of the header-embedded
// claims:
//
//   1. mint_fn round-trip — minted value equals seeded value
//   2. stance::PureLinear value access — value() deduces correctly
//      under const + rvalue self
//   3. safety_fn_t round-trip — fixy::fn<int>::safety_fn_t IS
//      safety::fn::Fn<int> (header asserts statically; runtime check
//      pins the name)
//   4. stance::IoFunction Effect row contains IO
//   5. stance::PureCopy resolves Usage to Copy
//
// Per HS14, this is the positive-compile sentinel; the matching
// neg-compile fixtures land under test/fixy_neg/neg_fixy_rule_*.cpp
// in the next task (FIXY-CLEAN-B4).

#include <crucible/fixy/Fn.h>

#include <type_traits>
#include <utility>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

// ─── 1. mint_fn round-trip ─────────────────────────────────────────

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

static_assert(noexcept(fixy::mint_fn<int,
    strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
    strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>, strict<D::Trust>, strict<D::Representation>,
    strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
    strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
    strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>,
    strict<D::Staleness>>(42)),
    "mint_fn<int, ...>(int) must be noexcept (int is "
    "nothrow-move-constructible).");

// ─── 2. value() deduces correctly under const and rvalue ──────────

static_assert(std::is_same_v<
    decltype(std::declval<fixy::stance::PureLinear<int>&>().value()),
    int&>,
    "fixy::fn<T>::value() on lvalue must return T&.");

static_assert(std::is_same_v<
    decltype(std::declval<const fixy::stance::PureLinear<int>&>().value()),
    const int&>,
    "fixy::fn<T>::value() on const lvalue must return const T&.");

static_assert(std::is_same_v<
    decltype(std::declval<fixy::stance::PureLinear<int>&&>().value()),
    int&&>,
    "fixy::fn<T>::value() on rvalue must return T&&.");

// ─── 3. safety_fn_t round-trip — fixy::fn<int>::safety_fn_t ───────
//   IS the all-default safety::fn::Fn<int>.

static_assert(std::is_same_v<
    typename fixy::stance::PureLinear<int>::safety_fn_t,
    crucible::safety::fn::Fn<int>>,
    "stance::PureLinear<int>::safety_fn_t must round-trip to "
    "safety::fn::Fn<int>'s all-default instantiation.");

// ─── 4. stance::IoFunction Effect row contains IO ─────────────────

static_assert(std::is_same_v<
    typename fixy::stance::IoFunction<int>::effect_row_t,
    crucible::effects::Row<crucible::effects::Effect::IO>>,
    "stance::IoFunction's Effect row must contain exactly Effect::IO.");

// ─── 5. stance::PureCopy resolves Usage to Copy ───────────────────

static_assert(fixy::stance::PureCopy<int>::usage_v
    == crucible::safety::fn::UsageMode::Copy,
    "stance::PureCopy must resolve Usage to UsageMode::Copy.");

// ─── 6. stance::AsyncEndpoint resolves Reentrancy to Coroutine ────

static_assert(fixy::stance::AsyncEndpoint<int>::reentrancy_v
    == crucible::safety::fn::ReentrancyMode::Coroutine,
    "stance::AsyncEndpoint must resolve Reentrancy to Coroutine.");

// ─── 7. BgWorker Effect row contains both Bg and Alloc ────────────

static_assert(std::is_same_v<
    typename fixy::stance::BgWorker<int>::effect_row_t,
    crucible::effects::Row<crucible::effects::Effect::Bg,
                           crucible::effects::Effect::Alloc>>,
    "stance::BgWorker's Effect row must contain Bg + Alloc.");

// ─── 8. EBO collapse across multiple types ────────────────────────

static_assert(sizeof(fixy::stance::PureLinear<int>)    == sizeof(int));
static_assert(sizeof(fixy::stance::PureLinear<char>)   == sizeof(char));
static_assert(sizeof(fixy::stance::PureLinear<double>) == sizeof(double));
static_assert(sizeof(fixy::stance::IoFunction<int>)    == sizeof(int));
static_assert(sizeof(fixy::stance::PureCopy<int>)      == sizeof(int));
static_assert(sizeof(fixy::stance::BgWorker<int>)      == sizeof(int));
static_assert(sizeof(fixy::stance::AsyncEndpoint<int>) == sizeof(int));

// ─── 9. Runtime witness — mint_fn produces a fixy::fn carrying the
//        supplied value, and value() retrieves it bit-exactly.

int main() {
    auto v1 = fixy::mint_fn<int,
        strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
        strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
        strict<D::Provenance>, strict<D::Trust>, strict<D::Representation>,
        strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
        strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
        strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>,
        strict<D::Staleness>>(7);
    if (v1.value() != 7) return 1;

    // Stance alias smoke
    fixy::stance::PureLinear<int> v2{11};
    if (v2.value() != 11) return 2;

    fixy::stance::IoFunction<int> v3{13};
    if (v3.value() != 13) return 3;

    fixy::stance::PureCopy<int> v4{17};
    if (v4.value() != 17) return 4;

    return 0;
}
