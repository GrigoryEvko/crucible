#pragma once

// Witness<Tier, T> pins a value to how strongly its invariant was
// established.
//
// The tiers form a chain from the weakest evidence to the strongest:
// UNWITNESSED, TYPE_CHECKED, TEST_PASSED, then FORMALLY_VERIFIED.
//
// satisfies<Required> asks whether the pinned tier covers what a
// consumer demands: stronger satisfies weaker.  A FORMALLY_VERIFIED
// value is admissible wherever TEST_PASSED is required.  The converse
// does not hold.
//
// relax<Weaker> moves down the chain and never up.  There is no
// tighten(): the only way to hold a stronger tier is to build one
// where the evidence was actually produced.  The substrate's
// weaken(), which does move up, is not exposed here.
//
// extract() carries no gate.  The burden sits on the producer, so
// reading the value out of a witnessed wrapper is sound at every
// tier.

#include <crucible/Platform.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/WitnessLattice.h>

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// The class template below shadows the lattice enum of the same name
// in this namespace, so the enum is re-exported under a distinct
// alias.
using ::crucible::algebra::lattices::WitnessLattice;
using Witness_v = ::crucible::algebra::lattices::Witness;

template <Witness_v Tier, typename T>
class [[nodiscard]] Witness {
public:
    using value_type = T;
    using lattice_type = WitnessLattice::At<Tier>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Comonad, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Comonad;

    static constexpr Witness_v tier = Tier;

private:
    graded_type impl_;

public:
    // The default constructor pins T{} to a tier no evidence backs.
    // Deleting it would be the truthful choice, but it is kept so the
    // wrapper can sit in an array element or a default-initialized
    // struct field.  A site holding the evidence uses the explicit
    // constructor or the mint below.
    constexpr Witness() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit Witness(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Witness(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                         && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    // Copying is permitted.  The tier records what evidence the
    // producer had, and a copy of the value inherits that same
    // history, so nothing is weakened by duplicating it.
    constexpr Witness(const Witness&) = default;
    constexpr Witness(Witness&&) = default;
    constexpr Witness& operator=(const Witness&) = default;
    constexpr Witness& operator=(Witness&&) = default;
    ~Witness() = default;

    [[nodiscard]] friend constexpr bool operator==(Witness const& a,
                                                   Witness const& b) noexcept(noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) {
            { x == y } -> std::convertible_to<bool>;
        }
    {
        return a.peek() == b.peek();
    }

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }

    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }

    // Mutation cannot break the pin.  The tier records how the value
    // came to be, not what its bytes hold now.
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    [[nodiscard]] constexpr T extract() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).extract();
    }

    constexpr void swap(Witness& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(Witness& a, Witness& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    template <Witness_v RequiredTier>
    static constexpr bool satisfies = WitnessLattice::leq(RequiredTier, Tier);

    template <Witness_v WeakerTier>
        requires(WitnessLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr Witness<WeakerTier, T> relax() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Witness<WeakerTier, T>{this->peek()};
    }

    template <Witness_v WeakerTier>
        requires(WitnessLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr Witness<WeakerTier, T> relax() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Witness<WeakerTier, T>{std::move(impl_).consume()};
    }
};

// This factory does the same work as the in-place constructor.  It
// exists so that every site claiming a tier is findable by searching
// for the mint_ prefix, and because the claim is sound only where the
// producer really holds the evidence.  Nothing here can check that,
// so the discipline lives at the call site.
template <Witness_v Tier, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Witness<Tier, T>
mint_witness(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return Witness<Tier, T>{std::in_place, std::forward<Args>(args)...};
}

// The namespace is witness_tier rather than witness because a set of
// proof-strength tag structs already occupies witness:: in this
// namespace, under some of the same English names.
namespace witness_tier {
template <typename T>
using Unwitnessed = Witness<Witness_v::UNWITNESSED, T>;
template <typename T>
using TypeChecked = Witness<Witness_v::TYPE_CHECKED, T>;
template <typename T>
using TestPassed = Witness<Witness_v::TEST_PASSED, T>;
template <typename T>
using FormallyVerified = Witness<Witness_v::FORMALLY_VERIFIED, T>;
}  // namespace witness_tier

namespace detail::witness_layout {

template <typename T>
using FormallyW = Witness<Witness_v::FORMALLY_VERIFIED, T>;
template <typename T>
using TestedW = Witness<Witness_v::TEST_PASSED, T>;
template <typename T>
using TypedW = Witness<Witness_v::TYPE_CHECKED, T>;
template <typename T>
using UnwitW = Witness<Witness_v::UNWITNESSED, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(FormallyW, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FormallyW, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FormallyW, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TestedW, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TestedW, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TypedW, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(UnwitW, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(UnwitW, double);

}  // namespace detail::witness_layout

static_assert(sizeof(Witness<Witness_v::UNWITNESSED, int>) == sizeof(int));
static_assert(sizeof(Witness<Witness_v::TYPE_CHECKED, int>) == sizeof(int));
static_assert(sizeof(Witness<Witness_v::TEST_PASSED, int>) == sizeof(int));
static_assert(sizeof(Witness<Witness_v::FORMALLY_VERIFIED, int>) == sizeof(int));
static_assert(sizeof(Witness<Witness_v::FORMALLY_VERIFIED, double>) == sizeof(double));
static_assert(sizeof(Witness<Witness_v::FORMALLY_VERIFIED, char>) == sizeof(char));

namespace detail::witness_self_test {

using FormallyInt = Witness<Witness_v::FORMALLY_VERIFIED, int>;
using TestedInt = Witness<Witness_v::TEST_PASSED, int>;
using TypedInt = Witness<Witness_v::TYPE_CHECKED, int>;
using UnwitnessedInt = Witness<Witness_v::UNWITNESSED, int>;

inline constexpr FormallyInt w_default{};
static_assert(w_default.peek() == 0);
static_assert(w_default.tier == Witness_v::FORMALLY_VERIFIED);

inline constexpr FormallyInt w_explicit{42};
static_assert(w_explicit.peek() == 42);

static_assert(FormallyInt::tier == Witness_v::FORMALLY_VERIFIED);
static_assert(TestedInt::tier == Witness_v::TEST_PASSED);
static_assert(TypedInt::tier == Witness_v::TYPE_CHECKED);
static_assert(UnwitnessedInt::tier == Witness_v::UNWITNESSED);

static_assert(FormallyInt::satisfies<Witness_v::FORMALLY_VERIFIED>);
static_assert(FormallyInt::satisfies<Witness_v::TEST_PASSED>);
static_assert(FormallyInt::satisfies<Witness_v::TYPE_CHECKED>);
static_assert(FormallyInt::satisfies<Witness_v::UNWITNESSED>);

static_assert(TestedInt::satisfies<Witness_v::TEST_PASSED>);
static_assert(TestedInt::satisfies<Witness_v::TYPE_CHECKED>);
static_assert(TestedInt::satisfies<Witness_v::UNWITNESSED>);
static_assert(!TestedInt::satisfies<Witness_v::FORMALLY_VERIFIED>);

static_assert(TypedInt::satisfies<Witness_v::TYPE_CHECKED>);
static_assert(TypedInt::satisfies<Witness_v::UNWITNESSED>);
static_assert(!TypedInt::satisfies<Witness_v::TEST_PASSED>);
static_assert(!TypedInt::satisfies<Witness_v::FORMALLY_VERIFIED>);

static_assert(UnwitnessedInt::satisfies<Witness_v::UNWITNESSED>);
static_assert(!UnwitnessedInt::satisfies<Witness_v::TYPE_CHECKED>);
static_assert(!UnwitnessedInt::satisfies<Witness_v::TEST_PASSED>);
static_assert(!UnwitnessedInt::satisfies<Witness_v::FORMALLY_VERIFIED>);

inline constexpr auto from_formally_to_tested = FormallyInt{42}.relax<Witness_v::TEST_PASSED>();
static_assert(from_formally_to_tested.peek() == 42);
static_assert(from_formally_to_tested.tier == Witness_v::TEST_PASSED);

inline constexpr auto from_formally_to_unwitnessed = FormallyInt{99}.relax<Witness_v::UNWITNESSED>();
static_assert(from_formally_to_unwitnessed.peek() == 99);
static_assert(from_formally_to_unwitnessed.tier == Witness_v::UNWITNESSED);

inline constexpr auto from_tested_to_typed = TestedInt{7}.relax<Witness_v::TYPE_CHECKED>();
static_assert(from_tested_to_typed.peek() == 7);
static_assert(from_tested_to_typed.tier == Witness_v::TYPE_CHECKED);

inline constexpr auto identity_relax = FormallyInt{100}.relax<Witness_v::FORMALLY_VERIFIED>();
static_assert(identity_relax.peek() == 100);
static_assert(identity_relax.tier == Witness_v::FORMALLY_VERIFIED);

inline constexpr auto minted_formally = mint_witness<Witness_v::FORMALLY_VERIFIED, int>(123);
static_assert(minted_formally.peek() == 123);
static_assert(minted_formally.tier == Witness_v::FORMALLY_VERIFIED);

static_assert(std::is_same_v<witness_tier::FormallyVerified<int>, FormallyInt>);
static_assert(std::is_same_v<witness_tier::TestPassed<int>, TestedInt>);
static_assert(std::is_same_v<witness_tier::TypeChecked<int>, TypedInt>);
static_assert(std::is_same_v<witness_tier::Unwitnessed<int>, UnwitnessedInt>);

static_assert(FormallyInt{42} == FormallyInt{42});
static_assert(!(FormallyInt{42} == FormallyInt{43}));

static_assert(std::is_copy_constructible_v<FormallyInt>);
static_assert(std::is_copy_assignable_v<FormallyInt>);
static_assert(std::is_move_constructible_v<FormallyInt>);
static_assert(std::is_move_assignable_v<FormallyInt>);

static_assert(FormallyInt::modality == ::crucible::algebra::ModalityKind::Comonad);

// The arguments below are runtime values so that the operations are
// exercised outside constant evaluation as well.
inline void runtime_smoke_test() {
    int seed = 17;

    FormallyInt w{seed * 2};
    if (w.peek() != 34) std::abort();
    if (w.tier != Witness_v::FORMALLY_VERIFIED) std::abort();

    w.peek_mut() = 99;
    if (w.peek() != 99) std::abort();

    FormallyInt e{seed * 3};
    int extracted = std::move(e).extract();
    if (extracted != 51) std::abort();

    FormallyInt source{seed * 4};
    auto relaxed = std::move(source).relax<Witness_v::TEST_PASSED>();
    if (relaxed.peek() != 68) std::abort();
    if (relaxed.tier != Witness_v::TEST_PASSED) std::abort();

    auto m = mint_witness<Witness_v::TEST_PASSED, int>(seed);
    int m_out = std::move(m).extract();
    if (m_out != 17) std::abort();

    FormallyInt c1{seed};
    FormallyInt c2 = c1;
    if (c1.peek() != c2.peek()) std::abort();
    if (c1.peek() != 17) std::abort();

    FormallyInt a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();
}

}  // namespace detail::witness_self_test

}  // namespace crucible::safety
