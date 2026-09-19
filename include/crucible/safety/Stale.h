#pragma once

// A value paired with a staleness grade drawn from the natural numbers
// extended with infinity.  The grade is a position in time, not an
// ownership claim, so two instances carrying the same value and grade
// are the same event and copying one is replay rather than duplication.

#include <crucible/Platform.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/_StalenessSemiring.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::safety {

template <typename T>
class [[nodiscard]] Stale {
public:
    using value_type = T;
    using semiring_type = ::crucible::algebra::lattices::StalenessSemiring;
    using semiring_t = semiring_type;
    // The grade is both a semiring and a chain lattice.  The second
    // spelling exists because the family-wide introspection surface
    // matches on the name `lattice_type`.
    using lattice_type = semiring_type;
    using staleness_t = typename semiring_type::element_type;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, semiring_type, T>;

private:
    graded_type impl_;

public:
    constexpr Stale() noexcept(std::is_nothrow_default_constructible_v<T>) : impl_{T{}, semiring_type::bottom()} {}

    constexpr Stale(T value, staleness_t tau) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), tau} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr Stale(std::in_place_t, staleness_t tau,
                    Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                             && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), tau} {}

    [[nodiscard]] static constexpr Stale fresh(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Stale{std::move(value), semiring_type::bottom()};
    }

    [[nodiscard]] static constexpr Stale at_infinity(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Stale{std::move(value), semiring_type::top()};
    }

    // The grade constructor rejects the reserved value that encodes
    // infinity, so reach for at_infinity rather than passing it here.
    [[nodiscard]] static constexpr Stale at(T value,
                                            std::uint64_t n) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Stale{std::move(value), ::crucible::algebra::lattices::staleness::at(n)};
    }

    constexpr Stale(const Stale&) = default;
    constexpr Stale(Stale&&) = default;
    constexpr Stale& operator=(const Stale&) = default;
    constexpr Stale& operator=(Stale&&) = default;
    ~Stale() = default;

    [[nodiscard]] friend constexpr bool
    operator==(Stale const& a,
               Stale const& b) noexcept(noexcept(a.peek() == b.peek()) && noexcept(a.staleness() == b.staleness())) {
        return a.peek() == b.peek() && a.staleness() == b.staleness();
    }

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }

    constexpr void swap(Stale& other) noexcept(std::is_nothrow_swappable_v<T>
                                               && std::is_nothrow_swappable_v<staleness_t>) {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(Stale& a, Stale& b) noexcept(std::is_nothrow_swappable_v<T>
                                                            && std::is_nothrow_swappable_v<staleness_t>) {
        a.swap(b);
    }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }

    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr staleness_t staleness() const noexcept { return impl_.grade(); }

    [[nodiscard]] constexpr bool is_fresh() const noexcept { return staleness() == semiring_type::bottom(); }
    [[nodiscard]] constexpr bool is_finite() const noexcept { return staleness().is_finite(); }
    [[nodiscard]] constexpr bool is_infinite() const noexcept { return staleness().is_infinite(); }

    // Mutating the payload leaves the grade valid, because the grade
    // records when the value was produced and not what it holds.
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    [[nodiscard]] constexpr bool fresher_than(Stale const& other) const noexcept {
        return semiring_type::leq(staleness(), other.staleness()) && !(staleness() == other.staleness());
    }

    [[nodiscard]] constexpr bool no_staler_than(Stale const& other) const noexcept {
        return semiring_type::leq(staleness(), other.staleness());
    }

    // The payload always comes from `*this`.  The other operand
    // contributes only its grade.
    [[nodiscard]] constexpr Stale
    combine_max(Stale const& other) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Stale{this->peek(), semiring_type::join(this->staleness(), other.staleness())};
    }

    [[nodiscard]] constexpr Stale combine_max(Stale const& other) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        staleness_t maxed = semiring_type::join(this->staleness(), other.staleness());
        return Stale{std::move(impl_).consume(), maxed};
    }

    [[nodiscard]] constexpr Stale
    combine_min(Stale const& other) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Stale{this->peek(), semiring_type::meet(this->staleness(), other.staleness())};
    }

    [[nodiscard]] constexpr Stale combine_min(Stale const& other) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        staleness_t minned = semiring_type::meet(this->staleness(), other.staleness());
        return Stale{std::move(impl_).consume(), minned};
    }

    // Multiplication in this semiring is saturating addition, which is
    // how staleness travels along a dependency chain.  A sum past the
    // representable range saturates to infinity, and infinity absorbs,
    // so a chain containing one unbounded stage stays unbounded.
    [[nodiscard]] constexpr Stale
    compose_add(Stale const& other) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Stale{this->peek(), semiring_type::mul(this->staleness(), other.staleness())};
    }

    [[nodiscard]] constexpr Stale compose_add(Stale const& other) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        staleness_t composed = semiring_type::mul(this->staleness(), other.staleness());
        return Stale{std::move(impl_).consume(), composed};
    }

    // A delta of zero is the multiplicative identity and leaves the
    // grade untouched.
    [[nodiscard]] constexpr Stale
    advance_by(std::uint64_t delta) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Stale{this->peek(),
                     semiring_type::mul(this->staleness(), ::crucible::algebra::lattices::staleness::at(delta))};
    }

    [[nodiscard]] constexpr Stale advance_by(std::uint64_t delta) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        staleness_t advanced =
            semiring_type::mul(this->staleness(), ::crucible::algebra::lattices::staleness::at(delta));
        return Stale{std::move(impl_).consume(), advanced};
    }
};

template <typename T>
Stale(T, ::crucible::algebra::lattices::StalenessSemiring::element_type) -> Stale<T>;

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

using SS = ::crucible::algebra::lattices::StalenessSemiring;
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

static_assert(s_at7 == S_i{123, ::crucible::algebra::lattices::staleness::at(7)});

static_assert(!(s_at7 == S_i{999, ::crucible::algebra::lattices::staleness::at(7)}));

static_assert(!(s_at7 == S_i{123, ::crucible::algebra::lattices::staleness::at(3)}));

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

// These calls repeat the compile-time checks on purpose.  They catch a
// divergence between the constant-evaluated and the runtime form of the
// saturating-add path.
inline void runtime_smoke_test() {
    Stale<int> a = Stale<int>::at(10, 3);
    Stale<int> b = Stale<int>::at(20, 8);
    Stale<int> inf = Stale<int>::at_infinity(99);

    [[maybe_unused]] bool fa = a.is_fresh();
    [[maybe_unused]] bool fi = a.is_finite();
    [[maybe_unused]] bool ii = inf.is_infinite();

    [[maybe_unused]] bool ord = a.fresher_than(b);
    [[maybe_unused]] bool nos = a.no_staler_than(b);

    Stale<int> watermark = a.combine_max(b);
    Stale<int> freshest = a.combine_min(b);
    Stale<int> chain = a.compose_add(b);
    Stale<int> ticked = a.advance_by(5);

    [[maybe_unused]] auto v1 = watermark.peek();
    [[maybe_unused]] auto vf = freshest.peek();
    [[maybe_unused]] auto t1 = chain.staleness();
    [[maybe_unused]] auto t2 = ticked.staleness();

    Stale<int> moved = std::move(a).combine_max(b);
    [[maybe_unused]] auto mv = moved.peek();

    Stale<int> def{};
    Stale<int> fr = Stale<int>::fresh(42);
    Stale<int> ai = Stale<int>::at(7, 100);
    [[maybe_unused]] auto def_t = def.staleness();
    [[maybe_unused]] auto fr_t = fr.staleness();
    [[maybe_unused]] auto ai_t = ai.staleness();

    chain.peek_mut() = 99;
}

}  // namespace detail::stale_self_test

}  // namespace crucible::safety
