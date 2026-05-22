#pragma once

// ── crucible::algebra::lattices::FpModeLattice ──────────────────────
//
// SCAFFOLDING header for FIXY-V-088.  Ships the 11 sub-axis enum
// declarations that V-089 will populate with per-axis ChainLattice
// algebras and V-090 will combine into a `FpModeProductLattice`
// composite.  V-088 itself ships NO lattice algebra — only the
// enumerator vocabulary + sanity asserts that the catalog is
// internally consistent.
//
// ── Why a dedicated FpMode axis (DimensionAxis::FpMode, dim 22) ─────
//
// The Precision axis (dim 12, FX dim 14) tracks ELEMENT-TYPE precision
// — FP32 / FP16 / BF16 / E4M3 / E5M2 / TF32 — i.e. the bit-width and
// mantissa layout of each tensor element.  FpMode is structurally
// ORTHOGONAL: two ops with identical FP32 element type produce bit-
// different results under different EVALUATION POLICIES:
//
//   * Rounding direction (RTE / RTZ / RTN / RTP / RTNA).
//   * Subnormal handling (gradual / flush-to-zero / denormals-are-zero).
//   * Operator contraction (allow FMA across `+` / `*` boundaries or not).
//   * Trap masks (overflow / underflow / inexact / div-by-zero / invalid).
//   * NaN policy (signalling / quiet / non-IEEE fast-NaN).
//   * Infinity policy (IEEE-compliant / flush / saturate).
//   * Complex layout (interleaved / split-arrays / Re-major / Im-major).
//   * Libm policy (vector-libm / per-vendor approximations / scalar).
//   * Reassociation (allow algebraic rewrite / forbid / partial-with-bound).
//   * Compile-time FP constant rounding (RTE / RTZ / RTN).
//   * Per-lane vs per-vector mode application (uniform / per-lane).
//
// Folding these onto Precision would make Merkle-hash-safe FP
// canonicalization (FIXY-V-093) intractable — two ops with identical
// IR001 nodes + identical Precision but different FpMode would hash
// equal under `row_hash` even though they cannot share a kernel-cache
// slot.  Pinning FpMode at the AXIS level (with its own per-wrapper
// `row_hash_contribution` once V-090 ships the wrappers) keeps the
// federation cache slots correctly distinguished.
//
// ── Tier classification (Tier-S Semiring with par=join) ─────────────
//
// FpMode is `TierKind::Semiring` per `tier_of_axis(FpMode)`.  The par-
// composition reading is "strictest-wins":
//
//   * Two call sites composing in parallel admit ONLY the
//     INTERSECTION of their FP-mode tolerances.  If site A pins
//     Rounding::RTE and site B pins Rounding::RTZ, the parallel
//     composition is REJECTED — neither tolerance subsumes the other.
//   * Two call sites composing in sequence admit the JOIN of their
//     FP-modes — but the join is well-defined ONLY along a chain
//     within each sub-axis; cross-sub-axis composition uses the
//     ProductLattice machinery V-090 will ship.
//
// This matches the par=join discipline pioneered by Synchronization
// (Wait + MemOrder, fixy-A3-008) and Regime (HotPath, fixy-A3-009):
// every Crucible-extension Tier-S axis follows the same composition
// reading.  Forge phase E.RecipeSelect consumes the FpMode row
// constraint when selecting a NumericalRecipe — pinning FpMode at
// IR001 lets phase E reject incompatible (recipe, FpMode) pairs at
// compile time.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe  — every sub-axis is a strong scoped enum (`enum class :
//                uint8_t`); cross-sub-axis mixing requires
//                `std::to_underlying` and surfaces at the call site.
//   InitSafe  — every enum has explicit enumerators with no implicit
//                "default" arm; reflection-driven coverage tests fire
//                automatically as V-089/V-090 ship per-sub-axis name
//                functions.
//   DetSafe   — operations (when V-089 ships them) will be
//                `constexpr` (not `consteval`) so Graded's runtime
//                `pre (L::leq(...))` precondition can fire under the
//                `enforce` contract semantic.
//   LeakSafe  — zero-state enums; no resources.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// V-088 scaffolding: zero cost (the enums compile to a single uint8_t
// per value, EBO-collapsed when wrapped via the `At<T>` singleton
// pattern V-089 will ship).
//
// ── Forward references ─────────────────────────────────────────────
//
//   FIXY-V-089 — Per-sub-axis ChainLattice algebras (11 lattices).
//                Each sub-axis enum below grows a peer
//                `<SubAxis>Lattice` struct extending ChainLatticeOps.
//   FIXY-V-090 — `FpModeProductLattice` composite via ProductLattice
//                + 11 `safety/Fp*.h` wrappers (one per sub-axis).
//   FIXY-V-091 — CollisionCatalog F101-F105 cross-axis rules
//                (FpMode × Precision, FpMode × Vendor, FpMode ×
//                NumericalRecipe, FpMode × DetSafe, FpMode × HotPath).
//   FIXY-V-092 — `fixy/Fp.h` ships 12 FP-mode grant tags + per-tag
//                `which_dim` metafunction routing to FpMode.
//   FIXY-V-093 — `fixy::fp::canonicalize` for Merkle-hash-safe FP
//                canonicalization at IR001 nodes.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── Sub-axis 1: Rounding direction ───────────────────────────────────
// IEEE 754 §4.3 rounding-direction attributes plus the
// nearest-magnitude-away tie-break used by some GPU ISAs (NV "RNA").
// Chain order (V-089 will pin): RTZ ⊏ RTN ⊏ RTP ⊏ RTE ⊏ RTNA — STRICT
// is the topmost ("most-IEEE-compliant"); RTZ is the bottom (cheapest
// but loses guarantees).
enum class FpRounding : std::uint8_t {
    RoundToZero            = 0,  // truncate toward zero
    RoundToNegativeInf     = 1,  // floor
    RoundToPositiveInf     = 2,  // ceiling
    RoundToNearestEven     = 3,  // IEEE 754 default (RTE)
    RoundToNearestAwayZero = 4,  // tie-break away from zero (RTNA / RNA)
};

// ── Sub-axis 2: Flush-to-zero (subnormal output handling) ───────────
// Whether the result of an arithmetic op that would produce a
// subnormal is flushed to zero.  Composes with DenormalsAreZero
// (sub-axis 5) which controls INPUT subnormal handling.
enum class FpFtz : std::uint8_t {
    PreserveSubnormals = 0,  // gradual underflow (IEEE 754 default)
    FlushToZero        = 1,  // subnormal outputs → ±0.0
};

// ── Sub-axis 3: Operator contraction (FMA-fusion across `+`/`*`) ────
// Whether the compiler is allowed to contract `a * b + c` into a
// single FMA instruction.  Tracks GCC `-ffp-contract=on/off/fast`.
enum class FpContract : std::uint8_t {
    Off       = 0,  // never contract — every `+` / `*` is a separate rounding boundary
    OnInExpr  = 1,  // contract within a single expression (IEEE 754-2008 default)
    Fast      = 2,  // contract across statements / arbitrary distances
};

// ── Sub-axis 4: Trap masks (FE_OVERFLOW / FE_UNDERFLOW / etc.) ──────
// IEEE 754 trap-enable bits.  Default-masked means traps are
// SILENCED — the FP env records the flag but no SIGFPE is raised.
// Crucible's DetSafe discipline pins TrapMaskedAll (the silent
// default) for hot paths; UnmaskedInvalid is admissible only in
// Forge phase A.Probe.
enum class FpTrapMask : std::uint8_t {
    AllMasked         = 0,  // silent (Crucible default — DetSafe-safe)
    UnmaskedInvalid   = 1,  // SIGFPE on invalid (NaN-from-NaN, 0/0)
    UnmaskedDivZero   = 2,  // SIGFPE on finite÷0
    UnmaskedOverflow  = 3,  // SIGFPE on overflow
    UnmaskedUnderflow = 4,  // SIGFPE on underflow
    UnmaskedInexact   = 5,  // SIGFPE on any inexact op (rarely used; perf killer)
};

// ── Sub-axis 5: Denormal-input handling (DAZ) ───────────────────────
// Whether subnormal INPUTS are treated as ±0.0 (paired with FTZ for
// outputs).  x86 MXCSR.DAZ bit; ARM FPCR.FZ bit.
enum class FpDenormalInput : std::uint8_t {
    HonorDenormals   = 0,  // subnormal inputs participate (IEEE 754 default)
    DenormalsAreZero = 1,  // subnormal inputs → ±0.0 (faster, lossy)
};

// ── Sub-axis 6: NaN policy (signalling / quiet / fast-NaN) ──────────
// Composes with Rounding to determine whether NaN payloads are
// preserved through arithmetic.  Fast-NaN is the non-IEEE shortcut
// where NaN propagation is dropped (e.g. `min(NaN, 0) = 0`).
enum class FpNanPolicy : std::uint8_t {
    PropagateQuiet      = 0,  // qNaN survives every op (IEEE 754 default)
    PropagateSignalling = 1,  // sNaN raises trap on consume; payload survives if masked
    FastNaN             = 2,  // non-IEEE: `min(NaN, x) = x`, `max(NaN, x) = x` (GPU fast-min/max)
};

// ── Sub-axis 7: Infinity policy ─────────────────────────────────────
// Whether infinities are IEEE-compliant or flushed/saturated.  GPU
// "fast-math" mode often saturates ±Inf to the max finite value.
enum class FpInfPolicy : std::uint8_t {
    PropagateInfinity = 0,  // ±Inf survives (IEEE 754 default)
    FlushInfToFinite  = 1,  // ±Inf → ±FLT_MAX (non-IEEE saturation)
};

// ── Sub-axis 8: Complex layout (interleaved / split / Re-major) ─────
// How complex tensors are laid out in memory.  Interleaved is the
// stdlib convention (`std::complex<float>`); split is the
// AMD-rocFFT / NV-cuFFT convention; Re-major / Im-major are NumPy
// fortran/C order variants.
enum class FpComplexLayout : std::uint8_t {
    Interleaved = 0,  // [Re0, Im0, Re1, Im1, ...] (std::complex)
    SplitRealImag = 1,  // [Re0, Re1, ..., Re_n, Im0, Im1, ..., Im_n]
    SplitImagReal = 2,  // [Im0, Im1, ..., Im_n, Re0, Re1, ..., Re_n]
};

// ── Sub-axis 9: Libm policy (per-vendor approximations) ─────────────
// Which transcendental implementation is used.  ScalarLibm calls
// system glibc; VectorLibmSleef calls SLEEF; FastApprox is the
// per-vendor low-precision approximation (CUDA `__sinf`, AMD
// `v_sin_f32` etc.).
enum class FpLibmPolicy : std::uint8_t {
    ScalarLibm        = 0,  // scalar glibc / musl libm
    VectorLibmSleef   = 1,  // SLEEF cross-platform vector libm
    VectorLibmSvml    = 2,  // Intel SVML
    VectorLibmLibmvec = 3,  // GCC libmvec
    FastApproxNv      = 4,  // CUDA `__sinf` / `__cosf` (relaxed ULP bound)
    FastApproxAm      = 5,  // AMD `v_sin_f32` instruction
};

// ── Sub-axis 10: Reassociation (algebraic rewrite eligibility) ──────
// Whether the compiler / Forge phase REWRITE may reassociate
// FP additions.  GCC `-fassociative-math`.  Crucible DetSafe pins
// `Forbidden` for BITEXACT recipes; `BoundedTreeDepth` admits a
// log-N reduction tree but no arbitrary tree.
enum class FpReassociate : std::uint8_t {
    Forbidden          = 0,  // no rewrite (IEEE 754 default; required for BITEXACT)
    BoundedTreeDepth   = 1,  // log-N tree only (well-defined topology)
    UnrestrictedRewrite = 2, // -fassociative-math (perf-only; breaks DetSafe)
};

// ── Sub-axis 11: Compile-time FP constant rounding ──────────────────
// Rounding applied to FP literals at constant folding.  Composes
// with Rounding (sub-axis 1) which controls RUNTIME rounding;
// FpConstant controls compile-time / NumericalRecipe-baked-in
// constant rounding for the (rare) cases where they diverge.
enum class FpConstantRounding : std::uint8_t {
    SameAsRuntime = 0,  // FpConstant follows the runtime Rounding enum
    AlwaysRTE     = 1,  // pin RoundToNearestEven for all literals
    AlwaysRTZ     = 2,  // pin RoundToZero for all literals
};

// ── Self-test (V-088 scaffolding sanity) ────────────────────────────
namespace detail::fp_mode_lattice_self_test {

// Catalog cardinality assertions — every sub-axis carries at least
// 2 enumerators (a chain lattice with <2 elements is degenerate).
inline constexpr std::size_t rounding_count =
    std::meta::enumerators_of(^^FpRounding).size();
inline constexpr std::size_t ftz_count =
    std::meta::enumerators_of(^^FpFtz).size();
inline constexpr std::size_t contract_count =
    std::meta::enumerators_of(^^FpContract).size();
inline constexpr std::size_t trap_mask_count =
    std::meta::enumerators_of(^^FpTrapMask).size();
inline constexpr std::size_t denormal_input_count =
    std::meta::enumerators_of(^^FpDenormalInput).size();
inline constexpr std::size_t nan_policy_count =
    std::meta::enumerators_of(^^FpNanPolicy).size();
inline constexpr std::size_t inf_policy_count =
    std::meta::enumerators_of(^^FpInfPolicy).size();
inline constexpr std::size_t complex_layout_count =
    std::meta::enumerators_of(^^FpComplexLayout).size();
inline constexpr std::size_t libm_policy_count =
    std::meta::enumerators_of(^^FpLibmPolicy).size();
inline constexpr std::size_t reassociate_count =
    std::meta::enumerators_of(^^FpReassociate).size();
inline constexpr std::size_t fp_constant_count =
    std::meta::enumerators_of(^^FpConstantRounding).size();

static_assert(rounding_count == 5,
    "FpRounding diverged from {RTZ, RTN, RTP, RTE, RTNA} per IEEE 754 "
    "§4.3 + NV/AMD ISA extensions; confirm intent before changing.");
static_assert(ftz_count == 2,
    "FpFtz must be a 2-element chain {PreserveSubnormals, FlushToZero}; "
    "expanding requires updating x86 MXCSR / ARM FPCR bit-decoders.");
static_assert(contract_count == 3,
    "FpContract diverged from {Off, OnInExpr, Fast}; matches GCC "
    "-ffp-contract={off, on, fast} surface.");
static_assert(trap_mask_count == 6,
    "FpTrapMask diverged from the IEEE 754 5-trap + AllMasked surface.");
static_assert(denormal_input_count == 2,
    "FpDenormalInput must be {HonorDenormals, DenormalsAreZero}; "
    "expanding requires updating x86 MXCSR.DAZ / ARM FPCR.FZ decoders.");
static_assert(nan_policy_count == 3,
    "FpNanPolicy diverged from {PropagateQuiet, PropagateSignalling, "
    "FastNaN}.");
static_assert(inf_policy_count == 2,
    "FpInfPolicy must be {PropagateInfinity, FlushInfToFinite}.");
static_assert(complex_layout_count == 3,
    "FpComplexLayout diverged from {Interleaved, SplitRealImag, "
    "SplitImagReal}.");
static_assert(libm_policy_count == 6,
    "FpLibmPolicy diverged from {ScalarLibm, VectorLibmSleef, "
    "VectorLibmSvml, VectorLibmLibmvec, FastApproxNv, FastApproxAm}.");
static_assert(reassociate_count == 3,
    "FpReassociate diverged from {Forbidden, BoundedTreeDepth, "
    "UnrestrictedRewrite}.");
static_assert(fp_constant_count == 3,
    "FpConstantRounding diverged from {SameAsRuntime, AlwaysRTE, "
    "AlwaysRTZ}.");

// Distinctness — every sub-axis is a structurally separate enum type;
// the type system guarantees `FpRounding` and `FpFtz` cannot be
// implicitly converted to each other (strong scoped enums).  This
// witnesses the "11 sub-axes are orthogonal" claim at the type level.
static_assert(!std::is_same_v<FpRounding,         FpFtz>);
static_assert(!std::is_same_v<FpRounding,         FpContract>);
static_assert(!std::is_same_v<FpFtz,              FpDenormalInput>);
static_assert(!std::is_same_v<FpContract,         FpReassociate>);
static_assert(!std::is_same_v<FpTrapMask,         FpNanPolicy>);
static_assert(!std::is_same_v<FpNanPolicy,        FpInfPolicy>);
static_assert(!std::is_same_v<FpComplexLayout,    FpLibmPolicy>);
static_assert(!std::is_same_v<FpLibmPolicy,       FpReassociate>);
static_assert(!std::is_same_v<FpRounding,         FpConstantRounding>);

// Bottom-element pin — every sub-axis's zero ordinal is the
// "weakest / least-constraining" element.  V-089 will turn this into
// a `bottom()` lattice operation; V-088 just asserts the encoding
// convention is uniform so V-089 can derive `bottom()` mechanically.
static_assert(std::to_underlying(FpRounding::RoundToZero)            == 0);
static_assert(std::to_underlying(FpFtz::PreserveSubnormals)          == 0);
static_assert(std::to_underlying(FpContract::Off)                    == 0);
static_assert(std::to_underlying(FpTrapMask::AllMasked)              == 0);
static_assert(std::to_underlying(FpDenormalInput::HonorDenormals)    == 0);
static_assert(std::to_underlying(FpNanPolicy::PropagateQuiet)        == 0);
static_assert(std::to_underlying(FpInfPolicy::PropagateInfinity)     == 0);
static_assert(std::to_underlying(FpComplexLayout::Interleaved)       == 0);
static_assert(std::to_underlying(FpLibmPolicy::ScalarLibm)           == 0);
static_assert(std::to_underlying(FpReassociate::Forbidden)           == 0);
static_assert(std::to_underlying(FpConstantRounding::SameAsRuntime)  == 0);

}  // namespace detail::fp_mode_lattice_self_test

}  // namespace crucible::algebra::lattices
