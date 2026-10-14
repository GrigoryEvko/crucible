// The compile-time checks of fixy/Refined.h.

#include <fixy/Refined.h>

namespace fixy {

namespace detail::admit_refined_self_test {

enum class Refusal : std::uint8_t {
    NotPositive
};

static_assert(admit_refined<positive>(3, Refusal::NotPositive).value().value() == 3);
static_assert(admit_refined<positive>(0, Refusal::NotPositive).error() == Refusal::NotPositive);
static_assert(admit_refined<positive>(-1, Refusal::NotPositive).error() == Refusal::NotPositive);

}  // namespace detail::admit_refined_self_test

// No byte route builds a refined value, and the constructors stay trivial
// so that a refined value still passes in a register.
static_assert(!std::is_trivially_copyable_v<Refined<positive, int>>
                  && !std::is_trivially_copyable_v<SealedRefined<positive, int>>,
              "std::bit_cast must not build a refined value from bytes that the predicate never saw");
static_assert(std::is_trivially_copy_constructible_v<Refined<positive, int>>
                  && std::is_trivially_move_constructible_v<Refined<positive, int>>
                  && std::is_trivially_destructible_v<Refined<positive, int>>
                  && sizeof(Refined<positive, int>) == sizeof(int),
              "a refined int keeps the layout and the register passing of an int");
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<Refined<positive, int>>,
              "the checked lifetime start refuses a refined value over bytes");

namespace detail::refined_self_test {

// The layout witnesses are one fold over a roster of wrapper types
// rather than one line per type.  The roster is the only hand list.
template <class... Ws>
[[nodiscard]] consteval bool all_collapse_to_value() noexcept {
    return ((sizeof(Ws) == sizeof(typename Ws::value_type)) && ...);
}

template <class... Ws>
[[nodiscard]] consteval bool all_graded_wrappers() noexcept {
    return (::foundation::algebra::GradedWrapper<Ws> && ...);
}

// One representative shape per alias: a scalar, a container that has
// both an emptiness test and a size, a parameterised length bound over
// that container, and the parameterised predicates, which are empty
// classes and so collapse inside the wrapper too.
static_assert(all_collapse_to_value<
              Refined<positive, int>, Refined<non_null, void*>, Refined<power_of_two, std::size_t>,
              Refined<aligned<64>, void*>, Refined<bounded_above<1024u>, int>, Refined<in_range<0, 100>, int>,
              Refined<length_ge<1>, void*>, NonZero<int>, NonEmpty<std::span<int>>, NonEmptySpan<int>,
              SealedRefined<positive, int>, SealedRefined<non_null, void*>,
              Refined<all_of<positive, bounded_above<1024>>, int>, Refined<any_of<positive, non_zero>, int>,
              Refined<negate<positive>, int>, Refined<implies<positive, non_zero>, int>, AlignedTo<64, void*>,
              Sized<8, std::array<int, 8>>, Bounded<0, 100, int>, Capped<255, std::uint32_t>, Floored<1, int>,
              MinSize<8, std::array<int, 16>>, DivisibleByN<4, std::size_t>, CacheLineAligned<int>,
              HugePageAligned<std::byte>>());

// Both wrappers are regime 1, so the composition is the payload.  The
// claim is the untracked build's: with fixy/Qtt.h's consume tracker on,
// Linear carries one byte of state on purpose and this collapse is the
// thing that build gives up.
static_assert(::fixy::qtt_consume_tracked || sizeof(LinearRefined<non_null, void*>) == sizeof(void*),
              "LinearRefined must collapse to sizeof(T)");

// The atomic and the composed refinements alike satisfy the wrapper
// concept, so that a future revision of a combinator that disturbs the
// substrate's lattice, value or modality contract fires here.
static_assert(
    all_graded_wrappers<
        Refined<positive, int>, Refined<non_null, void*>, SealedRefined<positive, int>, Refined<all_of<positive>, int>,
        Refined<all_of<positive, bounded_above<100>>, int>, Refined<any_of<positive, non_zero>, int>,
        Refined<negate<positive>, int>, Refined<implies<positive, non_zero>, int>,
        Refined<all_of<all_of<positive>, bounded_above<1024>>, int>, AlignedTo<64, void*>, Bounded<0, 100, int>,
        Capped<255, std::uint32_t>, Floored<1, int>, DivisibleByN<4, std::size_t>>(),
    "every Refined and SealedRefined shape must satisfy GradedWrapper, nested combinators included");

// The layout of the bare value, pinned over the arithmetic and pointer
// shapes, with one difference on purpose: a refined value is not
// trivially copyable, so no byte pattern becomes one.  The shared layout
// invariant asserts that parity, so the three other properties are stated
// here one by one, and the fourth inverted.
template <typename Refinedness, typename T>
inline constexpr bool keeps_the_value_layout =
    sizeof(Refinedness) == sizeof(T) && alignof(Refinedness) == alignof(T)
    && std::is_trivially_destructible_v<Refinedness> == std::is_trivially_destructible_v<T>
    && !std::is_trivially_copyable_v<Refinedness>;

template <typename T>
using SealedPositive = SealedRefined<positive, T>;
static_assert(keeps_the_value_layout<Positive<int>, int> && keeps_the_value_layout<Positive<double>, double>
              && keeps_the_value_layout<NonNull<void*>, void*> && keeps_the_value_layout<SealedPositive<int>, int>
              && keeps_the_value_layout<SealedPositive<double>, double>);

// One door: no public constructor takes a bare value.
static_assert(!std::is_constructible_v<Refined<positive, int>, int>);
static_assert(!std::is_default_constructible_v<Refined<positive, int>>);
static_assert(!std::is_constructible_v<SealedRefined<positive, int>, int>);
static_assert(!std::is_default_constructible_v<SealedRefined<positive, int>>);
static_assert(std::is_constructible_v<SealedRefined<positive, int>, Refined<positive, int>&&>);

// The mints run in a constant expression, and the checked one runs the
// predicate there.
static_assert(mint_refined<positive>(42).value() == 42);
static_assert(mint_refined_trusted<positive>(-1).value() == -1);
static_assert(mint_sealed_refined<positive>(42).value() == 42);
static_assert(mint_sealed_refined_trusted<positive>(-1).value() == -1);

static_assert(std::is_same_v<typename AlignedTo<64, void*>::predicate_type, std::remove_cv_t<decltype(aligned<64>)>>);
static_assert(
    std::is_same_v<typename Bounded<0, 100, int>::predicate_type, std::remove_cv_t<decltype(in_range<0, 100>)>>);
static_assert(std::is_same_v<typename Capped<255, std::uint32_t>::predicate_type,
                             std::remove_cv_t<decltype(bounded_above<255>)>>);

// The extraction traits see through cv and reference, reject a
// lookalike with the same member aliases, and tell the two wrappers
// apart.
struct LookalikeRefined {
    using value_type = int;
    using predicate_type = decltype(positive);
};

static_assert(is_refined_v<Refined<positive, int>>);
static_assert(is_refined_v<Refined<non_negative, int>>);
static_assert(is_refined_v<SealedRefined<positive, int>>);
static_assert(is_refined_v<Refined<positive, int>&>);
static_assert(is_refined_v<Refined<positive, int>&&>);
static_assert(is_refined_v<Refined<positive, int> const>);
static_assert(is_refined_v<Refined<positive, int> const&>);
static_assert(is_refined_v<Refined<positive, int> volatile>);
static_assert(!is_refined_v<int>);
static_assert(!is_refined_v<int*>);
static_assert(!is_refined_v<Refined<positive, int>*>);
static_assert(!is_refined_v<void>);
static_assert(!is_refined_v<LookalikeRefined>);
static_assert(IsRefined<Refined<positive, int>>);
static_assert(IsRefined<SealedRefined<positive, int> const&>);
static_assert(!IsRefined<int>);
static_assert(std::is_same_v<refined_value_t<Refined<positive, int>>, int>);
static_assert(std::is_same_v<refined_value_t<SealedRefined<positive, int> const&>, int>);
static_assert(std::is_same_v<refined_predicate_type_t<Refined<positive, int>>, refined::predicate_t<positive>>);
static_assert(!refined_is_sealed_v<Refined<positive, int>>);
static_assert(refined_is_sealed_v<SealedRefined<positive, int>>);

}  // namespace detail::refined_self_test

namespace detail::refined_algebra_self_test {

// Minting through the trusted door keeps this a test of whether the
// composed-predicate type is constructible, without also depending on
// the predicate being evaluated at compile time.
using PositiveCapped = Refined<all_of<positive, bounded_above<100>>, int>;
using AlignedNonNullPtr = Refined<all_of<non_null, aligned<64>>, void*>;

[[maybe_unused]] constexpr auto pc1_witness = mint_refined_trusted<all_of<positive, bounded_above<100>>, int>(42);
[[maybe_unused]] constexpr auto pc2_witness = mint_refined_trusted<all_of<positive, bounded_above<100>>, int>(1);

// The checked mint in a constant expression runs the predicate there,
// and the alias trampoline admits the boundary where Lo equals Hi.
[[maybe_unused]] constexpr Bounded<0, 100, int> b_normal_witness = mint_refined<in_range<0, 100>, int>(50);
[[maybe_unused]] constexpr Bounded<5, 5, int> b_equal_witness = mint_refined<in_range<5, 5>, int>(5);

}  // namespace detail::refined_algebra_self_test

}  // namespace fixy
