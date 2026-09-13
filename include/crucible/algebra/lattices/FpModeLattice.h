#pragma once

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>
#include <crucible/algebra/lattices/ProductLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// The five IEEE 754 rounding-direction attributes.  RoundToNearestEven is the
// IEEE 754 default.  RoundToNearestAwayZero is the tie-break some GPU ISAs
// spell RNA.
enum class FpRounding : std::uint8_t {
    RoundToZero = 0,
    RoundToNegativeInf = 1,
    RoundToPositiveInf = 2,
    RoundToNearestEven = 3,
    RoundToNearestAwayZero = 4,
};

// Subnormal handling for arithmetic RESULTS.  Subnormal INPUTS are governed
// separately by FpDenormalInput.
enum class FpFtz : std::uint8_t {
    PreserveSubnormals = 0,
    FlushToZero = 1,
};

// Whether `a * b + c` may contract into a single FMA.  Mirrors the GCC
// `-ffp-contract=off/on/fast` surface.  OnInExpr is the IEEE 754-2008 default.
enum class FpContract : std::uint8_t {
    Off = 0,
    OnInExpr = 1,  // contract within a single expression
    Fast = 2,  // contract across statements
};

// IEEE 754 trap-enable bits.  A masked trap still records its flag in the
// floating-point environment.  Only the SIGFPE is suppressed.
enum class FpTrapMask : std::uint8_t {
    AllMasked = 0,
    UnmaskedInvalid = 1,  // SIGFPE on invalid (NaN-from-NaN, 0/0)
    UnmaskedDivZero = 2,  // SIGFPE on finite÷0
    UnmaskedOverflow = 3,
    UnmaskedUnderflow = 4,
    UnmaskedInexact = 5,
};

// Subnormal handling for arithmetic INPUTS.  x86 spells this MXCSR.DAZ, ARM
// spells it FPCR.FZ.
enum class FpDenormalInput : std::uint8_t {
    HonorDenormals = 0,
    DenormalsAreZero = 1,
};

enum class FpNanPolicy : std::uint8_t {
    PropagateQuiet = 0,  // qNaN survives every op (IEEE 754 default)
    PropagateSignalling = 1,  // sNaN traps on consume; payload survives if masked
    FastNaN = 2,  // non-IEEE: `min(NaN, x) = x`, `max(NaN, x) = x`
};

enum class FpInfPolicy : std::uint8_t {
    PropagateInfinity = 0,  // IEEE 754 default
    FlushInfToFinite = 1,  // ±Inf → ±FLT_MAX (non-IEEE saturation)
};

enum class FpComplexLayout : std::uint8_t {
    Interleaved = 0,  // [Re0, Im0, Re1, Im1, ...] — the std::complex layout
    SplitRealImag = 1,  // [Re0, Re1, ..., Re_n, Im0, Im1, ..., Im_n]
    SplitImagReal = 2,  // [Im0, Im1, ..., Im_n, Re0, Re1, ..., Re_n]
};

// Which transcendental implementation evaluates sin, cos and friends.
enum class FpLibmPolicy : std::uint8_t {
    ScalarLibm = 0,  // scalar glibc / musl libm
    VectorLibmSleef = 1,
    VectorLibmSvml = 2,  // Intel SVML
    VectorLibmLibmvec = 3,  // GCC libmvec
    FastApproxNv = 4,  // CUDA `__sinf` / `__cosf` (relaxed ULP bound)
    FastApproxAm = 5,  // AMD `v_sin_f32` instruction
    // A source-pinned polynomial evaluated in strict IEEE 754 arithmetic with
    // no libm call.  The result depends only on the coefficient constants and
    // on IEEE 754 semantics, so it is the one bit-stable choice across
    // platforms.  Bit-exact recipe tiers require it.
    Polynomial = 6,
};

// Whether floating-point additions may be reassociated.  UnrestrictedRewrite
// is the GCC `-fassociative-math` behaviour.
enum class FpReassociate : std::uint8_t {
    Forbidden = 0,
    BoundedTreeDepth = 1,  // log-N tree only, so the topology stays pinned
    UnrestrictedRewrite = 2,
};

// Rounding applied to floating-point literals during constant folding, for the
// rare cases where it must differ from the runtime FpRounding mode.
enum class FpConstantRounding : std::uint8_t {
    SameAsRuntime = 0,
    AlwaysRTE = 1,  // pin RoundToNearestEven for all literals
    AlwaysRTZ = 2,  // pin RoundToZero for all literals
};

[[nodiscard]] consteval std::string_view fp_rounding_name(FpRounding t) noexcept {
    switch (t) {
        case FpRounding::RoundToZero:
            return "RoundToZero";
        case FpRounding::RoundToNegativeInf:
            return "RoundToNegativeInf";
        case FpRounding::RoundToPositiveInf:
            return "RoundToPositiveInf";
        case FpRounding::RoundToNearestEven:
            return "RoundToNearestEven";
        case FpRounding::RoundToNearestAwayZero:
            return "RoundToNearestAwayZero";
        default:
            return std::string_view{"<unknown FpRounding>"};
    }
}

struct FpRoundingLattice : ChainLatticeOps<FpRounding> {
    [[nodiscard]] static constexpr FpRounding bottom() noexcept { return FpRounding::RoundToZero; }
    [[nodiscard]] static constexpr FpRounding top() noexcept { return FpRounding::RoundToNearestAwayZero; }
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
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpRounding::RoundToZero:
                    return "FpRoundingLattice::At<RoundToZero>";
                case FpRounding::RoundToNegativeInf:
                    return "FpRoundingLattice::At<RoundToNegativeInf>";
                case FpRounding::RoundToPositiveInf:
                    return "FpRoundingLattice::At<RoundToPositiveInf>";
                case FpRounding::RoundToNearestEven:
                    return "FpRoundingLattice::At<RoundToNearestEven>";
                case FpRounding::RoundToNearestAwayZero:
                    return "FpRoundingLattice::At<RoundToNearestAwayZero>";
                default:
                    return "FpRoundingLattice::At<?>";
            }
        }
    };
};

[[nodiscard]] consteval std::string_view fp_ftz_name(FpFtz t) noexcept {
    switch (t) {
        case FpFtz::PreserveSubnormals:
            return "PreserveSubnormals";
        case FpFtz::FlushToZero:
            return "FlushToZero";
        default:
            return std::string_view{"<unknown FpFtz>"};
    }
}

struct FpFtzLattice : ChainLatticeOps<FpFtz> {
    [[nodiscard]] static constexpr FpFtz bottom() noexcept { return FpFtz::PreserveSubnormals; }
    [[nodiscard]] static constexpr FpFtz top() noexcept { return FpFtz::FlushToZero; }
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
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpFtz::PreserveSubnormals:
                    return "FpFtzLattice::At<PreserveSubnormals>";
                case FpFtz::FlushToZero:
                    return "FpFtzLattice::At<FlushToZero>";
                default:
                    return "FpFtzLattice::At<?>";
            }
        }
    };
};

[[nodiscard]] consteval std::string_view fp_contract_name(FpContract t) noexcept {
    switch (t) {
        case FpContract::Off:
            return "Off";
        case FpContract::OnInExpr:
            return "OnInExpr";
        case FpContract::Fast:
            return "Fast";
        default:
            return std::string_view{"<unknown FpContract>"};
    }
}

struct FpContractLattice : ChainLatticeOps<FpContract> {
    [[nodiscard]] static constexpr FpContract bottom() noexcept { return FpContract::Off; }
    [[nodiscard]] static constexpr FpContract top() noexcept { return FpContract::Fast; }
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
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpContract::Off:
                    return "FpContractLattice::At<Off>";
                case FpContract::OnInExpr:
                    return "FpContractLattice::At<OnInExpr>";
                case FpContract::Fast:
                    return "FpContractLattice::At<Fast>";
                default:
                    return "FpContractLattice::At<?>";
            }
        }
    };
};

[[nodiscard]] consteval std::string_view fp_trap_mask_name(FpTrapMask t) noexcept {
    switch (t) {
        case FpTrapMask::AllMasked:
            return "AllMasked";
        case FpTrapMask::UnmaskedInvalid:
            return "UnmaskedInvalid";
        case FpTrapMask::UnmaskedDivZero:
            return "UnmaskedDivZero";
        case FpTrapMask::UnmaskedOverflow:
            return "UnmaskedOverflow";
        case FpTrapMask::UnmaskedUnderflow:
            return "UnmaskedUnderflow";
        case FpTrapMask::UnmaskedInexact:
            return "UnmaskedInexact";
        default:
            return std::string_view{"<unknown FpTrapMask>"};
    }
}

struct FpTrapMaskLattice : ChainLatticeOps<FpTrapMask> {
    [[nodiscard]] static constexpr FpTrapMask bottom() noexcept { return FpTrapMask::AllMasked; }
    [[nodiscard]] static constexpr FpTrapMask top() noexcept { return FpTrapMask::UnmaskedInexact; }
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
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpTrapMask::AllMasked:
                    return "FpTrapMaskLattice::At<AllMasked>";
                case FpTrapMask::UnmaskedInvalid:
                    return "FpTrapMaskLattice::At<UnmaskedInvalid>";
                case FpTrapMask::UnmaskedDivZero:
                    return "FpTrapMaskLattice::At<UnmaskedDivZero>";
                case FpTrapMask::UnmaskedOverflow:
                    return "FpTrapMaskLattice::At<UnmaskedOverflow>";
                case FpTrapMask::UnmaskedUnderflow:
                    return "FpTrapMaskLattice::At<UnmaskedUnderflow>";
                case FpTrapMask::UnmaskedInexact:
                    return "FpTrapMaskLattice::At<UnmaskedInexact>";
                default:
                    return "FpTrapMaskLattice::At<?>";
            }
        }
    };
};

[[nodiscard]] consteval std::string_view fp_denormal_input_name(FpDenormalInput t) noexcept {
    switch (t) {
        case FpDenormalInput::HonorDenormals:
            return "HonorDenormals";
        case FpDenormalInput::DenormalsAreZero:
            return "DenormalsAreZero";
        default:
            return std::string_view{"<unknown FpDenormalInput>"};
    }
}

struct FpDenormalInputLattice : ChainLatticeOps<FpDenormalInput> {
    [[nodiscard]] static constexpr FpDenormalInput bottom() noexcept { return FpDenormalInput::HonorDenormals; }
    [[nodiscard]] static constexpr FpDenormalInput top() noexcept { return FpDenormalInput::DenormalsAreZero; }
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
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpDenormalInput::HonorDenormals:
                    return "FpDenormalInputLattice::At<HonorDenormals>";
                case FpDenormalInput::DenormalsAreZero:
                    return "FpDenormalInputLattice::At<DenormalsAreZero>";
                default:
                    return "FpDenormalInputLattice::At<?>";
            }
        }
    };
};

[[nodiscard]] consteval std::string_view fp_nan_policy_name(FpNanPolicy t) noexcept {
    switch (t) {
        case FpNanPolicy::PropagateQuiet:
            return "PropagateQuiet";
        case FpNanPolicy::PropagateSignalling:
            return "PropagateSignalling";
        case FpNanPolicy::FastNaN:
            return "FastNaN";
        default:
            return std::string_view{"<unknown FpNanPolicy>"};
    }
}

struct FpNanPolicyLattice : ChainLatticeOps<FpNanPolicy> {
    [[nodiscard]] static constexpr FpNanPolicy bottom() noexcept { return FpNanPolicy::PropagateQuiet; }
    [[nodiscard]] static constexpr FpNanPolicy top() noexcept { return FpNanPolicy::FastNaN; }
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
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpNanPolicy::PropagateQuiet:
                    return "FpNanPolicyLattice::At<PropagateQuiet>";
                case FpNanPolicy::PropagateSignalling:
                    return "FpNanPolicyLattice::At<PropagateSignalling>";
                case FpNanPolicy::FastNaN:
                    return "FpNanPolicyLattice::At<FastNaN>";
                default:
                    return "FpNanPolicyLattice::At<?>";
            }
        }
    };
};

[[nodiscard]] consteval std::string_view fp_inf_policy_name(FpInfPolicy t) noexcept {
    switch (t) {
        case FpInfPolicy::PropagateInfinity:
            return "PropagateInfinity";
        case FpInfPolicy::FlushInfToFinite:
            return "FlushInfToFinite";
        default:
            return std::string_view{"<unknown FpInfPolicy>"};
    }
}

struct FpInfPolicyLattice : ChainLatticeOps<FpInfPolicy> {
    [[nodiscard]] static constexpr FpInfPolicy bottom() noexcept { return FpInfPolicy::PropagateInfinity; }
    [[nodiscard]] static constexpr FpInfPolicy top() noexcept { return FpInfPolicy::FlushInfToFinite; }
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
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpInfPolicy::PropagateInfinity:
                    return "FpInfPolicyLattice::At<PropagateInfinity>";
                case FpInfPolicy::FlushInfToFinite:
                    return "FpInfPolicyLattice::At<FlushInfToFinite>";
                default:
                    return "FpInfPolicyLattice::At<?>";
            }
        }
    };
};

[[nodiscard]] consteval std::string_view fp_complex_layout_name(FpComplexLayout t) noexcept {
    switch (t) {
        case FpComplexLayout::Interleaved:
            return "Interleaved";
        case FpComplexLayout::SplitRealImag:
            return "SplitRealImag";
        case FpComplexLayout::SplitImagReal:
            return "SplitImagReal";
        default:
            return std::string_view{"<unknown FpComplexLayout>"};
    }
}

// The three layouts are mutually exclusive alternatives, not strictness tiers,
// so no semantic ordering exists between them.  This lattice nonetheless
// extends ChainLatticeOps because the carrier substrate requires a leq, and
// that leq compares the underlying ordinals only.  A leq that holds here does
// not mean one layout is admissible where the other is required.  The wrappers
// built on this lattice admit no cross-layout conversion at all.
struct FpComplexLayoutLattice : ChainLatticeOps<FpComplexLayout> {
    [[nodiscard]] static constexpr FpComplexLayout bottom() noexcept { return FpComplexLayout::Interleaved; }
    [[nodiscard]] static constexpr FpComplexLayout top() noexcept { return FpComplexLayout::SplitImagReal; }
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
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpComplexLayout::Interleaved:
                    return "FpComplexLayoutLattice::At<Interleaved>";
                case FpComplexLayout::SplitRealImag:
                    return "FpComplexLayoutLattice::At<SplitRealImag>";
                case FpComplexLayout::SplitImagReal:
                    return "FpComplexLayoutLattice::At<SplitImagReal>";
                default:
                    return "FpComplexLayoutLattice::At<?>";
            }
        }
    };
};

[[nodiscard]] consteval std::string_view fp_libm_policy_name(FpLibmPolicy t) noexcept {
    switch (t) {
        case FpLibmPolicy::ScalarLibm:
            return "ScalarLibm";
        case FpLibmPolicy::VectorLibmSleef:
            return "VectorLibmSleef";
        case FpLibmPolicy::VectorLibmSvml:
            return "VectorLibmSvml";
        case FpLibmPolicy::VectorLibmLibmvec:
            return "VectorLibmLibmvec";
        case FpLibmPolicy::FastApproxNv:
            return "FastApproxNv";
        case FpLibmPolicy::FastApproxAm:
            return "FastApproxAm";
        case FpLibmPolicy::Polynomial:
            return "Polynomial";
        default:
            return std::string_view{"<unknown FpLibmPolicy>"};
    }
}

// The libm policies are mutually exclusive vendor choices, not strictness
// tiers, so the leq below compares the underlying ordinals only and carries no
// semantic weight.  Recipe-tier eligibility is decided at the call site, never
// by asking this lattice whether one policy is below another.
struct FpLibmPolicyLattice : ChainLatticeOps<FpLibmPolicy> {
    [[nodiscard]] static constexpr FpLibmPolicy bottom() noexcept { return FpLibmPolicy::ScalarLibm; }
    [[nodiscard]] static constexpr FpLibmPolicy top() noexcept { return FpLibmPolicy::Polynomial; }
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
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpLibmPolicy::ScalarLibm:
                    return "FpLibmPolicyLattice::At<ScalarLibm>";
                case FpLibmPolicy::VectorLibmSleef:
                    return "FpLibmPolicyLattice::At<VectorLibmSleef>";
                case FpLibmPolicy::VectorLibmSvml:
                    return "FpLibmPolicyLattice::At<VectorLibmSvml>";
                case FpLibmPolicy::VectorLibmLibmvec:
                    return "FpLibmPolicyLattice::At<VectorLibmLibmvec>";
                case FpLibmPolicy::FastApproxNv:
                    return "FpLibmPolicyLattice::At<FastApproxNv>";
                case FpLibmPolicy::FastApproxAm:
                    return "FpLibmPolicyLattice::At<FastApproxAm>";
                case FpLibmPolicy::Polynomial:
                    return "FpLibmPolicyLattice::At<Polynomial>";
                default:
                    return "FpLibmPolicyLattice::At<?>";
            }
        }
    };
};

[[nodiscard]] consteval std::string_view fp_reassociate_name(FpReassociate t) noexcept {
    switch (t) {
        case FpReassociate::Forbidden:
            return "Forbidden";
        case FpReassociate::BoundedTreeDepth:
            return "BoundedTreeDepth";
        case FpReassociate::UnrestrictedRewrite:
            return "UnrestrictedRewrite";
        default:
            return std::string_view{"<unknown FpReassociate>"};
    }
}

struct FpReassociateLattice : ChainLatticeOps<FpReassociate> {
    [[nodiscard]] static constexpr FpReassociate bottom() noexcept { return FpReassociate::Forbidden; }
    [[nodiscard]] static constexpr FpReassociate top() noexcept { return FpReassociate::UnrestrictedRewrite; }
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
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpReassociate::Forbidden:
                    return "FpReassociateLattice::At<Forbidden>";
                case FpReassociate::BoundedTreeDepth:
                    return "FpReassociateLattice::At<BoundedTreeDepth>";
                case FpReassociate::UnrestrictedRewrite:
                    return "FpReassociateLattice::At<UnrestrictedRewrite>";
                default:
                    return "FpReassociateLattice::At<?>";
            }
        }
    };
};

[[nodiscard]] consteval std::string_view fp_constant_rounding_name(FpConstantRounding t) noexcept {
    switch (t) {
        case FpConstantRounding::SameAsRuntime:
            return "SameAsRuntime";
        case FpConstantRounding::AlwaysRTE:
            return "AlwaysRTE";
        case FpConstantRounding::AlwaysRTZ:
            return "AlwaysRTZ";
        default:
            return std::string_view{"<unknown FpConstantRounding>"};
    }
}

struct FpConstantRoundingLattice : ChainLatticeOps<FpConstantRounding> {
    [[nodiscard]] static constexpr FpConstantRounding bottom() noexcept { return FpConstantRounding::SameAsRuntime; }
    [[nodiscard]] static constexpr FpConstantRounding top() noexcept { return FpConstantRounding::AlwaysRTZ; }
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
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case FpConstantRounding::SameAsRuntime:
                    return "FpConstantRoundingLattice::At<SameAsRuntime>";
                case FpConstantRounding::AlwaysRTE:
                    return "FpConstantRoundingLattice::At<AlwaysRTE>";
                case FpConstantRounding::AlwaysRTZ:
                    return "FpConstantRoundingLattice::At<AlwaysRTZ>";
                default:
                    return "FpConstantRoundingLattice::At<?>";
            }
        }
    };
};

// Pinning a value's floating-point mode does not go through this composite.
// The wrappers nest one single-axis carrier per sub-axis instead, because that
// gives each axis a distinct hash contribution and so keeps the cache slots
// disjoint.  A single product-lattice carrier collapses all eleven axes into
// one contribution and cannot express that.  The composite is still the right
// answer for a consumer that must reason about the eleven axes as a lattice,
// such as taking the meet of two pinned modes.
using FpModeProductLattice = ::crucible::algebra::lattices::ProductLattice<
    FpRoundingLattice, FpFtzLattice, FpContractLattice, FpTrapMaskLattice, FpDenormalInputLattice, FpNanPolicyLattice,
    FpInfPolicyLattice, FpComplexLayoutLattice, FpLibmPolicyLattice, FpReassociateLattice, FpConstantRoundingLattice>;

static_assert(::crucible::algebra::Lattice<FpModeProductLattice>,
              "FpModeProductLattice must satisfy the Lattice concept "
              "(componentwise lift of 11 BoundedLattice chains).");
static_assert(::crucible::algebra::BoundedLattice<FpModeProductLattice>,
              "FpModeProductLattice must satisfy BoundedLattice — every component "
              "ChainLatticeOps<EnumT> publishes bottom() and top().");
static_assert(!::crucible::algebra::Semiring<FpModeProductLattice>,
              "FpModeProductLattice carries no ⊕/⊗ structure independent of "
              "join/meet — Semiring would be a falsehood at the type level.");
static_assert(FpModeProductLattice::arity == 11, "FpModeProductLattice must have arity 11 — one slot per FP sub-axis.");

namespace detail::fp_mode_lattice_self_test {

inline constexpr std::size_t rounding_count = std::meta::enumerators_of(^^FpRounding).size();
inline constexpr std::size_t ftz_count = std::meta::enumerators_of(^^FpFtz).size();
inline constexpr std::size_t contract_count = std::meta::enumerators_of(^^FpContract).size();
inline constexpr std::size_t trap_mask_count = std::meta::enumerators_of(^^FpTrapMask).size();
inline constexpr std::size_t denormal_input_count = std::meta::enumerators_of(^^FpDenormalInput).size();
inline constexpr std::size_t nan_policy_count = std::meta::enumerators_of(^^FpNanPolicy).size();
inline constexpr std::size_t inf_policy_count = std::meta::enumerators_of(^^FpInfPolicy).size();
inline constexpr std::size_t complex_layout_count = std::meta::enumerators_of(^^FpComplexLayout).size();
inline constexpr std::size_t libm_policy_count = std::meta::enumerators_of(^^FpLibmPolicy).size();
inline constexpr std::size_t reassociate_count = std::meta::enumerators_of(^^FpReassociate).size();
inline constexpr std::size_t fp_constant_count = std::meta::enumerators_of(^^FpConstantRounding).size();

static_assert(rounding_count == 5, "FpRounding diverged from {RTZ, RTN, RTP, RTE, RTNA} per IEEE 754 "
                                   "§4.3 + NV/AMD ISA extensions; confirm intent before changing.");
static_assert(ftz_count == 2, "FpFtz must be a 2-element chain {PreserveSubnormals, FlushToZero}; "
                              "expanding requires updating x86 MXCSR / ARM FPCR bit-decoders.");
static_assert(contract_count == 3, "FpContract diverged from {Off, OnInExpr, Fast}; matches GCC "
                                   "-ffp-contract={off, on, fast} surface.");
static_assert(trap_mask_count == 6, "FpTrapMask diverged from the IEEE 754 5-trap + AllMasked surface.");
static_assert(denormal_input_count == 2, "FpDenormalInput must be {HonorDenormals, DenormalsAreZero}; "
                                         "expanding requires updating x86 MXCSR.DAZ / ARM FPCR.FZ decoders.");
static_assert(nan_policy_count == 3, "FpNanPolicy diverged from {PropagateQuiet, PropagateSignalling, "
                                     "FastNaN}.");
static_assert(inf_policy_count == 2, "FpInfPolicy must be {PropagateInfinity, FlushInfToFinite}.");
static_assert(complex_layout_count == 3, "FpComplexLayout diverged from {Interleaved, SplitRealImag, "
                                         "SplitImagReal}.");
static_assert(libm_policy_count == 7, "FpLibmPolicy diverged from {ScalarLibm, VectorLibmSleef, "
                                      "VectorLibmSvml, VectorLibmLibmvec, FastApproxNv, FastApproxAm, "
                                      "Polynomial}.");
static_assert(reassociate_count == 3, "FpReassociate diverged from {Forbidden, BoundedTreeDepth, "
                                      "UnrestrictedRewrite}.");
static_assert(fp_constant_count == 3, "FpConstantRounding diverged from {SameAsRuntime, AlwaysRTE, "
                                      "AlwaysRTZ}.");

static_assert(!std::is_same_v<FpRounding, FpFtz>);
static_assert(!std::is_same_v<FpRounding, FpContract>);
static_assert(!std::is_same_v<FpFtz, FpDenormalInput>);
static_assert(!std::is_same_v<FpContract, FpReassociate>);
static_assert(!std::is_same_v<FpTrapMask, FpNanPolicy>);
static_assert(!std::is_same_v<FpNanPolicy, FpInfPolicy>);
static_assert(!std::is_same_v<FpComplexLayout, FpLibmPolicy>);
static_assert(!std::is_same_v<FpLibmPolicy, FpReassociate>);
static_assert(!std::is_same_v<FpRounding, FpConstantRounding>);

static_assert(std::to_underlying(FpRounding::RoundToZero) == 0);
static_assert(std::to_underlying(FpFtz::PreserveSubnormals) == 0);
static_assert(std::to_underlying(FpContract::Off) == 0);
static_assert(std::to_underlying(FpTrapMask::AllMasked) == 0);
static_assert(std::to_underlying(FpDenormalInput::HonorDenormals) == 0);
static_assert(std::to_underlying(FpNanPolicy::PropagateQuiet) == 0);
static_assert(std::to_underlying(FpInfPolicy::PropagateInfinity) == 0);
static_assert(std::to_underlying(FpComplexLayout::Interleaved) == 0);
static_assert(std::to_underlying(FpLibmPolicy::ScalarLibm) == 0);
static_assert(std::to_underlying(FpReassociate::Forbidden) == 0);
static_assert(std::to_underlying(FpConstantRounding::SameAsRuntime) == 0);

#define CRUCIBLE_FP_NAME_COVERAGE(SubAxis, NameFn, UnknownLit)                                              \
    [[nodiscard]] consteval bool every_##NameFn##_has_arm() noexcept {                                      \
        static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^SubAxis)); \
        _Pragma("GCC diagnostic push")                                                                      \
            _Pragma("GCC diagnostic ignored \"-Wshadow\"") template for (constexpr auto en : enumerators) { \
            if (NameFn([:en:]) == std::string_view{UnknownLit}) return false;                               \
        }                                                                                                   \
        _Pragma("GCC diagnostic pop") return true;                                                          \
    }                                                                                                       \
    static_assert(every_##NameFn##_has_arm(),                                                               \
                  #NameFn "() switch missing an arm for at least one " #SubAxis " enumerator.")

CRUCIBLE_FP_NAME_COVERAGE(FpRounding, fp_rounding_name, "<unknown FpRounding>");
CRUCIBLE_FP_NAME_COVERAGE(FpFtz, fp_ftz_name, "<unknown FpFtz>");
CRUCIBLE_FP_NAME_COVERAGE(FpContract, fp_contract_name, "<unknown FpContract>");
CRUCIBLE_FP_NAME_COVERAGE(FpTrapMask, fp_trap_mask_name, "<unknown FpTrapMask>");
CRUCIBLE_FP_NAME_COVERAGE(FpDenormalInput, fp_denormal_input_name, "<unknown FpDenormalInput>");
CRUCIBLE_FP_NAME_COVERAGE(FpNanPolicy, fp_nan_policy_name, "<unknown FpNanPolicy>");
CRUCIBLE_FP_NAME_COVERAGE(FpInfPolicy, fp_inf_policy_name, "<unknown FpInfPolicy>");
CRUCIBLE_FP_NAME_COVERAGE(FpComplexLayout, fp_complex_layout_name, "<unknown FpComplexLayout>");
CRUCIBLE_FP_NAME_COVERAGE(FpLibmPolicy, fp_libm_policy_name, "<unknown FpLibmPolicy>");
CRUCIBLE_FP_NAME_COVERAGE(FpReassociate, fp_reassociate_name, "<unknown FpReassociate>");
CRUCIBLE_FP_NAME_COVERAGE(FpConstantRounding, fp_constant_rounding_name, "<unknown FpConstantRounding>");

#undef CRUCIBLE_FP_NAME_COVERAGE

#define CRUCIBLE_FP_LATTICE_VERIFY(L)                                                                             \
    static_assert(Lattice<L>);                                                                                    \
    static_assert(BoundedLattice<L>);                                                                             \
    static_assert(!Semiring<L>);                                                                                  \
    static_assert(verify_chain_lattice_exhaustive<L>(), #L " chain-order lattice axioms failed at some triple."); \
    static_assert(verify_chain_lattice_distributive_exhaustive<L>(),                                              \
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

static_assert(FpRoundingLattice::bottom() == FpRounding::RoundToZero);
static_assert(FpRoundingLattice::top() == FpRounding::RoundToNearestAwayZero);
static_assert(FpFtzLattice::bottom() == FpFtz::PreserveSubnormals);
static_assert(FpFtzLattice::top() == FpFtz::FlushToZero);
static_assert(FpContractLattice::bottom() == FpContract::Off);
static_assert(FpContractLattice::top() == FpContract::Fast);
static_assert(FpTrapMaskLattice::bottom() == FpTrapMask::AllMasked);
static_assert(FpTrapMaskLattice::top() == FpTrapMask::UnmaskedInexact);
static_assert(FpDenormalInputLattice::bottom() == FpDenormalInput::HonorDenormals);
static_assert(FpDenormalInputLattice::top() == FpDenormalInput::DenormalsAreZero);
static_assert(FpNanPolicyLattice::bottom() == FpNanPolicy::PropagateQuiet);
static_assert(FpNanPolicyLattice::top() == FpNanPolicy::FastNaN);
static_assert(FpInfPolicyLattice::bottom() == FpInfPolicy::PropagateInfinity);
static_assert(FpInfPolicyLattice::top() == FpInfPolicy::FlushInfToFinite);
static_assert(FpComplexLayoutLattice::bottom() == FpComplexLayout::Interleaved);
static_assert(FpComplexLayoutLattice::top() == FpComplexLayout::SplitImagReal);
static_assert(FpLibmPolicyLattice::bottom() == FpLibmPolicy::ScalarLibm);
static_assert(FpLibmPolicyLattice::top() == FpLibmPolicy::Polynomial);
static_assert(FpReassociateLattice::bottom() == FpReassociate::Forbidden);
static_assert(FpReassociateLattice::top() == FpReassociate::UnrestrictedRewrite);
static_assert(FpConstantRoundingLattice::bottom() == FpConstantRounding::SameAsRuntime);
static_assert(FpConstantRoundingLattice::top() == FpConstantRounding::AlwaysRTZ);

static_assert(FpRoundingLattice::name() == std::string_view{"FpRoundingLattice"});
static_assert(FpFtzLattice::name() == std::string_view{"FpFtzLattice"});
static_assert(FpContractLattice::name() == std::string_view{"FpContractLattice"});
static_assert(FpTrapMaskLattice::name() == std::string_view{"FpTrapMaskLattice"});
static_assert(FpDenormalInputLattice::name() == std::string_view{"FpDenormalInputLattice"});
static_assert(FpNanPolicyLattice::name() == std::string_view{"FpNanPolicyLattice"});
static_assert(FpInfPolicyLattice::name() == std::string_view{"FpInfPolicyLattice"});
static_assert(FpComplexLayoutLattice::name() == std::string_view{"FpComplexLayoutLattice"});
static_assert(FpLibmPolicyLattice::name() == std::string_view{"FpLibmPolicyLattice"});
static_assert(FpReassociateLattice::name() == std::string_view{"FpReassociateLattice"});
static_assert(FpConstantRoundingLattice::name() == std::string_view{"FpConstantRoundingLattice"});

static_assert(FpRoundingLattice::leq(FpRounding::RoundToZero, FpRounding::RoundToNearestAwayZero));
static_assert(!FpRoundingLattice::leq(FpRounding::RoundToNearestAwayZero, FpRounding::RoundToZero));
static_assert(FpFtzLattice::leq(FpFtz::PreserveSubnormals, FpFtz::FlushToZero));
static_assert(!FpFtzLattice::leq(FpFtz::FlushToZero, FpFtz::PreserveSubnormals));
static_assert(FpContractLattice::leq(FpContract::Off, FpContract::Fast));
static_assert(!FpContractLattice::leq(FpContract::Fast, FpContract::Off));
static_assert(FpTrapMaskLattice::leq(FpTrapMask::AllMasked, FpTrapMask::UnmaskedInexact));
static_assert(!FpTrapMaskLattice::leq(FpTrapMask::UnmaskedInexact, FpTrapMask::AllMasked));
static_assert(FpDenormalInputLattice::leq(FpDenormalInput::HonorDenormals, FpDenormalInput::DenormalsAreZero));
static_assert(!FpDenormalInputLattice::leq(FpDenormalInput::DenormalsAreZero, FpDenormalInput::HonorDenormals));
static_assert(FpNanPolicyLattice::leq(FpNanPolicy::PropagateQuiet, FpNanPolicy::FastNaN));
static_assert(!FpNanPolicyLattice::leq(FpNanPolicy::FastNaN, FpNanPolicy::PropagateQuiet));
static_assert(FpInfPolicyLattice::leq(FpInfPolicy::PropagateInfinity, FpInfPolicy::FlushInfToFinite));
static_assert(!FpInfPolicyLattice::leq(FpInfPolicy::FlushInfToFinite, FpInfPolicy::PropagateInfinity));
static_assert(FpComplexLayoutLattice::leq(FpComplexLayout::Interleaved, FpComplexLayout::SplitImagReal));
static_assert(!FpComplexLayoutLattice::leq(FpComplexLayout::SplitImagReal, FpComplexLayout::Interleaved));
static_assert(FpLibmPolicyLattice::leq(FpLibmPolicy::ScalarLibm, FpLibmPolicy::FastApproxAm));
static_assert(!FpLibmPolicyLattice::leq(FpLibmPolicy::FastApproxAm, FpLibmPolicy::ScalarLibm));
static_assert(FpReassociateLattice::leq(FpReassociate::Forbidden, FpReassociate::UnrestrictedRewrite));
static_assert(!FpReassociateLattice::leq(FpReassociate::UnrestrictedRewrite, FpReassociate::Forbidden));
static_assert(FpConstantRoundingLattice::leq(FpConstantRounding::SameAsRuntime, FpConstantRounding::AlwaysRTZ));
static_assert(!FpConstantRoundingLattice::leq(FpConstantRounding::AlwaysRTZ, FpConstantRounding::SameAsRuntime));

// Chain polarity differs from the rest of the tree, and the difference is
// load-bearing.  Elsewhere the strictest element sits at the top, so composing
// two claims with join yields the strictest.  On nine of these eleven chains
// the bit-exact-safe element sits at the BOTTOM, so join yields the loosest
// behaviour of the two operands.  A consumer enforcing a bit-exact floor must
// compose those nine with meet, or compare against the floor with leq
// directly.  The two exceptions are FpRounding, whose strict element is the
// top, and FpComplexLayout, which has no strictness reading at all.
//
// The pins below witness the polarity of each chain, so a refactor that
// inverts one turns its assertion red.
static_assert(FpRoundingLattice::join(FpRounding::RoundToZero, FpRounding::RoundToNearestAwayZero)
                  == FpRounding::RoundToNearestAwayZero,
              "FpRoundingLattice JOIN must return the chain top "
              "(RoundToNearestAwayZero) — this chain is the one whose strictest "
              "element is the top.");
static_assert(FpRoundingLattice::meet(FpRounding::RoundToZero, FpRounding::RoundToNearestAwayZero)
                  == FpRounding::RoundToZero,
              "FpRoundingLattice MEET must return the chain bottom (RoundToZero).");

static_assert(FpFtzLattice::meet(FpFtz::PreserveSubnormals, FpFtz::FlushToZero) == FpFtz::PreserveSubnormals,
              "FpFtzLattice MEET must return PreserveSubnormals — the chain bottom "
              "is the bit-exact-safe element, so only MEET enforces the floor.");
static_assert(FpContractLattice::meet(FpContract::Off, FpContract::Fast) == FpContract::Off,
              "FpContractLattice MEET must return Off — the chain bottom is the "
              "bit-exact-safe element, so only MEET enforces the floor.");
static_assert(FpTrapMaskLattice::meet(FpTrapMask::AllMasked, FpTrapMask::UnmaskedInexact) == FpTrapMask::AllMasked,
              "FpTrapMaskLattice MEET must return AllMasked — the chain bottom is "
              "the deterministic-execution-safe element.");
static_assert(FpDenormalInputLattice::meet(FpDenormalInput::HonorDenormals, FpDenormalInput::DenormalsAreZero)
                  == FpDenormalInput::HonorDenormals,
              "FpDenormalInputLattice MEET must return HonorDenormals — the chain "
              "bottom is the IEEE-strict element.");
static_assert(FpNanPolicyLattice::meet(FpNanPolicy::PropagateQuiet, FpNanPolicy::FastNaN)
                  == FpNanPolicy::PropagateQuiet,
              "FpNanPolicyLattice MEET must return PropagateQuiet — the chain "
              "bottom is IEEE NaN propagation.");
static_assert(FpInfPolicyLattice::meet(FpInfPolicy::PropagateInfinity, FpInfPolicy::FlushInfToFinite)
                  == FpInfPolicy::PropagateInfinity,
              "FpInfPolicyLattice MEET must return PropagateInfinity — the chain "
              "bottom is IEEE infinity propagation.");
static_assert(FpLibmPolicyLattice::meet(FpLibmPolicy::ScalarLibm, FpLibmPolicy::FastApproxAm)
                  == FpLibmPolicy::ScalarLibm,
              "FpLibmPolicyLattice MEET must return ScalarLibm, the chain bottom.  "
              "The bit-stable policy is Polynomial, which sits at the chain top and "
              "is therefore not reachable by MEET.");
static_assert(FpReassociateLattice::meet(FpReassociate::Forbidden, FpReassociate::UnrestrictedRewrite)
                  == FpReassociate::Forbidden,
              "FpReassociateLattice MEET must return Forbidden — the chain bottom "
              "is the bit-exact-safe element.");
static_assert(FpConstantRoundingLattice::meet(FpConstantRounding::SameAsRuntime, FpConstantRounding::AlwaysRTZ)
                  == FpConstantRounding::SameAsRuntime,
              "FpConstantRoundingLattice MEET must return SameAsRuntime — the chain "
              "bottom is the element consistent with the runtime rounding mode.");

static_assert(FpComplexLayoutLattice::join(FpComplexLayout::Interleaved, FpComplexLayout::SplitImagReal)
                  == FpComplexLayout::SplitImagReal,
              "FpComplexLayoutLattice JOIN must return the ordinal maximum "
              "(SplitImagReal).  The order is ordinal-only, so this pins the "
              "encoding and carries no strictness reading.");
static_assert(FpComplexLayoutLattice::meet(FpComplexLayout::Interleaved, FpComplexLayout::SplitImagReal)
                  == FpComplexLayout::Interleaved,
              "FpComplexLayoutLattice MEET must return the ordinal minimum "
              "(Interleaved).  Polarity pin only, with no strictness reading.");

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

static_assert(!std::is_same_v<FpRoundingLattice, FpFtzLattice>);
static_assert(!std::is_same_v<FpFtzLattice, FpContractLattice>);
static_assert(!std::is_same_v<FpContractLattice, FpTrapMaskLattice>);
static_assert(!std::is_same_v<FpTrapMaskLattice, FpDenormalInputLattice>);
static_assert(!std::is_same_v<FpDenormalInputLattice, FpNanPolicyLattice>);
static_assert(!std::is_same_v<FpNanPolicyLattice, FpInfPolicyLattice>);
static_assert(!std::is_same_v<FpInfPolicyLattice, FpComplexLayoutLattice>);
static_assert(!std::is_same_v<FpComplexLayoutLattice, FpLibmPolicyLattice>);
static_assert(!std::is_same_v<FpLibmPolicyLattice, FpReassociateLattice>);
static_assert(!std::is_same_v<FpReassociateLattice, FpConstantRoundingLattice>);

// Calling each operation on runtime operands catches the defects the
// compile-time assertions above cannot see, such as an inline body that only
// ever instantiates in a consteval context.
inline void fp_mode_lattice_runtime_smoke_test() {
    FpRounding ra = FpRounding::RoundToZero;
    FpRounding rb = FpRounding::RoundToNearestAwayZero;
    [[maybe_unused]] bool rl1 = FpRoundingLattice::leq(ra, rb);
    [[maybe_unused]] FpRounding rj1 = FpRoundingLattice::join(ra, rb);
    [[maybe_unused]] FpRounding rm1 = FpRoundingLattice::meet(ra, rb);

    FpFtz fa = FpFtz::PreserveSubnormals;
    FpFtz fb = FpFtz::FlushToZero;
    [[maybe_unused]] FpFtz fj1 = FpFtzLattice::join(fa, fb);
    [[maybe_unused]] FpFtz fm1 = FpFtzLattice::meet(fa, fb);

    FpContract ca = FpContract::Off;
    FpContract cb = FpContract::Fast;
    [[maybe_unused]] FpContract cj1 = FpContractLattice::join(ca, cb);

    FpTrapMask ta = FpTrapMask::AllMasked;
    FpTrapMask tb = FpTrapMask::UnmaskedInexact;
    [[maybe_unused]] FpTrapMask tj1 = FpTrapMaskLattice::join(ta, tb);

    FpDenormalInput da = FpDenormalInput::HonorDenormals;
    FpDenormalInput db = FpDenormalInput::DenormalsAreZero;
    [[maybe_unused]] FpDenormalInput dj1 = FpDenormalInputLattice::join(da, db);

    FpNanPolicy na = FpNanPolicy::PropagateQuiet;
    FpNanPolicy nb = FpNanPolicy::FastNaN;
    [[maybe_unused]] FpNanPolicy nj1 = FpNanPolicyLattice::join(na, nb);

    FpInfPolicy ia = FpInfPolicy::PropagateInfinity;
    FpInfPolicy ib = FpInfPolicy::FlushInfToFinite;
    [[maybe_unused]] FpInfPolicy ij1 = FpInfPolicyLattice::join(ia, ib);

    FpComplexLayout xa = FpComplexLayout::Interleaved;
    FpComplexLayout xb = FpComplexLayout::SplitImagReal;
    [[maybe_unused]] FpComplexLayout xj1 = FpComplexLayoutLattice::join(xa, xb);

    FpLibmPolicy la = FpLibmPolicy::ScalarLibm;
    FpLibmPolicy lb = FpLibmPolicy::Polynomial;
    [[maybe_unused]] FpLibmPolicy lj1 = FpLibmPolicyLattice::join(la, lb);

    FpReassociate ea = FpReassociate::Forbidden;
    FpReassociate eb = FpReassociate::UnrestrictedRewrite;
    [[maybe_unused]] FpReassociate ej1 = FpReassociateLattice::join(ea, eb);

    FpConstantRounding ka = FpConstantRounding::SameAsRuntime;
    FpConstantRounding kb = FpConstantRounding::AlwaysRTZ;
    [[maybe_unused]] FpConstantRounding kj1 = FpConstantRoundingLattice::join(ka, kb);

    FpRoundingLattice::At<FpRounding::RoundToNearestEven>::element_type rte_pin{};
    [[maybe_unused]] FpRounding rte_recovered = rte_pin;

    FpFtzLattice::At<FpFtz::FlushToZero>::element_type ftz_pin{};
    [[maybe_unused]] FpFtz ftz_recovered = ftz_pin;
}

}  // namespace detail::fp_mode_lattice_self_test

}  // namespace crucible::algebra::lattices
