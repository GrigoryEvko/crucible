#pragma once

// ── crucible::algebra::lattices::VendorLattice ──────────────────────
//
// PARTIAL-ORDER lattice over the vendor-backend identity spectrum.
// Distinct from the eight sister chain wrappers because vendor
// identity is NOT linearly orderable: NV and AMD are mutually
// INCOMPARABLE (an NV-specific kernel does not subsume an AMD-
// specific kernel; neither runs on the other).
//
// The grading axis underlying the Vendor wrapper from
// 28_04_2026_effects.md §4.3.9 (FOUND-G53/G54).
//
// ── Structural shape (partial order, NOT a chain) ──────────────────
//
//                         ┌──────────────┐
//                         │  Portable ⊤  │  strongest claim
//                         └──────┬───────┘  (runs everywhere; admitted
//                                │          at every backend gate)
//                ┌──────┬──────┬─┴─┬──────┬──────┐
//                │      │      │   │      │      │
//                ▼      ▼      ▼   ▼      ▼      ▼
//            ┌─────┐┌─────┐┌─────┐┌─────┐┌─────┐┌─────┐
//            │ CPU ││ NV  ││ AMD ││ TPU ││ TRN ││ CER │  middle layer
//            └──┬──┘└──┬──┘└──┬──┘└──┬──┘└──┬──┘└──┬──┘  (mutually
//               └──────┴──────┴───┬──┴──────┴──────┘     incomparable;
//                                 ▼                       each satisfies
//                          ┌──────────────┐               only its own
//                          │  None    ⊥   │               consumer)
//                          └──────────────┘
//                              weakest claim
//                              (no kernel; admitted nowhere)
//
// Unlike the eight sister chain lattices (HotPath / DetSafe / Wait /
// MemOrder / Progress / AllocClass / CipherTier / ResidencyHeat),
// VendorLattice does NOT inherit ChainLatticeOps.  Its `leq` is a
// hand-written partial-order check; its `meet` and `join` route
// through the bottom (None) and top (Portable) sentinels respectively
// for cross-vendor pairs.
//
// THE LOAD-BEARING USE CASE (per 28_04 §4.3.9): every IR003* lowering
// in Mimic + every cross-vendor numerics CI check.  A function
// declared `requires Vendor<X>::satisfies<TargetBackend>` rejects
// callers whose pinned backend is incomparable to TargetBackend.
//
//   Production rejected at compile time:
//     - NV-pinned kernel passed to a function expecting AMD
//     - AMD-pinned kernel passed to a function expecting Portable
//     - any vendor-specific kernel passed where Portable required
//
//   Production accepted at compile time:
//     - Portable kernel passed anywhere (Portable ⊃ every vendor gate)
//     - vendor-X kernel passed to a vendor-X function (reflexivity)
//     - any kernel passed to a None-required gate (None ⊑ every X)
//
// ── The classification ──────────────────────────────────────────────
//
//     Portable — Vendor-agnostic kernel.  Compiles + runs identically
//                 on every supported backend.  Strongest residency
//                 claim — most permissive provider; admitted at every
//                 backend's gate.  Production: scalar reference
//                 oracles in mimic/cpu/, debug paths.
//     CPU      — x86_64/AArch64 host CPU.  Strongest claim about
//                 CPU-only execution.  Admits only at CPU consumer.
//     NV       — NVIDIA GPU (sm_8x/9x/10x).  Admits only at NV consumer.
//     AMD      — AMD GPU (gfx9xx/10xx/11xx).  Admits only at AMD consumer.
//     TPU      — Google TPU MXU.  Admits only at TPU consumer.
//     TRN      — AWS Trainium (NEFF).  Admits only at TRN consumer.
//     CER      — Cerebras WSE.  Admits only at CER consumer.
//     None     — Synthetic ⊥.  No kernel exists.  Admits nothing —
//                 used as the GLB of two distinct vendors (e.g.,
//                 meet(NV, AMD) = None) and as the default-init
//                 sentinel for slots not yet bound to a vendor.
//                 Production: KernelCacheSlot whose compile failed
//                 or hasn't run yet.
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class VendorBackend {None, CPU, NV, AMD, TPU, TRN, CER, Portable}.
// Order:   None ⊑ X ⊑ Portable for every middle X; X and Y are
//          incomparable for distinct middle vendors.
//
// Bottom = None      (weakest claim — no kernel, satisfies no gate)
// Top    = Portable  (strongest claim — universal kernel, satisfies
//                     every gate)
// Join   = LUB:
//          - x ∨ x        = x
//          - None ∨ x     = x      (bottom is identity for ∨)
//          - x ∨ Portable = Portable
//          - x ∨ y (distinct middle) = Portable  (only their LUB is ⊤)
// Meet   = GLB:
//          - x ∧ x        = x
//          - x ∧ Portable = x      (top is identity for ∧)
//          - None ∧ x     = None
//          - x ∧ y (distinct middle) = None      (only their GLB is ⊥)
//
// Lattice axioms (verified by exhaustive triple-test below):
//   - Reflexivity:    x ⊑ x                                    ∀x
//   - Antisymmetry:   x ⊑ y ∧ y ⊑ x ⇒ x = y                    ∀x, y
//   - Transitivity:   x ⊑ y ∧ y ⊑ z ⇒ x ⊑ z                    ∀x, y, z
//   - Idempotence:    x ∨ x = x ∧ x = x                        ∀x
//   - Commutativity:  x ∨ y = y ∨ x ;  x ∧ y = y ∧ x           ∀x, y
//   - Associativity:  (x ∨ y) ∨ z = x ∨ (y ∨ z)                ∀x, y, z
//   - Absorption:     x ∨ (x ∧ y) = x ;  x ∧ (x ∨ y) = x       ∀x, y
//
// NOT distributive in general — verified below.  This is the most
// distinctive divergence from the eight sister chain lattices (every
// chain is distributive).  Distributivity would require, e.g.:
//   (NV ∨ AMD) ∧ TPU = (NV ∧ TPU) ∨ (AMD ∧ TPU)
//   Portable    ∧ TPU = None       ∨ None
//   TPU                ≠ None
// Failure is structural and intentional — a vendor partial order
// CANNOT be both bounded with a single ⊤/⊥ AND have non-comparable
// middle elements AND be distributive.  Documented.
//
// ── Direction convention (matches the audit-verified universal) ────
//
// Stronger guarantee = HIGHER in the lattice.  `leq(weak, strong)`
// reads "a weaker-claim consumer is satisfied by a stronger-claim
// provider".  Portable (top) admits every consumer.  None (bottom)
// admits no consumer (because no consumer asks for "no kernel").
//
//   Vendor<Portable>::satisfies<NV>  = leq(NV, Portable)  = true ✓
//   Vendor<NV>::satisfies<AMD>       = leq(AMD, NV)       = false ✓
//   Vendor<NV>::satisfies<NV>        = leq(NV, NV)        = true ✓
//   Vendor<NV>::satisfies<Portable>  = leq(Portable, NV)  = false ✓
//   Vendor<NV>::satisfies<None>      = leq(None, NV)      = true ✓
//   Vendor<None>::satisfies<NV>      = leq(NV, None)      = false ✓
//
// ── DIVERGENCE FROM 28_04_2026_effects.md §4.3.9 SPEC ──────────────
//
// Spec posits "VendorLattice chain over enum {CPU=0, NV=1, AMD=2,
// TPU=3, TRN=4, CER=5, Portable=255}" with the suggestion
// "Portable ⊑ CPU ⊑ NV etc.".  This implementation INVERTS the
// claimed-chain interpretation: NV and AMD MUST be mutually
// incomparable for the wrapper's load-bearing safety guarantee
// (rejecting cross-vendor mismatches at compile time) to hold.
//
// A chain interpretation would silently admit `Vendor<NV>::satisfies
// <AMD>` (or its reverse, depending on chain direction) — exactly
// the bug the wrapper is meant to prevent.  Therefore we model
// Vendor as a partial order, NOT a chain.  The semantic contract
// from the spec ("a portable kernel satisfies every backend; a
// vendor-specific kernel satisfies only that vendor") is preserved
// EXACTLY by this partial-order structure.
//
// We also add a synthetic `None` enum value at ordinal 0 (the spec
// did not document a bottom because chain interpretations don't
// need one).  None is required for `meet` to close on cross-vendor
// pairs: meet(NV, AMD) = None.  Without None, the structure is
// only a meet-semilattice, which fails the BoundedLattice concept
// the substrate requires.
//
//   Axiom coverage:
//     TypeSafe — VendorBackend is a strong scoped enum;
//                cross-tier mixing requires `std::to_underlying`.
//   Runtime cost:
//     leq / join / meet — single integer compare; the eight-element
//     domain compiles to a 1-byte field.  When wrapped at a fixed
//     type-level backend via `VendorLattice::At<X>`, the grade EBO-
//     collapses to zero bytes.
//
// ── At<T> singleton sub-lattice ─────────────────────────────────────
//
// Mirrors the eight sister chain lattices: a per-VendorBackend
// singleton sub-lattice with empty element_type for regime-1 EBO
// collapse.  At the type-pinned wrapper level (`Vendor<NV, T>`), the
// grade is empty — the partial-order leq/meet/join operate on the
// FULL VendorLattice when the dispatcher checks `satisfies<>` /
// `relax<>` between two distinct fixed-tier wrappers.
//
// See FOUND-G53 (this file) for the lattice; FOUND-G54
// (safety/Vendor.h) for the type-pinned wrapper;
// 28_04_2026_effects.md §4.3.9 + §4.7 for the production-call-site
// rationale; MIMIC.md for the per-vendor backend that consumes this.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── VendorBackend — partial order over backend identity ───────────
//
// Ordinal convention: None=0 (synthetic ⊥), Portable=255 (synthetic
// ⊤), middle vendors at small distinct values.  The enum's
// underlying integer ordering is NOT the lattice order — VendorLattice
// is a partial order, not a chain.  Underlying values are chosen
// for compact 1-byte storage AND to match the spec's Portable=255
// convention (allowing fast "is_specific_vendor" check via
// `static_cast<uint8_t>(b) != 255`).
enum class VendorBackend : std::uint8_t {
    None     = 0,    // ⊥: synthetic, no kernel — admits nothing
    CPU      = 1,    // x86_64 / aarch64 host
    NV       = 2,    // NVIDIA GPU sm_8x/9x/10x
    AMD      = 3,    // AMD GPU gfx9xx/10xx/11xx
    TPU      = 4,    // Google TPU MXU
    TRN      = 5,    // AWS Trainium NEFF
    CER      = 6,    // Cerebras WSE
    Portable = 255,  // ⊤: vendor-agnostic, runs everywhere
};

inline constexpr std::size_t vendor_backend_count =
    std::meta::enumerators_of(^^VendorBackend).size();

[[nodiscard]] consteval std::string_view vendor_backend_name(
    VendorBackend b) noexcept {
    switch (b) {
        case VendorBackend::None:     return "None";
        case VendorBackend::CPU:      return "CPU";
        case VendorBackend::NV:       return "NV";
        case VendorBackend::AMD:      return "AMD";
        case VendorBackend::TPU:      return "TPU";
        case VendorBackend::TRN:      return "TRN";
        case VendorBackend::CER:      return "CER";
        case VendorBackend::Portable: return "Portable";
        default:                      return std::string_view{
            "<unknown VendorBackend>"};
    }
}

// ── Full VendorLattice (partial order) ──────────────────────────────
struct VendorLattice {
    using element_type = VendorBackend;

    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return VendorBackend::None;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return VendorBackend::Portable;
    }

    // leq(a, b) — partial-order check.  The hand-written core of the
    // VendorLattice; replaces ChainLatticeOps's std::to_underlying
    // comparison.  Three rules in priority order:
    //   1. Reflexive: x ⊑ x for every x.
    //   2. Bottom: None ⊑ x for every x.
    //   3. Top: x ⊑ Portable for every x.
    // No other pairs are related — distinct middle vendors are
    // mutually incomparable.
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        if (a == b) return true;
        if (a == VendorBackend::None) return true;
        if (b == VendorBackend::Portable) return true;
        return false;
    }

    // join(a, b) — least upper bound.  Routes through the top sentinel
    // for cross-vendor pairs.
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        if (a == b) return a;
        if (a == VendorBackend::None) return b;
        if (b == VendorBackend::None) return a;
        if (a == VendorBackend::Portable || b == VendorBackend::Portable) {
            return VendorBackend::Portable;
        }
        // Two distinct middle vendors: their LUB is Portable.
        return VendorBackend::Portable;
    }

    // meet(a, b) — greatest lower bound.  Routes through the bottom
    // sentinel for cross-vendor pairs.
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        if (a == b) return a;
        if (a == VendorBackend::Portable) return b;
        if (b == VendorBackend::Portable) return a;
        if (a == VendorBackend::None || b == VendorBackend::None) {
            return VendorBackend::None;
        }
        // Two distinct middle vendors: their GLB is None.
        return VendorBackend::None;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "VendorLattice";
    }

    // ── At<B> — per-backend singleton sub-lattice ─────────────────
    //
    // Mirrors the eight sister chain lattices.  Empty element_type
    // for regime-1 EBO collapse.  Within a single fixed backend B,
    // there is only one element; leq is trivially true, join/meet
    // return the singleton.
    template <VendorBackend B>
    struct At {
        struct element_type {
            using vendor_backend_value_type = VendorBackend;
            [[nodiscard]] constexpr operator vendor_backend_value_type() const noexcept {
                return B;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr VendorBackend backend = B;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (B) {
                case VendorBackend::None:     return "VendorLattice::At<None>";
                case VendorBackend::CPU:      return "VendorLattice::At<CPU>";
                case VendorBackend::NV:       return "VendorLattice::At<NV>";
                case VendorBackend::AMD:      return "VendorLattice::At<AMD>";
                case VendorBackend::TPU:      return "VendorLattice::At<TPU>";
                case VendorBackend::TRN:      return "VendorLattice::At<TRN>";
                case VendorBackend::CER:      return "VendorLattice::At<CER>";
                case VendorBackend::Portable: return "VendorLattice::At<Portable>";
                default:                      return "VendorLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace vendor_backend {
    using NoneVendor     = VendorLattice::At<VendorBackend::None>;
    using CpuVendor      = VendorLattice::At<VendorBackend::CPU>;
    using NvVendor       = VendorLattice::At<VendorBackend::NV>;
    using AmdVendor      = VendorLattice::At<VendorBackend::AMD>;
    using TpuVendor      = VendorLattice::At<VendorBackend::TPU>;
    using TrnVendor      = VendorLattice::At<VendorBackend::TRN>;
    using CerVendor      = VendorLattice::At<VendorBackend::CER>;
    using PortableVendor = VendorLattice::At<VendorBackend::Portable>;
}  // namespace vendor_backend

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::vendor_lattice_self_test {

static_assert(vendor_backend_count == 8,
    "VendorBackend catalog diverged from {None, CPU, NV, AMD, TPU, "
    "TRN, CER, Portable}; confirm intent and update both the lattice "
    "leq/join/meet (which special-case None and Portable) AND the "
    "Mimic per-vendor backend dispatcher's admission gates.");

[[nodiscard]] consteval bool every_vendor_backend_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^VendorBackend));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (vendor_backend_name([:en:]) ==
            std::string_view{"<unknown VendorBackend>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_vendor_backend_has_name(),
    "vendor_backend_name() switch missing arm for at least one backend.");

static_assert(Lattice<VendorLattice>);
static_assert(BoundedLattice<VendorLattice>);
static_assert(Lattice<vendor_backend::NoneVendor>);
static_assert(Lattice<vendor_backend::NvVendor>);
static_assert(Lattice<vendor_backend::AmdVendor>);
static_assert(Lattice<vendor_backend::PortableVendor>);
static_assert(BoundedLattice<vendor_backend::PortableVendor>);

static_assert(!UnboundedLattice<VendorLattice>);
static_assert(!Semiring<VendorLattice>);

static_assert(std::is_empty_v<vendor_backend::NoneVendor::element_type>);
static_assert(std::is_empty_v<vendor_backend::NvVendor::element_type>);
static_assert(std::is_empty_v<vendor_backend::AmdVendor::element_type>);
static_assert(std::is_empty_v<vendor_backend::PortableVendor::element_type>);

// ── Bottom + top witnesses ────────────────────────────────────────
static_assert(VendorLattice::bottom() == VendorBackend::None);
static_assert(VendorLattice::top()    == VendorBackend::Portable);

// ── Direct order witnesses — every meaningful pair ────────────────
//
// THE LOAD-BEARING witnesses for the partial-order discipline.
// Every cell here is a production safety guarantee.

// Reflexivity at every backend.
static_assert(VendorLattice::leq(VendorBackend::None,     VendorBackend::None));
static_assert(VendorLattice::leq(VendorBackend::CPU,      VendorBackend::CPU));
static_assert(VendorLattice::leq(VendorBackend::NV,       VendorBackend::NV));
static_assert(VendorLattice::leq(VendorBackend::AMD,      VendorBackend::AMD));
static_assert(VendorLattice::leq(VendorBackend::TPU,      VendorBackend::TPU));
static_assert(VendorLattice::leq(VendorBackend::TRN,      VendorBackend::TRN));
static_assert(VendorLattice::leq(VendorBackend::CER,      VendorBackend::CER));
static_assert(VendorLattice::leq(VendorBackend::Portable, VendorBackend::Portable));

// None ⊑ everything.
static_assert(VendorLattice::leq(VendorBackend::None, VendorBackend::CPU));
static_assert(VendorLattice::leq(VendorBackend::None, VendorBackend::NV));
static_assert(VendorLattice::leq(VendorBackend::None, VendorBackend::AMD));
static_assert(VendorLattice::leq(VendorBackend::None, VendorBackend::TPU));
static_assert(VendorLattice::leq(VendorBackend::None, VendorBackend::TRN));
static_assert(VendorLattice::leq(VendorBackend::None, VendorBackend::CER));
static_assert(VendorLattice::leq(VendorBackend::None, VendorBackend::Portable));

// everything ⊑ Portable.
static_assert(VendorLattice::leq(VendorBackend::CPU, VendorBackend::Portable));
static_assert(VendorLattice::leq(VendorBackend::NV,  VendorBackend::Portable));
static_assert(VendorLattice::leq(VendorBackend::AMD, VendorBackend::Portable));
static_assert(VendorLattice::leq(VendorBackend::TPU, VendorBackend::Portable));
static_assert(VendorLattice::leq(VendorBackend::TRN, VendorBackend::Portable));
static_assert(VendorLattice::leq(VendorBackend::CER, VendorBackend::Portable));

// Distinct vendors INCOMPARABLE — neither direction holds.
// THE LOAD-BEARING NEGATIVE: this is what the chain interpretation
// would silently break.  Every pair below MUST be rejected.
static_assert(!VendorLattice::leq(VendorBackend::NV,  VendorBackend::AMD));
static_assert(!VendorLattice::leq(VendorBackend::AMD, VendorBackend::NV));
static_assert(!VendorLattice::leq(VendorBackend::NV,  VendorBackend::TPU));
static_assert(!VendorLattice::leq(VendorBackend::TPU, VendorBackend::NV));
static_assert(!VendorLattice::leq(VendorBackend::CPU, VendorBackend::NV));
static_assert(!VendorLattice::leq(VendorBackend::NV,  VendorBackend::CPU));
static_assert(!VendorLattice::leq(VendorBackend::TRN, VendorBackend::CER));
static_assert(!VendorLattice::leq(VendorBackend::CER, VendorBackend::TRN));
static_assert(!VendorLattice::leq(VendorBackend::AMD, VendorBackend::TPU));
static_assert(!VendorLattice::leq(VendorBackend::TPU, VendorBackend::AMD));

// Reverse rules — Portable ⊑ X is FALSE for X ≠ Portable.
static_assert(!VendorLattice::leq(VendorBackend::Portable, VendorBackend::CPU));
static_assert(!VendorLattice::leq(VendorBackend::Portable, VendorBackend::NV));
static_assert(!VendorLattice::leq(VendorBackend::Portable, VendorBackend::AMD));
static_assert(!VendorLattice::leq(VendorBackend::Portable, VendorBackend::None));

// Reverse rules — X ⊑ None is FALSE for X ≠ None.
static_assert(!VendorLattice::leq(VendorBackend::CPU,      VendorBackend::None));
static_assert(!VendorLattice::leq(VendorBackend::NV,       VendorBackend::None));
static_assert(!VendorLattice::leq(VendorBackend::AMD,      VendorBackend::None));
static_assert(!VendorLattice::leq(VendorBackend::Portable, VendorBackend::None));

// ── Join / meet witnesses ─────────────────────────────────────────

// Distinct vendors → join = Portable, meet = None.
static_assert(VendorLattice::join(VendorBackend::NV,  VendorBackend::AMD) == VendorBackend::Portable);
static_assert(VendorLattice::meet(VendorBackend::NV,  VendorBackend::AMD) == VendorBackend::None);
static_assert(VendorLattice::join(VendorBackend::TPU, VendorBackend::TRN) == VendorBackend::Portable);
static_assert(VendorLattice::meet(VendorBackend::TPU, VendorBackend::TRN) == VendorBackend::None);
static_assert(VendorLattice::join(VendorBackend::CPU, VendorBackend::CER) == VendorBackend::Portable);
static_assert(VendorLattice::meet(VendorBackend::CPU, VendorBackend::CER) == VendorBackend::None);

// None as identity for join.
static_assert(VendorLattice::join(VendorBackend::None, VendorBackend::CPU) == VendorBackend::CPU);
static_assert(VendorLattice::join(VendorBackend::None, VendorBackend::NV)  == VendorBackend::NV);
static_assert(VendorLattice::join(VendorBackend::None, VendorBackend::Portable) == VendorBackend::Portable);

// Portable as identity for meet.
static_assert(VendorLattice::meet(VendorBackend::Portable, VendorBackend::CPU) == VendorBackend::CPU);
static_assert(VendorLattice::meet(VendorBackend::Portable, VendorBackend::NV)  == VendorBackend::NV);
static_assert(VendorLattice::meet(VendorBackend::Portable, VendorBackend::None) == VendorBackend::None);

// None absorbs in meet.
static_assert(VendorLattice::meet(VendorBackend::None, VendorBackend::CPU) == VendorBackend::None);
static_assert(VendorLattice::meet(VendorBackend::None, VendorBackend::NV)  == VendorBackend::None);
static_assert(VendorLattice::meet(VendorBackend::None, VendorBackend::Portable) == VendorBackend::None);

// Portable absorbs in join.
static_assert(VendorLattice::join(VendorBackend::Portable, VendorBackend::CPU) == VendorBackend::Portable);
static_assert(VendorLattice::join(VendorBackend::Portable, VendorBackend::None) == VendorBackend::Portable);

// Idempotence at every backend.
static_assert(VendorLattice::join(VendorBackend::NV,       VendorBackend::NV)       == VendorBackend::NV);
static_assert(VendorLattice::meet(VendorBackend::NV,       VendorBackend::NV)       == VendorBackend::NV);
static_assert(VendorLattice::join(VendorBackend::Portable, VendorBackend::Portable) == VendorBackend::Portable);
static_assert(VendorLattice::meet(VendorBackend::None,     VendorBackend::None)     == VendorBackend::None);

// ── Exhaustive lattice-axiom verification — (8 backends)³ = 512 ───
//
// Hand-written exhaustive verifier (cannot reuse
// verify_chain_lattice_exhaustive because Vendor is NOT a chain).
// Confirms reflexivity / antisymmetry / transitivity of leq plus
// idempotence / commutativity / associativity / absorption of
// meet+join across every (a, b, c) triple.

inline constexpr VendorBackend kAll[] = {
    VendorBackend::None,
    VendorBackend::CPU,
    VendorBackend::NV,
    VendorBackend::AMD,
    VendorBackend::TPU,
    VendorBackend::TRN,
    VendorBackend::CER,
    VendorBackend::Portable,
};

[[nodiscard]] consteval bool verify_partial_order_exhaustive() noexcept {
    using L = VendorLattice;
    for (auto a : kAll) {
        // Reflexivity.
        if (!L::leq(a, a)) return false;
        for (auto b : kAll) {
            // Antisymmetry: leq(a,b) ∧ leq(b,a) ⇒ a == b.
            if (L::leq(a, b) && L::leq(b, a) && a != b) return false;
            // Commutativity of join/meet.
            if (L::join(a, b) != L::join(b, a)) return false;
            if (L::meet(a, b) != L::meet(b, a)) return false;
            // Idempotence.
            if (L::join(a, a) != a) return false;
            if (L::meet(a, a) != a) return false;
            // Absorption: a ∨ (a ∧ b) = a; a ∧ (a ∨ b) = a.
            if (L::join(a, L::meet(a, b)) != a) return false;
            if (L::meet(a, L::join(a, b)) != a) return false;
            // Bounds.
            if (!L::leq(L::bottom(), a)) return false;
            if (!L::leq(a, L::top()))    return false;
            for (auto c : kAll) {
                // Transitivity: a ⊑ b ∧ b ⊑ c ⇒ a ⊑ c.
                if (L::leq(a, b) && L::leq(b, c) && !L::leq(a, c)) return false;
                // Associativity of join/meet.
                if (L::join(L::join(a, b), c) != L::join(a, L::join(b, c))) return false;
                if (L::meet(L::meet(a, b), c) != L::meet(a, L::meet(b, c))) return false;
                // leq is consistent with meet/join:
                //   a ⊑ b iff a ∧ b = a iff a ∨ b = b.
                bool by_meet = (L::meet(a, b) == a);
                bool by_join = (L::join(a, b) == b);
                bool by_leq  = L::leq(a, b);
                if (by_leq != by_meet) return false;
                if (by_leq != by_join) return false;
            }
        }
    }
    return true;
}
static_assert(verify_partial_order_exhaustive(),
    "VendorLattice's partial-order axioms must hold at every "
    "(VendorBackend)³ triple over the eight elements (None / CPU / "
    "NV / AMD / TPU / TRN / CER / Portable).  This is the exhaustive "
    "lattice-soundness witness — if it fires, one of leq / join / "
    "meet has a bug for some pair OR the special-case routing for "
    "None / Portable / cross-vendor distinct middle is wrong.");

// ── Non-distributivity witness ────────────────────────────────────
//
// VendorLattice is intentionally non-distributive (see lattice
// docblock).  Pinning the failure case at compile time to make the
// design choice review-discoverable: a future maintainer who tries
// to "simplify" VendorLattice into a chain would have to delete
// this assertion AND its docblock first.

[[nodiscard]] consteval bool non_distributive_witness() noexcept {
    using L = VendorLattice;
    // (NV ∨ AMD) ∧ TPU = Portable ∧ TPU = TPU
    // (NV ∧ TPU) ∨ (AMD ∧ TPU) = None ∨ None = None
    // TPU ≠ None — hence non-distributive.
    auto lhs = L::meet(L::join(VendorBackend::NV, VendorBackend::AMD),
                       VendorBackend::TPU);
    auto rhs = L::join(L::meet(VendorBackend::NV, VendorBackend::TPU),
                       L::meet(VendorBackend::AMD, VendorBackend::TPU));
    return lhs == VendorBackend::TPU && rhs == VendorBackend::None
        && lhs != rhs;
}
static_assert(non_distributive_witness(),
    "VendorLattice MUST be non-distributive (see docblock).  If "
    "this fires, either (a) the lattice was 'simplified' into a "
    "chain — DEFEATING the cross-vendor incomparability that the "
    "Vendor wrapper's safety guarantee depends on — or (b) someone "
    "added a synthetic intermediate element that closed the "
    "distributivity gap.  Either way, audit before resolving.");

// ── Names ────────────────────────────────────────────────────────
static_assert(VendorLattice::name() == "VendorLattice");
static_assert(vendor_backend::NoneVendor::name()     == "VendorLattice::At<None>");
static_assert(vendor_backend::CpuVendor::name()      == "VendorLattice::At<CPU>");
static_assert(vendor_backend::NvVendor::name()       == "VendorLattice::At<NV>");
static_assert(vendor_backend::AmdVendor::name()      == "VendorLattice::At<AMD>");
static_assert(vendor_backend::TpuVendor::name()      == "VendorLattice::At<TPU>");
static_assert(vendor_backend::TrnVendor::name()      == "VendorLattice::At<TRN>");
static_assert(vendor_backend::CerVendor::name()      == "VendorLattice::At<CER>");
static_assert(vendor_backend::PortableVendor::name() == "VendorLattice::At<Portable>");

[[nodiscard]] consteval bool every_at_vendor_backend_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^VendorBackend));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (VendorLattice::At<([:en:])>::name() ==
            std::string_view{"VendorLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_vendor_backend_has_name(),
    "VendorLattice::At<B>::name() switch missing an arm.");

static_assert(vendor_backend::NoneVendor::backend     == VendorBackend::None);
static_assert(vendor_backend::CpuVendor::backend      == VendorBackend::CPU);
static_assert(vendor_backend::NvVendor::backend       == VendorBackend::NV);
static_assert(vendor_backend::AmdVendor::backend      == VendorBackend::AMD);
static_assert(vendor_backend::TpuVendor::backend      == VendorBackend::TPU);
static_assert(vendor_backend::TrnVendor::backend      == VendorBackend::TRN);
static_assert(vendor_backend::CerVendor::backend      == VendorBackend::CER);
static_assert(vendor_backend::PortableVendor::backend == VendorBackend::Portable);

// ── Layout invariants ───────────────────────────────────────────────
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

template <typename T_>
using PortableGraded = Graded<ModalityKind::Absolute,
                              vendor_backend::PortableVendor, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableGraded, double);

template <typename T_>
using NvGraded = Graded<ModalityKind::Absolute,
                        vendor_backend::NvVendor, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NvGraded, EightByteValue);

template <typename T_>
using AmdGraded = Graded<ModalityKind::Absolute,
                         vendor_backend::AmdVendor, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AmdGraded, EightByteValue);

template <typename T_>
using NoneGraded = Graded<ModalityKind::Absolute,
                          vendor_backend::NoneVendor, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneGraded, EightByteValue);

inline void runtime_smoke_test() {
    VendorBackend a = VendorBackend::NV;
    VendorBackend b = VendorBackend::AMD;
    [[maybe_unused]] bool          l1   = VendorLattice::leq(a, b);
    [[maybe_unused]] VendorBackend j1   = VendorLattice::join(a, b);
    [[maybe_unused]] VendorBackend m1   = VendorLattice::meet(a, b);
    [[maybe_unused]] VendorBackend bot  = VendorLattice::bottom();
    [[maybe_unused]] VendorBackend topv = VendorLattice::top();

    VendorBackend portable = VendorBackend::Portable;
    [[maybe_unused]] VendorBackend j2 = VendorLattice::join(portable, a);   // Portable
    [[maybe_unused]] VendorBackend m2 = VendorLattice::meet(portable, b);   // AMD

    OneByteValue v{42};
    PortableGraded<OneByteValue> initial{v, vendor_backend::PortableVendor::bottom()};
    auto widened   = initial.weaken(vendor_backend::PortableVendor::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(vendor_backend::PortableVendor::top());

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    vendor_backend::PortableVendor::element_type e{};
    [[maybe_unused]] VendorBackend rec = e;
}

}  // namespace detail::vendor_lattice_self_test

}  // namespace crucible::algebra::lattices
