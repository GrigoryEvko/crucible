#pragma once

// ── crucible::safety::SimdWidthPinned<SimdIsa_v W, typename T> ───────
//
// FIXY-V-256 (Agent 11 §3.7): value-level Graded carrier for the V-253
// SimdIsa axis (V-250 SimdIsaLattice — a NON-DISTRIBUTIVE partial order
// over two trunks, x86 [Scalar ⊥ → Avx512Bw] and ARM [Neon → Sve2],
// joined only at the shared bottom Scalar and shared top Portable).
// Pins, at the TYPE level, the SIMD ISA-capability a value/kernel was
// produced with — so a consumer requiring a given ISA can reject a
// provider that lacks it at compile time.
//
//   Substrate: Graded<ModalityKind::Absolute, SimdIsaLattice::At<W>, T>
//   Regime:    1 (zero-cost EBO collapse — At<W>::element_type is empty,
//              sizeof(SimdWidthPinned<W, T>) == sizeof(T) at -O3).
//
// ── PROVIDER / subsumption semantics — Vendor's exact shape ────────
//
// SimdWidthPinned is the partial-order sibling of Vendor (V-G57); the
// SimdIsaLattice explicitly mirrors VendorLattice.  Unlike the V-254 Hw
// and V-255 BarrierGuarded chains (every pair comparable), SimdIsa has
// INCOMPARABLE pairs — `leq(Avx2, Sve2) == false` (cross-trunk: x86 code
// never runs on ARM).  The wrapper pins the ISA-capability a value HAS
// (provider); a requirement R is satisfied iff this provider SUBSUMES it.
//
//   satisfies<Required> := SimdIsaLattice::leq(Required, W)
//        (W subsumes Required — W is at-or-ABOVE Required in the partial
//         order; equivalently W-capable hardware can run Required's code)
//   relax<Weaker>()      — DOWN the partial order only (Weaker ⊑ W)
//
// Examples (W on the left, the wrapper's pinned capability):
//   SimdWidthPinned<Avx512Bw>::satisfies<Avx2>    = TRUE  (Avx2 ⊑ Avx512Bw, same x86 trunk)
//   SimdWidthPinned<Avx2>::satisfies<Avx512Bw>    = FALSE (Avx2 does NOT subsume Avx512Bw)
//   SimdWidthPinned<Avx2>::satisfies<Neon>        = FALSE (cross-trunk incomparable)
//   SimdWidthPinned<Portable>::satisfies<anything>= TRUE  (⊤ subsumes everything)
//   SimdWidthPinned<anything>::satisfies<Scalar>  = TRUE  (⊥ satisfied by anything)
//
// `relax<Weaker>()` is sound because UNDER-claiming capability (a SIMD
// kernel relabelled as needing only a weaker ISA) only narrows where it
// is offered, never lies about what it can run.  Relaxing UP (claiming
// MORE capability than W) or ACROSS to an incomparable trunk is a compile
// error — neg_simd_relax_up_or_cross_trunk.cpp pins the cross-trunk case;
// neg_simd_provider_too_weak.cpp pins the within-trunk admission case.
//
// ── §XVI canonical wrapper-nesting position ─────────────────────────
//
// SimdWidthPinned occupies the SimdIsa axis (Tier-L Lattice, the second
// Tier-L dimension peer to Representation), in the Repr neighborhood next
// to Vendor / Hw / ResidencyHeat.  The row_hash_contribution<
// SimdWidthPinned<W, Inner>> federation-cache discriminator (salt 0x2E)
// ships in safety/diag/RowHashFold.h — the row_hash key is the WRAPPER,
// never the lattice At<>.
//
//   Axiom coverage:
//     TypeSafe — SimdIsa_v is a strong scoped enum; cross-trunk mixing
//                requires std::to_underlying and surfaces at the call site.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//     DetSafe — the SIMD-ISA pin is the type-level WITNESS that a kernel's
//                vectorization is legal on the dispatched hardware; it is
//                the V-262/V-263 SwissTable / cntp::Fec #if-arm guard.
//   Runtime cost: sizeof(SimdWidthPinned<W, T>) == sizeof(T); verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.
//
// §XXI: `mint_simd_width_pinned<W, T>(args...)`.  HS14 neg fixtures:
// neg_simd_provider_too_weak.cpp + neg_simd_relax_up_or_cross_trunk.cpp.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/SimdIsaLattice.h>

#include <concepts>
#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::SimdIsaLattice;
using SimdIsa_v = ::crucible::algebra::lattices::SimdIsa;

template <SimdIsa_v W, typename T>
class [[nodiscard]] SimdWidthPinned {
public:
    // ── Public type aliases (GradedWrapper uniform surface) ─────────
    using value_type   = T;
    using lattice_type = SimdIsaLattice::At<W>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned ISA capability — exposed for callers doing ISA-aware
    // dispatch without instantiating the wrapper.
    static constexpr SimdIsa_v isa = W;

private:
    graded_type impl_;

public:
    // ── Construction ────────────────────────────────────────────────
    constexpr SimdWidthPinned() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit SimdWidthPinned(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit SimdWidthPinned(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    constexpr SimdWidthPinned(const SimdWidthPinned&)            = default;
    constexpr SimdWidthPinned(SimdWidthPinned&&)                 = default;
    constexpr SimdWidthPinned& operator=(const SimdWidthPinned&) = default;
    constexpr SimdWidthPinned& operator=(SimdWidthPinned&&)      = default;
    ~SimdWidthPinned()                                           = default;

    // Equality: compares value bytes within the SAME ISA pin.
    // Cross-ISA comparison rejected at overload resolution.
    [[nodiscard]] friend constexpr bool operator==(
        SimdWidthPinned const& a, SimdWidthPinned const& b)
        noexcept(noexcept(a.peek() == b.peek()))
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
    constexpr void swap(SimdWidthPinned& other) noexcept(std::is_nothrow_swappable_v<T>) {
        impl_.swap(other.impl_);
    }
    friend constexpr void swap(SimdWidthPinned& a, SimdWidthPinned& b)
        noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    // ── satisfies<Required> — PROVIDER subsumption (partial order) ──
    //
    // True iff this wrapper's pinned ISA W SUBSUMES the consumer's
    // required ISA (Required ⊑ W in the SimdIsaLattice partial order).  A
    // `satisfies<Avx2>` gate admits SimdWidthPinned<Avx2>, <Avx512F>,
    // <Avx512Bw>, <Portable> and rejects <Sse2> (below Avx2), <Neon>
    // (cross-trunk incomparable), <Scalar> (no SIMD).
    template <SimdIsa_v Required>
    static constexpr bool satisfies = SimdIsaLattice::leq(Required, W);

    // ── relax<Weaker>() — claim a WEAKER ISA capability ─────────────
    //
    // Returns a SimdWidthPinned<Weaker, T> carrying the same value bytes.
    // Allowed iff Weaker ⊑ W (DOWN the partial order only).  Relaxing UP
    // (claiming MORE capability than W) or ACROSS to an incomparable trunk
    // is a compile error — it would assert the value runs on hardware it
    // was never legalized for, defeating the Mimic ISA-availability gate.
    template <SimdIsa_v Weaker>
        requires (SimdIsaLattice::leq(Weaker, W))
    [[nodiscard]] constexpr SimdWidthPinned<Weaker, T> relax() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    { return SimdWidthPinned<Weaker, T>{this->peek()}; }

    template <SimdIsa_v Weaker>
        requires (SimdIsaLattice::leq(Weaker, W))
    [[nodiscard]] constexpr SimdWidthPinned<Weaker, T> relax() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    { return SimdWidthPinned<Weaker, T>{std::move(impl_).consume()}; }
};

// ── §XXI mint factory ───────────────────────────────────────────────
template <SimdIsa_v W, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr SimdWidthPinned<W, T> mint_simd_width_pinned(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
    return SimdWidthPinned<W, T>{std::in_place, std::forward<Args>(args)...};
}

// ── Convenience aliases (full SimdIsa enum) ─────────────────────────
namespace simd_pin {
    template <typename T> using Scalar         = SimdWidthPinned<SimdIsa_v::Scalar,         T>;
    template <typename T> using Sse2           = SimdWidthPinned<SimdIsa_v::Sse2,           T>;
    template <typename T> using Sse3           = SimdWidthPinned<SimdIsa_v::Sse3,           T>;
    template <typename T> using Ssse3          = SimdWidthPinned<SimdIsa_v::Ssse3,          T>;
    template <typename T> using Sse41          = SimdWidthPinned<SimdIsa_v::Sse41,          T>;
    template <typename T> using Sse42          = SimdWidthPinned<SimdIsa_v::Sse42,          T>;
    template <typename T> using Avx2           = SimdWidthPinned<SimdIsa_v::Avx2,           T>;
    template <typename T> using Avx512F        = SimdWidthPinned<SimdIsa_v::Avx512F,        T>;
    template <typename T> using Avx512Bw       = SimdWidthPinned<SimdIsa_v::Avx512Bw,       T>;
    template <typename T> using Neon           = SimdWidthPinned<SimdIsa_v::Neon,           T>;
    template <typename T> using NeonFp16       = SimdWidthPinned<SimdIsa_v::NeonFp16,       T>;
    template <typename T> using NeonDotProduct = SimdWidthPinned<SimdIsa_v::NeonDotProduct, T>;
    template <typename T> using Sve            = SimdWidthPinned<SimdIsa_v::Sve,            T>;
    template <typename T> using Sve2           = SimdWidthPinned<SimdIsa_v::Sve2,           T>;
    template <typename T> using Portable       = SimdWidthPinned<SimdIsa_v::Portable,       T>;
}  // namespace simd_pin

// ── Layout invariants — regime-1 EBO collapse ───────────────────────
namespace detail::simd_width_pinned_layout {

template <typename T> using ScalarSw   = SimdWidthPinned<SimdIsa_v::Scalar,   T>;
template <typename T> using Avx2Sw     = SimdWidthPinned<SimdIsa_v::Avx2,     T>;
template <typename T> using Avx512BwSw = SimdWidthPinned<SimdIsa_v::Avx512Bw, T>;
template <typename T> using NeonSw     = SimdWidthPinned<SimdIsa_v::Neon,     T>;
template <typename T> using PortableSw = SimdWidthPinned<SimdIsa_v::Portable, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(ScalarSw,   char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ScalarSw,   int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Avx2Sw,     int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Avx2Sw,     double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Avx512BwSw, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NeonSw,     int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableSw, int);

}  // namespace detail::simd_width_pinned_layout

static_assert(sizeof(SimdWidthPinned<SimdIsa_v::Scalar,   int>)    == sizeof(int));
static_assert(sizeof(SimdWidthPinned<SimdIsa_v::Avx2,     int>)    == sizeof(int));
static_assert(sizeof(SimdWidthPinned<SimdIsa_v::Avx512Bw, int>)    == sizeof(int));
static_assert(sizeof(SimdWidthPinned<SimdIsa_v::Neon,     int>)    == sizeof(int));
static_assert(sizeof(SimdWidthPinned<SimdIsa_v::Sve2,     int>)    == sizeof(int));
static_assert(sizeof(SimdWidthPinned<SimdIsa_v::Portable, int>)    == sizeof(int));
static_assert(sizeof(SimdWidthPinned<SimdIsa_v::Avx2,     double>) == sizeof(double));
static_assert(sizeof(SimdWidthPinned<SimdIsa_v::Scalar,   char>)   == sizeof(char));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::simd_width_pinned_self_test {

using ScalarInt   = SimdWidthPinned<SimdIsa_v::Scalar,   int>;
using Sse2Int     = SimdWidthPinned<SimdIsa_v::Sse2,     int>;
using Avx2Int     = SimdWidthPinned<SimdIsa_v::Avx2,     int>;
using Avx512BwInt = SimdWidthPinned<SimdIsa_v::Avx512Bw, int>;
using NeonInt     = SimdWidthPinned<SimdIsa_v::Neon,     int>;
using Sve2Int     = SimdWidthPinned<SimdIsa_v::Sve2,     int>;
using PortableInt = SimdWidthPinned<SimdIsa_v::Portable, int>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr Avx2Int a_default{};
static_assert(a_default.peek() == 0);
static_assert(Avx2Int::isa == SimdIsa_v::Avx2);

inline constexpr Avx2Int a_explicit{42};
static_assert(a_explicit.peek() == 42);

inline constexpr Avx2Int a_in_place{std::in_place, 7};
static_assert(a_in_place.peek() == 7);

static_assert(ScalarInt::isa   == SimdIsa_v::Scalar);
static_assert(PortableInt::isa == SimdIsa_v::Portable);
static_assert(ScalarInt::modality == ::crucible::algebra::ModalityKind::Absolute);

// ── satisfies<Required> — PROVIDER subsumption ─────────────────────
//
// Portable (⊤) subsumes every requirement.
static_assert(PortableInt::satisfies<SimdIsa_v::Scalar>);
static_assert(PortableInt::satisfies<SimdIsa_v::Avx2>);
static_assert(PortableInt::satisfies<SimdIsa_v::Neon>);
static_assert(PortableInt::satisfies<SimdIsa_v::Portable>);

// Avx512Bw subsumes Avx2 within the x86 trunk — THE LOAD-BEARING
// (comparable, higher-subsumes-lower) SUBSUMPTION.
static_assert(Avx512BwInt::satisfies<SimdIsa_v::Avx2>,
    "SimdWidthPinned<Avx512Bw>::satisfies<Avx2> MUST be TRUE — AVX512BW "
    "hardware runs AVX2 code (Avx2 ⊑ Avx512Bw, same x86 trunk).  This is "
    "the within-trunk subsumption HS14 fixture.");
static_assert(Avx512BwInt::satisfies<SimdIsa_v::Avx512Bw>);
static_assert(!Avx512BwInt::satisfies<SimdIsa_v::Neon>,
    "SimdWidthPinned<Avx512Bw>::satisfies<Neon> MUST be FALSE — x86 code "
    "never runs on ARM (cross-trunk incomparable).");

// Avx2 does NOT subsume Avx512Bw — the within-trunk admission rejection.
static_assert(!Avx2Int::satisfies<SimdIsa_v::Avx512Bw>,
    "SimdWidthPinned<Avx2>::satisfies<Avx512Bw> MUST be FALSE — an AVX2 "
    "provider lacks AVX512 instructions.  This is the provider-too-weak "
    "rejection that neg_simd_provider_too_weak.cpp pins at a real gate.");

// Cross-trunk incomparability is symmetric.
static_assert(!Avx2Int::satisfies<SimdIsa_v::Neon>);
static_assert(!NeonInt::satisfies<SimdIsa_v::Avx2>);
static_assert( Sve2Int::satisfies<SimdIsa_v::Neon>,
    "SimdWidthPinned<Sve2>::satisfies<Neon> MUST be TRUE — SVE2 subsumes "
    "NEON within the ARM trunk.");

// Scalar (⊥) is satisfied by every provider; Scalar provider subsumes
// only a Scalar requirement.
static_assert(Avx2Int::satisfies<SimdIsa_v::Scalar>);
static_assert(NeonInt::satisfies<SimdIsa_v::Scalar>);
static_assert( ScalarInt::satisfies<SimdIsa_v::Scalar>);
static_assert(!ScalarInt::satisfies<SimdIsa_v::Avx2>,
    "A scalar-only provider does NOT subsume an AVX2 requirement.");

// ── relax<Weaker>() — DOWN-the-partial-order conversion ────────────
inline constexpr auto avx512_to_avx2 = Avx512BwInt{42}.relax<SimdIsa_v::Avx2>();
static_assert(avx512_to_avx2.peek() == 42 && avx512_to_avx2.isa == SimdIsa_v::Avx2);

inline constexpr auto avx2_to_scalar = Avx2Int{9}.relax<SimdIsa_v::Scalar>();
static_assert(avx2_to_scalar.peek() == 9 && avx2_to_scalar.isa == SimdIsa_v::Scalar);

inline constexpr auto portable_to_neon = PortableInt{5}.relax<SimdIsa_v::Neon>();
static_assert(portable_to_neon.peek() == 5 && portable_to_neon.isa == SimdIsa_v::Neon);

inline constexpr auto avx2_reflexive = Avx2Int{55}.relax<SimdIsa_v::Avx2>();
static_assert(avx2_reflexive.peek() == 55);

// ── relax SFINAE detector — partial-order direction check ──────────
template <typename W2, SimdIsa_v Target>
concept can_relax = requires(W2 w) { { std::move(w).template relax<Target>() }; };

static_assert( can_relax<Avx512BwInt, SimdIsa_v::Avx2>);     // down x86 trunk
static_assert( can_relax<Avx2Int,     SimdIsa_v::Scalar>);   // down to ⊥
static_assert( can_relax<PortableInt, SimdIsa_v::Neon>);     // ⊤ down to ARM trunk
static_assert( can_relax<Avx2Int,     SimdIsa_v::Avx2>);     // reflexive
// Relax UP the trunk REJECTED — claiming MORE capability than held.
static_assert(!can_relax<Avx2Int,     SimdIsa_v::Avx512Bw>,
    "relax<Avx512Bw> on a SimdWidthPinned<Avx2> wrapper MUST be REJECTED "
    "— claiming a value runs AVX512 when it was only legalized for AVX2 "
    "defeats the Mimic ISA-availability gate.");
// Relax ACROSS trunks REJECTED — the partial-order signature.
static_assert(!can_relax<Avx2Int,     SimdIsa_v::Neon>,
    "relax<Neon> on a SimdWidthPinned<Avx2> wrapper MUST be REJECTED — "
    "x86 and ARM trunks are incomparable.  See "
    "neg_simd_relax_up_or_cross_trunk.cpp.");
static_assert(!can_relax<NeonInt,     SimdIsa_v::Avx2>);
static_assert(!can_relax<ScalarInt,   SimdIsa_v::Avx2>);     // ⊥ can't relax up

// ── Diagnostic forwarders ──────────────────────────────────────────
static_assert(Avx2Int::value_type_name().ends_with("int"));
static_assert(Avx2Int::lattice_name()     == "SimdIsaLattice::At<Avx2>");
static_assert(Avx512BwInt::lattice_name() == "SimdIsaLattice::At<Avx512Bw>");
static_assert(NeonInt::lattice_name()     == "SimdIsaLattice::At<Neon>");

// ── swap / peek_mut / operator== ───────────────────────────────────
[[nodiscard]] consteval bool swap_exchanges_within_same_isa() noexcept {
    Avx2Int a{10}; Avx2Int b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_isa());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    Avx2Int a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    Avx2Int a{42}; Avx2Int b{42}; Avx2Int c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

// ── Convenience aliases resolve correctly ──────────────────────────
static_assert(std::is_same_v<simd_pin::Avx2<int>, Avx2Int>);
static_assert(std::is_same_v<simd_pin::Portable<int>, PortableInt>);
static_assert(simd_pin::Avx512Bw<double>::isa == SimdIsa_v::Avx512Bw);
static_assert(!std::is_same_v<Avx2Int, NeonInt>);
static_assert(std::is_copy_constructible_v<Avx2Int>);

// ── mint_simd_width_pinned factory ─────────────────────────────────
inline constexpr auto minted = mint_simd_width_pinned<SimdIsa_v::Avx512Bw, int>(99);
static_assert(minted.peek() == 99 && minted.isa == SimdIsa_v::Avx512Bw);

// ── Mimic ISA-availability legalization simulation ─────────────────
//
// Production: a Mimic dispatcher targeting an AVX2-capable host admits
// only kernels whose ISA requirement is subsumed by Avx2-provided
// capability — i.e. a provider that satisfies an Avx2 floor.
template <typename Provider>
concept runs_on_avx2_host = Provider::template satisfies<SimdIsa_v::Avx2>;

static_assert( runs_on_avx2_host<Avx2Int>,
    "An AVX2 kernel MUST pass an AVX2-host legalization gate.");
static_assert( runs_on_avx2_host<Avx512BwInt>,
    "An AVX512BW kernel MUST pass an AVX2-host gate (Avx2 ⊑ Avx512Bw).");
static_assert(!runs_on_avx2_host<Sse2Int>,
    "An SSE2-only kernel MUST be REJECTED at an AVX2-host gate — it does "
    "NOT subsume the AVX2 requirement.");
static_assert(!runs_on_avx2_host<NeonInt>,
    "An ARM/NEON kernel MUST be REJECTED at an x86 AVX2-host gate "
    "(cross-trunk incomparable).");

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline: pure
// static_asserts can mask consteval/SFINAE/inline-body bugs; runtime
// ops with non-constant arguments catch them.
inline void runtime_smoke_test() {
    int seed = 21;
    Avx2Int n{seed * 2};
    if (n.peek() != 42) std::abort();
    n.peek_mut() = 9;
    if (n.peek() != 9) std::abort();

    auto r = Avx512BwInt{seed}.relax<SimdIsa_v::Avx2>();
    if (r.peek() != 21 || r.isa != SimdIsa_v::Avx2) std::abort();

    auto m = mint_simd_width_pinned<SimdIsa_v::Sve2, int>(seed);
    if (std::move(m).consume() != 21) std::abort();

    Avx2Int a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();

    [[maybe_unused]] bool s1 = Avx512BwInt::satisfies<SimdIsa_v::Avx2>;
    [[maybe_unused]] bool s2 = Avx2Int::satisfies<SimdIsa_v::Neon>;
    if (!s1 || s2) std::abort();

    // Convenience-alias instantiation across both trunks.
    simd_pin::Scalar<int>   alias_scalar{0};
    simd_pin::Sve2<int>     alias_sve{456};
    if (alias_scalar.peek() != 0 || alias_sve.peek() != 456) std::abort();
}

}  // namespace detail::simd_width_pinned_self_test

}  // namespace crucible::safety
