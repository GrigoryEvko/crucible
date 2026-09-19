// Sentinel TU for fixy/Stale.h: the wrapper lands in the stored-grade
// regime, its diagnostic surface agrees with the substrate, the
// detection surface answers through the one reflection query, and the
// header's runtime smoke test runs under the test flags.  The weaken
// contract is shown to abort at runtime; its constant-expression form
// is test/fixy/neg/neg_stale_weakened_downwards.cpp.
//
// Ported from test/test_is_stale.cpp and the Stale cells of
// test/test_migration_verification.cpp.

#include <fixy/Stale.h>

#include <foundation/algebra/GradedTrait.h>

#include "../foundation/abort_probe.h"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

namespace fa = ::foundation::algebra;
using ::fixy::Stale;
using ::foundation::test::aborts;
using SS = fa::lattices::StalenessSemiring;
namespace st = fa::lattices::staleness;

// Regime 4: the grade is stored beside the value.
static_assert(sizeof(Stale<int>) == sizeof(int) + sizeof(SS::element_type) + 4);
static_assert(sizeof(Stale<std::uint64_t>) == sizeof(std::uint64_t) + sizeof(SS::element_type));
static_assert(alignof(Stale<std::uint64_t>) == alignof(std::uint64_t));

// The diagnostic surface agrees with the substrate.
static_assert(fa::GradedWrapper<Stale<int>>);
static_assert(fa::is_graded_wrapper_v<Stale<int>>);
static_assert(Stale<int>::lattice_name() == "StalenessSemiring");
static_assert(Stale<int>::value_type_name().ends_with("int"));
static_assert(std::is_same_v<Stale<int>::lattice_type, SS>);
static_assert(std::is_same_v<Stale<int>::semiring_type, SS>);
static_assert(Stale<int>::modality == fa::ModalityKind::Absolute);
static_assert(!std::is_void_v<typename Stale<int>::graded_type>);

// Copyable: a value and a grade are an event, and copying replays it.
static_assert(std::is_copy_constructible_v<Stale<int>>);
static_assert(std::is_nothrow_move_constructible_v<Stale<int>>);
static_assert(std::is_trivially_copyable_v<Stale<int>>);

// consume takes an rvalue and nothing else.
template <typename W>
concept ConsumesLvalue = requires(W& w) { w.consume(); };
template <typename W>
concept ConsumesRvalue = requires(W&& w) { std::move(w).consume(); };
static_assert(!ConsumesLvalue<Stale<int>>);
static_assert(ConsumesRvalue<Stale<int>>);

// The two payload types are two wrappers with no bridge between them.
template <typename A, typename B>
concept CanComposeAcross = requires(A a, B b) { a.compose_add(b); };
static_assert(CanComposeAcross<Stale<int>, Stale<int>>);
static_assert(!CanComposeAcross<Stale<int>, Stale<double>>);

// The detection surface, on both spellings, with the cv-ref strip.
static_assert(::fixy::is_stale_v<Stale<int>>);
static_assert(::fixy::is_stale_v<Stale<int> const volatile&>);
static_assert(::fixy::IsStale<Stale<double>&&>);
static_assert(!::fixy::is_stale_v<int>);
static_assert(!::fixy::is_stale_v<Stale<int>*>);
static_assert(std::is_same_v<::fixy::stale_value_t<Stale<double> const&>, double>);
static_assert(std::is_same_v<::fixy::stale_semiring_t<Stale<int>>, SS>);
static_assert(std::is_same_v<::fixy::stale_staleness_t<Stale<int>>, SS::element_type>);

// The mint surface is usable in a constant expression.
constexpr Stale<int> fresh = Stale<int>::fresh(7);
static_assert(fresh.is_fresh() && fresh.peek() == 7);
constexpr Stale<int> weakened = fresh.weaken(st::at(4));
static_assert(weakened.staleness() == st::at(4) && weakened.peek() == 7);
static_assert(std::move(Stale<int>::at(1, 2)).weaken(SS::top()).is_infinite());

int check_weaken_aborts_downwards() {
    volatile std::uint64_t seed = 8;
    Stale<int> stale = Stale<int>::at(1, seed);

    // Upwards and level are admitted.
    Stale<int> up = stale.weaken(st::at(seed + 1));
    if (up.staleness().value != 9) return 10;
    Stale<int> level = stale.weaken(st::at(seed));
    if (level.staleness().value != 8) return 11;

    // Downwards is the contract this method exists to refuse.
    if (!aborts([&] { (void)stale.weaken(st::at(seed - 1)); })) return 12;
    if (!aborts([&] { (void)std::move(stale).weaken(st::at(0)); })) return 13;
    return 0;
}

}  // namespace

int main() {
    ::fixy::detail::stale_self_test::runtime_smoke_test();

    if (int rc = check_weaken_aborts_downwards(); rc != 0) return rc;

    return 0;
}
