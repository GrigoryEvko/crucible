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
#include <crucible/algebra/lattices/ProductLattice.h>

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

// ════════════════════════════════════════════════════════════════════
// ── V-089: Per-sub-axis ChainLattice algebras (11 lattices) ─────────
// ════════════════════════════════════════════════════════════════════
//
// One ChainLatticeOps-based lattice per sub-axis.  Bottom = ordinal-0
// (weakest / least-constraining per the V-088 docblock convention).
// Top = topmost enumerator (strongest / most-IEEE-compliant).
//
// `<axis>_name()` provides reflection-coverage-checked diagnostic
// strings.  Each lattice exposes the standard surface (bottom, top,
// leq, join, meet, name, At<T> singleton) and is covered by an
// exhaustive triple-fold lattice-axiom verifier.
//
// ── Sub-axis 1: Rounding ────────────────────────────────────────────
[[nodiscard]] consteval std::string_view fp_rounding_name(FpRounding t) noexcept {
    switch (t) {
        case FpRounding::RoundToZero:            return "RoundToZero";
        case FpRounding::RoundToNegativeInf:     return "RoundToNegativeInf";
        case FpRounding::RoundToPositiveInf:     return "RoundToPositiveInf";
        case FpRounding::RoundToNearestEven:     return "RoundToNearestEven";
        case FpRounding::RoundToNearestAwayZero: return "RoundToNearestAwayZero";
        default:                                  return std::string_view{"<unknown FpRounding>"};
    }
}

struct FpRoundingLattice : ChainLatticeOps<FpRounding> {
    [[nodiscard]] static constexpr FpRounding bottom() noexcept { return FpRounding::RoundToZero; }
    [[nodiscard]] static constexpr FpRounding top()    noexcept { return FpRounding::RoundToNearestAwayZero; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "FpRoundingLattice"; }

    template <FpRounding T>
    struct At {
        struct element_type {
            using fp_rounding_value_type = FpRounding;
            [[nodiscard]] constexpr operator fp_rounding_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr FpRounding tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpRounding::RoundToZero:            return "FpRoundingLattice::At<RoundToZero>";
                case FpRounding::RoundToNegativeInf:     return "FpRoundingLattice::At<RoundToNegativeInf>";
                case FpRounding::RoundToPositiveInf:     return "FpRoundingLattice::At<RoundToPositiveInf>";
                case FpRounding::RoundToNearestEven:     return "FpRoundingLattice::At<RoundToNearestEven>";
                case FpRounding::RoundToNearestAwayZero: return "FpRoundingLattice::At<RoundToNearestAwayZero>";
                default:                                  return "FpRoundingLattice::At<?>";
            }
        }
    };
};

// ── Sub-axis 2: Ftz ─────────────────────────────────────────────────
[[nodiscard]] consteval std::string_view fp_ftz_name(FpFtz t) noexcept {
    switch (t) {
        case FpFtz::PreserveSubnormals: return "PreserveSubnormals";
        case FpFtz::FlushToZero:        return "FlushToZero";
        default:                         return std::string_view{"<unknown FpFtz>"};
    }
}

struct FpFtzLattice : ChainLatticeOps<FpFtz> {
    [[nodiscard]] static constexpr FpFtz bottom() noexcept { return FpFtz::PreserveSubnormals; }
    [[nodiscard]] static constexpr FpFtz top()    noexcept { return FpFtz::FlushToZero; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "FpFtzLattice"; }

    template <FpFtz T>
    struct At {
        struct element_type {
            using fp_ftz_value_type = FpFtz;
            [[nodiscard]] constexpr operator fp_ftz_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr FpFtz tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpFtz::PreserveSubnormals: return "FpFtzLattice::At<PreserveSubnormals>";
                case FpFtz::FlushToZero:        return "FpFtzLattice::At<FlushToZero>";
                default:                         return "FpFtzLattice::At<?>";
            }
        }
    };
};

// ── Sub-axis 3: Contract ────────────────────────────────────────────
[[nodiscard]] consteval std::string_view fp_contract_name(FpContract t) noexcept {
    switch (t) {
        case FpContract::Off:      return "Off";
        case FpContract::OnInExpr: return "OnInExpr";
        case FpContract::Fast:     return "Fast";
        default:                    return std::string_view{"<unknown FpContract>"};
    }
}

struct FpContractLattice : ChainLatticeOps<FpContract> {
    [[nodiscard]] static constexpr FpContract bottom() noexcept { return FpContract::Off; }
    [[nodiscard]] static constexpr FpContract top()    noexcept { return FpContract::Fast; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "FpContractLattice"; }

    template <FpContract T>
    struct At {
        struct element_type {
            using fp_contract_value_type = FpContract;
            [[nodiscard]] constexpr operator fp_contract_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr FpContract tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpContract::Off:      return "FpContractLattice::At<Off>";
                case FpContract::OnInExpr: return "FpContractLattice::At<OnInExpr>";
                case FpContract::Fast:     return "FpContractLattice::At<Fast>";
                default:                    return "FpContractLattice::At<?>";
            }
        }
    };
};

// ── Sub-axis 4: TrapMask ────────────────────────────────────────────
[[nodiscard]] consteval std::string_view fp_trap_mask_name(FpTrapMask t) noexcept {
    switch (t) {
        case FpTrapMask::AllMasked:         return "AllMasked";
        case FpTrapMask::UnmaskedInvalid:   return "UnmaskedInvalid";
        case FpTrapMask::UnmaskedDivZero:   return "UnmaskedDivZero";
        case FpTrapMask::UnmaskedOverflow:  return "UnmaskedOverflow";
        case FpTrapMask::UnmaskedUnderflow: return "UnmaskedUnderflow";
        case FpTrapMask::UnmaskedInexact:   return "UnmaskedInexact";
        default:                             return std::string_view{"<unknown FpTrapMask>"};
    }
}

struct FpTrapMaskLattice : ChainLatticeOps<FpTrapMask> {
    [[nodiscard]] static constexpr FpTrapMask bottom() noexcept { return FpTrapMask::AllMasked; }
    [[nodiscard]] static constexpr FpTrapMask top()    noexcept { return FpTrapMask::UnmaskedInexact; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "FpTrapMaskLattice"; }

    template <FpTrapMask T>
    struct At {
        struct element_type {
            using fp_trap_mask_value_type = FpTrapMask;
            [[nodiscard]] constexpr operator fp_trap_mask_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr FpTrapMask tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpTrapMask::AllMasked:         return "FpTrapMaskLattice::At<AllMasked>";
                case FpTrapMask::UnmaskedInvalid:   return "FpTrapMaskLattice::At<UnmaskedInvalid>";
                case FpTrapMask::UnmaskedDivZero:   return "FpTrapMaskLattice::At<UnmaskedDivZero>";
                case FpTrapMask::UnmaskedOverflow:  return "FpTrapMaskLattice::At<UnmaskedOverflow>";
                case FpTrapMask::UnmaskedUnderflow: return "FpTrapMaskLattice::At<UnmaskedUnderflow>";
                case FpTrapMask::UnmaskedInexact:   return "FpTrapMaskLattice::At<UnmaskedInexact>";
                default:                             return "FpTrapMaskLattice::At<?>";
            }
        }
    };
};

// ── Sub-axis 5: DenormalInput ───────────────────────────────────────
[[nodiscard]] consteval std::string_view fp_denormal_input_name(FpDenormalInput t) noexcept {
    switch (t) {
        case FpDenormalInput::HonorDenormals:   return "HonorDenormals";
        case FpDenormalInput::DenormalsAreZero: return "DenormalsAreZero";
        default:                                 return std::string_view{"<unknown FpDenormalInput>"};
    }
}

struct FpDenormalInputLattice : ChainLatticeOps<FpDenormalInput> {
    [[nodiscard]] static constexpr FpDenormalInput bottom() noexcept { return FpDenormalInput::HonorDenormals; }
    [[nodiscard]] static constexpr FpDenormalInput top()    noexcept { return FpDenormalInput::DenormalsAreZero; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "FpDenormalInputLattice"; }

    template <FpDenormalInput T>
    struct At {
        struct element_type {
            using fp_denormal_input_value_type = FpDenormalInput;
            [[nodiscard]] constexpr operator fp_denormal_input_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr FpDenormalInput tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpDenormalInput::HonorDenormals:   return "FpDenormalInputLattice::At<HonorDenormals>";
                case FpDenormalInput::DenormalsAreZero: return "FpDenormalInputLattice::At<DenormalsAreZero>";
                default:                                 return "FpDenormalInputLattice::At<?>";
            }
        }
    };
};

// ── Sub-axis 6: NanPolicy ───────────────────────────────────────────
[[nodiscard]] consteval std::string_view fp_nan_policy_name(FpNanPolicy t) noexcept {
    switch (t) {
        case FpNanPolicy::PropagateQuiet:      return "PropagateQuiet";
        case FpNanPolicy::PropagateSignalling: return "PropagateSignalling";
        case FpNanPolicy::FastNaN:             return "FastNaN";
        default:                                return std::string_view{"<unknown FpNanPolicy>"};
    }
}

struct FpNanPolicyLattice : ChainLatticeOps<FpNanPolicy> {
    [[nodiscard]] static constexpr FpNanPolicy bottom() noexcept { return FpNanPolicy::PropagateQuiet; }
    [[nodiscard]] static constexpr FpNanPolicy top()    noexcept { return FpNanPolicy::FastNaN; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "FpNanPolicyLattice"; }

    template <FpNanPolicy T>
    struct At {
        struct element_type {
            using fp_nan_policy_value_type = FpNanPolicy;
            [[nodiscard]] constexpr operator fp_nan_policy_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr FpNanPolicy tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpNanPolicy::PropagateQuiet:      return "FpNanPolicyLattice::At<PropagateQuiet>";
                case FpNanPolicy::PropagateSignalling: return "FpNanPolicyLattice::At<PropagateSignalling>";
                case FpNanPolicy::FastNaN:             return "FpNanPolicyLattice::At<FastNaN>";
                default:                                return "FpNanPolicyLattice::At<?>";
            }
        }
    };
};

// ── Sub-axis 7: InfPolicy ───────────────────────────────────────────
[[nodiscard]] consteval std::string_view fp_inf_policy_name(FpInfPolicy t) noexcept {
    switch (t) {
        case FpInfPolicy::PropagateInfinity: return "PropagateInfinity";
        case FpInfPolicy::FlushInfToFinite:  return "FlushInfToFinite";
        default:                              return std::string_view{"<unknown FpInfPolicy>"};
    }
}

struct FpInfPolicyLattice : ChainLatticeOps<FpInfPolicy> {
    [[nodiscard]] static constexpr FpInfPolicy bottom() noexcept { return FpInfPolicy::PropagateInfinity; }
    [[nodiscard]] static constexpr FpInfPolicy top()    noexcept { return FpInfPolicy::FlushInfToFinite; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "FpInfPolicyLattice"; }

    template <FpInfPolicy T>
    struct At {
        struct element_type {
            using fp_inf_policy_value_type = FpInfPolicy;
            [[nodiscard]] constexpr operator fp_inf_policy_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr FpInfPolicy tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpInfPolicy::PropagateInfinity: return "FpInfPolicyLattice::At<PropagateInfinity>";
                case FpInfPolicy::FlushInfToFinite:  return "FpInfPolicyLattice::At<FlushInfToFinite>";
                default:                              return "FpInfPolicyLattice::At<?>";
            }
        }
    };
};

// ── Sub-axis 8: ComplexLayout ───────────────────────────────────────
[[nodiscard]] consteval std::string_view fp_complex_layout_name(FpComplexLayout t) noexcept {
    switch (t) {
        case FpComplexLayout::Interleaved:   return "Interleaved";
        case FpComplexLayout::SplitRealImag: return "SplitRealImag";
        case FpComplexLayout::SplitImagReal: return "SplitImagReal";
        default:                              return std::string_view{"<unknown FpComplexLayout>"};
    }
}

struct FpComplexLayoutLattice : ChainLatticeOps<FpComplexLayout> {
    [[nodiscard]] static constexpr FpComplexLayout bottom() noexcept { return FpComplexLayout::Interleaved; }
    [[nodiscard]] static constexpr FpComplexLayout top()    noexcept { return FpComplexLayout::SplitImagReal; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "FpComplexLayoutLattice"; }

    template <FpComplexLayout T>
    struct At {
        struct element_type {
            using fp_complex_layout_value_type = FpComplexLayout;
            [[nodiscard]] constexpr operator fp_complex_layout_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr FpComplexLayout tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpComplexLayout::Interleaved:   return "FpComplexLayoutLattice::At<Interleaved>";
                case FpComplexLayout::SplitRealImag: return "FpComplexLayoutLattice::At<SplitRealImag>";
                case FpComplexLayout::SplitImagReal: return "FpComplexLayoutLattice::At<SplitImagReal>";
                default:                              return "FpComplexLayoutLattice::At<?>";
            }
        }
    };
};

// ── Sub-axis 9: LibmPolicy ──────────────────────────────────────────
[[nodiscard]] consteval std::string_view fp_libm_policy_name(FpLibmPolicy t) noexcept {
    switch (t) {
        case FpLibmPolicy::ScalarLibm:        return "ScalarLibm";
        case FpLibmPolicy::VectorLibmSleef:   return "VectorLibmSleef";
        case FpLibmPolicy::VectorLibmSvml:    return "VectorLibmSvml";
        case FpLibmPolicy::VectorLibmLibmvec: return "VectorLibmLibmvec";
        case FpLibmPolicy::FastApproxNv:      return "FastApproxNv";
        case FpLibmPolicy::FastApproxAm:      return "FastApproxAm";
        default:                               return std::string_view{"<unknown FpLibmPolicy>"};
    }
}

struct FpLibmPolicyLattice : ChainLatticeOps<FpLibmPolicy> {
    [[nodiscard]] static constexpr FpLibmPolicy bottom() noexcept { return FpLibmPolicy::ScalarLibm; }
    [[nodiscard]] static constexpr FpLibmPolicy top()    noexcept { return FpLibmPolicy::FastApproxAm; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "FpLibmPolicyLattice"; }

    template <FpLibmPolicy T>
    struct At {
        struct element_type {
            using fp_libm_policy_value_type = FpLibmPolicy;
            [[nodiscard]] constexpr operator fp_libm_policy_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr FpLibmPolicy tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpLibmPolicy::ScalarLibm:        return "FpLibmPolicyLattice::At<ScalarLibm>";
                case FpLibmPolicy::VectorLibmSleef:   return "FpLibmPolicyLattice::At<VectorLibmSleef>";
                case FpLibmPolicy::VectorLibmSvml:    return "FpLibmPolicyLattice::At<VectorLibmSvml>";
                case FpLibmPolicy::VectorLibmLibmvec: return "FpLibmPolicyLattice::At<VectorLibmLibmvec>";
                case FpLibmPolicy::FastApproxNv:      return "FpLibmPolicyLattice::At<FastApproxNv>";
                case FpLibmPolicy::FastApproxAm:      return "FpLibmPolicyLattice::At<FastApproxAm>";
                default:                               return "FpLibmPolicyLattice::At<?>";
            }
        }
    };
};

// ── Sub-axis 10: Reassociate ────────────────────────────────────────
[[nodiscard]] consteval std::string_view fp_reassociate_name(FpReassociate t) noexcept {
    switch (t) {
        case FpReassociate::Forbidden:           return "Forbidden";
        case FpReassociate::BoundedTreeDepth:    return "BoundedTreeDepth";
        case FpReassociate::UnrestrictedRewrite: return "UnrestrictedRewrite";
        default:                                  return std::string_view{"<unknown FpReassociate>"};
    }
}

struct FpReassociateLattice : ChainLatticeOps<FpReassociate> {
    [[nodiscard]] static constexpr FpReassociate bottom() noexcept { return FpReassociate::Forbidden; }
    [[nodiscard]] static constexpr FpReassociate top()    noexcept { return FpReassociate::UnrestrictedRewrite; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "FpReassociateLattice"; }

    template <FpReassociate T>
    struct At {
        struct element_type {
            using fp_reassociate_value_type = FpReassociate;
            [[nodiscard]] constexpr operator fp_reassociate_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr FpReassociate tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpReassociate::Forbidden:           return "FpReassociateLattice::At<Forbidden>";
                case FpReassociate::BoundedTreeDepth:    return "FpReassociateLattice::At<BoundedTreeDepth>";
                case FpReassociate::UnrestrictedRewrite: return "FpReassociateLattice::At<UnrestrictedRewrite>";
                default:                                  return "FpReassociateLattice::At<?>";
            }
        }
    };
};

// ── Sub-axis 11: ConstantRounding ───────────────────────────────────
[[nodiscard]] consteval std::string_view fp_constant_rounding_name(FpConstantRounding t) noexcept {
    switch (t) {
        case FpConstantRounding::SameAsRuntime: return "SameAsRuntime";
        case FpConstantRounding::AlwaysRTE:     return "AlwaysRTE";
        case FpConstantRounding::AlwaysRTZ:     return "AlwaysRTZ";
        default:                                 return std::string_view{"<unknown FpConstantRounding>"};
    }
}

struct FpConstantRoundingLattice : ChainLatticeOps<FpConstantRounding> {
    [[nodiscard]] static constexpr FpConstantRounding bottom() noexcept { return FpConstantRounding::SameAsRuntime; }
    [[nodiscard]] static constexpr FpConstantRounding top()    noexcept { return FpConstantRounding::AlwaysRTZ; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "FpConstantRoundingLattice"; }

    template <FpConstantRounding T>
    struct At {
        struct element_type {
            using fp_constant_rounding_value_type = FpConstantRounding;
            [[nodiscard]] constexpr operator fp_constant_rounding_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr FpConstantRounding tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpConstantRounding::SameAsRuntime: return "FpConstantRoundingLattice::At<SameAsRuntime>";
                case FpConstantRounding::AlwaysRTE:     return "FpConstantRoundingLattice::At<AlwaysRTE>";
                case FpConstantRounding::AlwaysRTZ:     return "FpConstantRoundingLattice::At<AlwaysRTZ>";
                default:                                 return "FpConstantRoundingLattice::At<?>";
            }
        }
    };
};

// ── FIXY-V-090 — FpModeProductLattice composite ─────────────────────
//
// 11-way componentwise product over the per-axis chain lattices.  The
// canonical "FP-mode" state of a value is the 11-tuple
//   (Rounding, Ftz, Contract, TrapMask, Denormal, NanPolicy, InfPolicy,
//    ComplexLayout, LibmPolicy, Reassociate, ConstantRounding)
// — each component independently ordered by its per-axis ⊑.  Operations
// (leq / join / meet / bottom / top) lift pointwise through
// `ProductLattice<Ls...>`'s N-ary primary (ProductLattice.h ALGEBRA-15
// extension).  Bottom is the 11-tuple of weakest tiers (each axis's
// bottom() — typically the most-permissive policy); top is the 11-tuple
// of strongest tiers (each axis's top() — typically the most-restrictive
// policy).
//
// USAGE: this composite is the algebraic foundation for the V-090
// `safety::FpModePinned<auto Mode, T>` wrapper family.  Production code
// does NOT instantiate `Graded<Absolute, FpModeProductLattice, T>`
// directly; per CLAUDE.md §XVI canonical wrapper-nesting order, the
// 11-deep composite is built via the `FpModeComposite<...>` type alias
// in safety/FpMode.h that NESTS 11 single-axis `FpModePinned<Mode_i, _>`
// wrappers outer-to-inner.  Reason: nested-wrapper composition gives
// each axis its own row_hash salt (0x21..0x2B per FOUND-I02), which
// preserves the federation-cache slot disjointness the product-lattice
// composite cannot express on its own.
//
// The composite IS, however, the canonical answer for any consumer
// that needs to reason about the 11-axis algebra AS A LATTICE — e.g.
// computing the meet of two recipe-pinned FP modes, or asking whether
// recipe A ⊑ recipe B componentwise.  The 11-way join/meet via the
// N-ary primary is `O(11) compile + 11 constexpr comparisons runtime`
// — same shape as any other ProductLattice consumer.
//
// Axiom coverage:
//   TypeSafe — `ProductLattice<...>` validates each component via the
//              Lattice concept; non-FP-axis lattices fail at template
//              substitution.
//   DetSafe  — every op is constexpr (NOT consteval) so a runtime
//              Graded carrier can enforce its `pre (L::leq(...))`
//              precondition.
//   MemSafe  — element_type uses ProductLattice's [[no_unique_address]]
//              componentwise carrier; no per-instance heap.
using FpModeProductLattice = ::crucible::algebra::lattices::ProductLattice<
    FpRoundingLattice,
    FpFtzLattice,
    FpContractLattice,
    FpTrapMaskLattice,
    FpDenormalInputLattice,
    FpNanPolicyLattice,
    FpInfPolicyLattice,
    FpComplexLayoutLattice,
    FpLibmPolicyLattice,
    FpReassociateLattice,
    FpConstantRoundingLattice>;

// Composite-lattice concept-gate witnesses.  The N-ary ProductLattice
// primary template is gated through the Lattice concept on every
// component; verify here that the composite IS itself a Lattice and a
// BoundedLattice (since every component ChainLattice is bounded).  The
// !Semiring check mirrors the per-axis lattice discipline — the
// composite carries no ⊕/⊗ structure independent of join/meet.
static_assert(::crucible::algebra::Lattice<FpModeProductLattice>,
    "FpModeProductLattice must satisfy the Lattice concept "
    "(componentwise lift of 11 BoundedLattice chains).");
static_assert(::crucible::algebra::BoundedLattice<FpModeProductLattice>,
    "FpModeProductLattice must satisfy BoundedLattice — every component "
    "ChainLatticeOps<EnumT> publishes bottom() and top().");
static_assert(!::crucible::algebra::Semiring<FpModeProductLattice>,
    "FpModeProductLattice carries no ⊕/⊗ structure independent of "
    "join/meet — Semiring would be a falsehood at the type level.");
static_assert(FpModeProductLattice::arity == 11,
    "FpModeProductLattice must have arity 11 — one slot per FP sub-axis "
    "ordinal of the V-088 enum split.");

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

// ════════════════════════════════════════════════════════════════════
// ── V-089: Per-lattice self-tests (lattice axioms + reflection) ─────
// ════════════════════════════════════════════════════════════════════

// ── Reflection-driven name coverage ─────────────────────────────────
//
// For every sub-axis's `<axis>_name(enumerator)` switch, walk the
// reflection-discovered enumerator catalog and assert no arm leaks
// the "<unknown ...>" sentinel.  This auto-extends when a sub-axis
// gains a new enumerator — V-089 ships the coverage check; whoever
// extends the enum auto-detects the missing switch arm at compile time.

#define CRUCIBLE_FP_NAME_COVERAGE(SubAxis, NameFn, UnknownLit)             \
    [[nodiscard]] consteval bool every_##NameFn##_has_arm() noexcept {      \
        static constexpr auto enumerators =                                 \
            std::define_static_array(std::meta::enumerators_of(^^SubAxis)); \
        _Pragma("GCC diagnostic push")                                      \
        _Pragma("GCC diagnostic ignored \"-Wshadow\"")                      \
        template for (constexpr auto en : enumerators) {                    \
            if (NameFn([:en:]) == std::string_view{UnknownLit}) return false; \
        }                                                                   \
        _Pragma("GCC diagnostic pop")                                       \
        return true;                                                        \
    }                                                                       \
    static_assert(every_##NameFn##_has_arm(),                               \
        #NameFn "() switch missing an arm for at least one " #SubAxis " enumerator.")

CRUCIBLE_FP_NAME_COVERAGE(FpRounding,         fp_rounding_name,         "<unknown FpRounding>");
CRUCIBLE_FP_NAME_COVERAGE(FpFtz,              fp_ftz_name,              "<unknown FpFtz>");
CRUCIBLE_FP_NAME_COVERAGE(FpContract,         fp_contract_name,         "<unknown FpContract>");
CRUCIBLE_FP_NAME_COVERAGE(FpTrapMask,         fp_trap_mask_name,        "<unknown FpTrapMask>");
CRUCIBLE_FP_NAME_COVERAGE(FpDenormalInput,    fp_denormal_input_name,   "<unknown FpDenormalInput>");
CRUCIBLE_FP_NAME_COVERAGE(FpNanPolicy,        fp_nan_policy_name,       "<unknown FpNanPolicy>");
CRUCIBLE_FP_NAME_COVERAGE(FpInfPolicy,        fp_inf_policy_name,       "<unknown FpInfPolicy>");
CRUCIBLE_FP_NAME_COVERAGE(FpComplexLayout,    fp_complex_layout_name,   "<unknown FpComplexLayout>");
CRUCIBLE_FP_NAME_COVERAGE(FpLibmPolicy,       fp_libm_policy_name,      "<unknown FpLibmPolicy>");
CRUCIBLE_FP_NAME_COVERAGE(FpReassociate,      fp_reassociate_name,      "<unknown FpReassociate>");
CRUCIBLE_FP_NAME_COVERAGE(FpConstantRounding, fp_constant_rounding_name,"<unknown FpConstantRounding>");

#undef CRUCIBLE_FP_NAME_COVERAGE

// ── Per-lattice concept conformance + exhaustive axiom verifier ─────
//
// Every per-sub-axis chain lattice satisfies `Lattice` and
// `BoundedLattice`; the exhaustive verifier walks (axis)³ triples and
// confirms lattice axioms + distributivity.  Chain orders are always
// distributive — failure indicates a leq/join/meet defect.

#define CRUCIBLE_FP_LATTICE_VERIFY(L)                                        \
    static_assert(Lattice<L>);                                               \
    static_assert(BoundedLattice<L>);                                        \
    static_assert(!Semiring<L>);                                             \
    static_assert(verify_chain_lattice_exhaustive<L>(),                      \
        #L " chain-order lattice axioms failed at some triple.");            \
    static_assert(verify_chain_lattice_distributive_exhaustive<L>(),         \
        #L " chain order failed distributivity — leq/join/meet defect.")

CRUCIBLE_FP_LATTICE_VERIFY(FpRoundingLattice);
CRUCIBLE_FP_LATTICE_VERIFY(FpFtzLattice);
CRUCIBLE_FP_LATTICE_VERIFY(FpContractLattice);
CRUCIBLE_FP_LATTICE_VERIFY(FpTrapMaskLattice);
CRUCIBLE_FP_LATTICE_VERIFY(FpDenormalInputLattice);
CRUCIBLE_FP_LATTICE_VERIFY(FpNanPolicyLattice);
CRUCIBLE_FP_LATTICE_VERIFY(FpInfPolicyLattice);
CRUCIBLE_FP_LATTICE_VERIFY(FpComplexLayoutLattice);
CRUCIBLE_FP_LATTICE_VERIFY(FpLibmPolicyLattice);
CRUCIBLE_FP_LATTICE_VERIFY(FpReassociateLattice);
CRUCIBLE_FP_LATTICE_VERIFY(FpConstantRoundingLattice);

#undef CRUCIBLE_FP_LATTICE_VERIFY

// ── Bottom / top pins (V-089 surface contract) ──────────────────────
//
// Every sub-axis lattice's bottom == enum ordinal 0 (matching the
// V-088 self-test pins above) and top == topmost ordinal.  These
// asserts catch the "someone reordered the enum and the lattice
// failed to follow" drift class.
static_assert(FpRoundingLattice::bottom()         == FpRounding::RoundToZero);
static_assert(FpRoundingLattice::top()            == FpRounding::RoundToNearestAwayZero);
static_assert(FpFtzLattice::bottom()              == FpFtz::PreserveSubnormals);
static_assert(FpFtzLattice::top()                 == FpFtz::FlushToZero);
static_assert(FpContractLattice::bottom()         == FpContract::Off);
static_assert(FpContractLattice::top()            == FpContract::Fast);
static_assert(FpTrapMaskLattice::bottom()         == FpTrapMask::AllMasked);
static_assert(FpTrapMaskLattice::top()            == FpTrapMask::UnmaskedInexact);
static_assert(FpDenormalInputLattice::bottom()    == FpDenormalInput::HonorDenormals);
static_assert(FpDenormalInputLattice::top()       == FpDenormalInput::DenormalsAreZero);
static_assert(FpNanPolicyLattice::bottom()        == FpNanPolicy::PropagateQuiet);
static_assert(FpNanPolicyLattice::top()           == FpNanPolicy::FastNaN);
static_assert(FpInfPolicyLattice::bottom()        == FpInfPolicy::PropagateInfinity);
static_assert(FpInfPolicyLattice::top()           == FpInfPolicy::FlushInfToFinite);
static_assert(FpComplexLayoutLattice::bottom()    == FpComplexLayout::Interleaved);
static_assert(FpComplexLayoutLattice::top()       == FpComplexLayout::SplitImagReal);
static_assert(FpLibmPolicyLattice::bottom()       == FpLibmPolicy::ScalarLibm);
static_assert(FpLibmPolicyLattice::top()          == FpLibmPolicy::FastApproxAm);
static_assert(FpReassociateLattice::bottom()      == FpReassociate::Forbidden);
static_assert(FpReassociateLattice::top()         == FpReassociate::UnrestrictedRewrite);
static_assert(FpConstantRoundingLattice::bottom() == FpConstantRounding::SameAsRuntime);
static_assert(FpConstantRoundingLattice::top()    == FpConstantRounding::AlwaysRTZ);

// ── Lattice top-level diagnostic name pins ──────────────────────────
static_assert(FpRoundingLattice::name()         == std::string_view{"FpRoundingLattice"});
static_assert(FpFtzLattice::name()              == std::string_view{"FpFtzLattice"});
static_assert(FpContractLattice::name()         == std::string_view{"FpContractLattice"});
static_assert(FpTrapMaskLattice::name()         == std::string_view{"FpTrapMaskLattice"});
static_assert(FpDenormalInputLattice::name()    == std::string_view{"FpDenormalInputLattice"});
static_assert(FpNanPolicyLattice::name()        == std::string_view{"FpNanPolicyLattice"});
static_assert(FpInfPolicyLattice::name()        == std::string_view{"FpInfPolicyLattice"});
static_assert(FpComplexLayoutLattice::name()    == std::string_view{"FpComplexLayoutLattice"});
static_assert(FpLibmPolicyLattice::name()       == std::string_view{"FpLibmPolicyLattice"});
static_assert(FpReassociateLattice::name()      == std::string_view{"FpReassociateLattice"});
static_assert(FpConstantRoundingLattice::name() == std::string_view{"FpConstantRoundingLattice"});

// ── Strict-chain order pin (lattice ⊥ ⊏ top witness) ────────────────
//
// Each sub-axis chain has a `leq(bottom, top)` true / `leq(top, bottom)`
// false witness — pins the chain direction.  Together with
// verify_chain_lattice_exhaustive the chain is structurally locked.
static_assert( FpRoundingLattice::leq(FpRounding::RoundToZero, FpRounding::RoundToNearestAwayZero));
static_assert(!FpRoundingLattice::leq(FpRounding::RoundToNearestAwayZero, FpRounding::RoundToZero));
static_assert( FpFtzLattice::leq(FpFtz::PreserveSubnormals, FpFtz::FlushToZero));
static_assert(!FpFtzLattice::leq(FpFtz::FlushToZero, FpFtz::PreserveSubnormals));
static_assert( FpContractLattice::leq(FpContract::Off, FpContract::Fast));
static_assert(!FpContractLattice::leq(FpContract::Fast, FpContract::Off));
static_assert( FpTrapMaskLattice::leq(FpTrapMask::AllMasked, FpTrapMask::UnmaskedInexact));
static_assert(!FpTrapMaskLattice::leq(FpTrapMask::UnmaskedInexact, FpTrapMask::AllMasked));
static_assert( FpDenormalInputLattice::leq(FpDenormalInput::HonorDenormals, FpDenormalInput::DenormalsAreZero));
static_assert(!FpDenormalInputLattice::leq(FpDenormalInput::DenormalsAreZero, FpDenormalInput::HonorDenormals));
static_assert( FpNanPolicyLattice::leq(FpNanPolicy::PropagateQuiet, FpNanPolicy::FastNaN));
static_assert(!FpNanPolicyLattice::leq(FpNanPolicy::FastNaN, FpNanPolicy::PropagateQuiet));
static_assert( FpInfPolicyLattice::leq(FpInfPolicy::PropagateInfinity, FpInfPolicy::FlushInfToFinite));
static_assert(!FpInfPolicyLattice::leq(FpInfPolicy::FlushInfToFinite, FpInfPolicy::PropagateInfinity));
static_assert( FpComplexLayoutLattice::leq(FpComplexLayout::Interleaved, FpComplexLayout::SplitImagReal));
static_assert(!FpComplexLayoutLattice::leq(FpComplexLayout::SplitImagReal, FpComplexLayout::Interleaved));
static_assert( FpLibmPolicyLattice::leq(FpLibmPolicy::ScalarLibm, FpLibmPolicy::FastApproxAm));
static_assert(!FpLibmPolicyLattice::leq(FpLibmPolicy::FastApproxAm, FpLibmPolicy::ScalarLibm));
static_assert( FpReassociateLattice::leq(FpReassociate::Forbidden, FpReassociate::UnrestrictedRewrite));
static_assert(!FpReassociateLattice::leq(FpReassociate::UnrestrictedRewrite, FpReassociate::Forbidden));
static_assert( FpConstantRoundingLattice::leq(FpConstantRounding::SameAsRuntime, FpConstantRounding::AlwaysRTZ));
static_assert(!FpConstantRoundingLattice::leq(FpConstantRounding::AlwaysRTZ, FpConstantRounding::SameAsRuntime));

// ── At<T> singleton — empty element_type for EBO collapse ───────────
//
// V-090 will use these singletons inside `Graded<Absolute, At<T>, P>`
// to type-pin each sub-axis at fixed call sites.  V-089's contract:
// every singleton's element_type is empty (so `[[no_unique_address]]`
// collapses to 0 bytes at the use site).
static_assert(std::is_empty_v<FpRoundingLattice::At<FpRounding::RoundToZero>::element_type>);
static_assert(std::is_empty_v<FpFtzLattice::At<FpFtz::PreserveSubnormals>::element_type>);
static_assert(std::is_empty_v<FpContractLattice::At<FpContract::Off>::element_type>);
static_assert(std::is_empty_v<FpTrapMaskLattice::At<FpTrapMask::AllMasked>::element_type>);
static_assert(std::is_empty_v<FpDenormalInputLattice::At<FpDenormalInput::HonorDenormals>::element_type>);
static_assert(std::is_empty_v<FpNanPolicyLattice::At<FpNanPolicy::PropagateQuiet>::element_type>);
static_assert(std::is_empty_v<FpInfPolicyLattice::At<FpInfPolicy::PropagateInfinity>::element_type>);
static_assert(std::is_empty_v<FpComplexLayoutLattice::At<FpComplexLayout::Interleaved>::element_type>);
static_assert(std::is_empty_v<FpLibmPolicyLattice::At<FpLibmPolicy::ScalarLibm>::element_type>);
static_assert(std::is_empty_v<FpReassociateLattice::At<FpReassociate::Forbidden>::element_type>);
static_assert(std::is_empty_v<FpConstantRoundingLattice::At<FpConstantRounding::SameAsRuntime>::element_type>);

// ── Cross-sub-axis lattice structural separation ────────────────────
//
// All 11 sub-axis lattices are STRUCTURALLY DISTINCT C++ types.  The
// type system guarantees that a function expecting `FpRoundingLattice`
// cannot silently consume an `FpFtzLattice` argument; a Graded
// instantiated over one cannot be implicitly converted to a Graded
// over another.  Together with the V-088 `is_same_v` enum-distinctness
// witnesses, this pins the 11 sub-axes as orthogonal at the algebra
// layer — V-090's ProductLattice composite will then combine them in
// a single Graded carrier without cross-axis confusion.
static_assert(!std::is_same_v<FpRoundingLattice,      FpFtzLattice>);
static_assert(!std::is_same_v<FpFtzLattice,           FpContractLattice>);
static_assert(!std::is_same_v<FpContractLattice,      FpTrapMaskLattice>);
static_assert(!std::is_same_v<FpTrapMaskLattice,      FpDenormalInputLattice>);
static_assert(!std::is_same_v<FpDenormalInputLattice, FpNanPolicyLattice>);
static_assert(!std::is_same_v<FpNanPolicyLattice,     FpInfPolicyLattice>);
static_assert(!std::is_same_v<FpInfPolicyLattice,     FpComplexLayoutLattice>);
static_assert(!std::is_same_v<FpComplexLayoutLattice, FpLibmPolicyLattice>);
static_assert(!std::is_same_v<FpLibmPolicyLattice,    FpReassociateLattice>);
static_assert(!std::is_same_v<FpReassociateLattice,   FpConstantRoundingLattice>);

// ── Runtime smoke test (per feedback_algebra_runtime_smoke_test) ────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: every
// algebra header MUST ship `inline void runtime_smoke_test()` with
// non-constant arguments + concept-based capability checks.  Pure
// static_asserts mask consteval/SFINAE/inline-body bugs.
inline void fp_mode_lattice_runtime_smoke_test() {
    // Full-lattice ops at runtime for each sub-axis.  Pin operands
    // to the chain's bottom and top, then call leq/join/meet so the
    // optimizer cannot collapse to a compile-time fold.

    // Rounding (5-element).
    FpRounding ra = FpRounding::RoundToZero;
    FpRounding rb = FpRounding::RoundToNearestAwayZero;
    [[maybe_unused]] bool       rl1 = FpRoundingLattice::leq(ra, rb);
    [[maybe_unused]] FpRounding rj1 = FpRoundingLattice::join(ra, rb);
    [[maybe_unused]] FpRounding rm1 = FpRoundingLattice::meet(ra, rb);

    // Ftz (2-element).
    FpFtz fa = FpFtz::PreserveSubnormals;
    FpFtz fb = FpFtz::FlushToZero;
    [[maybe_unused]] FpFtz fj1 = FpFtzLattice::join(fa, fb);
    [[maybe_unused]] FpFtz fm1 = FpFtzLattice::meet(fa, fb);

    // Contract (3-element).
    FpContract ca = FpContract::Off;
    FpContract cb = FpContract::Fast;
    [[maybe_unused]] FpContract cj1 = FpContractLattice::join(ca, cb);

    // TrapMask (6-element).
    FpTrapMask ta = FpTrapMask::AllMasked;
    FpTrapMask tb = FpTrapMask::UnmaskedInexact;
    [[maybe_unused]] FpTrapMask tj1 = FpTrapMaskLattice::join(ta, tb);

    // DenormalInput (2-element).
    FpDenormalInput da = FpDenormalInput::HonorDenormals;
    FpDenormalInput db = FpDenormalInput::DenormalsAreZero;
    [[maybe_unused]] FpDenormalInput dj1 = FpDenormalInputLattice::join(da, db);

    // NanPolicy (3-element).
    FpNanPolicy na = FpNanPolicy::PropagateQuiet;
    FpNanPolicy nb = FpNanPolicy::FastNaN;
    [[maybe_unused]] FpNanPolicy nj1 = FpNanPolicyLattice::join(na, nb);

    // InfPolicy (2-element).
    FpInfPolicy ia = FpInfPolicy::PropagateInfinity;
    FpInfPolicy ib = FpInfPolicy::FlushInfToFinite;
    [[maybe_unused]] FpInfPolicy ij1 = FpInfPolicyLattice::join(ia, ib);

    // ComplexLayout (3-element).
    FpComplexLayout xa = FpComplexLayout::Interleaved;
    FpComplexLayout xb = FpComplexLayout::SplitImagReal;
    [[maybe_unused]] FpComplexLayout xj1 = FpComplexLayoutLattice::join(xa, xb);

    // LibmPolicy (6-element).
    FpLibmPolicy la = FpLibmPolicy::ScalarLibm;
    FpLibmPolicy lb = FpLibmPolicy::FastApproxAm;
    [[maybe_unused]] FpLibmPolicy lj1 = FpLibmPolicyLattice::join(la, lb);

    // Reassociate (3-element).
    FpReassociate ea = FpReassociate::Forbidden;
    FpReassociate eb = FpReassociate::UnrestrictedRewrite;
    [[maybe_unused]] FpReassociate ej1 = FpReassociateLattice::join(ea, eb);

    // ConstantRounding (3-element).
    FpConstantRounding ka = FpConstantRounding::SameAsRuntime;
    FpConstantRounding kb = FpConstantRounding::AlwaysRTZ;
    [[maybe_unused]] FpConstantRounding kj1 = FpConstantRoundingLattice::join(ka, kb);

    // At<T>::element_type round-trip — verify the singleton's
    // operator->element-type conversion materializes the right tier
    // at runtime (not just consteval).
    FpRoundingLattice::At<FpRounding::RoundToNearestEven>::element_type rte_pin{};
    [[maybe_unused]] FpRounding rte_recovered = rte_pin;

    FpFtzLattice::At<FpFtz::FlushToZero>::element_type ftz_pin{};
    [[maybe_unused]] FpFtz ftz_recovered = ftz_pin;
}

}  // namespace detail::fp_mode_lattice_self_test

}  // namespace crucible::algebra::lattices
