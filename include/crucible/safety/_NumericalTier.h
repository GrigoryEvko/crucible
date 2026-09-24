#pragma once

// NumericalTier<T_at, T> pins a value to how far its computation may
// deviate from an exact result.
//
// The tiers form a chain from the loosest bound to the tightest:
// RELAXED, then ULP_INT8, ULP_FP8, ULP_FP16, ULP_FP32, ULP_FP64, then
// BITEXACT.  RELAXED promises nothing.  Each ULP tier bounds the
// deviation in units in the last place of that format.  BITEXACT
// promises identical bits.
//
// satisfies<Required> asks whether the pinned tier covers what a
// consumer demands: tighter satisfies looser.  A BITEXACT value is
// admissible wherever ULP_FP16 is required.  The converse does not
// hold.
//
// relax<Looser> moves down the chain and never up.  There is no
// tighten(): the only way to hold a BITEXACT value is to build one
// where the computation really was exact.  The substrate's weaken(),
// which does move up, is deliberately not exposed here.

#include <crucible/Platform.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/_ToleranceLattice.h>

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::Tolerance;
using ::crucible::algebra::lattices::ToleranceLattice;

template <Tolerance T_at, typename T>
class [[nodiscard]] NumericalTier {
public:
    using value_type = T;
    using lattice_type = ToleranceLattice::At<T_at>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    static constexpr Tolerance tier = T_at;

private:
    graded_type impl_;

public:
    // The default constructor pins T{} to a tier no computation
    // earned.  For a trivially-zero T the claim is vacuously true,
    // because zero is the same under every recipe.  Deleting it would
    // be the truthful choice for every other T, but it is kept so the
    // wrapper can sit in an array element or a default-initialized
    // struct field.  A site that ran the computation uses the
    // explicit constructor.
    constexpr NumericalTier() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit NumericalTier(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit NumericalTier(std::in_place_t,
                                     Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                              && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    // Copying is permitted.  The tier records how the value was
    // computed, and a copy inherits that same history, so nothing is
    // weakened by duplicating it.
    constexpr NumericalTier(const NumericalTier&) = default;
    constexpr NumericalTier(NumericalTier&&) = default;
    constexpr NumericalTier& operator=(const NumericalTier&) = default;
    constexpr NumericalTier& operator=(NumericalTier&&) = default;
    ~NumericalTier() = default;

    [[nodiscard]] friend constexpr bool operator==(NumericalTier const& a,
                                                   NumericalTier const& b) noexcept(noexcept(a.peek() == b.peek()))
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

    // The tier records how the value was computed, not what its bytes
    // hold now, so mutation cannot break the pin.  A caller who writes
    // new bytes through this reference is asserting that those bytes
    // also meet the tier.  Nothing here checks that.
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    constexpr void swap(NumericalTier& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(NumericalTier& a, NumericalTier& b) noexcept(std::is_nothrow_swappable_v<T>) {
        a.swap(b);
    }

    template <Tolerance RequiredTier>
    static constexpr bool satisfies = ToleranceLattice::leq(RequiredTier, T_at);

    template <Tolerance LooserTier>
        requires(ToleranceLattice::leq(LooserTier, T_at))
    [[nodiscard]] constexpr NumericalTier<LooserTier, T>
    relax() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return NumericalTier<LooserTier, T>{this->peek()};
    }

    template <Tolerance LooserTier>
        requires(ToleranceLattice::leq(LooserTier, T_at))
    [[nodiscard]] constexpr NumericalTier<LooserTier, T> relax() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return NumericalTier<LooserTier, T>{std::move(impl_).consume()};
    }
};

// There is no deduction guide.  T_at cannot be deduced from a value,
// and adding one that defaulted it would let a call site acquire a
// tier it never stated.  Every site names the tier.

namespace numerical_tier {
template <typename T>
using Relaxed = NumericalTier<Tolerance::RELAXED, T>;
template <typename T>
using Int8 = NumericalTier<Tolerance::ULP_INT8, T>;
template <typename T>
using Fp8 = NumericalTier<Tolerance::ULP_FP8, T>;
template <typename T>
using Fp16 = NumericalTier<Tolerance::ULP_FP16, T>;
template <typename T>
using Fp32 = NumericalTier<Tolerance::ULP_FP32, T>;
template <typename T>
using Fp64 = NumericalTier<Tolerance::ULP_FP64, T>;
template <typename T>
using Bitexact = NumericalTier<Tolerance::BITEXACT, T>;
}  // namespace numerical_tier

namespace detail::numerical_tier_layout {

template <typename T>
using BitexactN = NumericalTier<Tolerance::BITEXACT, T>;
template <typename T>
using Fp32N = NumericalTier<Tolerance::ULP_FP32, T>;
template <typename T>
using RelaxedN = NumericalTier<Tolerance::RELAXED, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(BitexactN, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BitexactN, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BitexactN, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Fp32N, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Fp32N, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxedN, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxedN, double);

}  // namespace detail::numerical_tier_layout

static_assert(sizeof(NumericalTier<Tolerance::RELAXED, int>) == sizeof(int));
static_assert(sizeof(NumericalTier<Tolerance::ULP_INT8, int>) == sizeof(int));
static_assert(sizeof(NumericalTier<Tolerance::ULP_FP8, int>) == sizeof(int));
static_assert(sizeof(NumericalTier<Tolerance::ULP_FP16, int>) == sizeof(int));
static_assert(sizeof(NumericalTier<Tolerance::ULP_FP32, int>) == sizeof(int));
static_assert(sizeof(NumericalTier<Tolerance::ULP_FP64, int>) == sizeof(int));
static_assert(sizeof(NumericalTier<Tolerance::BITEXACT, int>) == sizeof(int));
static_assert(sizeof(NumericalTier<Tolerance::BITEXACT, double>) == sizeof(double));

namespace detail::numerical_tier_self_test {

using BitexactInt = NumericalTier<Tolerance::BITEXACT, int>;
using Fp16Int = NumericalTier<Tolerance::ULP_FP16, int>;
using RelaxedInt = NumericalTier<Tolerance::RELAXED, int>;

inline constexpr BitexactInt nt_default{};
static_assert(nt_default.peek() == 0);
static_assert(nt_default.tier == Tolerance::BITEXACT);

inline constexpr BitexactInt nt_explicit{42};
static_assert(nt_explicit.peek() == 42);

static_assert(BitexactInt::tier == Tolerance::BITEXACT);
static_assert(Fp16Int::tier == Tolerance::ULP_FP16);
static_assert(RelaxedInt::tier == Tolerance::RELAXED);

static_assert(BitexactInt::satisfies<Tolerance::BITEXACT>);
static_assert(BitexactInt::satisfies<Tolerance::ULP_FP64>);
static_assert(BitexactInt::satisfies<Tolerance::ULP_FP32>);
static_assert(BitexactInt::satisfies<Tolerance::ULP_FP16>);
static_assert(BitexactInt::satisfies<Tolerance::ULP_FP8>);
static_assert(BitexactInt::satisfies<Tolerance::ULP_INT8>);
static_assert(BitexactInt::satisfies<Tolerance::RELAXED>);

static_assert(Fp16Int::satisfies<Tolerance::ULP_FP16>);
static_assert(Fp16Int::satisfies<Tolerance::ULP_FP8>);
static_assert(Fp16Int::satisfies<Tolerance::ULP_INT8>);
static_assert(Fp16Int::satisfies<Tolerance::RELAXED>);
static_assert(!Fp16Int::satisfies<Tolerance::ULP_FP32>);
static_assert(!Fp16Int::satisfies<Tolerance::ULP_FP64>);
static_assert(!Fp16Int::satisfies<Tolerance::BITEXACT>);

static_assert(RelaxedInt::satisfies<Tolerance::RELAXED>);
static_assert(!RelaxedInt::satisfies<Tolerance::ULP_INT8>);
static_assert(!RelaxedInt::satisfies<Tolerance::BITEXACT>);

inline constexpr auto from_bitexact_to_fp16 = BitexactInt{42}.relax<Tolerance::ULP_FP16>();
static_assert(from_bitexact_to_fp16.peek() == 42);
static_assert(from_bitexact_to_fp16.tier == Tolerance::ULP_FP16);

inline constexpr auto from_bitexact_to_relaxed = BitexactInt{99}.relax<Tolerance::RELAXED>();
static_assert(from_bitexact_to_relaxed.peek() == 99);
static_assert(from_bitexact_to_relaxed.tier == Tolerance::RELAXED);

inline constexpr auto from_fp16_to_int8 = Fp16Int{7}.relax<Tolerance::ULP_INT8>();
static_assert(from_fp16_to_int8.peek() == 7);
static_assert(from_fp16_to_int8.tier == Tolerance::ULP_INT8);

inline constexpr auto from_fp16_to_self = Fp16Int{8}.relax<Tolerance::ULP_FP16>();
static_assert(from_fp16_to_self.peek() == 8);

template <typename W, Tolerance T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert(can_relax<BitexactInt, Tolerance::ULP_FP16>);
static_assert(can_relax<BitexactInt, Tolerance::RELAXED>);
static_assert(can_relax<Fp16Int, Tolerance::ULP_INT8>);
static_assert(can_relax<Fp16Int, Tolerance::ULP_FP16>);
static_assert(!can_relax<Fp16Int, Tolerance::ULP_FP32>);
static_assert(!can_relax<Fp16Int, Tolerance::BITEXACT>);
static_assert(!can_relax<RelaxedInt, Tolerance::ULP_INT8>);

// The check below is ends_with rather than an exact match because the
// reflected display string of a type varies with the context it is
// taken in.
static_assert(BitexactInt::value_type_name().ends_with("int"));

static_assert(BitexactInt::lattice_name() == "ToleranceLattice::At<BITEXACT>");
static_assert(Fp16Int::lattice_name() == "ToleranceLattice::At<ULP_FP16>");
static_assert(RelaxedInt::lattice_name() == "ToleranceLattice::At<RELAXED>");

[[nodiscard]] consteval bool swap_exchanges_within_same_tier() noexcept {
    BitexactInt a{10};
    BitexactInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_tier());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    BitexactInt a{10};
    BitexactInt b{20};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    BitexactInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    BitexactInt a{42};
    BitexactInt b{42};
    BitexactInt c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

struct NoEqualityT {
    int v{0};
    NoEqualityT() = default;
    explicit NoEqualityT(int x) : v{x} {}
    NoEqualityT(NoEqualityT&&) = default;
    NoEqualityT& operator=(NoEqualityT&&) = default;
    NoEqualityT(NoEqualityT const&) = delete;
    NoEqualityT& operator=(NoEqualityT const&) = delete;
};

template <typename W>
concept can_equality_compare = requires(W const& a, W const& b) {
    { a == b } -> std::convertible_to<bool>;
};

static_assert(can_equality_compare<BitexactInt>);
static_assert(!can_equality_compare<NumericalTier<Tolerance::BITEXACT, NoEqualityT>>,
              "operator== must drop out of the candidate set when T has no "
              "operator==.  Without the requires-clause the friend emits a "
              "hard error that a caller cannot dispatch around.");

[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    BitexactInt a{99};
    auto b = a.relax<Tolerance::BITEXACT>();
    return b.peek() == 99 && b.tier == Tolerance::BITEXACT;
}
static_assert(relax_to_self_is_identity());

static_assert(BitexactInt::value_type_name().size() > 0);
static_assert(BitexactInt::lattice_name().size() > 0);
static_assert(BitexactInt::lattice_name().starts_with("ToleranceLattice::At<"));

static_assert(numerical_tier::Bitexact<int>::tier == Tolerance::BITEXACT);
static_assert(numerical_tier::Fp32<int>::tier == Tolerance::ULP_FP32);
static_assert(numerical_tier::Fp16<int>::tier == Tolerance::ULP_FP16);
static_assert(numerical_tier::Fp8<int>::tier == Tolerance::ULP_FP8);
static_assert(numerical_tier::Int8<int>::tier == Tolerance::ULP_INT8);
static_assert(numerical_tier::Relaxed<int>::tier == Tolerance::RELAXED);

static_assert(std::is_same_v<numerical_tier::Bitexact<double>, NumericalTier<Tolerance::BITEXACT, double>>);

// The operations below run outside constant evaluation so that any
// divergence between the constexpr and the runtime path shows up.
inline void runtime_smoke_test() {
    BitexactInt a{};
    BitexactInt b{42};
    BitexactInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    // The branch keeps the read of the static accessor alive.
    if (BitexactInt::tier != Tolerance::BITEXACT) {
        std::abort();
    }

    BitexactInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    BitexactInt sx{1};
    BitexactInt sy{2};
    sx.swap(sy);

    using std::swap;
    swap(sx, sy);

    BitexactInt source{77};
    auto relaxed_copy = source.relax<Tolerance::ULP_FP16>();
    auto relaxed_move = std::move(source).relax<Tolerance::RELAXED>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    [[maybe_unused]] bool s1 = BitexactInt::satisfies<Tolerance::ULP_FP16>;
    [[maybe_unused]] bool s2 = Fp16Int::satisfies<Tolerance::BITEXACT>;

    BitexactInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    numerical_tier::Bitexact<int> alias_form{123};
    numerical_tier::Fp16<double> fp16_form{3.14};
    [[maybe_unused]] auto av = alias_form.peek();
    [[maybe_unused]] auto fv = fp16_form.peek();
}

}  // namespace detail::numerical_tier_self_test

}  // namespace crucible::safety
