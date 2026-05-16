#pragma once

// ── crucible::cog — CostProjection.h (FIXY-G11) ──────────────────────
//
// Per-Cog cost projection.  Given a fixy binding F carrying a
// `cg::cost_polynomial<...>` grant on dim::Cost AND a target Cog
// (CPU / NV-H100 / AM-MI300X / TPU-v4 / ...), returns the predicted
// wallclock-nanos for executing the binding at a given input size.
//
// Projection uses placeholder calibration constants here (one constant
// per CogKind atom).  Real per-Cog calibration data lives in
// cog/OpcodeLatencyTable.h and feeds in via the per-Cog Calibrate.h
// startup probe; this header ships the projection MECHANISM with
// sensible defaults so the cost-aware bindings compile and the
// downstream R015 / cost-budget checks have a working oracle.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   enum class CostModelError      — { Missing, Unbounded, Overflow }
//
//   template <CogKind K>
//   inline constexpr std::uint64_t cog_cost_multiplier_v
//                                  — placeholder per-Cog multiplier.
//
//   template <typename F, CogKind K, std::uint64_t InputSize>
//   inline constexpr std::uint64_t predicted_cost_v
//                                  — multiplied + evaluated cost in ns.
//
// ── Per-Cog multiplier scale ────────────────────────────────────────
//
// Multiplier = "how many cost units per Cog-tick relative to the
// polynomial's base unit".  Lower = faster Cog.  Stub values:
//
//   Gpu (NV-H100):    1     — fastest baseline
//   GpuPackage:       1
//   CpuCore (Zen4):  10     — ~10x slower per-op than tensor core
//   CpuSocket:       10
//   Default:        100     — conservative for unknown Cogs
//
// Real per-Cog calibration runs at startup and overwrites these via
// OpcodeLatencyTable specializations.
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §6 Phase G    — G11 per-Cog projection
//   algebra/CostSemiring.h                — CostPolynomial + evaluate_v
//   fixy/dim/Cost.h                       — cost grants + extraction
//   cog/CogIdentity.h                     — CogKind taxonomy

#include <crucible/algebra/CostSemiring.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/fixy/dim/Cost.h>

#include <cstdint>

namespace crucible::cog {

// ═════════════════════════════════════════════════════════════════════
// ── CostModelError — projection failure modes ──────────────────────
// ═════════════════════════════════════════════════════════════════════

enum class CostModelError : std::uint8_t {
    Missing   = 0,  // no calibration data for this Cog
    Unbounded = 1,  // binding carries cost_unknown
    Overflow  = 2,  // saturation hit UINT64_MAX
};

// ═════════════════════════════════════════════════════════════════════
// ── cog_cost_multiplier_v — per-Cog placeholder calibration ────────
// ═════════════════════════════════════════════════════════════════════

template <CogKind K>
inline constexpr std::uint64_t cog_cost_multiplier_v = 100;  // conservative default

template <> inline constexpr std::uint64_t cog_cost_multiplier_v<CogKind::Gpu>         = 1;
template <> inline constexpr std::uint64_t cog_cost_multiplier_v<CogKind::GpuPackage>  = 1;
template <> inline constexpr std::uint64_t cog_cost_multiplier_v<CogKind::CpuCore>     = 10;
template <> inline constexpr std::uint64_t cog_cost_multiplier_v<CogKind::CpuSocket>   = 10;
template <> inline constexpr std::uint64_t cog_cost_multiplier_v<CogKind::NicPort>     = 50;
template <> inline constexpr std::uint64_t cog_cost_multiplier_v<CogKind::NicCard>     = 50;
template <> inline constexpr std::uint64_t cog_cost_multiplier_v<CogKind::DramChannel> = 30;
template <> inline constexpr std::uint64_t cog_cost_multiplier_v<CogKind::NvSwitch>    = 5;

// ═════════════════════════════════════════════════════════════════════
// ── predicted_cost_v<F, K, InputSize> ──────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Concrete nanos prediction for binding F on Cog kind K at input size.
// Returns UINT64_MAX if the binding's cost polynomial is unbounded.

namespace detail {

[[nodiscard]] consteval std::uint64_t sat_mul64(std::uint64_t a, std::uint64_t b) noexcept {
    if (a == 0 || b == 0) return 0;
    if (a == UINT64_MAX || b == UINT64_MAX) return UINT64_MAX;
    if (a > UINT64_MAX / b) return UINT64_MAX;
    return a * b;
}

}  // namespace detail

template <typename F, CogKind K, std::uint64_t InputSize>
inline constexpr std::uint64_t predicted_cost_v = []() {
    using poly = ::crucible::fixy::fn_cost_polynomial_t<F>;
    constexpr std::uint64_t base_ns =
        ::crucible::algebra::evaluate_v<poly, InputSize>;
    constexpr std::uint64_t mult = cog_cost_multiplier_v<K>;
    return detail::sat_mul64(base_ns, mult);
}();

// ═════════════════════════════════════════════════════════════════════
// ── has_calibration_v<K> — true iff Cog has a non-default mult ─────
// ═════════════════════════════════════════════════════════════════════
//
// Used by R015's hot-path admission check.  Bindings flowing to a Cog
// without calibration data trigger CostModelError::Missing.

template <CogKind K>
inline constexpr bool has_calibration_v = (cog_cost_multiplier_v<K> != 100);

// ═════════════════════════════════════════════════════════════════════
// ── Self-tests ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace cost_projection_self_test {

namespace cg = ::crucible::fixy::grant;
namespace cf = ::crucible::fixy;
namespace cd = ::crucible::fixy::dim;

// Synthetic Cost-engaging binding for the static_assert below.  Built
// directly as a fixy::fn with all 20 standard axes engaged + the
// Cost-engaging grant (which is axis #21; IsAccepted doesn't gate on
// it, so the binding compiles).
using LinearBinding = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>,
    cg::cost_polynomial<5, 3>>;  // 5 + 3·n nanos

static_assert(cf::HasCostGrant<LinearBinding>);

// Cost(n=10) base = 5 + 3*10 = 35.  On Gpu (mult=1) → 35 ns.
static_assert(predicted_cost_v<LinearBinding, CogKind::Gpu, 10> == 35);
// On CpuCore (mult=10) → 350 ns.
static_assert(predicted_cost_v<LinearBinding, CogKind::CpuCore, 10> == 350);

// Cost(n=100): base = 5 + 300 = 305.  Gpu → 305; CpuCore → 3050.
static_assert(predicted_cost_v<LinearBinding, CogKind::Gpu, 100> == 305);
static_assert(predicted_cost_v<LinearBinding, CogKind::CpuCore, 100> == 3050);

// Calibration coverage.
static_assert(has_calibration_v<CogKind::Gpu>);
static_assert(has_calibration_v<CogKind::CpuCore>);
static_assert(!has_calibration_v<CogKind::OpticalTransceiver>);  // default 100

}  // namespace cost_projection_self_test

}  // namespace crucible::cog
