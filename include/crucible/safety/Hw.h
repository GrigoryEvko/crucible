#pragma once

// ── crucible::safety::Hw<HwInstruction_v Tier, typename T> ──────────
//
// FIXY-V-254 (Agent 11 §3.7): value-level Graded carrier for the V-253
// HwInstruction axis (NoneAllowed ⊑ Scalar ⊑ Vectorizable ⊑
// NonDeterministicTsc ⊑ PrivilegedMsr — V-251 HwInstructionLattice).
// Pins, at the TYPE level, the hardware-instruction capability tier a
// value's producing/consuming kernel actually issues.
//
//   Substrate: Graded<ModalityKind::Absolute, HwInstructionLattice::At<Tier>, T>
//   Regime:    1 (zero-cost EBO collapse — At<Tier>::element_type is
//              empty, sizeof(Hw<Tier, T>) == sizeof(T) at -O3).
//
// Absolute modality.  CAPABILITY-CEILING semantics, identical in shape
// to StdioPinned (chain wrapper): bottom (NoneAllowed) is the safest —
// no hardware-specific instruction, portable to every Mimic backend
// with no ISA proof and the only tier admissible where DetSafe-strict
// BITEXACT holds; top (PrivilegedMsr) is ring-0 MSR/port I/O, admitted
// only inside an Init-context with Permission<warden::tag::Root>.
//
//   satisfies<Ceiling> := HwInstructionLattice::leq(Tier, Ceiling)
//   widen<Higher>()                                  — UP the chain only
//
// A consumer that admits a ceiling C accepts any Hw<Tier, T> with
// Tier ⊑ C.  `Hw<Scalar>` flows into a `satisfies<Vectorizable>`
// admission gate (Scalar ⊑ Vectorizable); `Hw<PrivilegedMsr>` does NOT
// flow into a `satisfies<NonDeterministicTsc>` gate (PrivilegedMsr ⊄
// NonDeterministicTsc) — the consumer-too-weak rejection that
// neg_hw_consumer_too_weak.cpp pins.
//
// ── §XVI canonical wrapper-nesting position ─────────────────────────
//
// Hw sits in the Repr-Vendor neighborhood — between Vendor (WHICH
// backend) and ResidencyHeat (WHERE the value lives).  A worked stack
// for a vectorizable NV kernel:
//
//   Vendor<NV, Hw<Vectorizable, CompiledKernel>>
//
// Each layer EBO-collapses; the nesting cost is sizeof(CompiledKernel).
// The row_hash_contribution<Hw<Tier, Inner>> federation-cache
// discriminator (salt 0x2C) ships in safety/diag/RowHashFold.h, exactly
// like every sister wrapper — the row_hash key is the WRAPPER, never
// the lattice At<>.
//
// ── Use cases (V-262/V-263/V-264 consumers) ────────────────────────
//   - SwissTable.h / cntp/Fec.h #if arms: the SIMD path declares
//     Hw<Vectorizable, T>; the scalar fallback declares Hw<Scalar, T>.
//   - Mimic backend instruction legalization: a kernel pinned ⊑ Scalar
//     is portable to every backend with no ISA proof; a Vectorizable
//     kernel composes with the V-256 SimdWidthPinned ISA proof; an MSR
//     kernel can only land on a ring-0 execution context.
//
//   Axiom coverage:
//     TypeSafe — HwInstruction_v is a strong scoped enum; cross-tier
//                mixing requires std::to_underlying and surfaces at the
//                call site.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//     DetSafe — orthogonal axis; Hw does NOT itself enforce
//                determinism, but NonDeterministicTsc is the tier above
//                which a binding cannot be BITEXACT (couples to a
//                DetSafe downgrade at V-257 grant level).
//   Runtime cost: sizeof(Hw<Tier, T>) == sizeof(T); verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.
//
// §XXI: `mint_hw<Tier, T>(args...)`.  HS14 neg fixtures:
// neg_hw_consumer_too_weak.cpp + neg_hw_widen_to_lower.cpp.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/HwInstructionLattice.h>

#include <concepts>
#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::HwInstructionLattice;
using HwInstruction_v = ::crucible::algebra::lattices::HwInstruction;

template <HwInstruction_v Tier, typename T>
class [[nodiscard]] Hw {
public:
    // ── Public type aliases (GradedWrapper uniform surface) ─────────
    using value_type   = T;
    using lattice_type = HwInstructionLattice::At<Tier>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned instruction tier — exposed for callers doing
    // tier-aware dispatch without instantiating the wrapper.
    static constexpr HwInstruction_v tier = Tier;

private:
    graded_type impl_;

public:
    // ── Construction ────────────────────────────────────────────────
    constexpr Hw() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit Hw(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Hw(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    constexpr Hw(const Hw&)            = default;
    constexpr Hw(Hw&&)                 = default;
    constexpr Hw& operator=(const Hw&) = default;
    constexpr Hw& operator=(Hw&&)      = default;
    ~Hw()                              = default;

    // Equality: compares value bytes within the SAME tier pin.
    // Cross-tier comparison rejected at overload resolution.
    [[nodiscard]] friend constexpr bool operator==(
        Hw const& a, Hw const& b) noexcept(noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek() == b.peek();
    }

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }

    // ── Read-only / mutable access ──────────────────────────────────
    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }
    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    { return std::move(impl_).consume(); }
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    // ── swap ────────────────────────────────────────────────────────
    constexpr void swap(Hw& other) noexcept(std::is_nothrow_swappable_v<T>) {
        impl_.swap(other.impl_);
    }
    friend constexpr void swap(Hw& a, Hw& b)
        noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    // ── satisfies<Ceiling> — capability-ceiling subsumption ────────
    //
    // True iff this wrapper's pinned tier is at-or-below the consumer's
    // admitted ceiling.  A `satisfies<Vectorizable>` gate admits
    // Hw<NoneAllowed>, Hw<Scalar>, Hw<Vectorizable> and rejects
    // Hw<NonDeterministicTsc>, Hw<PrivilegedMsr>.
    template <HwInstruction_v Ceiling>
    static constexpr bool satisfies = HwInstructionLattice::leq(Tier, Ceiling);

    // ── widen<Higher>() — claim a higher instruction-capability tier ─
    //
    // Returns an Hw<Higher, T> carrying the same value bytes.  Allowed
    // iff Tier ⊑ Higher (UP the chain only).  Widening DOWN is a compile
    // error — it would CLAIM a narrower instruction class than the value
    // actually needs (e.g. relabel an rdtsc kernel as Scalar), defeating
    // the Mimic legalization gate.
    template <HwInstruction_v Higher>
        requires (HwInstructionLattice::leq(Tier, Higher))
    [[nodiscard]] constexpr Hw<Higher, T> widen() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    { return Hw<Higher, T>{this->peek()}; }

    template <HwInstruction_v Higher>
        requires (HwInstructionLattice::leq(Tier, Higher))
    [[nodiscard]] constexpr Hw<Higher, T> widen() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    { return Hw<Higher, T>{std::move(impl_).consume()}; }
};

// ── §XXI mint factory ───────────────────────────────────────────────
template <HwInstruction_v Tier, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Hw<Tier, T> mint_hw(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
    return Hw<Tier, T>{std::in_place, std::forward<Args>(args)...};
}

// ── Convenience aliases ─────────────────────────────────────────────
namespace hw_pin {
    template <typename T> using NoneAllowed         = Hw<HwInstruction_v::NoneAllowed,         T>;
    template <typename T> using Scalar              = Hw<HwInstruction_v::Scalar,              T>;
    template <typename T> using Vectorizable        = Hw<HwInstruction_v::Vectorizable,        T>;
    template <typename T> using NonDeterministicTsc = Hw<HwInstruction_v::NonDeterministicTsc, T>;
    template <typename T> using PrivilegedMsr       = Hw<HwInstruction_v::PrivilegedMsr,       T>;
}  // namespace hw_pin

// ── Layout invariants — regime-1 EBO collapse ───────────────────────
namespace detail::hw_layout {

template <typename T> using NoneAllowedHw   = Hw<HwInstruction_v::NoneAllowed,   T>;
template <typename T> using ScalarHw        = Hw<HwInstruction_v::Scalar,        T>;
template <typename T> using VectorizableHw  = Hw<HwInstruction_v::Vectorizable,  T>;
template <typename T> using PrivilegedMsrHw = Hw<HwInstruction_v::PrivilegedMsr, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneAllowedHw,   char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneAllowedHw,   int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneAllowedHw,   double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ScalarHw,        int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(VectorizableHw,  double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PrivilegedMsrHw, int);

}  // namespace detail::hw_layout

static_assert(sizeof(Hw<HwInstruction_v::NoneAllowed,         int>)    == sizeof(int));
static_assert(sizeof(Hw<HwInstruction_v::Scalar,              int>)    == sizeof(int));
static_assert(sizeof(Hw<HwInstruction_v::Vectorizable,        int>)    == sizeof(int));
static_assert(sizeof(Hw<HwInstruction_v::NonDeterministicTsc, int>)    == sizeof(int));
static_assert(sizeof(Hw<HwInstruction_v::PrivilegedMsr,       int>)    == sizeof(int));
static_assert(sizeof(Hw<HwInstruction_v::Vectorizable,        double>) == sizeof(double));
static_assert(sizeof(Hw<HwInstruction_v::NoneAllowed,         char>)   == sizeof(char));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::hw_self_test {

using NoneInt   = Hw<HwInstruction_v::NoneAllowed,         int>;
using ScalarInt = Hw<HwInstruction_v::Scalar,             int>;
using VecInt    = Hw<HwInstruction_v::Vectorizable,       int>;
using TscInt    = Hw<HwInstruction_v::NonDeterministicTsc, int>;
using MsrInt    = Hw<HwInstruction_v::PrivilegedMsr,      int>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr ScalarInt s_default{};
static_assert(s_default.peek() == 0);
static_assert(ScalarInt::tier == HwInstruction_v::Scalar);

inline constexpr ScalarInt s_explicit{42};
static_assert(s_explicit.peek() == 42);

inline constexpr ScalarInt s_in_place{std::in_place, 7};
static_assert(s_in_place.peek() == 7);

static_assert(NoneInt::tier == HwInstruction_v::NoneAllowed);
static_assert(MsrInt::tier  == HwInstruction_v::PrivilegedMsr);
static_assert(NoneInt::modality == ::crucible::algebra::ModalityKind::Absolute);

// ── satisfies<Ceiling> — capability-ceiling subsumption ────────────
//
// NoneAllowed satisfies every ceiling (it is the bottom — needs nothing).
static_assert(NoneInt::satisfies<HwInstruction_v::NoneAllowed>);
static_assert(NoneInt::satisfies<HwInstruction_v::Scalar>);
static_assert(NoneInt::satisfies<HwInstruction_v::Vectorizable>);
static_assert(NoneInt::satisfies<HwInstruction_v::PrivilegedMsr>);

// Scalar flows into a Vectorizable gate — THE LOAD-BEARING SUBSUMPTION.
static_assert(ScalarInt::satisfies<HwInstruction_v::Scalar>);
static_assert(ScalarInt::satisfies<HwInstruction_v::Vectorizable>,
    "Hw<Scalar>::satisfies<Vectorizable> MUST be TRUE — a scalar kernel "
    "is admissible everywhere a Vectorizable ceiling is allowed "
    "(Scalar ⊑ Vectorizable).  This is the subsumption HS14 fixture.");
static_assert(!ScalarInt::satisfies<HwInstruction_v::NoneAllowed>,
    "Hw<Scalar>::satisfies<NoneAllowed> MUST be FALSE — a scalar kernel "
    "issues hardware instructions and cannot satisfy a no-hw ceiling.");

// PrivilegedMsr satisfies ONLY itself (it is the top).
static_assert( MsrInt::satisfies<HwInstruction_v::PrivilegedMsr>);
static_assert(!MsrInt::satisfies<HwInstruction_v::NonDeterministicTsc>,
    "Hw<PrivilegedMsr>::satisfies<NonDeterministicTsc> MUST be FALSE — "
    "an MSR kernel exceeds a TSC ceiling.  This is the consumer-too-weak "
    "rejection that neg_hw_consumer_too_weak.cpp pins at a real gate.");
static_assert(!MsrInt::satisfies<HwInstruction_v::Scalar>);
static_assert(!MsrInt::satisfies<HwInstruction_v::NoneAllowed>);

// ── widen<Higher>() — UP-the-chain conversion ──────────────────────
inline constexpr auto scalar_to_vec = ScalarInt{42}.widen<HwInstruction_v::Vectorizable>();
static_assert(scalar_to_vec.peek() == 42 && scalar_to_vec.tier == HwInstruction_v::Vectorizable);

inline constexpr auto none_to_msr = NoneInt{9}.widen<HwInstruction_v::PrivilegedMsr>();
static_assert(none_to_msr.peek() == 9 && none_to_msr.tier == HwInstruction_v::PrivilegedMsr);

inline constexpr auto scalar_reflexive = ScalarInt{55}.widen<HwInstruction_v::Scalar>();
static_assert(scalar_reflexive.peek() == 55);

// ── widen SFINAE detector — chain-direction check ──────────────────
template <typename W, HwInstruction_v Target>
concept can_widen = requires(W w) { { std::move(w).template widen<Target>() }; };

static_assert( can_widen<NoneInt,   HwInstruction_v::PrivilegedMsr>);
static_assert( can_widen<ScalarInt, HwInstruction_v::Vectorizable>);
static_assert( can_widen<ScalarInt, HwInstruction_v::Scalar>);
// Widen DOWN the chain REJECTED — the load-bearing negative.
static_assert(!can_widen<VecInt,    HwInstruction_v::Scalar>,
    "widen<Scalar> on an Hw<Vectorizable> wrapper MUST be REJECTED — "
    "widening DOWN would relabel a SIMD kernel as scalar-only, defeating "
    "the Mimic instruction-legalization gate.  See neg_hw_widen_to_lower.cpp.");
static_assert(!can_widen<MsrInt,    HwInstruction_v::NonDeterministicTsc>);
static_assert(!can_widen<TscInt,    HwInstruction_v::Vectorizable>);

// ── Diagnostic forwarders ──────────────────────────────────────────
static_assert(ScalarInt::value_type_name().ends_with("int"));
static_assert(ScalarInt::lattice_name() == "HwInstructionLattice::At<Scalar>");
static_assert(VecInt::lattice_name()    == "HwInstructionLattice::At<Vectorizable>");
static_assert(MsrInt::lattice_name()    == "HwInstructionLattice::At<PrivilegedMsr>");

// ── swap / peek_mut / operator== ───────────────────────────────────
[[nodiscard]] consteval bool swap_exchanges_within_same_tier() noexcept {
    ScalarInt a{10}; ScalarInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_tier());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    ScalarInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    ScalarInt a{42}; ScalarInt b{42}; ScalarInt c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

// ── Convenience aliases resolve correctly ──────────────────────────
static_assert(std::is_same_v<hw_pin::Scalar<int>, ScalarInt>);
static_assert(std::is_same_v<hw_pin::PrivilegedMsr<int>, MsrInt>);
static_assert(hw_pin::Vectorizable<double>::tier == HwInstruction_v::Vectorizable);
static_assert(!std::is_same_v<ScalarInt, VecInt>);
static_assert(std::is_copy_constructible_v<ScalarInt>);

// ── mint_hw factory ────────────────────────────────────────────────
inline constexpr auto minted = mint_hw<HwInstruction_v::Vectorizable, int>(99);
static_assert(minted.peek() == 99 && minted.tier == HwInstruction_v::Vectorizable);

// ── Mimic instruction-legalization admission simulation ────────────
//
// Production: a Mimic backend that targets a portable ISA admits only
// kernels whose Hw pin is ⊑ Scalar (no SIMD proof required).
template <typename W>
concept is_portable_legal = W::template satisfies<HwInstruction_v::Scalar>;

static_assert( is_portable_legal<NoneInt>,
    "A no-hw kernel MUST pass the portable-ISA legalization gate.");
static_assert( is_portable_legal<ScalarInt>,
    "A scalar kernel MUST pass the portable-ISA legalization gate.");
static_assert(!is_portable_legal<VecInt>,
    "A Vectorizable kernel MUST be REJECTED at the portable-ISA gate — "
    "it needs the V-256 SimdWidthPinned ISA-availability proof first.");
static_assert(!is_portable_legal<MsrInt>);

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline: pure
// static_asserts can mask consteval/SFINAE/inline-body bugs; runtime
// ops with non-constant arguments catch them.
inline void runtime_smoke_test() {
    int seed = 21;
    ScalarInt n{seed * 2};
    if (n.peek() != 42) std::abort();
    n.peek_mut() = 9;
    if (n.peek() != 9) std::abort();

    auto w = ScalarInt{seed}.widen<HwInstruction_v::Vectorizable>();
    if (w.peek() != 21 || w.tier != HwInstruction_v::Vectorizable) std::abort();

    auto m = mint_hw<HwInstruction_v::PrivilegedMsr, int>(seed);
    if (std::move(m).consume() != 21) std::abort();

    ScalarInt a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();

    [[maybe_unused]] bool s1 = ScalarInt::satisfies<HwInstruction_v::Vectorizable>;
    [[maybe_unused]] bool s2 = MsrInt::satisfies<HwInstruction_v::Scalar>;
    if (!s1 || s2) std::abort();

    // Convenience-alias instantiation.
    hw_pin::NoneAllowed<int>   alias_none{0};
    hw_pin::Vectorizable<int>  alias_vec{456};
    if (alias_none.peek() != 0 || alias_vec.peek() != 456) std::abort();
}

}  // namespace detail::hw_self_test

}  // namespace crucible::safety
