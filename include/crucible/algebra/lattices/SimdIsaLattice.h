#pragma once

// ── crucible::algebra::lattices::SimdIsaLattice ─────────────────────
//
// NON-DISTRIBUTIVE partial-order lattice over the SIMD-ISA capability
// spectrum.  Two MUTUALLY-INCOMPARABLE internal chains — the x86 trunk
// and the ARM trunk — joined only at a shared bottom (Scalar, no SIMD)
// and a shared top (Portable, the all-or-any kernel).  The grading axis
// underlying the SimdWidthPinned wrapper (FIXY-V-256, Agent 11 §3.7).
//
// ── Structural shape (two chains sharing ⊤ and ⊥) ──────────────────
//
//                          ┌──────────────┐
//                          │  Portable ⊤  │  strongest claim
//                          └──────┬───────┘  (runs everywhere)
//                  ┌──────────────┴──────────────┐
//                  │                              │
//             ┌────┴─────┐                  ┌─────┴────┐
//             │ AVX512BW │ (x86 top)        │   SVE2   │ (ARM top)
//             │ AVX512F  │                  │   SVE    │
//             │ AVX2     │                  │ NEON-Dot │
//             │ SSE4.2   │                  │ NEON-FP16│
//             │ SSE4.1   │                  │   NEON   │ (ARM bottom)
//             │ SSSE3    │                  └─────┬────┘
//             │ SSE3     │                        │
//             │ SSE2     │ (x86 bottom)           │
//             └────┬─────┘                        │
//                  └──────────────┬───────────────┘
//                                 ▼
//                          ┌──────────────┐
//                          │  Scalar  ⊥   │  weakest claim
//                          └──────────────┘  (no SIMD; satisfies no gate)
//
// Each trunk is an INTERNAL CHAIN (totally ordered: SSE2 ⊑ SSE3 ⊑ ... ⊑
// AVX512BW; NEON ⊑ NEON-FP16 ⊑ ... ⊑ SVE2).  Across trunks every pair is
// INCOMPARABLE — an AVX2 binary does not run on an ARM CPU and vice
// versa; neither subsumes the other.  Like VendorLattice, this is a
// hand-written partial order, NOT a ChainLattice.
//
// THE LOAD-BEARING USE CASE (Agent 11 §3.7, consumed by V-256
// SimdWidthPinned + V-260 collision rule S001): a function declared
// `requires SimdIsa<Avx2>::satisfies<RuntimeFloor>` admits a load on an
// AVX-512 CPU (Avx2 ⊑ Avx512F) but rejects an SSE2 CPU (Avx2 ⋢ Sse2) AND
// rejects every ARM CPU (cross-trunk incomparable).  The `satisfies`
// wrapper is V-256's deliverable; this header ships only the lattice
// (leq / join / meet / At<>), mirroring the VendorLattice → safety/
// Vendor.h split exactly.
//
// ── Direction convention (matches the audit-verified universal) ────
//
// Stronger capability = HIGHER in the lattice.  `leq(consumer, provider)`
// reads "a consumer pinned at ISA `consumer` is satisfied by a provider
// CPU at ISA `provider`" — i.e. the compiled-for ISA must be at-or-below
// the runtime CPU's ISA for the binary to run.
//
//   leq(Sse2,   Avx2)     = true  (within x86 trunk; AVX2 CPU runs SSE2 code)
//   leq(Avx2,   Sse2)     = false (SSE2 CPU lacks AVX2 — would #UD)
//   leq(Avx2,   Sve2)     = false (cross-trunk: x86 code never runs on ARM)
//   leq(Scalar, Avx2)     = true  (⊥: scalar code runs anywhere)
//   leq(Avx2,   Portable) = true  (⊤: a Portable provider runs every ISA's code)
//   leq(Portable, Avx2)   = false (a Portable kernel is not "satisfied by" AVX2-only)
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class SimdIsa.  Underlying encoding packs the trunk into
// the high nibble (x86 = 0x1_, ARM = 0x2_) with the per-trunk rank in the
// low nibble, so the integer order WITHIN a trunk equals the capability
// rank.  Scalar = 0x00 (⊥), Portable = 0xFF (⊤).  The underlying-integer
// order is NOT the lattice order across trunks — SimdIsa is a partial
// order, not a chain.
//
// Bottom = Scalar    (weakest — no SIMD; satisfies no SIMD gate)
// Top    = Portable  (strongest — universal kernel; satisfies every gate)
// Join (LUB):
//   - x ∨ x                    = x
//   - Scalar ∨ x               = x        (bottom is identity for ∨)
//   - x ∨ Portable             = Portable
//   - same trunk               = the higher-rank element (max)
//   - distinct trunks          = Portable (their only common upper bound is ⊤)
// Meet (GLB):
//   - x ∧ x                    = x
//   - x ∧ Portable             = x        (top is identity for ∧)
//   - Scalar ∧ x               = Scalar
//   - same trunk               = the lower-rank element (min)
//   - distinct trunks          = Scalar (their only common lower bound is ⊥)
//
// NOT distributive — verified below.  Like VendorLattice, a partial order
// that is bounded by a single ⊤/⊥ AND has two incomparable internal
// chains CANNOT be distributive.  The canonical failure (each operand in
// a different trunk, with one trunk-internal pair related):
//   (AVX2 ∨ NEON) ∧ SVE = Portable ∧ SVE = SVE
//   (AVX2 ∧ SVE) ∨ (NEON ∧ SVE) = Scalar ∨ NEON = NEON   (NEON ⊑ SVE in ARM trunk)
//   SVE ≠ NEON — hence non-distributive.
// (The Agent 11 §3.1 worked example writes the RHS as Scalar; the precise
// value is NEON because NEON ⊑ SVE are both ARM-trunk, so NEON ∧ SVE =
// NEON, not Scalar.  Either way LHS ≠ RHS — the lattice is non-
// distributive, which is the load-bearing property.)
//
//   Axiom coverage:
//     TypeSafe — SimdIsa is a strong scoped enum; cross-tier mixing
//                requires `std::to_underlying`.
//   Runtime cost:
//     leq / join / meet — a handful of integer compares over a 15-element
//     domain; the carrier compiles to a 1-byte field.  Type-pinned via
//     `SimdIsaLattice::At<ISA>`, the grade EBO-collapses to zero bytes.
//
// See FIXY-V-251 (HwInstructionLattice) + FIXY-V-252 (BarrierStrengthLattice)
// for the sibling HW-axis lattices; FIXY-V-256 (safety/SimdWidthPinned.h)
// for the type-pinned wrapper that ships `satisfies<>` + the
// `row_hash_contribution` federation-cache discriminator (deferred to the
// wrapper exactly as VendorLattice defers to safety/Vendor.h — the lattice
// layer pulls no safety/diag header).

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── SimdIsa — partial order over SIMD-ISA capability ──────────────
//
// Encoding: high nibble = trunk (1 = x86, 2 = ARM), low nibble = rank.
// Scalar = 0x00 (⊥), Portable = 0xFF (⊤).  Within a trunk the integer
// order IS the capability rank; across trunks the integer order is
// meaningless (the lattice is a partial order).
enum class SimdIsa : std::uint8_t {
    Scalar         = 0x00,  // ⊥: no SIMD — runs on any CPU, satisfies no SIMD gate
    // x86 trunk (bottom → top)
    Sse2           = 0x10,
    Sse3           = 0x11,
    Ssse3          = 0x12,
    Sse41          = 0x13,
    Sse42          = 0x14,
    Avx2           = 0x15,  // AVX2 + BMI2 + FMA (the Haswell baseline)
    Avx512F        = 0x16,
    Avx512Bw       = 0x17,
    // ARM trunk (bottom → top)
    Neon           = 0x20,
    NeonFp16       = 0x21,
    NeonDotProduct = 0x22,
    Sve            = 0x23,
    Sve2           = 0x24,
    Portable       = 0xFF,  // ⊤: vendor/ISA-agnostic — satisfies every gate
};

inline constexpr std::size_t simd_isa_count =
    std::meta::enumerators_of(^^SimdIsa).size();

[[nodiscard]] consteval std::string_view simd_isa_name(SimdIsa x) noexcept {
    switch (x) {
        case SimdIsa::Scalar:         return "Scalar";
        case SimdIsa::Sse2:           return "Sse2";
        case SimdIsa::Sse3:           return "Sse3";
        case SimdIsa::Ssse3:          return "Ssse3";
        case SimdIsa::Sse41:          return "Sse41";
        case SimdIsa::Sse42:          return "Sse42";
        case SimdIsa::Avx2:           return "Avx2";
        case SimdIsa::Avx512F:        return "Avx512F";
        case SimdIsa::Avx512Bw:       return "Avx512Bw";
        case SimdIsa::Neon:           return "Neon";
        case SimdIsa::NeonFp16:       return "NeonFp16";
        case SimdIsa::NeonDotProduct: return "NeonDotProduct";
        case SimdIsa::Sve:            return "Sve";
        case SimdIsa::Sve2:           return "Sve2";
        case SimdIsa::Portable:       return "Portable";
        default:                      return std::string_view{"<unknown SimdIsa>"};
    }
}

// ── Trunk classification (the partial-order discriminator) ─────────
[[nodiscard]] constexpr bool simd_isa_is_x86(SimdIsa x) noexcept {
    const auto u = std::to_underlying(x);
    return u >= std::to_underlying(SimdIsa::Sse2) &&
           u <= std::to_underlying(SimdIsa::Avx512Bw);
}
[[nodiscard]] constexpr bool simd_isa_is_arm(SimdIsa x) noexcept {
    const auto u = std::to_underlying(x);
    return u >= std::to_underlying(SimdIsa::Neon) &&
           u <= std::to_underlying(SimdIsa::Sve2);
}
// Two ISAs share a trunk iff both are x86-trunk or both are ARM-trunk.
// Scalar and Portable belong to NEITHER trunk (they are the shared
// sentinels), so same_trunk(Scalar, x) and same_trunk(Portable, x) are
// always false — leq/join/meet special-case them before this check.
[[nodiscard]] constexpr bool simd_isa_same_trunk(SimdIsa a, SimdIsa b) noexcept {
    return (simd_isa_is_x86(a) && simd_isa_is_x86(b)) ||
           (simd_isa_is_arm(a) && simd_isa_is_arm(b));
}

// ── Full SimdIsaLattice (partial order) ─────────────────────────────
struct SimdIsaLattice {
    using element_type = SimdIsa;

    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return SimdIsa::Scalar;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return SimdIsa::Portable;
    }

    // leq(a, b) — partial-order check.  Priority order:
    //   1. Reflexive: x ⊑ x.
    //   2. Bottom: Scalar ⊑ x for every x.
    //   3. Top: x ⊑ Portable for every x.
    //   4. Same trunk: compare per-trunk rank (the low-nibble integer).
    //   5. Otherwise (cross-trunk) incomparable.
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        if (a == b) return true;
        if (a == SimdIsa::Scalar) return true;
        if (b == SimdIsa::Portable) return true;
        if (simd_isa_same_trunk(a, b)) {
            return std::to_underlying(a) <= std::to_underlying(b);
        }
        return false;
    }

    // join(a, b) — least upper bound.  Cross-trunk pairs route through ⊤.
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        if (a == b) return a;
        if (a == SimdIsa::Scalar) return b;
        if (b == SimdIsa::Scalar) return a;
        if (a == SimdIsa::Portable || b == SimdIsa::Portable) {
            return SimdIsa::Portable;
        }
        if (simd_isa_same_trunk(a, b)) {
            return std::to_underlying(a) >= std::to_underlying(b) ? a : b;
        }
        return SimdIsa::Portable;  // distinct trunks: LUB is ⊤
    }

    // meet(a, b) — greatest lower bound.  Cross-trunk pairs route through ⊥.
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        if (a == b) return a;
        if (a == SimdIsa::Portable) return b;
        if (b == SimdIsa::Portable) return a;
        if (a == SimdIsa::Scalar || b == SimdIsa::Scalar) {
            return SimdIsa::Scalar;
        }
        if (simd_isa_same_trunk(a, b)) {
            return std::to_underlying(a) <= std::to_underlying(b) ? a : b;
        }
        return SimdIsa::Scalar;  // distinct trunks: GLB is ⊥
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "SimdIsaLattice";
    }

    // ── At<ISA> — per-ISA singleton sub-lattice ───────────────────
    //
    // Mirrors VendorLattice::At<B>.  Empty element_type for regime-1 EBO
    // collapse; within a single fixed ISA there is one element, so leq is
    // trivially true and join/meet return the singleton.  The full
    // SimdIsaLattice partial order is consulted when the dispatcher checks
    // `satisfies<>` between two distinct fixed-ISA wrappers (V-256).
    template <SimdIsa I>
    struct At {
        struct element_type {
            using simd_isa_value_type = SimdIsa;
            [[nodiscard]] constexpr operator simd_isa_value_type() const noexcept {
                return I;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr SimdIsa isa = I;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (I) {
                case SimdIsa::Scalar:         return "SimdIsaLattice::At<Scalar>";
                case SimdIsa::Sse2:           return "SimdIsaLattice::At<Sse2>";
                case SimdIsa::Sse3:           return "SimdIsaLattice::At<Sse3>";
                case SimdIsa::Ssse3:          return "SimdIsaLattice::At<Ssse3>";
                case SimdIsa::Sse41:          return "SimdIsaLattice::At<Sse41>";
                case SimdIsa::Sse42:          return "SimdIsaLattice::At<Sse42>";
                case SimdIsa::Avx2:           return "SimdIsaLattice::At<Avx2>";
                case SimdIsa::Avx512F:        return "SimdIsaLattice::At<Avx512F>";
                case SimdIsa::Avx512Bw:       return "SimdIsaLattice::At<Avx512Bw>";
                case SimdIsa::Neon:           return "SimdIsaLattice::At<Neon>";
                case SimdIsa::NeonFp16:       return "SimdIsaLattice::At<NeonFp16>";
                case SimdIsa::NeonDotProduct: return "SimdIsaLattice::At<NeonDotProduct>";
                case SimdIsa::Sve:            return "SimdIsaLattice::At<Sve>";
                case SimdIsa::Sve2:           return "SimdIsaLattice::At<Sve2>";
                case SimdIsa::Portable:       return "SimdIsaLattice::At<Portable>";
                default:                      return "SimdIsaLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace simd_isa {
    using ScalarIsa   = SimdIsaLattice::At<SimdIsa::Scalar>;
    using Sse2Isa     = SimdIsaLattice::At<SimdIsa::Sse2>;
    using Sse3Isa     = SimdIsaLattice::At<SimdIsa::Sse3>;
    using Ssse3Isa    = SimdIsaLattice::At<SimdIsa::Ssse3>;
    using Sse41Isa    = SimdIsaLattice::At<SimdIsa::Sse41>;
    using Sse42Isa    = SimdIsaLattice::At<SimdIsa::Sse42>;
    using Avx2Isa     = SimdIsaLattice::At<SimdIsa::Avx2>;
    using Avx512fIsa  = SimdIsaLattice::At<SimdIsa::Avx512F>;
    using Avx512bwIsa = SimdIsaLattice::At<SimdIsa::Avx512Bw>;
    using NeonIsa     = SimdIsaLattice::At<SimdIsa::Neon>;
    using NeonFp16Isa = SimdIsaLattice::At<SimdIsa::NeonFp16>;
    using NeonDotIsa  = SimdIsaLattice::At<SimdIsa::NeonDotProduct>;
    using SveIsa      = SimdIsaLattice::At<SimdIsa::Sve>;
    using Sve2Isa     = SimdIsaLattice::At<SimdIsa::Sve2>;
    using PortableIsa = SimdIsaLattice::At<SimdIsa::Portable>;
}  // namespace simd_isa

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::simd_isa_lattice_self_test {

static_assert(simd_isa_count == 15,
    "SimdIsa catalog diverged from {Scalar, 8 x86 trunk, 5 ARM trunk, "
    "Portable}; confirm intent and update the trunk-classification helpers "
    "(simd_isa_is_x86 / simd_isa_is_arm bound the trunk ranges) AND the "
    "V-256 SimdWidthPinned wrapper's satisfies<> + V-260 collision rule.");

[[nodiscard]] consteval bool every_simd_isa_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^SimdIsa));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (simd_isa_name([:en:]) == std::string_view{"<unknown SimdIsa>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_simd_isa_has_name(),
    "simd_isa_name() switch missing an arm for at least one ISA.");

static_assert(Lattice<SimdIsaLattice>);
static_assert(BoundedLattice<SimdIsaLattice>);
static_assert(Lattice<simd_isa::ScalarIsa>);
static_assert(Lattice<simd_isa::Avx2Isa>);
static_assert(Lattice<simd_isa::SveIsa>);
static_assert(Lattice<simd_isa::PortableIsa>);
static_assert(BoundedLattice<simd_isa::PortableIsa>);

static_assert(!UnboundedLattice<SimdIsaLattice>);
static_assert(!Semiring<SimdIsaLattice>);

static_assert(std::is_empty_v<simd_isa::ScalarIsa::element_type>);
static_assert(std::is_empty_v<simd_isa::Avx2Isa::element_type>);
static_assert(std::is_empty_v<simd_isa::SveIsa::element_type>);
static_assert(std::is_empty_v<simd_isa::PortableIsa::element_type>);

// ── Bottom + top witnesses ────────────────────────────────────────
static_assert(SimdIsaLattice::bottom() == SimdIsa::Scalar);
static_assert(SimdIsaLattice::top()    == SimdIsa::Portable);

// ── Trunk-classification witnesses ────────────────────────────────
static_assert(simd_isa_is_x86(SimdIsa::Sse2));
static_assert(simd_isa_is_x86(SimdIsa::Avx512Bw));
static_assert(!simd_isa_is_x86(SimdIsa::Neon));
static_assert(!simd_isa_is_x86(SimdIsa::Scalar));
static_assert(!simd_isa_is_x86(SimdIsa::Portable));
static_assert(simd_isa_is_arm(SimdIsa::Neon));
static_assert(simd_isa_is_arm(SimdIsa::Sve2));
static_assert(!simd_isa_is_arm(SimdIsa::Avx2));
static_assert(!simd_isa_is_arm(SimdIsa::Scalar));
static_assert(!simd_isa_is_arm(SimdIsa::Portable));
static_assert(simd_isa_same_trunk(SimdIsa::Sse2, SimdIsa::Avx2));
static_assert(simd_isa_same_trunk(SimdIsa::Neon, SimdIsa::Sve2));
static_assert(!simd_isa_same_trunk(SimdIsa::Avx2, SimdIsa::Sve));
static_assert(!simd_isa_same_trunk(SimdIsa::Scalar, SimdIsa::Neon));
static_assert(!simd_isa_same_trunk(SimdIsa::Portable, SimdIsa::Avx2));

// ── Reflexivity at every ISA ──────────────────────────────────────
static_assert(SimdIsaLattice::leq(SimdIsa::Scalar,   SimdIsa::Scalar));
static_assert(SimdIsaLattice::leq(SimdIsa::Avx2,     SimdIsa::Avx2));
static_assert(SimdIsaLattice::leq(SimdIsa::Sve,      SimdIsa::Sve));
static_assert(SimdIsaLattice::leq(SimdIsa::Portable, SimdIsa::Portable));

// ── Scalar ⊑ everything ───────────────────────────────────────────
static_assert(SimdIsaLattice::leq(SimdIsa::Scalar, SimdIsa::Sse2));
static_assert(SimdIsaLattice::leq(SimdIsa::Scalar, SimdIsa::Avx512Bw));
static_assert(SimdIsaLattice::leq(SimdIsa::Scalar, SimdIsa::Neon));
static_assert(SimdIsaLattice::leq(SimdIsa::Scalar, SimdIsa::Sve2));
static_assert(SimdIsaLattice::leq(SimdIsa::Scalar, SimdIsa::Portable));

// ── everything ⊑ Portable ─────────────────────────────────────────
static_assert(SimdIsaLattice::leq(SimdIsa::Sse2,     SimdIsa::Portable));
static_assert(SimdIsaLattice::leq(SimdIsa::Avx512Bw, SimdIsa::Portable));
static_assert(SimdIsaLattice::leq(SimdIsa::Neon,     SimdIsa::Portable));
static_assert(SimdIsaLattice::leq(SimdIsa::Sve2,     SimdIsa::Portable));

// ── x86 trunk subsumption chain (the LOAD-BEARING positive) ───────
// SSE2 ⊑ SSE3 ⊑ SSSE3 ⊑ SSE4.1 ⊑ SSE4.2 ⊑ AVX2 ⊑ AVX512F ⊑ AVX512BW
static_assert(SimdIsaLattice::leq(SimdIsa::Sse2,    SimdIsa::Sse3));
static_assert(SimdIsaLattice::leq(SimdIsa::Sse3,    SimdIsa::Ssse3));
static_assert(SimdIsaLattice::leq(SimdIsa::Ssse3,   SimdIsa::Sse41));
static_assert(SimdIsaLattice::leq(SimdIsa::Sse41,   SimdIsa::Sse42));
static_assert(SimdIsaLattice::leq(SimdIsa::Sse42,   SimdIsa::Avx2));
static_assert(SimdIsaLattice::leq(SimdIsa::Avx2,    SimdIsa::Avx512F));
static_assert(SimdIsaLattice::leq(SimdIsa::Avx512F, SimdIsa::Avx512Bw));
// Transitive endpoints + the use-case witness from the docblock.
static_assert(SimdIsaLattice::leq(SimdIsa::Sse2,    SimdIsa::Avx512Bw));
static_assert(SimdIsaLattice::leq(SimdIsa::Avx2,    SimdIsa::Avx512F));    // AVX-512 CPU runs AVX2 code
static_assert(!SimdIsaLattice::leq(SimdIsa::Avx2,   SimdIsa::Sse2));       // SSE2 CPU rejects AVX2 code
static_assert(!SimdIsaLattice::leq(SimdIsa::Avx512Bw, SimdIsa::Avx2));     // descending direction is false

// ── ARM trunk subsumption chain (the LOAD-BEARING positive) ───────
// NEON ⊑ NEON-FP16 ⊑ NEON-Dot ⊑ SVE ⊑ SVE2
static_assert(SimdIsaLattice::leq(SimdIsa::Neon,           SimdIsa::NeonFp16));
static_assert(SimdIsaLattice::leq(SimdIsa::NeonFp16,       SimdIsa::NeonDotProduct));
static_assert(SimdIsaLattice::leq(SimdIsa::NeonDotProduct, SimdIsa::Sve));
static_assert(SimdIsaLattice::leq(SimdIsa::Sve,            SimdIsa::Sve2));
static_assert(SimdIsaLattice::leq(SimdIsa::Neon,           SimdIsa::Sve2));   // transitive
static_assert(!SimdIsaLattice::leq(SimdIsa::Sve2,          SimdIsa::Neon));   // descending is false

// ── Cross-trunk incomparability (THE LOAD-BEARING NEGATIVE) ───────
// Every x86 × ARM pair MUST be mutually incomparable — this is what the
// SimdWidthPinned wrapper's safety guarantee depends on (an x86 binary
// must never be admitted on an ARM CPU and vice versa).
static_assert(!SimdIsaLattice::leq(SimdIsa::Avx2,     SimdIsa::Sve));
static_assert(!SimdIsaLattice::leq(SimdIsa::Sve,      SimdIsa::Avx2));
static_assert(!SimdIsaLattice::leq(SimdIsa::Sse2,     SimdIsa::Neon));
static_assert(!SimdIsaLattice::leq(SimdIsa::Neon,     SimdIsa::Sse2));
static_assert(!SimdIsaLattice::leq(SimdIsa::Avx512Bw, SimdIsa::Sve2));
static_assert(!SimdIsaLattice::leq(SimdIsa::Sve2,     SimdIsa::Avx512Bw));
static_assert(!SimdIsaLattice::leq(SimdIsa::Avx2,     SimdIsa::Neon));
static_assert(!SimdIsaLattice::leq(SimdIsa::Neon,     SimdIsa::Avx512F));

// ── Reverse rules — Portable ⊑ X false, X ⊑ Scalar false ──────────
static_assert(!SimdIsaLattice::leq(SimdIsa::Portable, SimdIsa::Avx2));
static_assert(!SimdIsaLattice::leq(SimdIsa::Portable, SimdIsa::Sve));
static_assert(!SimdIsaLattice::leq(SimdIsa::Portable, SimdIsa::Scalar));
static_assert(!SimdIsaLattice::leq(SimdIsa::Avx2,     SimdIsa::Scalar));
static_assert(!SimdIsaLattice::leq(SimdIsa::Sve,      SimdIsa::Scalar));

// ── Join / meet witnesses ─────────────────────────────────────────
// Same trunk → join = higher rank, meet = lower rank.
static_assert(SimdIsaLattice::join(SimdIsa::Sse2, SimdIsa::Avx2) == SimdIsa::Avx2);
static_assert(SimdIsaLattice::meet(SimdIsa::Sse2, SimdIsa::Avx2) == SimdIsa::Sse2);
static_assert(SimdIsaLattice::join(SimdIsa::Neon, SimdIsa::Sve)  == SimdIsa::Sve);
static_assert(SimdIsaLattice::meet(SimdIsa::Neon, SimdIsa::Sve)  == SimdIsa::Neon);
// Cross-trunk → join = Portable, meet = Scalar.
static_assert(SimdIsaLattice::join(SimdIsa::Avx2, SimdIsa::Sve)  == SimdIsa::Portable);
static_assert(SimdIsaLattice::meet(SimdIsa::Avx2, SimdIsa::Sve)  == SimdIsa::Scalar);
static_assert(SimdIsaLattice::join(SimdIsa::Sse2, SimdIsa::Neon) == SimdIsa::Portable);
static_assert(SimdIsaLattice::meet(SimdIsa::Sse2, SimdIsa::Neon) == SimdIsa::Scalar);
// Scalar identity for join; Portable identity for meet.
static_assert(SimdIsaLattice::join(SimdIsa::Scalar, SimdIsa::Avx2)     == SimdIsa::Avx2);
static_assert(SimdIsaLattice::join(SimdIsa::Scalar, SimdIsa::Portable) == SimdIsa::Portable);
static_assert(SimdIsaLattice::meet(SimdIsa::Portable, SimdIsa::Sve)    == SimdIsa::Sve);
static_assert(SimdIsaLattice::meet(SimdIsa::Portable, SimdIsa::Scalar) == SimdIsa::Scalar);
// Scalar absorbs in meet; Portable absorbs in join.
static_assert(SimdIsaLattice::meet(SimdIsa::Scalar, SimdIsa::Avx2)     == SimdIsa::Scalar);
static_assert(SimdIsaLattice::join(SimdIsa::Portable, SimdIsa::Neon)   == SimdIsa::Portable);
// Idempotence.
static_assert(SimdIsaLattice::join(SimdIsa::Avx2, SimdIsa::Avx2) == SimdIsa::Avx2);
static_assert(SimdIsaLattice::meet(SimdIsa::Sve,  SimdIsa::Sve)  == SimdIsa::Sve);

// ── Exhaustive lattice-axiom verification — (15 ISAs)³ = 3375 ──────
//
// Hand-written exhaustive verifier (cannot reuse the chain verifier —
// SimdIsa is NOT a chain).  Mirrors VendorLattice's: reflexivity /
// antisymmetry / transitivity of leq + idempotence / commutativity /
// associativity / absorption / bounds of meet+join + leq-consistency.
inline constexpr SimdIsa kAll[] = {
    SimdIsa::Scalar,
    SimdIsa::Sse2,  SimdIsa::Sse3,  SimdIsa::Ssse3, SimdIsa::Sse41,
    SimdIsa::Sse42, SimdIsa::Avx2,  SimdIsa::Avx512F, SimdIsa::Avx512Bw,
    SimdIsa::Neon,  SimdIsa::NeonFp16, SimdIsa::NeonDotProduct,
    SimdIsa::Sve,   SimdIsa::Sve2,
    SimdIsa::Portable,
};

[[nodiscard]] consteval bool verify_partial_order_exhaustive() noexcept {
    using L = SimdIsaLattice;
    for (auto a : kAll) {
        if (!L::leq(a, a)) return false;
        for (auto b : kAll) {
            if (L::leq(a, b) && L::leq(b, a) && a != b) return false;
            if (L::join(a, b) != L::join(b, a)) return false;
            if (L::meet(a, b) != L::meet(b, a)) return false;
            if (L::join(a, a) != a) return false;
            if (L::meet(a, a) != a) return false;
            if (L::join(a, L::meet(a, b)) != a) return false;
            if (L::meet(a, L::join(a, b)) != a) return false;
            if (!L::leq(L::bottom(), a)) return false;
            if (!L::leq(a, L::top()))    return false;
            for (auto c : kAll) {
                if (L::leq(a, b) && L::leq(b, c) && !L::leq(a, c)) return false;
                if (L::join(L::join(a, b), c) != L::join(a, L::join(b, c))) return false;
                if (L::meet(L::meet(a, b), c) != L::meet(a, L::meet(b, c))) return false;
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
    "SimdIsaLattice's partial-order axioms must hold at every (SimdIsa)³ "
    "triple over the 15 elements.  If this fires, one of leq / join / meet "
    "has a bug for some pair OR the trunk routing (Scalar / Portable / "
    "same-trunk-rank / cross-trunk) is wrong.");

// ── Non-distributivity witness ────────────────────────────────────
[[nodiscard]] consteval bool non_distributive_witness() noexcept {
    using L = SimdIsaLattice;
    // (AVX2 ∨ NEON) ∧ SVE = Portable ∧ SVE = SVE
    // (AVX2 ∧ SVE) ∨ (NEON ∧ SVE) = Scalar ∨ NEON = NEON   (NEON ⊑ SVE)
    // SVE ≠ NEON — hence non-distributive.
    auto lhs = L::meet(L::join(SimdIsa::Avx2, SimdIsa::Neon), SimdIsa::Sve);
    auto rhs = L::join(L::meet(SimdIsa::Avx2, SimdIsa::Sve),
                       L::meet(SimdIsa::Neon, SimdIsa::Sve));
    return lhs == SimdIsa::Sve && rhs == SimdIsa::Neon && lhs != rhs;
}
static_assert(non_distributive_witness(),
    "SimdIsaLattice MUST be non-distributive (see docblock).  If this "
    "fires, either (a) the two trunks were collapsed into one chain — "
    "DEFEATING the cross-trunk incomparability the SimdWidthPinned safety "
    "guarantee depends on — or (b) a synthetic intermediate element closed "
    "the distributivity gap.  Audit before resolving.");

// ── Names ────────────────────────────────────────────────────────
static_assert(SimdIsaLattice::name() == "SimdIsaLattice");
static_assert(simd_isa::ScalarIsa::name()   == "SimdIsaLattice::At<Scalar>");
static_assert(simd_isa::Avx2Isa::name()     == "SimdIsaLattice::At<Avx2>");
static_assert(simd_isa::Avx512bwIsa::name() == "SimdIsaLattice::At<Avx512Bw>");
static_assert(simd_isa::NeonIsa::name()     == "SimdIsaLattice::At<Neon>");
static_assert(simd_isa::Sve2Isa::name()     == "SimdIsaLattice::At<Sve2>");
static_assert(simd_isa::PortableIsa::name() == "SimdIsaLattice::At<Portable>");

[[nodiscard]] consteval bool every_at_simd_isa_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^SimdIsa));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (SimdIsaLattice::At<([:en:])>::name() ==
            std::string_view{"SimdIsaLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_simd_isa_has_name(),
    "SimdIsaLattice::At<I>::name() switch missing an arm.");

static_assert(simd_isa::Avx2Isa::isa     == SimdIsa::Avx2);
static_assert(simd_isa::SveIsa::isa      == SimdIsa::Sve);
static_assert(simd_isa::ScalarIsa::isa   == SimdIsa::Scalar);
static_assert(simd_isa::PortableIsa::isa == SimdIsa::Portable);

// ── Layout invariants ───────────────────────────────────────────────
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

template <typename T_>
using PortableIsaGraded = Graded<ModalityKind::Absolute,
                                 simd_isa::PortableIsa, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableIsaGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableIsaGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableIsaGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableIsaGraded, double);

template <typename T_>
using Avx2Graded = Graded<ModalityKind::Absolute, simd_isa::Avx2Isa, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Avx2Graded, EightByteValue);

template <typename T_>
using SveGraded = Graded<ModalityKind::Absolute, simd_isa::SveIsa, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SveGraded, EightByteValue);

inline void runtime_smoke_test() {
    SimdIsa a = SimdIsa::Avx2;
    SimdIsa b = SimdIsa::Sve;
    [[maybe_unused]] bool    l1   = SimdIsaLattice::leq(a, b);
    [[maybe_unused]] SimdIsa j1   = SimdIsaLattice::join(a, b);
    [[maybe_unused]] SimdIsa m1   = SimdIsaLattice::meet(a, b);
    [[maybe_unused]] SimdIsa bot  = SimdIsaLattice::bottom();
    [[maybe_unused]] SimdIsa topv = SimdIsaLattice::top();

    SimdIsa sse2 = SimdIsa::Sse2;
    [[maybe_unused]] bool    within = SimdIsaLattice::leq(sse2, a);  // Sse2 ⊑ Avx2
    [[maybe_unused]] bool    xtrunk = simd_isa_same_trunk(a, b);     // false

    OneByteValue v{42};
    PortableIsaGraded<OneByteValue> initial{v, simd_isa::PortableIsa::bottom()};
    auto widened  = initial.weaken(simd_isa::PortableIsa::top());
    auto composed = initial.compose(widened);
    [[maybe_unused]] auto g  = widened.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    simd_isa::PortableIsa::element_type e{};
    [[maybe_unused]] SimdIsa rec = e;
}

}  // namespace detail::simd_isa_lattice_self_test

}  // namespace crucible::algebra::lattices
