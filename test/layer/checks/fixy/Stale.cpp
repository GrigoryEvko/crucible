// The compile-time checks of fixy/Stale.h.

#include <fixy/Stale.h>

#include <fixy/Ctx.h>
#include <foundation/core/Algorithms.h>
#include <foundation/effects/Ctx.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace fixy {

namespace detail::sequence_source_checks {

namespace fe = ::foundation::effects;

// A stamp comes only from a source, and no byte route builds one.
static_assert(!std::is_default_constructible_v<SequenceStamp>);
static_assert(!std::is_constructible_v<SequenceStamp, std::uint64_t>);
static_assert(!std::is_trivially_copyable_v<SequenceStamp>);

// A source is the one owner of its sequence, so it neither copies nor moves.
static_assert(!std::is_default_constructible_v<SequenceSource>);
static_assert(!std::is_copy_constructible_v<SequenceSource> && !std::is_move_constructible_v<SequenceSource>);
static_assert(!std::is_copy_assignable_v<SequenceSource> && !std::is_move_assignable_v<SequenceSource>);
static_assert(!std::is_trivially_copyable_v<SequenceSource>);

// Only a context that owns Init mints a source.  test/fixy/neg holds the two
// fixtures of the mint, for a foreground and a background context.

// The grade of a stamp is its distance from the current position, so an old
// stamp gives a larger grade at a later read.
[[nodiscard]] consteval bool old_stamp_grows_its_grade() noexcept {
    SequenceSource source = mint_sequence_source(ColdInitCtx{fe::testing::init()});
    SequenceStamp const first = source.stamp();
    static_cast<void>(source.advance());
    std::uint64_t const one_behind = source.steps_behind(first).value;
    static_cast<void>(source.advance());
    return one_behind == 1 && source.steps_behind(first).value == 2 && source.steps_behind(source.stamp()).value == 0;
}
static_assert(old_stamp_grows_its_grade());

// A source vouches only for a position that it has reached.
[[nodiscard]] consteval bool stamp_at_refuses_a_later_position() noexcept {
    SequenceSource source = mint_sequence_source(ColdInitCtx{fe::testing::init()});
    static_cast<void>(source.advance());
    return source.stamp_at(1).is_some() && source.stamp_at(0).is_some() && !source.stamp_at(2).is_some();
}
static_assert(stamp_at_refuses_a_later_position());

// A stamp of a second source that is ahead of this source gets the infinite
// grade, never a fresh one.
[[nodiscard]] consteval bool stamp_of_a_source_ahead_is_infinite() noexcept {
    SequenceSource real = mint_sequence_source(ColdInitCtx{fe::testing::init()});
    SequenceSource rogue = mint_sequence_source(ColdInitCtx{fe::testing::init()});
    static_cast<void>(rogue.advance());
    static_cast<void>(rogue.advance());
    return real.steps_behind(rogue.stamp()).is_infinite() && Stale<int>::at(5, real, rogue.stamp()).is_infinite();
}
static_assert(stamp_of_a_source_ahead_is_infinite());

}  // namespace detail::sequence_source_checks

namespace detail::stale_layout {

using S_int64 = Stale<std::int64_t>;
using S_voidp = Stale<void*>;
using S_dbl = Stale<double>;

static_assert(sizeof(S_int64) <= sizeof(std::int64_t) + sizeof(std::uint64_t) + 8,
              "Stale<int64> exceeds the value size plus an 8-byte grade plus 8 "
              "bytes of slack.  Check the alignment of the staleness element "
              "type and the placement of the grade field.");
static_assert(sizeof(S_voidp) <= sizeof(void*) + sizeof(std::uint64_t) + 8);
static_assert(sizeof(S_dbl) <= sizeof(double) + sizeof(std::uint64_t) + 8);

}  // namespace detail::stale_layout

namespace detail::stale_self_test {

using SS = ::foundation::algebra::lattices::StalenessSemiring;
using S_i = Stale<int>;

// A value of the grade `steps`: the stamp of position zero, read when its
// source is `steps` positions further.
[[nodiscard]] consteval S_i stale_at(int value, std::uint64_t steps) noexcept {
    SequenceSource source = mint_sequence_source(ColdInitCtx{::foundation::effects::testing::init()});
    SequenceStamp const sampled = source.stamp();
    for (std::uint64_t step = 0; step < steps; ++step)
        static_cast<void>(source.advance());
    return S_i::at(value, source, sampled);
}

// A value at the current position of its source.
[[nodiscard]] consteval S_i stale_fresh(int value) noexcept {
    SequenceSource const source = mint_sequence_source(ColdInitCtx{::foundation::effects::testing::init()});
    return S_i::fresh(value, source);
}

// at_in_place builds the value from its arguments, at the grade of at.
[[nodiscard]] consteval bool at_in_place_takes_the_grade_of_at() noexcept {
    SequenceSource source = mint_sequence_source(ColdInitCtx{::foundation::effects::testing::init()});
    SequenceStamp const sampled = source.stamp();
    static_cast<void>(source.advance());
    static_cast<void>(source.advance());
    S_i const built = S_i::at_in_place(source, sampled, 77);
    return built.peek() == 77 && built.staleness().value == 2 && built == S_i::at(77, source, sampled);
}
static_assert(at_in_place_takes_the_grade_of_at());

// A default value claims the weakest grade, because nothing measured it.
inline constexpr S_i s_default{};
static_assert(s_default.staleness() == SS::top());
static_assert(s_default.peek() == 0);
static_assert(!s_default.is_fresh());
static_assert(s_default.is_infinite());

// A grade comes only through a named door, and no constructor takes one.
// test/fixy/neg holds a fixture for the private constructor from a value
// and a grade.  No door takes a grade as a number.
static_assert(!std::is_constructible_v<S_i, int, SS::element_type>);
template <typename S>
concept takes_a_number_as_grade = requires { S::at(1, std::uint64_t{7}); };
static_assert(!takes_a_number_as_grade<S_i>);

inline constexpr S_i s_fresh = stale_fresh(42);
static_assert(s_fresh.staleness() == SS::bottom());
static_assert(s_fresh.peek() == 42);

inline constexpr S_i s_inf = S_i::at_infinity(99);
static_assert(s_inf.is_infinite());
static_assert(!s_inf.is_finite());
static_assert(s_inf.peek() == 99);

inline constexpr S_i s_at7 = stale_at(123, 7);
static_assert(s_at7.staleness().value == 7);
static_assert(s_at7.peek() == 123);

static_assert(s_at7 == stale_at(123, 7));

static_assert(!(s_at7 == stale_at(999, 7)));

static_assert(!(s_at7 == stale_at(123, 3)));

static_assert(s_fresh.fresher_than(s_at7));
static_assert(s_at7.fresher_than(s_inf));
static_assert(!s_at7.fresher_than(s_fresh));
static_assert(s_fresh.no_staler_than(s_fresh));
static_assert(s_fresh.no_staler_than(s_at7));
static_assert(!s_at7.no_staler_than(s_fresh));

inline constexpr S_i s_a = stale_at(10, 3);
inline constexpr S_i s_b = stale_at(20, 8);
inline constexpr S_i s_combined = s_a.combine_max(s_b);
static_assert(s_combined.staleness().value == 8);
static_assert(s_combined.peek() == 10);

inline constexpr S_i s_combined_inf = s_a.combine_max(s_inf);
static_assert(s_combined_inf.is_infinite());

inline constexpr S_i s_min_pair = s_a.combine_min(s_b);
static_assert(s_min_pair.staleness().value == 3);
static_assert(s_min_pair.peek() == 10);

// The fresher value keeps its own grade when it is the right operand.
inline constexpr S_i s_min_swapped = s_b.combine_min(s_a);
static_assert(s_min_swapped.staleness().value == 3);
static_assert(s_min_swapped.peek() == 10);
static_assert(S_i{s_b}.combine_min(s_a).peek() == 10);

inline constexpr S_i s_min_with_inf = s_a.combine_min(s_inf);
static_assert(s_min_with_inf.staleness().value == 3);
static_assert(s_min_with_inf.is_finite());
static_assert(s_inf.combine_min(s_a).peek() == 10);

static_assert(s_a.combine_min(s_a).staleness() == s_a.staleness());

inline constexpr S_i s_composed = s_a.compose_add(s_b);
static_assert(s_composed.staleness().value == 11);
static_assert(s_composed.peek() == 10);

inline constexpr S_i s_composed_inf = s_a.compose_add(s_inf);
static_assert(s_composed_inf.is_infinite());

inline constexpr S_i s_advanced = s_a.advance_by(5);
static_assert(s_advanced.staleness().value == 8);
static_assert(s_advanced.peek() == 10);

inline constexpr S_i s_advanced_zero = s_a.advance_by(0);
static_assert(s_advanced_zero.staleness().value == 3);

// Upwards is the admitted direction, and the same grade is its
// boundary.  The downward step is
// test/fixy/neg/neg_stale_weakened_downwards.cpp.
inline constexpr S_i s_weakened = s_a.weaken(::foundation::algebra::lattices::staleness::at(9));
static_assert(s_weakened.staleness().value == 9);
static_assert(s_weakened.peek() == 10);
static_assert(s_a.weaken(s_a.staleness()).staleness() == s_a.staleness());
static_assert(s_a.weaken(SS::top()).is_infinite());

static_assert(SS::bottom() == s_fresh.staleness());
static_assert(SS::top() == s_inf.staleness());

// The reflected display string is sensitive to the translation-unit
// context it is formed in, so match on a suffix rather than equality.
static_assert(::foundation::core::ends_with(S_i::value_type_name(), "int"));

static_assert(S_i::lattice_name() == "StalenessSemiring");

[[nodiscard]] consteval bool swap_exchanges_both_components() noexcept {
    S_i a = stale_at(10, 3);
    S_i b = stale_at(20, 8);
    a.swap(b);
    return a.peek() == 20 && a.staleness().value == 8 && b.peek() == 10 && b.staleness().value == 3;
}
static_assert(swap_exchanges_both_components());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    S_i a = stale_at(10, 3);
    S_i b = stale_at(20, 8);
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

struct payload {};
using S_payload = Stale<payload>;

struct LookalikeStale {
    using value_type = int;
    using staleness_t = unsigned long long;
};

static_assert(is_stale_v<S_i>);
static_assert(is_stale_v<S_payload>);

static_assert(is_stale_v<S_i&>);
static_assert(is_stale_v<S_i&&>);
static_assert(is_stale_v<S_i const>);
static_assert(is_stale_v<S_i const&>);
static_assert(is_stale_v<S_i volatile>);

static_assert(!is_stale_v<int>);
static_assert(!is_stale_v<int*>);
static_assert(!is_stale_v<S_i*>);
static_assert(!is_stale_v<void>);
static_assert(!is_stale_v<LookalikeStale>);

static_assert(IsStale<S_i>);
static_assert(IsStale<S_payload const&>);
static_assert(!IsStale<int>);

static_assert(std::is_same_v<stale_value_t<S_i>, int>);
static_assert(std::is_same_v<stale_value_t<S_payload const&>, payload>);
static_assert(std::is_same_v<stale_semiring_t<S_i>, ::foundation::algebra::lattices::StalenessSemiring>);
static_assert(std::is_same_v<stale_staleness_t<S_i>, ::foundation::algebra::lattices::StalenessSemiring::element_type>);

}  // namespace detail::stale_self_test

}  // namespace fixy
