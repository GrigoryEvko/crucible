// FIXY-V-250 sentinel TU: algebra/lattices/SimdIsaLattice.h —
// non-distributive partial-order lattice over the SIMD-ISA capability
// spectrum (x86 trunk × ARM trunk joined only at Scalar/Portable).
//
// V-250 ships ONLY the lattice (the algebraic substrate): the enum +
// leq/join/meet + At<ISA> singletons + trunk-classification helpers +
// reflection-driven cardinality/name-coverage + exhaustive (15³ = 3375)
// lattice-axiom verification + the non-distributivity witness.  It ships
// NO value-level machinery and NO DimensionAxis enumerator:
//   - FIXY-V-253 ships DimensionAxis::SimdIsa (the axis enumerator) — so
//     this TU does NOT reference safety/DimensionTraits.h (the axis does
//     not exist yet; referencing it would not compile).
//   - FIXY-V-256 ships safety/SimdWidthPinned.h (the Graded<Absolute,
//     SimdIsaLattice::At<W>, P> carrier) PLUS the `satisfies<>` runtime
//     gate AND the row_hash_contribution<SimdWidthPinned<...>> federation-
//     cache discriminator — exactly mirroring how VendorLattice defers its
//     wrapper + row_hash to safety/Vendor.h + safety/diag/RowHashFold.h.
//     The lattice layer pulls NO safety/diag header by design.
//   - FIXY-V-260 ships the S001/S002 CollisionCatalog rules that consume
//     the cross-trunk incomparability witnessed below.
//
// Why a dedicated SIMD-ISA axis (not folded onto Vendor): VendorLattice
// records WHO compiled the kernel (NV / AMD / TPU / ...).  It has no
// notion of WHICH ISA-extension the host CPU must provide for a compiled
// SIMD kernel to issue without #UD.  An AVX2 binary and an SSE2 binary
// are the SAME vendor (x86) yet are NOT interchangeable; an AVX2 binary
// and an SVE binary are mutually un-runnable.  The two-trunk partial
// order — NOT a chain, NOT the vendor lattice — is the only structure
// that expresses both intra-trunk subsumption AND cross-trunk
// incompatibility.
//
// THE LOAD-BEARING PROPERTY this TU defends: cross-trunk pairs are
// MUTUALLY INCOMPARABLE.  The V-256 SimdWidthPinned safety guarantee
// ("an AVX2-pinned binary is admitted on an AVX-512 CPU but NEVER on an
// SSE2 CPU and NEVER on any ARM CPU") reduces directly to leq() over this
// lattice.  If the two trunks were ever collapsed into one chain, that
// guarantee silently evaporates — hence the exhaustive cross-trunk
// negative assertions AND the non-distributivity witness below (a single
// chain WOULD be distributive; this lattice MUST NOT be).

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/SimdIsaLattice.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cal = ::crucible::algebra::lattices;
namespace si  = ::crucible::algebra::lattices::simd_isa;

namespace {

using cal::SimdIsa;
using L = cal::SimdIsaLattice;

// ── Concept satisfaction — bounded lattice, not a semiring ──────────
static_assert(crucible::algebra::Lattice<L>,
    "FIXY-V-250: SimdIsaLattice must satisfy the Lattice concept "
    "(element_type + leq + join + meet).");
static_assert(crucible::algebra::BoundedLattice<L>,
    "FIXY-V-250: SimdIsaLattice has both bottom() (Scalar) and top() "
    "(Portable) — it is a bounded lattice.");
static_assert(!crucible::algebra::UnboundedLattice<L>);
static_assert(!crucible::algebra::Semiring<L>,
    "FIXY-V-250: SimdIsaLattice is NOT a semiring — it carries no "
    "equality+add+mul algebra, only the order-theoretic operations.");

// ── Cardinality — 15 ISAs (Scalar + 8 x86 + 5 ARM + Portable) ───────
static_assert(cal::simd_isa_count == 15,
    "FIXY-V-250: SimdIsa must have exactly 15 enumerators. Adding an ISA "
    "requires (a) placing it inside the correct trunk numeric range so "
    "simd_isa_is_x86 / simd_isa_is_arm classify it, (b) extending both "
    "name switches, (c) bumping the V-256 SimdWidthPinned satisfies<> + "
    "V-260 collision rule, AND (d) updating kAll[] in the header verifier.");

static_assert(std::is_same_v<std::underlying_type_t<SimdIsa>, std::uint8_t>,
    "FIXY-V-250: SimdIsa must use uint8_t underlying type — the trunk is "
    "packed into the high nibble (x86=0x1_, ARM=0x2_) so within-trunk "
    "integer order equals capability rank.");

// ── Trunk encoding — high nibble carries the trunk ──────────────────
static_assert(std::to_underlying(SimdIsa::Scalar)   == 0x00, "Scalar = bottom sentinel");
static_assert(std::to_underlying(SimdIsa::Sse2)     == 0x10, "x86 trunk base");
static_assert(std::to_underlying(SimdIsa::Avx512Bw) == 0x17, "x86 trunk top");
static_assert(std::to_underlying(SimdIsa::Neon)     == 0x20, "ARM trunk base");
static_assert(std::to_underlying(SimdIsa::Sve2)     == 0x24, "ARM trunk top");
static_assert(std::to_underlying(SimdIsa::Portable) == 0xFF, "Portable = top sentinel");

// ── Trunk classification helpers ────────────────────────────────────
static_assert(cal::simd_isa_is_x86(SimdIsa::Sse2));
static_assert(cal::simd_isa_is_x86(SimdIsa::Avx2));
static_assert(cal::simd_isa_is_x86(SimdIsa::Avx512Bw));
static_assert(!cal::simd_isa_is_x86(SimdIsa::Neon));
static_assert(!cal::simd_isa_is_x86(SimdIsa::Scalar),
    "FIXY-V-250: Scalar belongs to NEITHER trunk — it is the shared "
    "bottom; same_trunk(Scalar, _) must be false so leq special-cases it.");
static_assert(!cal::simd_isa_is_x86(SimdIsa::Portable));
static_assert(cal::simd_isa_is_arm(SimdIsa::Neon));
static_assert(cal::simd_isa_is_arm(SimdIsa::Sve2));
static_assert(!cal::simd_isa_is_arm(SimdIsa::Avx2));
static_assert(!cal::simd_isa_is_arm(SimdIsa::Portable));
static_assert(cal::simd_isa_same_trunk(SimdIsa::Sse2, SimdIsa::Avx512Bw));
static_assert(cal::simd_isa_same_trunk(SimdIsa::Neon, SimdIsa::Sve2));
static_assert(!cal::simd_isa_same_trunk(SimdIsa::Avx2, SimdIsa::Sve),
    "FIXY-V-250: x86 × ARM is the cross-trunk case — the load-bearing "
    "non-distributivity / incomparability source.");

// ── Bounds ──────────────────────────────────────────────────────────
static_assert(L::bottom() == SimdIsa::Scalar);
static_assert(L::top()    == SimdIsa::Portable);

// ── x86 trunk subsumption chain (intra-trunk total order) ───────────
// SSE2 ⊑ SSE3 ⊑ SSSE3 ⊑ SSE4.1 ⊑ SSE4.2 ⊑ AVX2 ⊑ AVX512F ⊑ AVX512BW
static_assert(L::leq(SimdIsa::Sse2,    SimdIsa::Sse3));
static_assert(L::leq(SimdIsa::Sse3,    SimdIsa::Ssse3));
static_assert(L::leq(SimdIsa::Ssse3,   SimdIsa::Sse41));
static_assert(L::leq(SimdIsa::Sse41,   SimdIsa::Sse42));
static_assert(L::leq(SimdIsa::Sse42,   SimdIsa::Avx2));
static_assert(L::leq(SimdIsa::Avx2,    SimdIsa::Avx512F));
static_assert(L::leq(SimdIsa::Avx512F, SimdIsa::Avx512Bw));
static_assert(L::leq(SimdIsa::Sse2,    SimdIsa::Avx512Bw), "transitive endpoints");
// The docblock use-case witness: an AVX-512 host runs an AVX2 binary; an
// SSE2 host rejects it.
static_assert(L::leq(SimdIsa::Avx2,  SimdIsa::Avx512F),
    "FIXY-V-250: AVX2 ⊑ AVX512F — an AVX2-pinned binary IS satisfied by "
    "an AVX-512 host CPU.");
static_assert(!L::leq(SimdIsa::Avx2, SimdIsa::Sse2),
    "FIXY-V-250: AVX2 ⋢ SSE2 — an AVX2-pinned binary is NOT satisfied by "
    "an SSE2-only host; admitting it would #UD at runtime.");

// ── ARM trunk subsumption chain (intra-trunk total order) ───────────
// NEON ⊑ NEON-FP16 ⊑ NEON-Dot ⊑ SVE ⊑ SVE2
static_assert(L::leq(SimdIsa::Neon,           SimdIsa::NeonFp16));
static_assert(L::leq(SimdIsa::NeonFp16,       SimdIsa::NeonDotProduct));
static_assert(L::leq(SimdIsa::NeonDotProduct, SimdIsa::Sve));
static_assert(L::leq(SimdIsa::Sve,            SimdIsa::Sve2));
static_assert(L::leq(SimdIsa::Neon,           SimdIsa::Sve2), "transitive endpoints");
static_assert(!L::leq(SimdIsa::Sve2,          SimdIsa::Neon), "descending is false");

// ── Bottom ⊑ everything ⊑ Top ───────────────────────────────────────
static_assert(L::leq(SimdIsa::Scalar, SimdIsa::Avx2));
static_assert(L::leq(SimdIsa::Scalar, SimdIsa::Sve2));
static_assert(L::leq(SimdIsa::Scalar, SimdIsa::Portable));
static_assert(L::leq(SimdIsa::Avx512Bw, SimdIsa::Portable));
static_assert(L::leq(SimdIsa::Sve2,     SimdIsa::Portable));

// ── THE LOAD-BEARING NEGATIVE — cross-trunk incomparability ─────────
//
// Every x86 × ARM pair MUST be mutually incomparable in BOTH directions.
// This is the structural fact the SimdWidthPinned safety guarantee rests
// on: an x86 binary is never admitted on an ARM CPU and vice versa.
static_assert(!L::leq(SimdIsa::Avx2,     SimdIsa::Sve));
static_assert(!L::leq(SimdIsa::Sve,      SimdIsa::Avx2));
static_assert(!L::leq(SimdIsa::Sse2,     SimdIsa::Neon));
static_assert(!L::leq(SimdIsa::Neon,     SimdIsa::Sse2));
static_assert(!L::leq(SimdIsa::Avx512Bw, SimdIsa::Sve2));
static_assert(!L::leq(SimdIsa::Sve2,     SimdIsa::Avx512Bw));
static_assert(!L::leq(SimdIsa::Avx512F,  SimdIsa::Neon));
static_assert(!L::leq(SimdIsa::Neon,     SimdIsa::Avx512F));

// ── Reverse sentinels — Portable ⋢ X, X ⋢ Scalar ────────────────────
static_assert(!L::leq(SimdIsa::Portable, SimdIsa::Avx2));
static_assert(!L::leq(SimdIsa::Portable, SimdIsa::Scalar));
static_assert(!L::leq(SimdIsa::Avx2,     SimdIsa::Scalar));
static_assert(!L::leq(SimdIsa::Sve,      SimdIsa::Scalar));

// ── Join — intra-trunk = higher rank; cross-trunk = Portable ────────
static_assert(L::join(SimdIsa::Sse2, SimdIsa::Avx2) == SimdIsa::Avx2);
static_assert(L::join(SimdIsa::Neon, SimdIsa::Sve)  == SimdIsa::Sve);
static_assert(L::join(SimdIsa::Avx2, SimdIsa::Sve)  == SimdIsa::Portable,
    "FIXY-V-250: cross-trunk join = Portable — the only common upper "
    "bound of an x86 ISA and an ARM ISA is the universal kernel.");
static_assert(L::join(SimdIsa::Sse2, SimdIsa::Neon) == SimdIsa::Portable);
static_assert(L::join(SimdIsa::Scalar, SimdIsa::Avx2) == SimdIsa::Avx2,
    "Scalar is the join identity");
static_assert(L::join(SimdIsa::Portable, SimdIsa::Sve) == SimdIsa::Portable,
    "Portable absorbs in join");

// ── Meet — intra-trunk = lower rank; cross-trunk = Scalar ───────────
static_assert(L::meet(SimdIsa::Sse2, SimdIsa::Avx2) == SimdIsa::Sse2);
static_assert(L::meet(SimdIsa::Neon, SimdIsa::Sve)  == SimdIsa::Neon);
static_assert(L::meet(SimdIsa::Avx2, SimdIsa::Sve)  == SimdIsa::Scalar,
    "FIXY-V-250: cross-trunk meet = Scalar — the only common lower bound "
    "of an x86 ISA and an ARM ISA is the no-SIMD scalar floor.");
static_assert(L::meet(SimdIsa::Sse2, SimdIsa::Neon) == SimdIsa::Scalar);
static_assert(L::meet(SimdIsa::Portable, SimdIsa::Sve) == SimdIsa::Sve,
    "Portable is the meet identity");
static_assert(L::meet(SimdIsa::Scalar, SimdIsa::Avx2) == SimdIsa::Scalar,
    "Scalar absorbs in meet");

// ── NON-DISTRIBUTIVITY (witness re-stated at TU level) ──────────────
//
// (AVX2 ∨ NEON) ∧ SVE = Portable ∧ SVE = SVE
// (AVX2 ∧ SVE) ∨ (NEON ∧ SVE) = Scalar ∨ NEON = NEON   (NEON ⊑ SVE, both ARM)
// SVE ≠ NEON  ⟹  non-distributive.
//
// A single-chain lattice WOULD be distributive — this assertion is what
// forbids a future "simplification" that collapses the two trunks.
static_assert(
    L::meet(L::join(SimdIsa::Avx2, SimdIsa::Neon), SimdIsa::Sve) == SimdIsa::Sve,
    "FIXY-V-250: LHS of the distributivity test must be SVE.");
static_assert(
    L::join(L::meet(SimdIsa::Avx2, SimdIsa::Sve),
            L::meet(SimdIsa::Neon, SimdIsa::Sve)) == SimdIsa::Neon,
    "FIXY-V-250: RHS of the distributivity test must be NEON (NEON ⊑ SVE "
    "because both are ARM-trunk, so NEON ∧ SVE = NEON — NOT Scalar).");
static_assert(
    L::meet(L::join(SimdIsa::Avx2, SimdIsa::Neon), SimdIsa::Sve) !=
    L::join(L::meet(SimdIsa::Avx2, SimdIsa::Sve),
            L::meet(SimdIsa::Neon, SimdIsa::Sve)),
    "FIXY-V-250: SimdIsaLattice MUST be non-distributive. If this fires, "
    "the two trunks were collapsed into a single chain — DEFEATING the "
    "cross-trunk incomparability the V-256 SimdWidthPinned safety "
    "guarantee depends on.");

// ── At<ISA> singleton sub-lattice — empty element_type, EBO collapse ─
static_assert(crucible::algebra::Lattice<si::ScalarIsa>);
static_assert(crucible::algebra::Lattice<si::Avx2Isa>);
static_assert(crucible::algebra::Lattice<si::SveIsa>);
static_assert(crucible::algebra::BoundedLattice<si::PortableIsa>);
static_assert(std::is_empty_v<si::ScalarIsa::element_type>,
    "FIXY-V-250: At<Scalar>::element_type must be empty so "
    "Graded<Absolute, At<Scalar>, P> EBO-collapses to sizeof(P) — a "
    "zero-byte ISA annotation at every binding site.");
static_assert(std::is_empty_v<si::Avx2Isa::element_type>);
static_assert(std::is_empty_v<si::SveIsa::element_type>);
static_assert(std::is_empty_v<si::PortableIsa::element_type>);
static_assert(si::Avx2Isa::isa     == SimdIsa::Avx2,
    "FIXY-V-250: At<I>::isa must equal I at the type level so the V-256 "
    "wrapper reads the pinned ISA with no runtime data.");
static_assert(si::SveIsa::isa      == SimdIsa::Sve);
static_assert(si::PortableIsa::isa == SimdIsa::Portable);

// ── EBO collapse witness — Graded<Absolute, At<ISA>, P> == sizeof(P) ─
struct EightByteValue { unsigned long long v{0}; };
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                     si::Avx2Isa, EightByteValue>)
    == sizeof(EightByteValue),
    "FIXY-V-250: regime-1 EBO collapse — pinning an AVX2 ISA grade adds "
    "zero bytes to an 8-byte payload.");
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                     si::PortableIsa, int>)
    == sizeof(int));

// ── Name surface ────────────────────────────────────────────────────
static_assert(L::name() == std::string_view{"SimdIsaLattice"});
static_assert(si::Avx2Isa::name()  == std::string_view{"SimdIsaLattice::At<Avx2>"});
static_assert(si::NeonIsa::name()  == std::string_view{"SimdIsaLattice::At<Neon>"});
static_assert(si::ScalarIsa::name()== std::string_view{"SimdIsaLattice::At<Scalar>"});
static_assert(cal::simd_isa_name(SimdIsa::Sve2) == std::string_view{"Sve2"});

}  // namespace

int main() {
    cal::detail::simd_isa_lattice_self_test::runtime_smoke_test();
    return 0;
}
