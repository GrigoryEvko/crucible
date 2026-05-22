#pragma once

// ── crucible::safety::FpModePinned<auto Mode, typename T> ──────────
//
// FIXY-V-090: 11 Graded wrappers + composite for the FP-mode sub-axes
// shipped in V-088 (DimensionAxis::FpMode enumerator + lattice
// scaffolding) and V-089 (11 ChainLattice algebras).
//
// One generic class template `FpModePinned<Mode, T>` is parameterized
// on an `auto Mode` NTTP whose enum type discriminates which sub-axis
// the wrapper pins.  11 type aliases (`FpRoundingPinned`,
// `FpFtzPinned`, ..., `FpConstantRoundingPinned`) bind the concrete
// per-axis spelling.  Distinct enum types → distinct class template
// instantiations → 11 disjoint types per (Mode, T) — necessary for
// the row_hash specializations (RowHashFold.h salts 0x21..0x2B) and
// DimensionTraits bindings to dispatch cleanly per sub-axis.
//
// Substrate per wrapper:
//   Graded<ModalityKind::Absolute, Fp<Axis>Lattice::At<Mode>, T>
//
// Regime 1 (zero-cost EBO collapse): Fp<Axis>Lattice::At<Mode>::
// element_type is empty by construction (V-089 verifies this for all
// 11 sub-axes); Graded's [[no_unique_address]] grade_ EBO-collapses;
// sizeof(FpModePinned<Mode, T>) == sizeof(T) at -O3.
//
// ── Why Modality::Absolute (not Comonad) ────────────────────────────
//
// An FP-mode pin is a STATIC property of the producer's evaluation
// regime — "this value was computed under FpRounding::RoundToNearestEven
// + FpFtz::FlushToZero + ...".  Mutating the inner T (e.g., updating an
// FP accumulator under the same rounding regime) does NOT change the
// pinned mode.  Absolute is the right modality: it admits `peek_mut`
// and `swap`, which are necessary for production sites that compute
// in-place under a pinned regime.
//
// Contrast Witness<Tier, T> (Comonad — the witness extraction is the
// counit) and Secret<T> (Comonad — declassify is the counit, gated by
// policy tags).  FP-mode pins do not have a "counit" — extracting the
// raw T does not erase a meaningful invariant; the mode metadata is
// type-level annotation about how the producer computed the value,
// not about the value's bytes.
//
// ── Why NO relax<>/satisfies<> conversion API ───────────────────────
//
// Unlike Witness — where a producer at FORMALLY_VERIFIED satisfies a
// consumer at TEST_PASSED (stronger proof serves weaker requirement)
// — FP-mode wrappers admit NO cross-mode conversion.  An IEEE 754 FP
// value computed under FpRounding::RoundToZero has DIFFERENT BIT
// PATTERNS than the same expression computed under RoundToNearestEven.
// "Relaxing" the mode pin would lie about provenance — the bits are
// only valid under their original mode.  The only sound mode change
// is to RECOMPUTE under the new mode at a producer site that has
// hardware/software gates for that mode.
//
// Consequence: each FpModePinned<Mode, T> is a strictly-pinned type
// at every (Mode, T) cell.  Cross-mode assignment / equality / mixing
// is a compile error.  This is the HS14 mismatch class neg-compile
// fixtures witness — see test/safety_neg/neg_fp_*_cross_mode.cpp.
//
// ── Composite FpModeComposite<...> — canonical §XVI nested stack ────
//
// The 11 axes compose ORTHOGONALLY (each is a separate dimension of
// the FP-mode taxonomy).  The natural composite is a nested wrapper
// stack — outermost FpRoundingPinned (canonical first axis) inward
// to FpConstantRoundingPinned (last) wrapping T.  Each layer EBO-
// collapses; the full 11-deep nest has sizeof(T).
//
// The composite IS NOT a separate class — it is a type alias for the
// canonical nesting.  Composing row_hash through the 11 layers is
// automatic via the existing per-axis row_hash_contribution
// specializations (no separate composite row_hash needed).
//
// At the lattice level, FpModeProductLattice (in algebra/lattices/
// FpModeLattice.h) is the algebraic peer of the composite — a
// ProductLattice over the 11 sub-axis ChainLattices.  Most production
// code uses the wrapper nest; FpModeProductLattice exists for
// algebraic compositions that need to reason about the 11-axis order
// as a single object (e.g., V-091 CollisionCatalog rules F101-F105
// expressing forbidden 2-axis combinations).
//
// ── §XXI Universal Mint Pattern ─────────────────────────────────────
//
// Per CLAUDE.md §XXI: every cross-tier composition factory is named
// `mint_<noun>` so `grep "mint_"` finds the boundary.  V-090 ships
// 11 mint factories + 1 composite mint:
//   mint_fp_rounding<Mode, T>(args...)
//   mint_fp_ftz<Mode, T>(args...)
//   mint_fp_contract<Mode, T>(args...)
//   ... × 11
//   mint_fp_mode_composite<R, F, C, Tr, D, N, I, Cl, L, Re, Cr, T>(args...)
//
// Each is `[[nodiscard]] constexpr noexcept` with the requires-clause
// gating T's construction from args.
//
// HS14 floor: ≥2 distinct mismatch class neg-compile fixtures per
// per-axis mint.  Coverage shipped at test/safety_neg/:
//   1. cross_mode  — Mode mismatch between two FpAxisPinned<...> values
//   2. mint_unconstructible — mint<...,T>(bad-args) where T is not
//      constructible from the supplied args
// 11 axes × 2 = 22 fixtures, plus 2 composite-mint fixtures = 24 total.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/FpModeLattice.h>

#include <concepts>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the 11 FpMode sub-axis enums into the safety:: namespace so
// production call sites can write `safety::FpRoundingPinned<RTE, T>`
// without spelling the full `algebra::lattices::FpRounding` path.
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

// Hoist the per-axis Lattice structs too — needed for At<Mode>
// resolution inside the generic FpModePinned class.
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

// ── Per-mode-type → Lattice traits ──────────────────────────────────
//
// Given an FP-mode enum type (FpRounding / FpFtz / ...), recover the
// associated ChainLattice from V-089.  The generic FpModePinned
// class uses this to compute its lattice_type from the NTTP Mode's
// enum type at instantiation site.

template <typename E> struct fp_axis_lattice_for;  // primary undefined

template <> struct fp_axis_lattice_for<FpRounding>          { using type = FpRoundingLattice;          };
template <> struct fp_axis_lattice_for<FpFtz>               { using type = FpFtzLattice;               };
template <> struct fp_axis_lattice_for<FpContract>          { using type = FpContractLattice;          };
template <> struct fp_axis_lattice_for<FpTrapMask>          { using type = FpTrapMaskLattice;          };
template <> struct fp_axis_lattice_for<FpDenormalInput>     { using type = FpDenormalInputLattice;     };
template <> struct fp_axis_lattice_for<FpNanPolicy>         { using type = FpNanPolicyLattice;         };
template <> struct fp_axis_lattice_for<FpInfPolicy>         { using type = FpInfPolicyLattice;         };
template <> struct fp_axis_lattice_for<FpComplexLayout>     { using type = FpComplexLayoutLattice;     };
template <> struct fp_axis_lattice_for<FpLibmPolicy>        { using type = FpLibmPolicyLattice;        };
template <> struct fp_axis_lattice_for<FpReassociate>       { using type = FpReassociateLattice;       };
template <> struct fp_axis_lattice_for<FpConstantRounding>  { using type = FpConstantRoundingLattice;  };

template <typename E>
using fp_axis_lattice_for_t = typename fp_axis_lattice_for<E>::type;

template <typename E>
concept IsFpAxisMode = requires { typename fp_axis_lattice_for<E>::type; };

}  // namespace detail::fp_mode_traits

// ── FpModePinned<auto Mode, typename T> ─────────────────────────────
//
// The generic per-axis FP-mode carrier.  `Mode`'s enum type selects
// the lattice via fp_axis_lattice_for.  11 type aliases below bind
// the per-axis spellings.
//
// Constraints note: the class template is intentionally unconstrained
// so its forward-decl in safety/diag/RowHashFold.h (which cannot reach
// detail::fp_mode_traits) matches the definition.  Misuse with a non-FP
// NTTP type produces a hard error on the first reference to
// `fp_axis_lattice_for_t<mode_type>` below — which is fine: no SFINAE
// overload-set membership depends on FpModePinned's well-formedness.
template <auto Mode, typename T>
class [[nodiscard]] FpModePinned {
public:
    using mode_type     = decltype(Mode);
    using outer_lattice = detail::fp_mode_traits::fp_axis_lattice_for_t<mode_type>;
    using value_type    = T;
    using lattice_type  = typename outer_lattice::template At<Mode>;
    using graded_type   = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;
    static constexpr mode_type mode = Mode;

private:
    graded_type impl_;

public:
    // Default: T{} at the pinned mode.  Production callers should
    // prefer the explicit-T constructor or `mint_fp_<axis><...>(args)`
    // factories at sites that actually establish the FP regime.
    constexpr FpModePinned() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit FpModePinned(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit FpModePinned(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    constexpr FpModePinned(const FpModePinned&)            = default;
    constexpr FpModePinned(FpModePinned&&)                 = default;
    constexpr FpModePinned& operator=(const FpModePinned&) = default;
    constexpr FpModePinned& operator=(FpModePinned&&)      = default;
    ~FpModePinned()                                        = default;

    // ── Diagnostic surface (per GradedWrapper concept) ─────────────
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }

    // ── Read-only access ────────────────────────────────────────────
    [[nodiscard]] constexpr T const& peek() const& noexcept {
        return impl_.peek();
    }

    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).consume();
    }

    // ── Mutable access ──────────────────────────────────────────────
    //
    // Absolute modality admits peek_mut/swap unconditionally — the
    // pinned mode is a TYPE-level fact about how the value was produced,
    // not a content-derived invariant.  In-place computation under the
    // pinned mode (e.g., reduction accumulator updates) is sound.
    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        return impl_.peek_mut();
    }

    constexpr void swap(FpModePinned& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(FpModePinned& a, FpModePinned& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── Equality (same-mode only) ───────────────────────────────────
    //
    // The friend signature pins both args to the SAME <Mode, T>
    // instantiation; cross-mode equality is rejected at overload
    // resolution.  HS14 mismatch class 1 fixtures witness this.
    [[nodiscard]] friend constexpr bool operator==(
        FpModePinned const& a, FpModePinned const& b) noexcept(
        noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) {
            { x == y } -> std::convertible_to<bool>;
        }
    {
        return a.peek() == b.peek();
    }
};

// ── 11 type aliases for per-axis spellings ──────────────────────────
//
// Each alias instantiates FpModePinned with the corresponding sub-
// axis enum's NTTP type.  Distinct enum types → distinct template
// instantiations → 11 disjoint types per (Mode, T).

template <FpRounding         Mode, typename T> using FpRoundingPinned         = FpModePinned<Mode, T>;
template <FpFtz              Mode, typename T> using FpFtzPinned              = FpModePinned<Mode, T>;
template <FpContract         Mode, typename T> using FpContractPinned         = FpModePinned<Mode, T>;
template <FpTrapMask         Mode, typename T> using FpTrapMaskPinned         = FpModePinned<Mode, T>;
template <FpDenormalInput    Mode, typename T> using FpDenormalInputPinned    = FpModePinned<Mode, T>;
template <FpNanPolicy        Mode, typename T> using FpNanPolicyPinned        = FpModePinned<Mode, T>;
template <FpInfPolicy        Mode, typename T> using FpInfPolicyPinned        = FpModePinned<Mode, T>;
template <FpComplexLayout    Mode, typename T> using FpComplexLayoutPinned    = FpModePinned<Mode, T>;
template <FpLibmPolicy       Mode, typename T> using FpLibmPolicyPinned       = FpModePinned<Mode, T>;
template <FpReassociate      Mode, typename T> using FpReassociatePinned      = FpModePinned<Mode, T>;
template <FpConstantRounding Mode, typename T> using FpConstantRoundingPinned = FpModePinned<Mode, T>;

// ── §XXI Universal Mint factories (11 per-axis + 1 composite) ──────

#define CRUCIBLE_FP_AXIS_MINT(MintName, AliasName, ModeEnum)               \
    template <ModeEnum Mode, typename T, typename... Args>                 \
        requires std::is_constructible_v<T, Args...>                       \
    [[nodiscard]] constexpr AliasName<Mode, T> MintName(Args&&... args)    \
        noexcept(std::is_nothrow_constructible_v<T, Args...>)              \
    {                                                                      \
        return AliasName<Mode, T>{std::in_place,                           \
                                  std::forward<Args>(args)...};            \
    }

CRUCIBLE_FP_AXIS_MINT(mint_fp_rounding,          FpRoundingPinned,         FpRounding)
CRUCIBLE_FP_AXIS_MINT(mint_fp_ftz,               FpFtzPinned,              FpFtz)
CRUCIBLE_FP_AXIS_MINT(mint_fp_contract,          FpContractPinned,         FpContract)
CRUCIBLE_FP_AXIS_MINT(mint_fp_trap_mask,         FpTrapMaskPinned,         FpTrapMask)
CRUCIBLE_FP_AXIS_MINT(mint_fp_denormal_input,    FpDenormalInputPinned,    FpDenormalInput)
CRUCIBLE_FP_AXIS_MINT(mint_fp_nan_policy,        FpNanPolicyPinned,        FpNanPolicy)
CRUCIBLE_FP_AXIS_MINT(mint_fp_inf_policy,        FpInfPolicyPinned,        FpInfPolicy)
CRUCIBLE_FP_AXIS_MINT(mint_fp_complex_layout,    FpComplexLayoutPinned,    FpComplexLayout)
CRUCIBLE_FP_AXIS_MINT(mint_fp_libm_policy,       FpLibmPolicyPinned,       FpLibmPolicy)
CRUCIBLE_FP_AXIS_MINT(mint_fp_reassociate,       FpReassociatePinned,      FpReassociate)
CRUCIBLE_FP_AXIS_MINT(mint_fp_constant_rounding, FpConstantRoundingPinned, FpConstantRounding)

#undef CRUCIBLE_FP_AXIS_MINT

// ── Composite FpModeComposite — canonical 11-deep nested stack ──────
//
// The canonical §XVI nesting order for FP-mode wrappers, defined as a
// type alias over the 11 per-axis wrappers.  Outermost is
// FpRoundingPinned (first sub-axis in V-088's enum ordering); innermost
// is FpConstantRoundingPinned wrapping the raw T.
//
// Each layer is regime-1 EBO collapse, so sizeof(FpModeComposite<...,
// T>) == sizeof(T).  row_hash composes automatically through the 11
// per-axis row_hash_contribution specializations.

template <FpRounding         R,
          FpFtz              F,
          FpContract         C,
          FpTrapMask         Tr,
          FpDenormalInput    D,
          FpNanPolicy        N,
          FpInfPolicy        I,
          FpComplexLayout    Cl,
          FpLibmPolicy       L,
          FpReassociate      Re,
          FpConstantRounding Cr,
          typename T>
using FpModeComposite =
    FpRoundingPinned<R,
        FpFtzPinned<F,
            FpContractPinned<C,
                FpTrapMaskPinned<Tr,
                    FpDenormalInputPinned<D,
                        FpNanPolicyPinned<N,
                            FpInfPolicyPinned<I,
                                FpComplexLayoutPinned<Cl,
                                    FpLibmPolicyPinned<L,
                                        FpReassociatePinned<Re,
                                            FpConstantRoundingPinned<Cr, T>
                                        >
                                    >
                                >
                            >
                        >
                    >
                >
            >
        >
    >;

// Composite mint — synthesizes the full 11-axis nest from inside out.
// Each layer's explicit-T constructor accepts the inner-layer value;
// the recursive expansion builds bottom-up and folds outward.
template <FpRounding         R,
          FpFtz              F,
          FpContract         C,
          FpTrapMask         Tr,
          FpDenormalInput    D,
          FpNanPolicy        N,
          FpInfPolicy        I,
          FpComplexLayout    Cl,
          FpLibmPolicy       L,
          FpReassociate      Re,
          FpConstantRounding Cr,
          typename T,
          typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr FpModeComposite<R, F, C, Tr, D, N, I, Cl, L, Re, Cr, T>
mint_fp_mode_composite(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
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
    return L01{
        L02{
            L03{
                L04{
                    L05{
                        L06{
                            L07{
                                L08{
                                    L09{
                                        L10{
                                            L11{std::in_place,
                                                std::forward<Args>(args)...}
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    };
}

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline: every safety
// wrapper header MUST ship `inline void runtime_smoke_test()` with
// non-constant arguments so consteval/SFINAE/inline-body bugs surface
// at link/run time rather than masking under pure static_assert.
inline void fp_mode_runtime_smoke_test() {
    // Round-trip mint → peek → consume across all 11 axes.
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

    // Composite stack — build inside out then move-out the value.
    {
        FpConstantRoundingPinned<FpConstantRounding::SameAsRuntime, int> inner{99};
        FpReassociatePinned<FpReassociate::Forbidden,
            decltype(inner)> e{std::move(inner)};
        FpLibmPolicyPinned<FpLibmPolicy::ScalarLibm,
            decltype(e)> l{std::move(e)};
        FpComplexLayoutPinned<FpComplexLayout::Interleaved,
            decltype(l)> x{std::move(l)};
        FpInfPolicyPinned<FpInfPolicy::PropagateInfinity,
            decltype(x)> i{std::move(x)};
        FpNanPolicyPinned<FpNanPolicy::PropagateQuiet,
            decltype(i)> n{std::move(i)};
        FpDenormalInputPinned<FpDenormalInput::HonorDenormals,
            decltype(n)> d{std::move(n)};
        FpTrapMaskPinned<FpTrapMask::AllMasked,
            decltype(d)> t{std::move(d)};
        FpContractPinned<FpContract::Off,
            decltype(t)> c{std::move(t)};
        FpFtzPinned<FpFtz::PreserveSubnormals,
            decltype(c)> f{std::move(c)};
        FpRoundingPinned<FpRounding::RoundToZero,
            decltype(f)> r{std::move(f)};
        // The composite is byte-equivalent to the bare int.
        static_assert(sizeof(decltype(r)) == sizeof(int));
        [[maybe_unused]] auto out = std::move(r).consume();
    }
}

// ── Compile-time invariants ─────────────────────────────────────────
//
// Cross-cutting properties asserted at header-inclusion time.

namespace detail::fp_mode_safety_self_test {

// ── EBO collapse: every per-axis wrapper is sizeof(T) for trivial T ─
//
// All 11 sub-axis At<Mode>::element_type are EMPTY (V-089 pinned this
// at FpModeLattice.h's CRUCIBLE_FP_LATTICE_VERIFY block).  Combined
// with Graded's [[no_unique_address]] grade_, the wrapper is
// byte-equivalent to T.  If any of the 11 lattices' At<> regresses,
// this assertion lights.
static_assert(sizeof(FpRoundingPinned<FpRounding::RoundToNearestEven, int>)         == sizeof(int));
static_assert(sizeof(FpFtzPinned<FpFtz::FlushToZero, int>)                          == sizeof(int));
static_assert(sizeof(FpContractPinned<FpContract::Off, int>)                        == sizeof(int));
static_assert(sizeof(FpTrapMaskPinned<FpTrapMask::AllMasked, int>)                  == sizeof(int));
static_assert(sizeof(FpDenormalInputPinned<FpDenormalInput::HonorDenormals, int>)   == sizeof(int));
static_assert(sizeof(FpNanPolicyPinned<FpNanPolicy::PropagateQuiet, int>)           == sizeof(int));
static_assert(sizeof(FpInfPolicyPinned<FpInfPolicy::PropagateInfinity, int>)        == sizeof(int));
static_assert(sizeof(FpComplexLayoutPinned<FpComplexLayout::Interleaved, int>)      == sizeof(int));
static_assert(sizeof(FpLibmPolicyPinned<FpLibmPolicy::ScalarLibm, int>)             == sizeof(int));
static_assert(sizeof(FpReassociatePinned<FpReassociate::Forbidden, int>)            == sizeof(int));
static_assert(sizeof(FpConstantRoundingPinned<FpConstantRounding::SameAsRuntime, int>) == sizeof(int));

// ── Cross-axis type distinctness (NTTP-keyed instantiations) ────────
//
// Two FpModePinned<...> with NTTPs of different enum types instantiate
// to DIFFERENT class types.  This is what makes the per-axis row_hash
// salts and DimensionTraits specializations dispatch cleanly.
static_assert(!std::is_same_v<FpRoundingPinned<FpRounding::RoundToZero, int>,
                              FpFtzPinned<FpFtz::PreserveSubnormals, int>>);
static_assert(!std::is_same_v<FpContractPinned<FpContract::Off, int>,
                              FpReassociatePinned<FpReassociate::Forbidden, int>>);
static_assert(!std::is_same_v<FpTrapMaskPinned<FpTrapMask::AllMasked, int>,
                              FpNanPolicyPinned<FpNanPolicy::PropagateQuiet, int>>);

// ── Cross-MODE type distinctness within same axis ───────────────────
//
// Two FpRoundingPinned<...> with different rounding modes are distinct
// types — the HS14 cross_mode neg-compile fixtures rely on this.
static_assert(!std::is_same_v<FpRoundingPinned<FpRounding::RoundToZero, int>,
                              FpRoundingPinned<FpRounding::RoundToNearestEven, int>>);
static_assert(!std::is_same_v<FpFtzPinned<FpFtz::PreserveSubnormals, int>,
                              FpFtzPinned<FpFtz::FlushToZero, int>>);

// ── Static `mode` accessor returns the pinned NTTP ──────────────────
//
// Per-wrapper `mode` exposes the NTTP for dispatch / introspection at
// downstream consumer sites.  Pinning the recovery here guards against
// a class-body refactor that silently changes the spelling.
static_assert(FpRoundingPinned<FpRounding::RoundToNearestEven, int>::mode
              == FpRounding::RoundToNearestEven);
static_assert(FpFtzPinned<FpFtz::FlushToZero, int>::mode
              == FpFtz::FlushToZero);
static_assert(FpContractPinned<FpContract::Fast, int>::mode
              == FpContract::Fast);

// ── Composite EBO collapse ──────────────────────────────────────────
//
// The 11-deep canonical nest collapses to sizeof(T).  This pins the
// load-bearing claim that V-090 incurs ZERO runtime cost per axis.
static_assert(sizeof(FpModeComposite<
    FpRounding::RoundToZero, FpFtz::PreserveSubnormals,
    FpContract::Off, FpTrapMask::AllMasked,
    FpDenormalInput::HonorDenormals, FpNanPolicy::PropagateQuiet,
    FpInfPolicy::PropagateInfinity, FpComplexLayout::Interleaved,
    FpLibmPolicy::ScalarLibm, FpReassociate::Forbidden,
    FpConstantRounding::SameAsRuntime, int>) == sizeof(int));

}  // namespace detail::fp_mode_safety_self_test

}  // namespace crucible::safety
