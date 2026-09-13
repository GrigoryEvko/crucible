#pragma once

// A value tagged with the floating-point evaluation regime its producer
// computed it under.  There is deliberately no widening or relaxing
// conversion between modes: the same expression evaluated under two
// rounding modes yields different bit patterns, so a value's bits are
// only meaningful under the mode that produced them.  The one sound way
// to change mode is to recompute at a producer that establishes the new
// one.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/FpModeLattice.h>

#include <concepts>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::FpRounding;
using ::crucible::algebra::lattices::FpFtz;
using ::crucible::algebra::lattices::FpContract;
using ::crucible::algebra::lattices::FpTrapMask;
using ::crucible::algebra::lattices::FpDenormalInput;
using ::crucible::algebra::lattices::FpNanPolicy;
using ::crucible::algebra::lattices::FpInfPolicy;
using ::crucible::algebra::lattices::FpComplexLayout;
using ::crucible::algebra::lattices::FpLibmPolicy;
using ::crucible::algebra::lattices::FpReassociate;
using ::crucible::algebra::lattices::FpConstantRounding;

using ::crucible::algebra::lattices::FpRoundingLattice;
using ::crucible::algebra::lattices::FpFtzLattice;
using ::crucible::algebra::lattices::FpContractLattice;
using ::crucible::algebra::lattices::FpTrapMaskLattice;
using ::crucible::algebra::lattices::FpDenormalInputLattice;
using ::crucible::algebra::lattices::FpNanPolicyLattice;
using ::crucible::algebra::lattices::FpInfPolicyLattice;
using ::crucible::algebra::lattices::FpComplexLayoutLattice;
using ::crucible::algebra::lattices::FpLibmPolicyLattice;
using ::crucible::algebra::lattices::FpReassociateLattice;
using ::crucible::algebra::lattices::FpConstantRoundingLattice;

namespace detail::fp_mode_traits {

template <typename E>
struct fp_axis_lattice_for;

template <>
struct fp_axis_lattice_for<FpRounding> {
    using type = FpRoundingLattice;
};
template <>
struct fp_axis_lattice_for<FpFtz> {
    using type = FpFtzLattice;
};
template <>
struct fp_axis_lattice_for<FpContract> {
    using type = FpContractLattice;
};
template <>
struct fp_axis_lattice_for<FpTrapMask> {
    using type = FpTrapMaskLattice;
};
template <>
struct fp_axis_lattice_for<FpDenormalInput> {
    using type = FpDenormalInputLattice;
};
template <>
struct fp_axis_lattice_for<FpNanPolicy> {
    using type = FpNanPolicyLattice;
};
template <>
struct fp_axis_lattice_for<FpInfPolicy> {
    using type = FpInfPolicyLattice;
};
template <>
struct fp_axis_lattice_for<FpComplexLayout> {
    using type = FpComplexLayoutLattice;
};
template <>
struct fp_axis_lattice_for<FpLibmPolicy> {
    using type = FpLibmPolicyLattice;
};
template <>
struct fp_axis_lattice_for<FpReassociate> {
    using type = FpReassociateLattice;
};
template <>
struct fp_axis_lattice_for<FpConstantRounding> {
    using type = FpConstantRoundingLattice;
};

template <typename E>
using fp_axis_lattice_for_t = typename fp_axis_lattice_for<E>::type;

template <typename E>
concept IsFpAxisMode = requires { typename fp_axis_lattice_for<E>::type; };

}  // namespace detail::fp_mode_traits

// The template carries no constraint on purpose, so that a forward
// declaration in a header that cannot reach the traits above still
// matches this definition.  A non-mode NTTP is a hard error at the
// first use of the trait, and no overload set depends on this template
// being well formed.
template <auto Mode, typename T>
class [[nodiscard]] FpModePinned {
public:
    using mode_type = decltype(Mode);
    using outer_lattice = detail::fp_mode_traits::fp_axis_lattice_for_t<mode_type>;
    using value_type = T;
    using lattice_type = typename outer_lattice::template At<Mode>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;
    static constexpr mode_type mode = Mode;

private:
    graded_type impl_;

public:
    constexpr FpModePinned() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit FpModePinned(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit FpModePinned(std::in_place_t,
                                    Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                             && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr FpModePinned(const FpModePinned&) = default;
    constexpr FpModePinned(FpModePinned&&) = default;
    constexpr FpModePinned& operator=(const FpModePinned&) = default;
    constexpr FpModePinned& operator=(FpModePinned&&) = default;
    ~FpModePinned() = default;

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }

    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }

    // Mutation in place is sound because the pinned mode describes how
    // the value is produced rather than what it holds.  An accumulator
    // updated under the same regime keeps its pin.
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    constexpr void swap(FpModePinned& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(FpModePinned& a, FpModePinned& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    // Both operands are pinned to one instantiation, so comparing values
    // produced under different modes fails overload resolution.
    [[nodiscard]] friend constexpr bool operator==(FpModePinned const& a,
                                                   FpModePinned const& b) noexcept(noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) {
            { x == y } -> std::convertible_to<bool>;
        }
    {
        return a.peek() == b.peek();
    }
};

template <FpRounding Mode, typename T>
using FpRoundingPinned = FpModePinned<Mode, T>;
template <FpFtz Mode, typename T>
using FpFtzPinned = FpModePinned<Mode, T>;
template <FpContract Mode, typename T>
using FpContractPinned = FpModePinned<Mode, T>;
template <FpTrapMask Mode, typename T>
using FpTrapMaskPinned = FpModePinned<Mode, T>;
template <FpDenormalInput Mode, typename T>
using FpDenormalInputPinned = FpModePinned<Mode, T>;
template <FpNanPolicy Mode, typename T>
using FpNanPolicyPinned = FpModePinned<Mode, T>;
template <FpInfPolicy Mode, typename T>
using FpInfPolicyPinned = FpModePinned<Mode, T>;
template <FpComplexLayout Mode, typename T>
using FpComplexLayoutPinned = FpModePinned<Mode, T>;
template <FpLibmPolicy Mode, typename T>
using FpLibmPolicyPinned = FpModePinned<Mode, T>;
template <FpReassociate Mode, typename T>
using FpReassociatePinned = FpModePinned<Mode, T>;
template <FpConstantRounding Mode, typename T>
using FpConstantRoundingPinned = FpModePinned<Mode, T>;

#define CRUCIBLE_FP_AXIS_MINT(MintName, AliasName, ModeEnum)                      \
    template <ModeEnum Mode, typename T, typename... Args>                        \
        requires std::is_constructible_v<T, Args...>                              \
    [[nodiscard]] constexpr AliasName<Mode, T> MintName(Args&&... args) noexcept( \
        std::is_nothrow_constructible_v<T, Args...>) {                            \
        return AliasName<Mode, T>{std::in_place, std::forward<Args>(args)...};    \
    }

CRUCIBLE_FP_AXIS_MINT(mint_fp_rounding, FpRoundingPinned, FpRounding)
CRUCIBLE_FP_AXIS_MINT(mint_fp_ftz, FpFtzPinned, FpFtz)
CRUCIBLE_FP_AXIS_MINT(mint_fp_contract, FpContractPinned, FpContract)
CRUCIBLE_FP_AXIS_MINT(mint_fp_trap_mask, FpTrapMaskPinned, FpTrapMask)
CRUCIBLE_FP_AXIS_MINT(mint_fp_denormal_input, FpDenormalInputPinned, FpDenormalInput)
CRUCIBLE_FP_AXIS_MINT(mint_fp_nan_policy, FpNanPolicyPinned, FpNanPolicy)
CRUCIBLE_FP_AXIS_MINT(mint_fp_inf_policy, FpInfPolicyPinned, FpInfPolicy)
CRUCIBLE_FP_AXIS_MINT(mint_fp_complex_layout, FpComplexLayoutPinned, FpComplexLayout)
CRUCIBLE_FP_AXIS_MINT(mint_fp_libm_policy, FpLibmPolicyPinned, FpLibmPolicy)
CRUCIBLE_FP_AXIS_MINT(mint_fp_reassociate, FpReassociatePinned, FpReassociate)
CRUCIBLE_FP_AXIS_MINT(mint_fp_constant_rounding, FpConstantRoundingPinned, FpConstantRounding)

#undef CRUCIBLE_FP_AXIS_MINT

// The nesting order is canonical.  A stack built in a different order
// is a different type and hashes to a different cache key.

template <FpRounding R, FpFtz F, FpContract C, FpTrapMask Tr, FpDenormalInput D, FpNanPolicy N, FpInfPolicy I,
          FpComplexLayout Cl, FpLibmPolicy L, FpReassociate Re, FpConstantRounding Cr, typename T>
using FpModeComposite = FpRoundingPinned<
    R,
    FpFtzPinned<
        F,
        FpContractPinned<
            C,
            FpTrapMaskPinned<
                Tr, FpDenormalInputPinned<
                        D, FpNanPolicyPinned<
                               N, FpInfPolicyPinned<
                                      I, FpComplexLayoutPinned<
                                             Cl, FpLibmPolicyPinned<L, FpReassociatePinned<Re, FpConstantRoundingPinned<
                                                                                                   Cr, T>>>>>>>>>>>;

template <FpRounding R, FpFtz F, FpContract C, FpTrapMask Tr, FpDenormalInput D, FpNanPolicy N, FpInfPolicy I,
          FpComplexLayout Cl, FpLibmPolicy L, FpReassociate Re, FpConstantRounding Cr, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr FpModeComposite<R, F, C, Tr, D, N, I, Cl, L, Re, Cr, T>
mint_fp_mode_composite(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    using L11 = FpConstantRoundingPinned<Cr, T>;
    using L10 = FpReassociatePinned<Re, L11>;
    using L09 = FpLibmPolicyPinned<L, L10>;
    using L08 = FpComplexLayoutPinned<Cl, L09>;
    using L07 = FpInfPolicyPinned<I, L08>;
    using L06 = FpNanPolicyPinned<N, L07>;
    using L05 = FpDenormalInputPinned<D, L06>;
    using L04 = FpTrapMaskPinned<Tr, L05>;
    using L03 = FpContractPinned<C, L04>;
    using L02 = FpFtzPinned<F, L03>;
    using L01 = FpRoundingPinned<R, L02>;
    return L01{L02{L03{L04{L05{L06{L07{L08{L09{L10{L11{std::in_place, std::forward<Args>(args)...}}}}}}}}}}};
}

// The arguments here are non-constant on purpose.  A pure static_assert
// suite masks bugs that only appear when the body is instantiated for
// runtime evaluation.
inline void fp_mode_runtime_smoke_test() {
    {
        auto r = mint_fp_rounding<FpRounding::RoundToNearestEven, int>(7);
        [[maybe_unused]] int v = r.peek();
        [[maybe_unused]] int w = std::move(r).consume();
    }
    {
        auto f = mint_fp_ftz<FpFtz::FlushToZero, int>(42);
        f.peek_mut() += 1;
        [[maybe_unused]] int v = std::move(f).consume();
    }
    {
        auto c = mint_fp_contract<FpContract::Fast, int>(13);
        [[maybe_unused]] int v = std::move(c).consume();
    }
    {
        auto t = mint_fp_trap_mask<FpTrapMask::AllMasked, int>(0);
        [[maybe_unused]] int v = std::move(t).consume();
    }
    {
        auto d = mint_fp_denormal_input<FpDenormalInput::HonorDenormals, int>(1);
        [[maybe_unused]] int v = std::move(d).consume();
    }
    {
        auto n = mint_fp_nan_policy<FpNanPolicy::PropagateQuiet, int>(2);
        [[maybe_unused]] int v = std::move(n).consume();
    }
    {
        auto i = mint_fp_inf_policy<FpInfPolicy::PropagateInfinity, int>(3);
        [[maybe_unused]] int v = std::move(i).consume();
    }
    {
        auto x = mint_fp_complex_layout<FpComplexLayout::Interleaved, int>(4);
        [[maybe_unused]] int v = std::move(x).consume();
    }
    {
        auto l = mint_fp_libm_policy<FpLibmPolicy::ScalarLibm, int>(5);
        [[maybe_unused]] int v = std::move(l).consume();
    }
    {
        auto e = mint_fp_reassociate<FpReassociate::Forbidden, int>(6);
        [[maybe_unused]] int v = std::move(e).consume();
    }
    {
        auto k = mint_fp_constant_rounding<FpConstantRounding::SameAsRuntime, int>(8);
        [[maybe_unused]] int v = std::move(k).consume();
    }

    {
        FpConstantRoundingPinned<FpConstantRounding::SameAsRuntime, int> inner{99};
        FpReassociatePinned<FpReassociate::Forbidden, decltype(inner)> e{std::move(inner)};
        FpLibmPolicyPinned<FpLibmPolicy::ScalarLibm, decltype(e)> l{std::move(e)};
        FpComplexLayoutPinned<FpComplexLayout::Interleaved, decltype(l)> x{std::move(l)};
        FpInfPolicyPinned<FpInfPolicy::PropagateInfinity, decltype(x)> i{std::move(x)};
        FpNanPolicyPinned<FpNanPolicy::PropagateQuiet, decltype(i)> n{std::move(i)};
        FpDenormalInputPinned<FpDenormalInput::HonorDenormals, decltype(n)> d{std::move(n)};
        FpTrapMaskPinned<FpTrapMask::AllMasked, decltype(d)> t{std::move(d)};
        FpContractPinned<FpContract::Off, decltype(t)> c{std::move(t)};
        FpFtzPinned<FpFtz::PreserveSubnormals, decltype(c)> f{std::move(c)};
        FpRoundingPinned<FpRounding::RoundToZero, decltype(f)> r{std::move(f)};
        static_assert(sizeof(decltype(r)) == sizeof(int));
        [[maybe_unused]] auto out = std::move(r).consume();
    }
}

namespace detail::fp_mode_safety_self_test {

static_assert(sizeof(FpRoundingPinned<FpRounding::RoundToNearestEven, int>) == sizeof(int));
static_assert(sizeof(FpFtzPinned<FpFtz::FlushToZero, int>) == sizeof(int));
static_assert(sizeof(FpContractPinned<FpContract::Off, int>) == sizeof(int));
static_assert(sizeof(FpTrapMaskPinned<FpTrapMask::AllMasked, int>) == sizeof(int));
static_assert(sizeof(FpDenormalInputPinned<FpDenormalInput::HonorDenormals, int>) == sizeof(int));
static_assert(sizeof(FpNanPolicyPinned<FpNanPolicy::PropagateQuiet, int>) == sizeof(int));
static_assert(sizeof(FpInfPolicyPinned<FpInfPolicy::PropagateInfinity, int>) == sizeof(int));
static_assert(sizeof(FpComplexLayoutPinned<FpComplexLayout::Interleaved, int>) == sizeof(int));
static_assert(sizeof(FpLibmPolicyPinned<FpLibmPolicy::ScalarLibm, int>) == sizeof(int));
static_assert(sizeof(FpReassociatePinned<FpReassociate::Forbidden, int>) == sizeof(int));
static_assert(sizeof(FpConstantRoundingPinned<FpConstantRounding::SameAsRuntime, int>) == sizeof(int));

static_assert(
    !std::is_same_v<FpRoundingPinned<FpRounding::RoundToZero, int>, FpFtzPinned<FpFtz::PreserveSubnormals, int>>);
static_assert(
    !std::is_same_v<FpContractPinned<FpContract::Off, int>, FpReassociatePinned<FpReassociate::Forbidden, int>>);
static_assert(
    !std::is_same_v<FpTrapMaskPinned<FpTrapMask::AllMasked, int>, FpNanPolicyPinned<FpNanPolicy::PropagateQuiet, int>>);

static_assert(!std::is_same_v<FpRoundingPinned<FpRounding::RoundToZero, int>,
                              FpRoundingPinned<FpRounding::RoundToNearestEven, int>>);
static_assert(!std::is_same_v<FpFtzPinned<FpFtz::PreserveSubnormals, int>, FpFtzPinned<FpFtz::FlushToZero, int>>);

static_assert(FpRoundingPinned<FpRounding::RoundToNearestEven, int>::mode == FpRounding::RoundToNearestEven);
static_assert(FpFtzPinned<FpFtz::FlushToZero, int>::mode == FpFtz::FlushToZero);
static_assert(FpContractPinned<FpContract::Fast, int>::mode == FpContract::Fast);

static_assert(
    sizeof(FpModeComposite<FpRounding::RoundToZero, FpFtz::PreserveSubnormals, FpContract::Off, FpTrapMask::AllMasked,
                           FpDenormalInput::HonorDenormals, FpNanPolicy::PropagateQuiet, FpInfPolicy::PropagateInfinity,
                           FpComplexLayout::Interleaved, FpLibmPolicy::ScalarLibm, FpReassociate::Forbidden,
                           FpConstantRounding::SameAsRuntime, int>)
    == sizeof(int));

}  // namespace detail::fp_mode_safety_self_test

}  // namespace crucible::safety
