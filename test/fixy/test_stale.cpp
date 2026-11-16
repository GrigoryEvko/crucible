// Sentinel TU for fixy/Stale.h: the wrapper lands in the stored-grade
// regime, its diagnostic surface agrees with the substrate, the
// detection surface answers through the one reflection query, and the
// semiring answers the same at run time as in a constant expression,
// including the saturating arm no constant-expression cell reaches.  The
// weaken contract is shown to abort at runtime; its constant-expression
// form is test/fixy/neg/neg_stale_weakened_downwards.cpp.

#include <fixy/Ctx.h>
#include <fixy/Stale.h>

#include <foundation/algebra/GradedTrait.h>
#include <foundation/effects/Ctx.h>

#include <fixy/Core.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

namespace fa = ::foundation::algebra;
using ::fixy::Stale;
using SS = fa::lattices::StalenessSemiring;
namespace st = fa::lattices::staleness;

// A value of the grade `steps`: the stamp of position zero, read when its
// source is `steps` positions further.  Complexity: linear in steps.
[[nodiscard]] constexpr Stale<int> stale_at(int value, std::uint64_t steps) noexcept {
    ::fixy::SequenceSource source =
        ::fixy::mint_sequence_source(::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    ::fixy::SequenceStamp const sampled = source.stamp();
    for (std::uint64_t step = 0; step < steps; ++step)
        static_cast<void>(source.advance());
    return Stale<int>::at(value, source, sampled);
}

// A value at the current position of its source.
[[nodiscard]] constexpr Stale<int> stale_fresh(int value) noexcept {
    ::fixy::SequenceSource const source =
        ::fixy::mint_sequence_source(::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    return Stale<int>::fresh(value, source);
}

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
constexpr Stale<int> fresh = stale_fresh(7);
static_assert(fresh.is_fresh() && fresh.peek() == 7);
constexpr Stale<int> weakened = fresh.weaken(st::at(4));
static_assert(weakened.staleness() == st::at(4) && weakened.peek() == 7);
static_assert(std::move(stale_at(1, 2)).weaken(SS::top()).is_infinite());

int check_weaken_aborts_downwards() {
    volatile std::uint64_t seed = 8;
    Stale<int> stale = stale_at(1, seed);

    // Upwards and level are admitted.
    Stale<int> up = stale.weaken(st::at(seed + 1));
    if (up.staleness().value != 9) return 10;
    Stale<int> level = stale.weaken(st::at(seed));
    if (level.staleness().value != 8) return 11;

    // Downwards is the contract this method exists to refuse.  Each refused
    // weaken runs in a child through expect_dies of the Check family.
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::check::expect_dies(
        test, [&stale, &seed] noexcept { (void)stale.weaken(st::at(seed - 1)); },
        ::fixy::TextView{"foundation: contract violation: semiring_type::leq(staleness(), tau)"});
    ::fixy::check::expect_dies(
        test, [&stale] noexcept { (void)std::move(stale).weaken(st::at(0)); },
        ::fixy::TextView{"foundation: contract violation: semiring_type::leq(was, tau)"});
    return 0;
}

// The semiring operations against values the compiler cannot fold.  The
// constant-expression cells in the header reach only the unsaturated arm
// of the multiply, so the clamp at the top of the range is checked here
// and nowhere else.
int check_semiring_at_runtime() {
    volatile std::uint64_t seed = 3;
    const auto near_top = SS::top().value - 2;

    Stale<int> a = stale_at(10, static_cast<std::uint64_t>(seed));
    Stale<int> b = stale_at(20, static_cast<std::uint64_t>(seed) + 5);
    Stale<int> inf = Stale<int>::at_infinity(99);

    if (!a.is_finite() || a.is_fresh()) return 20;
    if (!inf.is_infinite()) return 21;
    if (!a.fresher_than(b) || !a.no_staler_than(b)) return 22;

    // combine_max keeps the value of the left operand with the staler grade.
    // combine_min keeps the fresher value with its own grade, from each side.
    const Stale<int> watermark = a.combine_max(b);
    if (watermark.staleness().value != 8 || watermark.peek() != 10) return 23;
    const Stale<int> freshest = a.combine_min(b);
    if (freshest.staleness().value != 3 || freshest.peek() != 10) return 24;
    const Stale<int> fresher_on_right = b.combine_min(a);
    if (fresher_on_right.staleness().value != 3 || fresher_on_right.peek() != 10) return 39;
    Stale<int> staler = b;
    const Stale<int> moved_fresher = std::move(staler).combine_min(a);
    if (moved_fresher.staleness().value != 3 || moved_fresher.peek() != 10) return 40;

    // Infinity absorbs under join and is ignored under meet.
    if (!a.combine_max(inf).is_infinite()) return 25;
    if (!a.combine_min(inf).is_finite()) return 26;

    // Multiplication in this semiring is addition of the grades.
    Stale<int> chain = a.compose_add(b);
    if (chain.staleness().value != 11 || chain.peek() != 10) return 27;
    if (!a.compose_add(inf).is_infinite()) return 28;
    if (a.advance_by(5).staleness().value != 8) return 29;
    if (a.advance_by(0).staleness().value != 3) return 30;

    // The saturating arm: a step past the top of the range clamps to
    // infinity rather than wrapping to a fresh grade.
    Stale<int> almost = stale_at(1, 0).advance_by(near_top);
    if (almost.staleness().value != near_top) return 41;
    if (!almost.advance_by(5).is_infinite()) return 31;
    if (!almost.compose_add(stale_at(2, 5)).is_infinite()) return 32;

    // The default claims the weakest grade, and the two named mints land
    // where they claim.
    if (Stale<int>{}.staleness() != SS::top()) return 33;
    if (!stale_fresh(42).is_fresh()) return 34;
    if (stale_at(7, 100).staleness().value != 100) return 35;

    chain.peek_mut() = 99;
    if (chain.peek() != 99) return 36;

    // The rvalue overloads carry the value out rather than copying it.
    Stale<int> moved = std::move(a).combine_max(b);
    if (moved.peek() != 10 || moved.staleness().value != 8) return 37;
    if (!std::move(moved).weaken(inf.staleness()).is_infinite()) return 38;

    return 0;
}

// The doors that take a grade from a source, with a count of steps that the
// compiler cannot fold.
int check_source_doors() {
    ::fixy::SequenceSource source =
        ::fixy::mint_sequence_source(::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    ::fixy::SequenceStamp const first = source.stamp();
    volatile std::uint64_t steps = 3;
    for (std::uint64_t step = 0; step < steps; ++step)
        static_cast<void>(source.advance());

    Stale<int> const built = Stale<int>::at_in_place(source, first, 5);
    if (built.peek() != 5 || built.staleness().value != 3) return 50;
    if (!Stale<int>::fresh(9, source).is_fresh()) return 51;
    if (source.stamp_at(steps + 1).is_some()) return 52;
    if (!source.stamp_at(steps).is_some()) return 53;

    // A later read of the same stamp gives a larger grade.
    static_cast<void>(source.advance());
    if (Stale<int>::at(5, source, first).staleness().value != 4) return 54;
    return 0;
}

}  // namespace

int main() {
    if (int rc = check_semiring_at_runtime(); rc != 0) return rc;
    if (int rc = check_source_doors(); rc != 0) return rc;
    if (int rc = check_weaken_aborts_downwards(); rc != 0) return rc;

    return 0;
}
