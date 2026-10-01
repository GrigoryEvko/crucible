// The compile-time checks of fixy/Stale.h.

#include <fixy/Stale.h>

namespace fixy {

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

inline constexpr S_i s_default{};
static_assert(s_default.staleness() == SS::bottom());
static_assert(s_default.peek() == 0);
static_assert(s_default.is_fresh());
static_assert(s_default.is_finite());

inline constexpr S_i s_fresh = S_i::fresh(42);
static_assert(s_fresh.staleness() == SS::bottom());
static_assert(s_fresh.peek() == 42);

inline constexpr S_i s_inf = S_i::at_infinity(99);
static_assert(s_inf.is_infinite());
static_assert(!s_inf.is_finite());
static_assert(s_inf.peek() == 99);

inline constexpr S_i s_at7 = S_i::at(123, 7);
static_assert(s_at7.staleness().value == 7);
static_assert(s_at7.peek() == 123);

static_assert(s_at7 == S_i{123, ::foundation::algebra::lattices::staleness::at(7)});

static_assert(!(s_at7 == S_i{999, ::foundation::algebra::lattices::staleness::at(7)}));

static_assert(!(s_at7 == S_i{123, ::foundation::algebra::lattices::staleness::at(3)}));

static_assert(s_fresh.fresher_than(s_at7));
static_assert(s_at7.fresher_than(s_inf));
static_assert(!s_at7.fresher_than(s_fresh));
static_assert(s_fresh.no_staler_than(s_fresh));
static_assert(s_fresh.no_staler_than(s_at7));
static_assert(!s_at7.no_staler_than(s_fresh));

inline constexpr S_i s_a = S_i::at(10, 3);
inline constexpr S_i s_b = S_i::at(20, 8);
inline constexpr S_i s_combined = s_a.combine_max(s_b);
static_assert(s_combined.staleness().value == 8);
static_assert(s_combined.peek() == 10);

inline constexpr S_i s_combined_inf = s_a.combine_max(s_inf);
static_assert(s_combined_inf.is_infinite());

inline constexpr S_i s_min_pair = s_a.combine_min(s_b);
static_assert(s_min_pair.staleness().value == 3);
static_assert(s_min_pair.peek() == 10);

inline constexpr S_i s_min_with_inf = s_a.combine_min(s_inf);
static_assert(s_min_with_inf.staleness().value == 3);
static_assert(s_min_with_inf.is_finite());

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
static_assert(S_i::value_type_name().ends_with("int"));

static_assert(S_i::lattice_name() == "StalenessSemiring");

[[nodiscard]] consteval bool swap_exchanges_both_components() noexcept {
    S_i a = S_i::at(10, 3);
    S_i b = S_i::at(20, 8);
    a.swap(b);
    return a.peek() == 20 && a.staleness().value == 8 && b.peek() == 10 && b.staleness().value == 3;
}
static_assert(swap_exchanges_both_components());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    S_i a = S_i::at(10, 3);
    S_i b = S_i::at(20, 8);
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
