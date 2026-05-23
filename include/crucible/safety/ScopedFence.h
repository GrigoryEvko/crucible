#pragma once

// ── crucible::safety::ScopedFence<MemoryScope S, typename T> ─────────
//
// FIXY-V-267 (Agent WMEM): value-level Graded carrier for the V-266
// MemoryScope axis (V-265 MemoryScopeLattice — a NON-DISTRIBUTIVE partial
// order over two trunks, accelerator [Warp ⊑ Cta ⊑ Cluster ⊑ Gpu] and ARM
// shareability [Inner ⊑ Outer], joined only at the shared bottom Thread
// (⊥) and shared top System (⊤)).  Pins, at the TYPE level, the
// memory-visibility SCOPE a value/publication was released under — so a
// consumer requiring a given visibility scope can reject a fence that
// publishes too narrowly (or in the wrong coherence domain) at compile
// time.  The provider-subsumption sibling of SimdWidthPinned (V-256).
//
//   Substrate: Graded<ModalityKind::Absolute, MemoryScopeLattice::At<S>, T>
//   Regime:    1 (zero-cost EBO collapse — At<S>::element_type is empty,
//              sizeof(ScopedFence<S, T>) == sizeof(T) at -O3).
//
// ── PROVIDER / subsumption semantics — the publish-scope shape ─────
//
// ScopedFence pins the scope S a fence PUBLISHES at (provider); a consumer
// requirement R is satisfied iff this provider SUBSUMES it.  Direction
// (MemoryScopeLattice docblock): wider visibility = HIGHER; `leq(R, S)`
// reads "a value requiring scope R is satisfied by a fence publishing at
// scope S" — R must be at-or-below S in the partial order.  Unlike the
// V-254 Hw / V-255 BarrierGuarded total-order chains (every pair
// comparable), MemoryScope has INCOMPARABLE pairs — `leq(Cta, Inner) ==
// false` (cross-trunk: a GPU block scope has no ordering relation to an
// ARM inner-shareable domain).
//
//   satisfies<Required> := MemoryScopeLattice::leq(Required, S)
//        (S subsumes Required — S is at-or-ABOVE Required in the partial
//         order; equivalently a fence publishing at S also publishes at
//         every narrower scope it dominates)
//   relax<Narrower>()   — DOWN the partial order only (Narrower ⊑ S)
//
// Examples (S on the left, the wrapper's pinned publish scope):
//   ScopedFence<Gpu>::satisfies<Cta>      = TRUE  (Cta ⊑ Gpu, same accel trunk)
//   ScopedFence<Cta>::satisfies<Gpu>      = FALSE (Cta does NOT subsume Gpu)
//   ScopedFence<Cta>::satisfies<Inner>    = FALSE (cross-trunk incomparable)
//   ScopedFence<System>::satisfies<any>   = TRUE  (⊤ subsumes everything)
//   ScopedFence<any>::satisfies<Thread>   = TRUE  (⊥ satisfied by anything)
//
// `relax<Narrower>()` is sound because a wider fence genuinely DOES publish
// at every narrower scope it dominates (a device-wide fence makes writes
// visible at block scope too) — re-labelling it as covering only the
// narrower requirement only narrows where it is offered, never lies about
// what it covers.  Relaxing UP (claiming WIDER visibility than S) or ACROSS
// to an incomparable trunk is a compile error — neg_scoped_fence_relax_up_
// or_cross_trunk.cpp pins the cross-trunk case; neg_scoped_fence_provider_
// too_narrow.cpp pins the within-trunk admission case.
//
// ── §XVI canonical wrapper-nesting position ─────────────────────────
//
// ScopedFence occupies the MemoryScope axis (Tier-L Lattice, a Crucible
// extension peer to SimdIsa / Representation), in the Repr neighborhood
// next to Vendor / Hw / SimdWidthPinned / ResidencyHeat.  The
// row_hash_contribution<ScopedFence<S, Inner>> federation-cache
// discriminator (salt 0x2F) ships in safety/diag/RowHashFold.h — the
// row_hash key is the WRAPPER, never the lattice At<>.
//
//   Axiom coverage:
//     TypeSafe — MemoryScope is a strong scoped enum; cross-trunk mixing
//                requires std::to_underlying and surfaces at the call site.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//     DetSafe — the visibility-scope pin is the type-level WITNESS that a
//                publication's coherence reach is legal for the consuming
//                observer; it is the V-268 collision-rule + V-272
//                lower_fence guard.
//   Runtime cost: sizeof(ScopedFence<S, T>) == sizeof(T); verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.
//
// §XXI: `mint_scoped_fence<S, T>(args...)`.  HS14 neg fixtures:
// neg_scoped_fence_provider_too_narrow.cpp + neg_scoped_fence_relax_up_or_
// cross_trunk.cpp.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/MemoryScopeLattice.h>

#include <concepts>
#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::MemoryScopeLattice;
using MemoryScope_v = ::crucible::algebra::lattices::MemoryScope;

template <MemoryScope_v S, typename T>
class [[nodiscard]] ScopedFence {
public:
    // ── Public type aliases (GradedWrapper uniform surface) ─────────
    using value_type   = T;
    using lattice_type = MemoryScopeLattice::At<S>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned publish scope — exposed for callers doing scope-aware
    // dispatch without instantiating the wrapper.
    static constexpr MemoryScope_v scope = S;

private:
    graded_type impl_;

public:
    // ── Construction ────────────────────────────────────────────────
    constexpr ScopedFence() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit ScopedFence(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit ScopedFence(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    constexpr ScopedFence(const ScopedFence&)            = default;
    constexpr ScopedFence(ScopedFence&&)                 = default;
    constexpr ScopedFence& operator=(const ScopedFence&) = default;
    constexpr ScopedFence& operator=(ScopedFence&&)      = default;
    ~ScopedFence()                                       = default;

    // Equality: compares value bytes within the SAME publish scope.
    // Cross-scope comparison rejected at overload resolution.
    [[nodiscard]] friend constexpr bool operator==(
        ScopedFence const& a, ScopedFence const& b)
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
    constexpr void swap(ScopedFence& other) noexcept(std::is_nothrow_swappable_v<T>) {
        impl_.swap(other.impl_);
    }
    friend constexpr void swap(ScopedFence& a, ScopedFence& b)
        noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    // ── satisfies<Required> — PROVIDER subsumption (partial order) ──
    //
    // True iff this wrapper's pinned publish scope S SUBSUMES the consumer's
    // required visibility scope (Required ⊑ S in the MemoryScopeLattice
    // partial order).  A `satisfies<Cta>` gate admits ScopedFence<Cta>,
    // <Cluster>, <Gpu>, <System> and rejects <Warp> (below Cta), <Inner>
    // (cross-trunk incomparable), <Thread> (narrower than Cta).
    template <MemoryScope_v Required>
    static constexpr bool satisfies = MemoryScopeLattice::leq(Required, S);

    // ── relax<Narrower>() — claim a NARROWER publish scope ──────────
    //
    // Returns a ScopedFence<Narrower, T> carrying the same value bytes.
    // Allowed iff Narrower ⊑ S (DOWN the partial order only).  Relaxing UP
    // (claiming WIDER visibility than S) or ACROSS to an incomparable trunk
    // is a compile error — it would assert the value is visible to observers
    // it was never published to, defeating the V-272 lower_fence legality
    // gate.
    template <MemoryScope_v Narrower>
        requires (MemoryScopeLattice::leq(Narrower, S))
    [[nodiscard]] constexpr ScopedFence<Narrower, T> relax() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    { return ScopedFence<Narrower, T>{this->peek()}; }

    template <MemoryScope_v Narrower>
        requires (MemoryScopeLattice::leq(Narrower, S))
    [[nodiscard]] constexpr ScopedFence<Narrower, T> relax() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    { return ScopedFence<Narrower, T>{std::move(impl_).consume()}; }
};

// ── §XXI mint factory ───────────────────────────────────────────────
//
// NAME-COLLISION NOTE (V-269): this `safety::mint_scoped_fence<S, T>(args...)`
// is a Graded-wrapper TOKEN mint (wraps a value in a MemoryScope-provider
// carrier).  It is DISTINCT from `fixy::hw::mint_scoped_fence<Scope, Arch>(
// ctx)` (fixy/Hw.h), which is a §XXI ctx-bound GRANT mint synthesizing a
// `grant::hw::scope<Scope, Arch>` declaration tag (no value).  Different
// namespace, parameter shape, and return category — never ADL-ambiguous
// because callers qualify the namespace.
template <MemoryScope_v S, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr ScopedFence<S, T> mint_scoped_fence(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
    return ScopedFence<S, T>{std::in_place, std::forward<Args>(args)...};
}

// ── Convenience aliases (full MemoryScope enum) ─────────────────────
namespace scoped_fence {
    template <typename T> using Thread  = ScopedFence<MemoryScope_v::Thread,  T>;
    template <typename T> using Warp    = ScopedFence<MemoryScope_v::Warp,    T>;
    template <typename T> using Cta     = ScopedFence<MemoryScope_v::Cta,     T>;
    template <typename T> using Cluster = ScopedFence<MemoryScope_v::Cluster, T>;
    template <typename T> using Gpu     = ScopedFence<MemoryScope_v::Gpu,     T>;
    template <typename T> using Inner   = ScopedFence<MemoryScope_v::Inner,   T>;
    template <typename T> using Outer   = ScopedFence<MemoryScope_v::Outer,   T>;
    template <typename T> using System  = ScopedFence<MemoryScope_v::System,  T>;
}  // namespace scoped_fence

// ── Layout invariants — regime-1 EBO collapse ───────────────────────
namespace detail::scoped_fence_layout {

template <typename T> using ThreadSf  = ScopedFence<MemoryScope_v::Thread,  T>;
template <typename T> using CtaSf      = ScopedFence<MemoryScope_v::Cta,    T>;
template <typename T> using GpuSf       = ScopedFence<MemoryScope_v::Gpu,   T>;
template <typename T> using InnerSf    = ScopedFence<MemoryScope_v::Inner,  T>;
template <typename T> using SystemSf  = ScopedFence<MemoryScope_v::System,  T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(ThreadSf, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ThreadSf, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CtaSf,    int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CtaSf,    double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(GpuSf,    int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(InnerSf,  int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SystemSf, int);

}  // namespace detail::scoped_fence_layout

static_assert(sizeof(ScopedFence<MemoryScope_v::Thread,  int>)    == sizeof(int));
static_assert(sizeof(ScopedFence<MemoryScope_v::Cta,     int>)    == sizeof(int));
static_assert(sizeof(ScopedFence<MemoryScope_v::Gpu,     int>)    == sizeof(int));
static_assert(sizeof(ScopedFence<MemoryScope_v::Inner,   int>)    == sizeof(int));
static_assert(sizeof(ScopedFence<MemoryScope_v::Outer,   int>)    == sizeof(int));
static_assert(sizeof(ScopedFence<MemoryScope_v::System,  int>)    == sizeof(int));
static_assert(sizeof(ScopedFence<MemoryScope_v::Cta,     double>) == sizeof(double));
static_assert(sizeof(ScopedFence<MemoryScope_v::Thread,  char>)   == sizeof(char));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::scoped_fence_self_test {

using ThreadInt  = ScopedFence<MemoryScope_v::Thread,  int>;
using WarpInt    = ScopedFence<MemoryScope_v::Warp,    int>;
using CtaInt     = ScopedFence<MemoryScope_v::Cta,     int>;
using ClusterInt = ScopedFence<MemoryScope_v::Cluster, int>;
using GpuInt     = ScopedFence<MemoryScope_v::Gpu,     int>;
using InnerInt   = ScopedFence<MemoryScope_v::Inner,   int>;
using OuterInt   = ScopedFence<MemoryScope_v::Outer,   int>;
using SystemInt  = ScopedFence<MemoryScope_v::System,  int>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr CtaInt c_default{};
static_assert(c_default.peek() == 0);
static_assert(CtaInt::scope == MemoryScope_v::Cta);

inline constexpr CtaInt c_explicit{42};
static_assert(c_explicit.peek() == 42);

inline constexpr CtaInt c_in_place{std::in_place, 7};
static_assert(c_in_place.peek() == 7);

static_assert(ThreadInt::scope == MemoryScope_v::Thread);
static_assert(SystemInt::scope == MemoryScope_v::System);
static_assert(ThreadInt::modality == ::crucible::algebra::ModalityKind::Absolute);

// ── satisfies<Required> — PROVIDER subsumption ─────────────────────
//
// System (⊤) subsumes every requirement.
static_assert(SystemInt::satisfies<MemoryScope_v::Thread>);
static_assert(SystemInt::satisfies<MemoryScope_v::Cta>);
static_assert(SystemInt::satisfies<MemoryScope_v::Inner>);
static_assert(SystemInt::satisfies<MemoryScope_v::System>);

// Gpu subsumes Cta within the accel trunk — THE LOAD-BEARING
// (comparable, wider-subsumes-narrower) SUBSUMPTION.
static_assert(GpuInt::satisfies<MemoryScope_v::Cta>,
    "ScopedFence<Gpu>::satisfies<Cta> MUST be TRUE — a device-wide fence "
    "publishes at block scope too (Cta ⊑ Gpu, same accel trunk).  This is "
    "the within-trunk subsumption HS14 fixture.");
static_assert(GpuInt::satisfies<MemoryScope_v::Gpu>);
static_assert(!GpuInt::satisfies<MemoryScope_v::Inner>,
    "ScopedFence<Gpu>::satisfies<Inner> MUST be FALSE — a GPU device fence "
    "has no ordering relation to an ARM inner-shareable domain "
    "(cross-trunk incomparable).");

// Cta does NOT subsume Gpu — the within-trunk admission rejection.
static_assert(!CtaInt::satisfies<MemoryScope_v::Gpu>,
    "ScopedFence<Cta>::satisfies<Gpu> MUST be FALSE — a block-scope fence "
    "is too narrow for a device-wide requirement.  This is the "
    "provider-too-narrow rejection that neg_scoped_fence_provider_too_"
    "narrow.cpp pins at a real gate.");

// Cross-trunk incomparability is symmetric.
static_assert(!CtaInt::satisfies<MemoryScope_v::Inner>);
static_assert(!InnerInt::satisfies<MemoryScope_v::Cta>);
static_assert( OuterInt::satisfies<MemoryScope_v::Inner>,
    "ScopedFence<Outer>::satisfies<Inner> MUST be TRUE — an outer-shareable "
    "fence subsumes an inner-shareable requirement within the ARM trunk.");

// Thread (⊥) is satisfied by every provider; Thread provider subsumes
// only a Thread requirement.
static_assert(CtaInt::satisfies<MemoryScope_v::Thread>);
static_assert(InnerInt::satisfies<MemoryScope_v::Thread>);
static_assert( ThreadInt::satisfies<MemoryScope_v::Thread>);
static_assert(!ThreadInt::satisfies<MemoryScope_v::Cta>,
    "A thread-local provider does NOT subsume a block-scope requirement.");

// ── relax<Narrower>() — DOWN-the-partial-order conversion ──────────
inline constexpr auto gpu_to_cta = GpuInt{42}.relax<MemoryScope_v::Cta>();
static_assert(gpu_to_cta.peek() == 42 && gpu_to_cta.scope == MemoryScope_v::Cta);

inline constexpr auto cta_to_thread = CtaInt{9}.relax<MemoryScope_v::Thread>();
static_assert(cta_to_thread.peek() == 9 && cta_to_thread.scope == MemoryScope_v::Thread);

inline constexpr auto system_to_inner = SystemInt{5}.relax<MemoryScope_v::Inner>();
static_assert(system_to_inner.peek() == 5 && system_to_inner.scope == MemoryScope_v::Inner);

inline constexpr auto cta_reflexive = CtaInt{55}.relax<MemoryScope_v::Cta>();
static_assert(cta_reflexive.peek() == 55);

// ── relax SFINAE detector — partial-order direction check ──────────
template <typename W2, MemoryScope_v Target>
concept can_relax = requires(W2 w) { { std::move(w).template relax<Target>() }; };

static_assert( can_relax<GpuInt,    MemoryScope_v::Cta>);     // down accel trunk
static_assert( can_relax<CtaInt,    MemoryScope_v::Thread>);  // down to ⊥
static_assert( can_relax<SystemInt, MemoryScope_v::Inner>);   // ⊤ down to ARM trunk
static_assert( can_relax<CtaInt,    MemoryScope_v::Cta>);     // reflexive
// Relax UP the trunk REJECTED — claiming WIDER visibility than held.
static_assert(!can_relax<CtaInt,    MemoryScope_v::Gpu>,
    "relax<Gpu> on a ScopedFence<Cta> wrapper MUST be REJECTED — claiming a "
    "value is device-visible when it was only published at block scope "
    "defeats the V-272 lower_fence legality gate.");
// Relax ACROSS trunks REJECTED — the partial-order signature.
static_assert(!can_relax<CtaInt,    MemoryScope_v::Inner>,
    "relax<Inner> on a ScopedFence<Cta> wrapper MUST be REJECTED — the "
    "accel and ARM trunks are incomparable.  See "
    "neg_scoped_fence_relax_up_or_cross_trunk.cpp.");
static_assert(!can_relax<InnerInt,  MemoryScope_v::Cta>);
static_assert(!can_relax<ThreadInt, MemoryScope_v::Cta>);     // ⊥ can't relax up

// ── Diagnostic forwarders ──────────────────────────────────────────
static_assert(CtaInt::value_type_name().ends_with("int"));
static_assert(CtaInt::lattice_name()   == "MemoryScopeLattice::At<Cta>");
static_assert(GpuInt::lattice_name()   == "MemoryScopeLattice::At<Gpu>");
static_assert(InnerInt::lattice_name() == "MemoryScopeLattice::At<Inner>");

// ── swap / peek_mut / operator== ───────────────────────────────────
[[nodiscard]] consteval bool swap_exchanges_within_same_scope() noexcept {
    CtaInt a{10}; CtaInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_scope());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    CtaInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    CtaInt a{42}; CtaInt b{42}; CtaInt c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

// ── Convenience aliases resolve correctly ──────────────────────────
static_assert(std::is_same_v<scoped_fence::Cta<int>, CtaInt>);
static_assert(std::is_same_v<scoped_fence::System<int>, SystemInt>);
static_assert(scoped_fence::Gpu<double>::scope == MemoryScope_v::Gpu);
static_assert(!std::is_same_v<CtaInt, InnerInt>);
static_assert(std::is_copy_constructible_v<CtaInt>);

// ── mint_scoped_fence factory ──────────────────────────────────────
inline constexpr auto minted = mint_scoped_fence<MemoryScope_v::Gpu, int>(99);
static_assert(minted.peek() == 99 && minted.scope == MemoryScope_v::Gpu);

// ── Memory-model visibility legalization simulation ────────────────
//
// Production: a publication consumer requiring block-scope (Cta) visibility
// admits only fences whose publish scope SUBSUMES Cta — i.e. a provider that
// satisfies a Cta floor.
template <typename Provider>
concept covers_cta_requirement = Provider::template satisfies<MemoryScope_v::Cta>;

static_assert( covers_cta_requirement<CtaInt>,
    "A block-scope fence MUST pass a Cta-requirement gate.");
static_assert( covers_cta_requirement<GpuInt>,
    "A device-wide fence MUST pass a Cta-requirement gate (Cta ⊑ Gpu).");
static_assert(!covers_cta_requirement<WarpInt>,
    "A warp-only fence MUST be REJECTED at a Cta-requirement gate — it does "
    "NOT subsume the block-scope requirement.");
static_assert(!covers_cta_requirement<InnerInt>,
    "An ARM inner-shareable fence MUST be REJECTED at an accel Cta-"
    "requirement gate (cross-trunk incomparable).");

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline: pure
// static_asserts can mask consteval/SFINAE/inline-body bugs; runtime
// ops with non-constant arguments catch them.
inline void runtime_smoke_test() {
    int seed = 21;
    CtaInt n{seed * 2};
    if (n.peek() != 42) std::abort();
    n.peek_mut() = 9;
    if (n.peek() != 9) std::abort();

    auto r = GpuInt{seed}.relax<MemoryScope_v::Cta>();
    if (r.peek() != 21 || r.scope != MemoryScope_v::Cta) std::abort();

    auto m = mint_scoped_fence<MemoryScope_v::Outer, int>(seed);
    if (std::move(m).consume() != 21) std::abort();

    CtaInt a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();

    [[maybe_unused]] bool s1 = GpuInt::satisfies<MemoryScope_v::Cta>;
    [[maybe_unused]] bool s2 = CtaInt::satisfies<MemoryScope_v::Inner>;
    if (!s1 || s2) std::abort();

    // Convenience-alias instantiation across both trunks.
    scoped_fence::Thread<int> alias_thread{0};
    scoped_fence::Outer<int>  alias_outer{456};
    if (alias_thread.peek() != 0 || alias_outer.peek() != 456) std::abort();
}

}  // namespace detail::scoped_fence_self_test

}  // namespace crucible::safety
