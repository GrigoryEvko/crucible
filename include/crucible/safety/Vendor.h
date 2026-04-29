#pragma once

// ── crucible::safety::Vendor<VendorBackend_v Backend, T> ────────────
//
// Type-pinned vendor-backend wrapper.  A value of type T whose
// vendor backend identity (None ⊑ {CPU, NV, AMD, TPU, TRN, CER} ⊑
// Portable in a partial-order lattice) is fixed at the type level
// via the non-type template parameter Backend.  Ninth chain-style
// wrapper from 28_04_2026_effects.md §4.3.9 (FOUND-G54) — but the
// FIRST whose underlying lattice is a true PARTIAL ORDER, not a
// chain.  Composes directly with the eight sister chain wrappers
// in canonical wrapper-nesting order:
//
//   Vendor<NV,
//       HotPath<Hot,
//           DetSafe<Pure,
//               NumericalTier<BITEXACT, T>>>>
//
// Each layer EBO-collapses; the wrapper-nesting cost is sizeof(T)
// at -O3.  Per 28_04 §4.7: wrappers compose orthogonally; the
// dispatcher (FOUND-D) reads the stack via reflection.
//
//   Substrate: Graded<ModalityKind::Absolute,
//                     VendorLattice::At<Backend>,
//                     T>
//   Regime:    1 (zero-cost EBO collapse — At<Backend>::element_type
//                 is empty, sizeof(Vendor<Backend, T>) == sizeof(T))
//
//   Use cases (per 28_04 §4.3.9 + MIMIC.md):
//     - mimic::nv::emit_kernel  → returns Vendor<NV, CompiledKernel>
//     - mimic::am::emit_kernel  → returns Vendor<AMD, CompiledKernel>
//     - mimic::tpu::emit_kernel → returns Vendor<TPU, CompiledKernel>
//     - mimic::cpu::reference   → returns Vendor<Portable, T>
//                                  (the bit-exact CPU oracle that
//                                   serves as the cross-vendor
//                                   numerics CI ground truth)
//     - cross-vendor numerics CI compares pairwise:
//         expected = Vendor<Portable>::peek()  (run on CPU oracle)
//         actual   = Vendor<NV>::peek()         (run on NV)
//       — these are DIFFERENT TYPES; comparing them at the value
//       level requires explicit unwrap, ensuring the CI-author
//       acknowledges the cross-vendor boundary.
//
//   The bug class caught: a refactor that publishes an NV-pinned
//   kernel into a function expecting an AMD-pinned input.  Today
//   caught only at runtime by NV-driver-rejects-AMD-PTX failure
//   modes minutes into the run; with the wrapper, becomes a compile
//   error at the call boundary because the function's `requires AMD`
//   rejects `Vendor<NV>`.  This is THE LOAD-BEARING SAFETY GUARANTEE
//   that motivated implementing Vendor as a partial order rather
//   than a chain — see VendorLattice.h docblock for the design
//   rationale.
//
//   ORTHOGONAL TO every sister wrapper:
//     - HotPath captures execution-budget tier
//     - CipherTier captures storage-residency tier
//     - ResidencyHeat captures cache-residency tier
//     - DetSafe captures determinism-safety tier
//     - AllocClass captures allocator class
//     - Vendor captures BACKEND IDENTITY — orthogonal to all the
//       above.  An NV kernel may be HotPath<Hot> AND DetSafe<Pure>
//       AND CipherTier<Warm>; vendor identity adds a SEVENTH
//       independent typed axis.
//
//   Axiom coverage:
//     TypeSafe — VendorBackend_v is a strong scoped enum;
//                cross-backend mismatches are compile errors via
//                the `relax<WeakerBackend>()` and
//                `satisfies<RequiredBackend>` gates.
//     DetSafe — orthogonal axis; Vendor does NOT itself enforce
//                determinism.  Composes via wrapper-nesting.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//   Runtime cost:
//     sizeof(Vendor<Backend, T>) == sizeof(T).  Verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.  At<Backend>::
//     element_type is empty; Graded's [[no_unique_address]] grade_
//     EBO-collapses; the wrapper is byte-equivalent to the bare T
//     at -O3.
//
// ── Why Modality::Absolute ─────────────────────────────────────────
//
// A vendor pin is a STATIC property of WHICH BACKEND the kernel was
// compiled for — not a context the value carries independent of its
// compilation target.  The bytes themselves carry no information
// about their backend; the wrapper carries that information at the
// TYPE level.  Mirrors HotPath / CipherTier / DetSafe / AllocClass /
// ResidencyHeat — all Absolute modalities over At<>-pinned grades.
//
// ── Tier-conversion API: relax + satisfies ─────────────────────────
//
// Vendor's API is BROADER than the chain wrappers' because the
// underlying lattice is a partial order:
//
//   - `Vendor<Portable>::relax<NV>()`     — VALID (Portable ⊑ NV is FALSE,
//                                            but leq(NV, Portable) = TRUE,
//                                            i.e., NV ≤ Portable; relaxing
//                                            from Portable DOWN to NV is the
//                                            structural specialization
//                                            "I have a portable kernel; I'm
//                                            committing it to NV here")
//   - `Vendor<NV>::relax<None>()`         — VALID (None is bottom; any
//                                            backend can relax to None,
//                                            "I have an NV kernel; I'm
//                                            erasing the binding here")
//   - `Vendor<NV>::relax<NV>()`           — VALID (reflexive)
//   - `Vendor<NV>::relax<AMD>()`          — REJECTED (NV and AMD are
//                                            incomparable in the lattice;
//                                            cross-vendor relax is the
//                                            LOAD-BEARING bug-rejection)
//   - `Vendor<NV>::relax<Portable>()`     — REJECTED (NV is below Portable;
//                                            relaxing UP is forbidden — would
//                                            CLAIM more portability than
//                                            the source provides)
//
// `Self.satisfies<R>` = leq(R, Self):
//   - `Vendor<Portable>::satisfies<X>` = TRUE for every X (Portable subsumes everything)
//   - `Vendor<X>::satisfies<None>`     = TRUE for every X (None is satisfied by anything)
//   - `Vendor<X>::satisfies<X>`        = TRUE (reflexive)
//   - `Vendor<NV>::satisfies<AMD>`     = FALSE (mutually incomparable)
//   - `Vendor<NV>::satisfies<Portable>` = FALSE (NV does NOT subsume Portable)
//
// SEMANTIC NOTE on the "relax" naming for Vendor: with Vendor's
// partial-order structure, "relaxing" can mean either
// "specializing from Portable down to a vendor" or "erasing to None".
// Both are downward moves in the lattice (toward bottom); neither is
// an upgrade of guarantee.  The API uses `relax` for uniformity with
// the eight sister chain wrappers; the lattice direction enforces
// correctness regardless of the naming.
//
// `Graded::weaken` on the substrate goes UP the lattice (stronger
// promise) — that operation has NO MEANINGFUL SEMANTICS for a
// type-pinned backend and would be the LOAD-BEARING BUG: an NV-tier
// value claiming Portable compatibility would defeat the cross-vendor
// safety discipline.  Hidden by the wrapper.
//
// See FOUND-G53 (algebra/lattices/VendorLattice.h) for the
// underlying partial-order substrate; 28_04_2026_effects.md §4.3.9 +
// §4.7 for the production-call-site rationale and the canonical
// wrapper-nesting story; MIMIC.md for the per-vendor backend
// architecture that this wrapper type-fences.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/VendorLattice.h>

#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the VendorBackend enum into the safety:: namespace under
// `VendorBackend_v`.  No name collision — the wrapper class is
// `Vendor`, not `VendorBackend`.  Naming convention matches
// HotPathTier_v / CipherTierTag_v / etc. from sibling wrappers.
using ::crucible::algebra::lattices::VendorLattice;
using VendorBackend_v = ::crucible::algebra::lattices::VendorBackend;

template <VendorBackend_v Backend, typename T>
class [[nodiscard]] Vendor {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type   = T;
    using lattice_type = VendorLattice::At<Backend>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned backend — exposed as a static constexpr for callers
    // doing backend-aware dispatch without instantiating the wrapper.
    static constexpr VendorBackend_v backend = Backend;

private:
    graded_type impl_;

public:

    // ── Construction ────────────────────────────────────────────────
    //
    // Default: T{} at the pinned backend.
    //
    // SEMANTIC NOTE: a default-constructed Vendor<NV, T> claims its
    // T{} bytes were produced by the NV-vendor backend.  For
    // trivially-zero T, this is vacuously true.  For non-trivial T
    // or non-zero T{} in a populated context, the claim becomes
    // meaningful only if the wrapper is constructed in a context
    // that genuinely honors the backend (e.g., mimic::nv::emit_kernel
    // returning a kernel just compiled by the NVIDIA driver).
    // Production callers SHOULD prefer the explicit-T constructor
    // at backend-anchored production sites; the default ctor exists
    // for compatibility with std::array<Vendor<NV, T>, N> /
    // struct-field default-init contexts.
    //
    // Vendor<None, T> default-construction is the canonical
    // "uninitialized backend slot" pattern — KernelCacheSlot uses
    // this as the zero state before a vendor is bound.
    constexpr Vendor() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    // Explicit construction from a T value.  The most common
    // production pattern — a backend-anchored production site
    // constructs the wrapper at the appropriate vendor.
    constexpr explicit Vendor(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    // In-place construction.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Vendor(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    // Defaulted copy/move/destroy — Vendor IS COPYABLE within the
    // same backend pin.
    constexpr Vendor(const Vendor&)            = default;
    constexpr Vendor(Vendor&&)                 = default;
    constexpr Vendor& operator=(const Vendor&) = default;
    constexpr Vendor& operator=(Vendor&&)      = default;
    ~Vendor()                                  = default;

    // Equality: compares value bytes within the SAME backend pin.
    // Cross-backend comparison rejected at overload resolution.
    [[nodiscard]] friend constexpr bool operator==(
        Vendor const& a, Vendor const& b) noexcept(
        noexcept(a.peek() == b.peek()))
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

    // ── Read-only access ────────────────────────────────────────────
    [[nodiscard]] constexpr T const& peek() const& noexcept {
        return impl_.peek();
    }

    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        return impl_.peek_mut();
    }

    // ── swap ────────────────────────────────────────────────────────
    constexpr void swap(Vendor& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(Vendor& a, Vendor& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── satisfies<RequiredBackend> — partial-order subsumption ─────
    //
    // True iff this wrapper's pinned backend is at least as strong
    // as RequiredBackend.  Stronger-or-equal subsumes weaker-or-
    // incomparable.  Crucially:
    //   - Vendor<Portable>::satisfies<X>     = TRUE  for every X
    //   - Vendor<X>::satisfies<None>          = TRUE  for every X
    //   - Vendor<X>::satisfies<X>             = TRUE  (reflexive)
    //   - Vendor<NV>::satisfies<AMD>          = FALSE (incomparable)
    //   - Vendor<NV>::satisfies<Portable>     = FALSE (specific backend
    //                                                  does not subsume the
    //                                                  universal target)
    //
    // Use:
    //   static_assert(Vendor<VendorBackend_v::Portable, T>
    //                     ::satisfies<VendorBackend_v::NV>);
    //   // ✓ — Portable subsumes NV
    //
    //   static_assert(!Vendor<VendorBackend_v::NV, T>
    //                      ::satisfies<VendorBackend_v::AMD>);
    //   // ✓ — NV does NOT subsume AMD (they're incomparable)
    template <VendorBackend_v RequiredBackend>
    static constexpr bool satisfies =
        VendorLattice::leq(RequiredBackend, Backend);

    // ── relax<WeakerBackend> — convert to a less-strict backend ────
    //
    // Returns a Vendor<WeakerBackend, T> carrying the same value
    // bytes.  Allowed iff WeakerBackend ≤ Backend in the lattice
    // (the weaker tier is below or equal to the pinned tier).
    //
    // For Vendor's partial order, this means:
    //   - Self == Backend       → identity
    //   - Self == Portable      → may relax to ANY backend (Portable
    //                              subsumes everything)
    //   - WeakerBackend == None → always allowed (None is bottom)
    //   - Two distinct middle vendors → REJECTED at compile time
    //
    // Compile error when WeakerBackend ⊄ Backend in the lattice —
    // would CLAIM more vendor portability than the source provides.
    template <VendorBackend_v WeakerBackend>
        requires (VendorLattice::leq(WeakerBackend, Backend))
    [[nodiscard]] constexpr Vendor<WeakerBackend, T> relax() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Vendor<WeakerBackend, T>{this->peek()};
    }

    template <VendorBackend_v WeakerBackend>
        requires (VendorLattice::leq(WeakerBackend, Backend))
    [[nodiscard]] constexpr Vendor<WeakerBackend, T> relax() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return Vendor<WeakerBackend, T>{
            std::move(impl_).consume()};
    }
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace vendor {
    template <typename T> using None     = Vendor<VendorBackend_v::None,     T>;
    template <typename T> using Cpu      = Vendor<VendorBackend_v::CPU,      T>;
    template <typename T> using Nv       = Vendor<VendorBackend_v::NV,       T>;
    template <typename T> using Amd      = Vendor<VendorBackend_v::AMD,      T>;
    template <typename T> using Tpu      = Vendor<VendorBackend_v::TPU,      T>;
    template <typename T> using Trn      = Vendor<VendorBackend_v::TRN,      T>;
    template <typename T> using Cer      = Vendor<VendorBackend_v::CER,      T>;
    template <typename T> using Portable = Vendor<VendorBackend_v::Portable, T>;
}  // namespace vendor

// ── Layout invariants ───────────────────────────────────────────────
namespace detail::vendor_layout {

template <typename T> using PortableV = Vendor<VendorBackend_v::Portable, T>;
template <typename T> using NvV       = Vendor<VendorBackend_v::NV,       T>;
template <typename T> using AmdV      = Vendor<VendorBackend_v::AMD,      T>;
template <typename T> using NoneV     = Vendor<VendorBackend_v::None,     T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableV, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableV, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableV, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NvV,       int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NvV,       double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AmdV,      int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AmdV,      double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneV,     int);

}  // namespace detail::vendor_layout

static_assert(sizeof(Vendor<VendorBackend_v::Portable, int>)    == sizeof(int));
static_assert(sizeof(Vendor<VendorBackend_v::NV,       int>)    == sizeof(int));
static_assert(sizeof(Vendor<VendorBackend_v::AMD,      int>)    == sizeof(int));
static_assert(sizeof(Vendor<VendorBackend_v::TPU,      int>)    == sizeof(int));
static_assert(sizeof(Vendor<VendorBackend_v::TRN,      int>)    == sizeof(int));
static_assert(sizeof(Vendor<VendorBackend_v::CER,      int>)    == sizeof(int));
static_assert(sizeof(Vendor<VendorBackend_v::CPU,      int>)    == sizeof(int));
static_assert(sizeof(Vendor<VendorBackend_v::None,     int>)    == sizeof(int));
static_assert(sizeof(Vendor<VendorBackend_v::Portable, double>) == sizeof(double));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::vendor_self_test {

using PortableInt = Vendor<VendorBackend_v::Portable, int>;
using CpuInt      = Vendor<VendorBackend_v::CPU,      int>;
using NvInt       = Vendor<VendorBackend_v::NV,       int>;
using AmdInt      = Vendor<VendorBackend_v::AMD,      int>;
using TpuInt      = Vendor<VendorBackend_v::TPU,      int>;
using TrnInt      = Vendor<VendorBackend_v::TRN,      int>;
using CerInt      = Vendor<VendorBackend_v::CER,      int>;
using NoneInt     = Vendor<VendorBackend_v::None,     int>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr NvInt n_default{};
static_assert(n_default.peek() == 0);
static_assert(n_default.backend == VendorBackend_v::NV);

inline constexpr NvInt n_explicit{42};
static_assert(n_explicit.peek() == 42);

inline constexpr NvInt n_in_place{std::in_place, 7};
static_assert(n_in_place.peek() == 7);

// ── Pinned backend accessor ───────────────────────────────────────
static_assert(PortableInt::backend == VendorBackend_v::Portable);
static_assert(CpuInt::backend      == VendorBackend_v::CPU);
static_assert(NvInt::backend       == VendorBackend_v::NV);
static_assert(AmdInt::backend      == VendorBackend_v::AMD);
static_assert(TpuInt::backend      == VendorBackend_v::TPU);
static_assert(TrnInt::backend      == VendorBackend_v::TRN);
static_assert(CerInt::backend      == VendorBackend_v::CER);
static_assert(NoneInt::backend     == VendorBackend_v::None);

// ── satisfies<RequiredBackend> — partial-order subsumption ────────
//
// Portable subsumes EVERY consumer.  THE LOAD-BEARING POSITIVE.
static_assert(PortableInt::satisfies<VendorBackend_v::Portable>);
static_assert(PortableInt::satisfies<VendorBackend_v::CPU>);
static_assert(PortableInt::satisfies<VendorBackend_v::NV>);
static_assert(PortableInt::satisfies<VendorBackend_v::AMD>);
static_assert(PortableInt::satisfies<VendorBackend_v::TPU>);
static_assert(PortableInt::satisfies<VendorBackend_v::TRN>);
static_assert(PortableInt::satisfies<VendorBackend_v::CER>);
static_assert(PortableInt::satisfies<VendorBackend_v::None>);

// Each specific vendor satisfies ONLY itself + None.
static_assert( NvInt::satisfies<VendorBackend_v::NV>);    // self
static_assert( NvInt::satisfies<VendorBackend_v::None>);  // bottom
static_assert(!NvInt::satisfies<VendorBackend_v::AMD>,    // INCOMPARABLE — load-bearing reject
    "Vendor<NV>::satisfies<AMD> MUST be FALSE — this is the LOAD-"
    "BEARING REJECTION that distinguishes the partial-order "
    "VendorLattice from a chain.  If this fires, NV and AMD have "
    "become comparable, and an NV kernel could silently flow into "
    "an AMD-required function — exactly the bug Vendor was designed "
    "to prevent.  See VendorLattice.h's non_distributive_witness "
    "block for the lattice-shape guard against the same regression.");
static_assert(!NvInt::satisfies<VendorBackend_v::TPU>);
static_assert(!NvInt::satisfies<VendorBackend_v::TRN>);
static_assert(!NvInt::satisfies<VendorBackend_v::CER>);
static_assert(!NvInt::satisfies<VendorBackend_v::CPU>);
static_assert(!NvInt::satisfies<VendorBackend_v::Portable>,    // STRONGER
    "Vendor<NV>::satisfies<Portable> MUST be FALSE — an NV-pinned "
    "kernel does NOT satisfy a Portable requirement (Portable "
    "demands universal coverage; NV provides only NV).");

// AMD: same shape — only AMD + None.
static_assert( AmdInt::satisfies<VendorBackend_v::AMD>);
static_assert( AmdInt::satisfies<VendorBackend_v::None>);
static_assert(!AmdInt::satisfies<VendorBackend_v::NV>);
static_assert(!AmdInt::satisfies<VendorBackend_v::TPU>);
static_assert(!AmdInt::satisfies<VendorBackend_v::Portable>);

// CPU: same shape.
static_assert( CpuInt::satisfies<VendorBackend_v::CPU>);
static_assert( CpuInt::satisfies<VendorBackend_v::None>);
static_assert(!CpuInt::satisfies<VendorBackend_v::NV>);
static_assert(!CpuInt::satisfies<VendorBackend_v::Portable>);

// None satisfies only None.
static_assert( NoneInt::satisfies<VendorBackend_v::None>);
static_assert(!NoneInt::satisfies<VendorBackend_v::NV>);
static_assert(!NoneInt::satisfies<VendorBackend_v::CPU>);
static_assert(!NoneInt::satisfies<VendorBackend_v::Portable>,
    "Vendor<None>::satisfies<Portable> MUST be FALSE — None has "
    "no kernel; cannot satisfy any consumer requirement.");

// ── relax<WeakerBackend> — DOWN-the-lattice conversion ────────────

// Portable can relax to ANY backend (it's the top).
inline constexpr auto from_portable_to_nv =
    PortableInt{42}.relax<VendorBackend_v::NV>();
static_assert(from_portable_to_nv.peek() == 42);
static_assert(from_portable_to_nv.backend == VendorBackend_v::NV);

inline constexpr auto from_portable_to_amd =
    PortableInt{42}.relax<VendorBackend_v::AMD>();
static_assert(from_portable_to_amd.backend == VendorBackend_v::AMD);

inline constexpr auto from_portable_to_none =
    PortableInt{42}.relax<VendorBackend_v::None>();
static_assert(from_portable_to_none.backend == VendorBackend_v::None);

// Any backend can relax to None.
inline constexpr auto from_nv_to_none =
    NvInt{99}.relax<VendorBackend_v::None>();
static_assert(from_nv_to_none.peek() == 99);
static_assert(from_nv_to_none.backend == VendorBackend_v::None);

inline constexpr auto from_amd_to_none =
    AmdInt{99}.relax<VendorBackend_v::None>();
static_assert(from_amd_to_none.backend == VendorBackend_v::None);

// Reflexivity at every backend.
inline constexpr auto from_nv_to_nv =
    NvInt{55}.relax<VendorBackend_v::NV>();
static_assert(from_nv_to_nv.peek() == 55);

inline constexpr auto from_portable_to_portable =
    PortableInt{77}.relax<VendorBackend_v::Portable>();
static_assert(from_portable_to_portable.peek() == 77);

inline constexpr auto from_none_to_none =
    NoneInt{0}.relax<VendorBackend_v::None>();
static_assert(from_none_to_none.peek() == 0);

// ── relax SFINAE detector — partial-order check ───────────────────
template <typename W, VendorBackend_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

// Portable can relax to anything.
static_assert( can_relax<PortableInt, VendorBackend_v::Portable>);
static_assert( can_relax<PortableInt, VendorBackend_v::CPU>);
static_assert( can_relax<PortableInt, VendorBackend_v::NV>);
static_assert( can_relax<PortableInt, VendorBackend_v::AMD>);
static_assert( can_relax<PortableInt, VendorBackend_v::TPU>);
static_assert( can_relax<PortableInt, VendorBackend_v::TRN>);
static_assert( can_relax<PortableInt, VendorBackend_v::CER>);
static_assert( can_relax<PortableInt, VendorBackend_v::None>);

// Any backend can relax to None or itself.
static_assert( can_relax<NvInt,       VendorBackend_v::NV>);
static_assert( can_relax<NvInt,       VendorBackend_v::None>);
static_assert( can_relax<AmdInt,      VendorBackend_v::AMD>);
static_assert( can_relax<AmdInt,      VendorBackend_v::None>);
static_assert( can_relax<NoneInt,     VendorBackend_v::None>);

// Cross-vendor relax REJECTED.  THE LOAD-BEARING NEGATIVE.
static_assert(!can_relax<NvInt, VendorBackend_v::AMD>,
    "relax<AMD> on a Vendor<NV> wrapper MUST be REJECTED — "
    "this is the LOAD-BEARING SAFETY GUARANTEE that the partial-"
    "order VendorLattice provides over a chain interpretation. "
    "If this fires, an NV-pinned kernel can silently re-type to "
    "AMD, defeating the entire reason Vendor<> was implemented as "
    "a partial order.");
static_assert(!can_relax<NvInt,  VendorBackend_v::TPU>);
static_assert(!can_relax<NvInt,  VendorBackend_v::TRN>);
static_assert(!can_relax<NvInt,  VendorBackend_v::CER>);
static_assert(!can_relax<NvInt,  VendorBackend_v::CPU>);
static_assert(!can_relax<AmdInt, VendorBackend_v::NV>);
static_assert(!can_relax<TpuInt, VendorBackend_v::CER>);

// Relax UP rejected (specific → Portable).
static_assert(!can_relax<NvInt,   VendorBackend_v::Portable>,
    "relax<Portable> on a Vendor<NV> wrapper MUST be REJECTED — "
    "claiming Portable from an NV-pinned source would defeat the "
    "cross-vendor numerics CI's ability to distinguish portable "
    "kernels (genuinely runs everywhere) from NV-specialized "
    "kernels (runs on NV only).");
static_assert(!can_relax<AmdInt,  VendorBackend_v::Portable>);
static_assert(!can_relax<NoneInt, VendorBackend_v::NV>,
    "relax<NV> on a Vendor<None> wrapper MUST be REJECTED — "
    "None has no kernel to specialize; claiming NV from None "
    "would synthesize a backend pin out of nothing.");
static_assert(!can_relax<NoneInt, VendorBackend_v::Portable>);

// ── Diagnostic forwarders ─────────────────────────────────────────
static_assert(NvInt::value_type_name().ends_with("int"));
static_assert(NvInt::lattice_name()       == "VendorLattice::At<NV>");
static_assert(AmdInt::lattice_name()      == "VendorLattice::At<AMD>");
static_assert(PortableInt::lattice_name() == "VendorLattice::At<Portable>");
static_assert(NoneInt::lattice_name()     == "VendorLattice::At<None>");

// ── swap exchanges T values within the same backend pin ─────────
[[nodiscard]] consteval bool swap_exchanges_within_same_backend() noexcept {
    NvInt a{10};
    NvInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_backend());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    NvInt a{10};
    NvInt b{20};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

// ── peek_mut allows in-place mutation ─────────────────────────────
[[nodiscard]] consteval bool peek_mut_works() noexcept {
    NvInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

// ── operator== — same-backend, same-T comparison ─────────────────
[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    NvInt a{42};
    NvInt b{42};
    NvInt c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

// SFINAE: operator== is only present when T has its own ==.
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

static_assert( can_equality_compare<NvInt>);
static_assert(!can_equality_compare<Vendor<VendorBackend_v::NV, NoEqualityT>>);

// NoEqualityT has DELETED copy ctor — the wrapper inherits.
static_assert(!std::is_copy_constructible_v<Vendor<VendorBackend_v::NV, NoEqualityT>>,
    "Vendor<Backend, T> must transitively inherit T's copy-deletion. "
    "If this fires, NoEqualityT's deleted copy ctor is no longer "
    "visible through the wrapper.");
static_assert(std::is_move_constructible_v<Vendor<VendorBackend_v::NV, NoEqualityT>>);

// ── relax<>() && works on move-only T ─────────────────────────────
struct MoveOnlyT {
    int v{0};
    constexpr MoveOnlyT() = default;
    constexpr explicit MoveOnlyT(int x) : v{x} {}
    constexpr MoveOnlyT(MoveOnlyT&&) = default;
    constexpr MoveOnlyT& operator=(MoveOnlyT&&) = default;
    MoveOnlyT(MoveOnlyT const&) = delete;
    MoveOnlyT& operator=(MoveOnlyT const&) = delete;
};

template <typename W, VendorBackend_v T_target>
concept can_relax_rvalue = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};
template <typename W, VendorBackend_v T_target>
concept can_relax_lvalue = requires(W const& w) {
    { w.template relax<T_target>() };
};

using PortableMoveOnly = Vendor<VendorBackend_v::Portable, MoveOnlyT>;
static_assert( can_relax_rvalue<PortableMoveOnly, VendorBackend_v::NV>,
    "relax<>() && MUST work for move-only T — the rvalue overload "
    "moves through consume(), no copy required.");
static_assert(!can_relax_lvalue<PortableMoveOnly, VendorBackend_v::NV>,
    "relax<>() const& on move-only T MUST be rejected — the const& "
    "overload requires copy_constructible<T>.");

[[nodiscard]] consteval bool relax_move_only_works() noexcept {
    PortableMoveOnly src{MoveOnlyT{77}};
    auto dst = std::move(src).relax<VendorBackend_v::AMD>();
    return dst.peek().v == 77 && dst.backend == VendorBackend_v::AMD;
}
static_assert(relax_move_only_works());

// ── Stable-name introspection (FOUND-E07/H06 surface) ────────────
static_assert(NvInt::value_type_name().size() > 0);
static_assert(NvInt::lattice_name().size() > 0);
static_assert(NvInt::lattice_name().starts_with("VendorLattice::At<"));

// ── Convenience aliases resolve correctly ────────────────────────
static_assert(vendor::Portable<int>::backend == VendorBackend_v::Portable);
static_assert(vendor::Nv<int>::backend       == VendorBackend_v::NV);
static_assert(vendor::Amd<int>::backend      == VendorBackend_v::AMD);
static_assert(vendor::None<int>::backend     == VendorBackend_v::None);

static_assert(std::is_same_v<vendor::Nv<double>,
                             Vendor<VendorBackend_v::NV, double>>);

// ── Mimic vendor-dispatch admission simulations — load-bearing ───
//
// Production: Forge Phase H Mimic per-vendor compile_kernel returns
// Vendor<TargetBackend, CompiledKernel>; the dispatcher's vendor-
// gating concept admits only kernels whose Vendor pin matches the
// target backend.

template <typename W>
concept is_nv_admissible = W::template satisfies<VendorBackend_v::NV>;

static_assert( is_nv_admissible<PortableInt>,
    "Portable kernel MUST pass the NV admission gate.");
static_assert( is_nv_admissible<NvInt>,
    "NV kernel MUST pass the NV admission gate (reflexive).");
static_assert(!is_nv_admissible<AmdInt>,
    "AMD kernel MUST be REJECTED at the NV admission gate — "
    "this is the LOAD-BEARING TEST.  Without this rejection, an "
    "AMD-pinned kernel could be sent to mimic::nv::launch_kernel "
    "and the NV driver would reject the AMDGPU PTX bitstream "
    "minutes into the run.  With the wrapper, the bug is caught "
    "at compile time.");
static_assert(!is_nv_admissible<TpuInt>);
static_assert(!is_nv_admissible<CpuInt>);
static_assert(!is_nv_admissible<NoneInt>);

template <typename W>
concept is_portable_required = W::template satisfies<VendorBackend_v::Portable>;

static_assert( is_portable_required<PortableInt>,
    "Portable kernel MUST pass the Portable-required gate.");
static_assert(!is_portable_required<NvInt>,
    "NV kernel MUST be REJECTED at the Portable-required gate — "
    "the cross-vendor numerics CI's reference oracle requires "
    "true portability (not vendor-specific compilation).");
static_assert(!is_portable_required<AmdInt>);
static_assert(!is_portable_required<CpuInt>);

// ── Runtime smoke test ─────────────────────────────────────────────
inline void runtime_smoke_test() {
    // Construction paths.
    NvInt a{};
    NvInt b{42};
    NvInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    // Static backend accessor at runtime.
    if (NvInt::backend != VendorBackend_v::NV) {
        std::abort();
    }

    // peek_mut.
    NvInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    // Swap at runtime.
    NvInt sx{1};
    NvInt sy{2};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    // relax — Portable to specific vendor.
    PortableInt source{77};
    auto specialized_nv  = source.relax<VendorBackend_v::NV>();
    auto specialized_amd = std::move(source).relax<VendorBackend_v::AMD>();
    [[maybe_unused]] auto vnv  = specialized_nv.peek();
    [[maybe_unused]] auto vamd = specialized_amd.peek();

    // relax — any vendor to None.
    NvInt nv_value{55};
    auto erased = std::move(nv_value).relax<VendorBackend_v::None>();
    [[maybe_unused]] auto verased = erased.peek();

    // satisfies<...> at runtime.
    [[maybe_unused]] bool s1 = PortableInt::satisfies<VendorBackend_v::NV>;
    [[maybe_unused]] bool s2 = NvInt::satisfies<VendorBackend_v::AMD>;

    // operator== — same-backend.
    NvInt eq_a{42};
    NvInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    // Move-construct from consumed inner.
    NvInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    // Convenience-alias instantiation.
    vendor::Portable<int> alias_portable{123};
    vendor::Nv<int>       alias_nv{456};
    vendor::Amd<int>      alias_amd{789};
    vendor::None<int>     alias_none{0};
    [[maybe_unused]] auto vp = alias_portable.peek();
    [[maybe_unused]] auto vn = alias_nv.peek();
    [[maybe_unused]] auto vd = alias_amd.peek();
    [[maybe_unused]] auto vo = alias_none.peek();

    // Mimic admission simulations at runtime.
    [[maybe_unused]] bool can_nv_pass        = is_nv_admissible<PortableInt>;
    [[maybe_unused]] bool can_portable_pass  = is_portable_required<PortableInt>;
}

}  // namespace detail::vendor_self_test

}  // namespace crucible::safety
