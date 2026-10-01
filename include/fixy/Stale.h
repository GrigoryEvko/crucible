#pragma once

// A value paired with a staleness grade drawn from the natural numbers
// extended with infinity.  The grade is a position in time, not an
// ownership claim, so two instances carrying the same value and grade
// are the same event and copying one is replay rather than duplication.

#include <fixy/GradedFacade.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/StalenessSemiring.h>
#include <foundation/contracts/Pre.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fixy {

template <typename T>
class [[nodiscard]] Stale : public graded_facade<::foundation::algebra::ModalityKind::Absolute,
                                                 ::foundation::algebra::lattices::StalenessSemiring, T> {
public:
    // value_type, modality and the two name forwarders arrive from
    // graded_facade.  The base is dependent, so the names this class
    // body uses unqualified are re-declared here rather than found by
    // lookup.
    using facade_ = graded_facade<::foundation::algebra::ModalityKind::Absolute,
                                  ::foundation::algebra::lattices::StalenessSemiring, T>;
    using typename facade_::graded_type;
    // The grade is both a semiring and a chain lattice.  `lattice_type`
    // is what the family-wide introspection surface matches on, and the
    // two spellings beside it are what this header's own readers use.
    using typename facade_::lattice_type;
    using semiring_type = lattice_type;
    using semiring_t = semiring_type;
    using staleness_t = typename semiring_type::element_type;

private:
    graded_type impl_;

    using key_ = ::foundation::algebra::grade_key<Stale>;

public:
    constexpr Stale() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{key_{}, T{}, semiring_type::bottom()} {}

    constexpr Stale(T value, staleness_t tau) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{key_{}, std::move(value), tau} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr Stale(std::in_place_t, staleness_t tau,
                    Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                             && std::is_nothrow_move_constructible_v<T>)
        : impl_{key_{}, T(std::forward<Args>(args)...), tau} {}

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
        return Stale{std::move(value), ::foundation::algebra::lattices::staleness::at(n)};
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
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(key_{}); }

    [[nodiscard]] constexpr bool fresher_than(Stale const& other) const noexcept {
        return semiring_type::leq(staleness(), other.staleness()) && !(staleness() == other.staleness());
    }

    [[nodiscard]] constexpr bool no_staler_than(Stale const& other) const noexcept {
        return semiring_type::leq(staleness(), other.staleness());
    }

    // The one direction the grade may move by fiat is towards more
    // stale: a value known to be n steps behind is also at most n + k
    // steps behind, so the claim stays true.  A fresher grade is new
    // evidence and needs a new Stale.  The substrate's own weaken
    // carries this rule as a native pre() clause, which a stock GCC 16
    // skips during constant evaluation; the in-body CRUCIBLE_PRE fires
    // there too, and the negative-compile fixture for this method runs
    // in a constant expression.  The result is built before the check;
    // the order is immaterial, since the constructor does nothing
    // beyond its own bounds check.
    [[nodiscard]] constexpr Stale weaken(staleness_t tau) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        Stale result{this->peek(), tau};
        CRUCIBLE_PRE(semiring_type::leq(staleness(), tau));
        return result;
    }

    [[nodiscard]] constexpr Stale weaken(staleness_t tau) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        staleness_t const was = staleness();
        Stale result{std::move(impl_).consume(), tau};
        CRUCIBLE_PRE(semiring_type::leq(was, tau));
        return result;
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
                     semiring_type::mul(this->staleness(), ::foundation::algebra::lattices::staleness::at(delta))};
    }

    [[nodiscard]] constexpr Stale advance_by(std::uint64_t delta) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        staleness_t advanced =
            semiring_type::mul(this->staleness(), ::foundation::algebra::lattices::staleness::at(delta));
        return Stale{std::move(impl_).consume(), advanced};
    }
};

template <typename T>
Stale(T, ::foundation::algebra::lattices::StalenessSemiring::element_type) -> Stale<T>;

// The detection surface of Stale: a concept that asks if a type is a
// Stale, and the associated types of one.  One reflection query answers
// it, and the associated types are read off the wrapper's own typedefs,
// so there is no primary-plus-specialization ladder to keep in step
// with the class.  The concept is the question.  The value spelling is
// derived from it, and nothing that gates reads it.

template <typename T>
concept IsStale = ::foundation::reflect::IsInstanceOf<T, ^^Stale>;

template <typename T>
inline constexpr bool is_stale_v = IsStale<T>;

template <typename T>
    requires IsStale<T>
using stale_value_t = typename std::remove_cvref_t<T>::value_type;

template <typename T>
    requires is_stale_v<T>
using stale_semiring_t = typename std::remove_cvref_t<T>::semiring_type;

template <typename T>
    requires is_stale_v<T>
using stale_staleness_t = typename std::remove_cvref_t<T>::staleness_t;

}  // namespace fixy
