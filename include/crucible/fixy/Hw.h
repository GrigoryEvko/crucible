#pragma once

// ── crucible::fixy::hw — hardware-instruction grant surface (FIXY-V-257) ─
//
// The fixy band-3 surface for Agent 11's three hardware axes (V-253):
// HwInstruction (V-251 lattice), BarrierStrength (V-252 lattice), and
// SimdIsa (V-250 lattice).  Ten grant tag families let a `fixy::fn<...>`
// binding DECLARE which hardware-instruction class it issues, and five
// §XXI ctx-bound mint factories synthesize the grants that require an
// authorization step (a CPU-pin proof, a Root permission, a non-empty
// rationale, or a sanctioned width).
//
// ── The ten grant tag families (all final : grant_base, EBO = 0) ──────
//
//   grant::hw::cache<CacheOp, Locality>     → HwInstruction
//   grant::hw::barrier<BarrierArch, Kind>   → BarrierStrength
//   grant::hw::tsc<TscMode>                 → HwInstruction
//   grant::hw::rng<RngSource>               → HwInstruction
//   grant::hw::cpuid<Leaf>                  → HwInstruction (sanctioned leaves)
//   grant::hw::msr<MsrId>                   → HwInstruction (privileged)
//   grant::hw::port_io<Port>                → HwInstruction (privileged)
//   grant::hw::asm_<Reason>                 → HwInstruction (rationale-bearing)
//   grant::hw::simd_width<WidthBits>        → SimdIsa
//   grant::hw::vendor_intrinsic<Backend, Id>→ Representation
//
// Why the grant tags live in `crucible::fixy::grant::hw` (NOT
// `crucible::fixy::hw`): Grant.h's namespace-purity discipline (CR-09)
// requires every `which_dim` specialization to live syntactically inside
// `namespace crucible::fixy::grant`.  This header reopens that namespace
// for the routing specializations; `scripts/check-fixy-grant-namespace-
// purity.sh` allowlists Hw.h alongside Fp.h / Fs.h / grant/Ctrl.h.  The
// `grant::hw` sub-namespace open is NOT the locked namespace and needs no
// allowlist entry — exactly the precedent set by `grant::fs` / `grant::ctrl`.
//
// ── The Pause hint is BLESSED — no grant required ─────────────────────
//
// `CRUCIBLE_SPIN_PAUSE` (Platform.h) stays universally blessed: it
// expands to `_mm_pause()` on x86 and `yield` on ARM, already
// arch-bracketed at the macro definition.  It adds no latency, issues no
// privileged or non-deterministic instruction, and is the canonical
// hot-wait primitive (CLAUDE.md §IX).  A greenfield spin loop uses
// `CRUCIBLE_SPIN_PAUSE` directly and carries NO `grant::hw::*` tag.
//
// ── Five §XXI ctx-bound mint factories (CLAUDE.md §XXI) ───────────────
//
//   mint_asm_grant<Reason>(ctx)                  → asm_<Reason>
//   mint_simd_width<WidthBits>(ctx)              → simd_width<WidthBits>
//   mint_vendor_intrinsic<Id, Backend>(ctx)      → vendor_intrinsic<Backend, Id>
//   mint_tsc_grant<Mode>(ctx, CpuPinProof)       → tsc<Mode>
//   mint_msr_grant<MsrId>(ctx, Permission<root>) → msr<MsrId>
//
// Each is `[[nodiscard]] constexpr noexcept`, takes `Ctx const&` first,
// and gates on ONE concept (`CtxFits*Mint`) per §XXI's single-concept
// rule.  The mint's `requires`-clause rejection IS the diagnostic surface
// — there is no runtime `diag::Category` emission because a failed mint
// never executes (it fails to compile).  The conceptual "FixyHwGrant*"
// categorization maps to the named concepts below; no closed-enum
// `safety::diag::Category` entry is added (matching the Fs.h / Ctrl.h /
// Fp.h precedent — none of the sibling grant headers touch the closed
// Category enum, which is reserved for the foundation's wrapper axes).
//
// ── The two privileged-tier proof tokens ─────────────────────────────
//
//   CpuPinProof  — phantom witness that the caller pinned the current
//                  thread to a single core before reading the TSC, so
//                  rdtscp is at least core-stable.  V-187 (safety/
//                  CpuPinned.h) will replace this shim with the real
//                  affinity-witnessing token via a zero-churn using-alias.
//   root         — the privileged-capability tag consumed (as
//                  `safety::Permission<root>&&`) by mint_msr_grant.  V-260's
//                  H003 collision rule couples this to warden::tag::Root.
//
// ── Forward-compat with V-258 / V-259 ─────────────────────────────────
//
// `simd_width<WidthBits>` and `vendor_intrinsic<Backend, Id>` are the
// canonical grant tags minted here (V-257 ships their mints).  V-259
// (fixy/Simd.h: `width<WidthBits>` + width_scalar/128/256/512 aliases)
// and V-258 (fixy/Vendor.h: `intrinsic<V, I>` + 7 canonical aliases) add
// the richer enum + alias surface and re-export these tags via zero-churn
// using-aliases (feedback_promote_first_pattern: ship the canonical tag
// now, generalize later).
//
// ── Axiom coverage (CLAUDE.md §II) ────────────────────────────────────
//
//   InitSafe   — every tag is `final` empty struct, NSDMI-trivial;
//                proof tokens are zero-state; no uninit output.
//   TypeSafe   — strong scoped enums for cache op / barrier arch / tsc
//                mode / rng source; NTTP-typed leaf / port / width /
//                rationale; cross-axis mixing is a compile error.
//   NullSafe   — no raw pointer surface; mints return value-type grants.
//   MemSafe    — mint_msr_grant CONSUMES the Permission<root> by rvalue-ref
//                (linearity); no aliased authority.
//   BorrowSafe — proof tokens are pass-by-value witnesses; no shared state.
//   ThreadSafe — every factory is pure / stateless / constexpr.
//   LeakSafe   — zero-state tags + tokens; no resources to leak.
//   DetSafe    — same grants + same ctx → same tag types on any platform;
//                the tsc family is the explicit NON-deterministic carrier
//                (couples to a DetSafe downgrade downstream).
//
// ── HS14 fixtures (≥2 per NEW mint → 10 fixtures in test/fixy_neg/) ───
//
//   mint_asm_grant       : empty-rationale          + non-ctx
//   mint_simd_width      : invalid-width            + non-ctx
//   mint_vendor_intrinsic: empty-id                 + non-ctx
//   mint_tsc_grant       : Mode==NotAllowed         + missing CpuPinProof
//   mint_msr_grant       : missing Permission<root> + wrong-tag Permission

#include <crucible/fixy/Grant.h>                          // grant_base, which_dim, IsGrantTag
#include <crucible/fixy/Dim.h>                            // dim::DimensionAxis
#include <crucible/fixy/grant/Ctrl.h>                     // ctrl::rationale (fixed-string NTTP)

#include <crucible/algebra/lattices/BarrierStrengthLattice.h>  // BarrierStrength
#include <crucible/safety/Vendor.h>                       // VendorBackend_v
#include <crucible/permissions/Permission.h>              // safety::Permission

#include <crucible/effects/ExecCtx.h>                     // effects::IsExecCtx

#include <cstdint>
#include <type_traits>

namespace crucible::fixy::hw {

// ═════════════════════════════════════════════════════════════════════
// ── Typed axis vocabulary ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Cache-instruction operation class (clflush / clflushopt / clwb /
// clinvalidate / prefetch).  Each maps to a non-privileged, ring-3
// cache-control instruction; Locality 0-3 is the temporal hint for
// prefetch (0 = no temporal locality / streaming, 3 = high reuse).
enum class CacheOp : std::uint8_t {
    Flush,       // clflush     — flush + invalidate a line
    FlushOpt,    // clflushopt  — weakly-ordered flush (faster)
    Writeback,   // clwb        — write-back, keep line valid
    Invalidate,  // clinvalidate-class
    Prefetch,    // prefetcht0..nta — speculative fetch
};

// Barrier-instruction arch family.  The fence KIND is a BarrierStrength
// tier (V-252); the arch picks which concrete instruction realizes it.
enum class BarrierArch : std::uint8_t {
    X86,       // lfence / sfence / mfence
    Arm,       // dmb ish / dmb ld / dmb st
    Compiler,  // asm volatile("":::"memory") + std::atomic ordering
};

// TSC-read posture.  NotAllowed is the strict default; SerializedPinned
// (rdtscp + lfence with a CpuPinProof) is the only hot-path-admissible
// form; Raw is bench-only (NEVER hot path); SteadyClockFallback measures
// a DIFFERENT QUANTITY (monotonic wall-ns, not cycles) and so is a
// distinct stance, not a drop-in substitute.
enum class TscMode : std::uint8_t {
    NotAllowed,           // strict default — no TSC read at all
    SerializedPinned,     // rdtscp + lfence, requires CpuPinProof
    Raw,                  // rdtsc, bench-only, non-serialized
    SteadyClockFallback,  // chrono::steady_clock — different quantity
};

// Randomness source.  NotAllowed is the strict default; PhiloxCounter is
// the safe deterministic counter-based RNG (the only DetSafe-clean
// source); the OS / hardware sources are non-deterministic entropy reads.
enum class RngSource : std::uint8_t {
    NotAllowed,     // strict default — no randomness source
    PhiloxCounter,  // Philox4x32 counter-based (deterministic, DetSafe)
    OsGetrandom,    // ::getrandom(2) — OS CSPRNG
    RdRand,         // rdrand — on-die DRBG
    RdSeed,         // rdseed — on-die entropy source
};

// Re-export of the V-252 BarrierStrength tier into the hw namespace so a
// `barrier<Arch, Kind>` grant cites the canonical lattice enum.
using BarrierStrength = ::crucible::algebra::lattices::BarrierStrength;

// Re-export of the V-250-lineage VendorBackend enum for vendor_intrinsic.
using VendorBackend = ::crucible::safety::VendorBackend_v;

// ── Validity predicates (TypeSafe gates folded into the grants/mints) ─

// SIMD width must be one of {scalar, 128, 256, 512}-bit register classes.
template <std::uint16_t WidthBits>
inline constexpr bool valid_simd_width_v =
    (WidthBits == 0 || WidthBits == 128 || WidthBits == 256 || WidthBits == 512);

// cpuid leaf allow-list — the sanctioned, side-effect-free leaves used
// for capability detection.  An unsanctioned leaf (e.g. a vendor-specific
// debug leaf) rejects at the grant template-id.
template <std::uint32_t Leaf>
inline constexpr bool is_sanctioned_cpuid_leaf_v =
       Leaf == 0x00000000u  // max basic leaf + vendor string
    || Leaf == 0x00000001u  // feature flags (SSE/AVX/...)
    || Leaf == 0x00000007u  // extended features (AVX2/AVX512/...)
    || Leaf == 0x0000000Bu  // x2APIC topology
    || Leaf == 0x0000000Du  // XSAVE / extended state
    || Leaf == 0x00000016u  // CPU frequency
    || Leaf == 0x80000000u  // max extended leaf
    || Leaf == 0x80000001u  // extended feature flags
    || Leaf == 0x80000002u  // brand string part 1
    || Leaf == 0x80000003u  // brand string part 2
    || Leaf == 0x80000004u  // brand string part 3
    || Leaf == 0x80000008u; // address sizes

// A rationale carries audit identity iff it holds at least one real
// character (N includes the trailing NUL, so empty `""` has size 1).
template <::crucible::fixy::grant::ctrl::rationale Reason>
inline constexpr bool rationale_nonempty_v = (Reason.size() > 1);

// ═════════════════════════════════════════════════════════════════════
// ── Privileged-tier proof tokens ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// CpuPinProof — phantom witness consumed by mint_tsc_grant.  Its
// presence in the signature forces the caller to ACKNOWLEDGE the
// affinity requirement (a TSC read on an unpinned thread can migrate
// cores and read a different counter).  V-187 (safety/CpuPinned.h) will
// replace this shim with the real affinity-witnessing token via a
// zero-churn using-alias; until then it is a deliberate, grep-discoverable
// marker (`CpuPinProof{}` at the call site).
struct CpuPinProof final {
    constexpr CpuPinProof() noexcept = default;
};

// root — the privileged-capability tag.  mint_msr_grant consumes a
// `safety::Permission<root>` by rvalue-ref (linearity): minting an MSR /
// port-IO grant SPENDS the Root authority.  V-260's H003 collision rule
// couples this to warden::tag::Root.
struct root {};

}  // namespace crucible::fixy::hw

// ═════════════════════════════════════════════════════════════════════
// ── grant tag families (crucible::fixy::grant::hw) ────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::grant::hw {

namespace fh = ::crucible::fixy::hw;

// (1) cache<Op, Locality> — cache-control instruction (HwInstruction).
template <fh::CacheOp Op, int Locality = 0>
    requires (Locality >= 0 && Locality <= 3)
struct cache final : grant_base {};

// (2) barrier<Arch, Kind> — fence instruction (BarrierStrength).
template <fh::BarrierArch Arch, fh::BarrierStrength Kind>
struct barrier final : grant_base {};

// (3) tsc<Mode> — timestamp-counter read posture (HwInstruction).
template <fh::TscMode Mode>
struct tsc final : grant_base {};

// (4) rng<Source> — randomness source (HwInstruction).
template <fh::RngSource Source>
struct rng final : grant_base {};

// (5) cpuid<Leaf> — sanctioned cpuid leaf (HwInstruction).
template <std::uint32_t Leaf>
    requires fh::is_sanctioned_cpuid_leaf_v<Leaf>
struct cpuid final : grant_base {};

// (6) msr<MsrId> — privileged model-specific-register access (HwInstruction).
template <std::uint32_t MsrId>
struct msr final : grant_base {};

// (7) port_io<Port> — privileged IN/OUT port I/O (HwInstruction).
template <std::uint16_t Port>
struct port_io final : grant_base {};

// (8) asm_<Reason> — inline-asm site with mandatory rationale (HwInstruction).
template <ctrl::rationale Reason>
struct asm_ final : grant_base {};

// (9) simd_width<WidthBits> — SIMD register-width pin (SimdIsa).
template <std::uint16_t WidthBits>
    requires fh::valid_simd_width_v<WidthBits>
struct simd_width final : grant_base {};

// (10) vendor_intrinsic<Backend, Id> — vendor-pinned intrinsic (Representation).
template <fh::VendorBackend Backend, ctrl::rationale Id>
struct vendor_intrinsic final : grant_base {};

}  // namespace crucible::fixy::grant::hw

// ── which_dim routing — CR-09 locked namespace ───────────────────────

namespace crucible::fixy::grant {

namespace fh = ::crucible::fixy::hw;

template <fh::CacheOp Op, int Locality>
struct which_dim<hw::cache<Op, Locality>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::HwInstruction> {};

template <fh::BarrierArch Arch, fh::BarrierStrength Kind>
struct which_dim<hw::barrier<Arch, Kind>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::BarrierStrength> {};

template <fh::TscMode Mode>
struct which_dim<hw::tsc<Mode>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::HwInstruction> {};

template <fh::RngSource Source>
struct which_dim<hw::rng<Source>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::HwInstruction> {};

template <std::uint32_t Leaf>
    requires fh::is_sanctioned_cpuid_leaf_v<Leaf>
struct which_dim<hw::cpuid<Leaf>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::HwInstruction> {};

template <std::uint32_t MsrId>
struct which_dim<hw::msr<MsrId>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::HwInstruction> {};

template <std::uint16_t Port>
struct which_dim<hw::port_io<Port>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::HwInstruction> {};

template <ctrl::rationale Reason>
struct which_dim<hw::asm_<Reason>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::HwInstruction> {};

template <std::uint16_t WidthBits>
    requires fh::valid_simd_width_v<WidthBits>
struct which_dim<hw::simd_width<WidthBits>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SimdIsa> {};

template <fh::VendorBackend Backend, ctrl::rationale Id>
struct which_dim<hw::vendor_intrinsic<Backend, Id>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Representation> {};

// ── Engagement markers for the three hw axes ──────────────────────────
using accept_default_strict_for_HwInstruction =
    accept_default_strict_for<dim::DimensionAxis::HwInstruction>;
using accept_default_strict_for_BarrierStrength =
    accept_default_strict_for<dim::DimensionAxis::BarrierStrength>;
using accept_default_strict_for_SimdIsa =
    accept_default_strict_for<dim::DimensionAxis::SimdIsa>;

}  // namespace crucible::fixy::grant

// ═════════════════════════════════════════════════════════════════════
// ── Canonical aliases + the five §XXI mint factories ──────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::hw {

namespace ghw = ::crucible::fixy::grant::hw;

// ── Cache aliases (the common hot-path cache-control ops) ─────────────
using cache_prefetch_rw_t0 = ghw::cache<CacheOp::Prefetch, 0>;  // streaming prefetch
using cache_clflushopt     = ghw::cache<CacheOp::FlushOpt, 0>;  // weakly-ordered flush
using cache_clwb           = ghw::cache<CacheOp::Writeback, 0>; // write-back, keep valid

// ── Barrier aliases (per-arch fence kinds) ────────────────────────────
using barrier_x86_lfence        = ghw::barrier<BarrierArch::X86, BarrierStrength::AcquireLoad>;
using barrier_x86_sfence        = ghw::barrier<BarrierArch::X86, BarrierStrength::ReleaseStore>;
using barrier_x86_mfence        = ghw::barrier<BarrierArch::X86, BarrierStrength::FullFence>;
using barrier_arm_dmb_ish       = ghw::barrier<BarrierArch::Arm, BarrierStrength::FullFence>;
using barrier_arm_dmb_ld        = ghw::barrier<BarrierArch::Arm, BarrierStrength::AcquireLoad>;
using barrier_arm_dmb_st        = ghw::barrier<BarrierArch::Arm, BarrierStrength::ReleaseStore>;
using barrier_compiler_portable = ghw::barrier<BarrierArch::Compiler, BarrierStrength::CompilerBarrier>;
using barrier_compiler_acquire  = ghw::barrier<BarrierArch::Compiler, BarrierStrength::AcquireLoad>;
using barrier_compiler_release  = ghw::barrier<BarrierArch::Compiler, BarrierStrength::ReleaseStore>;
using barrier_compiler_seqcst   = ghw::barrier<BarrierArch::Compiler, BarrierStrength::SeqCst>;

// ── §XXI ctx-fit concepts — ONE concept per mint ─────────────────────

// Base: a valid ExecCtx is the floor for every hw-grant mint.
template <typename Ctx>
concept CtxFitsHwGrant = ::crucible::effects::IsExecCtx<Ctx>;

// asm_ requires a non-empty rationale (every greenfield asm site MUST
// document WHY it drops to inline assembly).
template <typename Ctx, ::crucible::fixy::grant::ctrl::rationale Reason>
concept CtxFitsAsmMint =
    CtxFitsHwGrant<Ctx> && rationale_nonempty_v<Reason>;

// simd_width requires a recognized register-width class.
template <typename Ctx, std::uint16_t WidthBits>
concept CtxFitsSimdWidthMint =
    CtxFitsHwGrant<Ctx> && valid_simd_width_v<WidthBits>;

// vendor_intrinsic requires a non-empty intrinsic mnemonic.
template <typename Ctx, ::crucible::fixy::grant::ctrl::rationale Id>
concept CtxFitsVendorIntrinsicMint =
    CtxFitsHwGrant<Ctx> && rationale_nonempty_v<Id>;

// tsc requires a non-strict-default posture (you do NOT mint a grant for
// "no TSC read").  The CpuPinProof argument carries the affinity witness.
template <typename Ctx, TscMode Mode>
concept CtxFitsTscMint =
    CtxFitsHwGrant<Ctx> && (Mode != TscMode::NotAllowed);

// msr is privileged: ctx fit + the consumed Permission<root> (the
// rvalue-ref parameter is the load-bearing authority gate).
template <typename Ctx>
concept CtxFitsMsrMint = CtxFitsHwGrant<Ctx>;

// ── mint_asm_grant<Reason>(ctx) → grant::hw::asm_<Reason> ─────────────
template <::crucible::fixy::grant::ctrl::rationale Reason,
          ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsAsmMint<Ctx, Reason>
[[nodiscard]] constexpr ghw::asm_<Reason> mint_asm_grant(Ctx const&) noexcept {
    return {};
}

// ── mint_simd_width<WidthBits>(ctx) → grant::hw::simd_width<WidthBits> ─
template <std::uint16_t WidthBits, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsSimdWidthMint<Ctx, WidthBits>
[[nodiscard]] constexpr ghw::simd_width<WidthBits> mint_simd_width(Ctx const&) noexcept {
    return {};
}

// ── mint_vendor_intrinsic<Id, Backend>(ctx) → vendor_intrinsic<Backend, Id> ─
//
// Param order matches the §3.8 spec `<I, V, Ctx>` (Intrinsic id first,
// Vendor backend second); the grant tag stores them Backend-first
// because the backend identifies the Representation-axis position.
template <::crucible::fixy::grant::ctrl::rationale Id,
          VendorBackend Backend,
          ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsVendorIntrinsicMint<Ctx, Id>
[[nodiscard]] constexpr ghw::vendor_intrinsic<Backend, Id>
mint_vendor_intrinsic(Ctx const&) noexcept {
    return {};
}

// ── mint_tsc_grant<Mode>(ctx, CpuPinProof) → grant::hw::tsc<Mode> ─────
template <TscMode Mode, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsTscMint<Ctx, Mode>
[[nodiscard]] constexpr ghw::tsc<Mode> mint_tsc_grant(Ctx const&, CpuPinProof) noexcept {
    return {};
}

// ── mint_msr_grant<MsrId>(ctx, Permission<root>&&) → grant::hw::msr<MsrId> ─
//
// CONSUMES the Root permission by rvalue-ref: minting an MSR grant spends
// the privileged authority (linearity — the token cannot be re-used).
template <std::uint32_t MsrId, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsMsrMint<Ctx>
[[nodiscard]] constexpr ghw::msr<MsrId>
mint_msr_grant(Ctx const&, ::crucible::safety::Permission<root>&&) noexcept {
    return {};
}

}  // namespace crucible::fixy::hw

// ═════════════════════════════════════════════════════════════════════
// ── Self-test (compile-time) ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::hw::detail::v257_self_test {

namespace ghw = ::crucible::fixy::grant::hw;
using ::crucible::fixy::grant::IsGrantTag;
using ::crucible::fixy::grant::which_dim_v;
using D = ::crucible::fixy::dim::DimensionAxis;
namespace eff = ::crucible::effects;

// ── Layer 1: every grant tag is a final + grant_base + cv-ref-free marker ─
static_assert(IsGrantTag<ghw::cache<CacheOp::Flush>>);
static_assert(IsGrantTag<ghw::barrier<BarrierArch::X86, BarrierStrength::FullFence>>);
static_assert(IsGrantTag<ghw::tsc<TscMode::SerializedPinned>>);
static_assert(IsGrantTag<ghw::rng<RngSource::PhiloxCounter>>);
static_assert(IsGrantTag<ghw::cpuid<0x00000007u>>);
static_assert(IsGrantTag<ghw::msr<0x10u>>);
static_assert(IsGrantTag<ghw::port_io<0x80u>>);
static_assert(IsGrantTag<ghw::asm_<"vpternlogd fast-path">>);
static_assert(IsGrantTag<ghw::simd_width<256>>);
static_assert(IsGrantTag<ghw::vendor_intrinsic<VendorBackend::NV, "wgmma">>);

// ── Layer 2: sizeof — EBO-collapsible (1 byte standalone) ─────────────
static_assert(sizeof(ghw::cache<CacheOp::Prefetch, 3>)                       == 1);
static_assert(sizeof(ghw::barrier<BarrierArch::Arm, BarrierStrength::AcqRel>) == 1);
static_assert(sizeof(ghw::tsc<TscMode::Raw>)                                 == 1);
static_assert(sizeof(ghw::rng<RngSource::RdRand>)                            == 1);
static_assert(sizeof(ghw::cpuid<0x1u>)                                       == 1);
static_assert(sizeof(ghw::msr<0xC0000080u>)                                  == 1);
static_assert(sizeof(ghw::port_io<0xCF8u>)                                   == 1);
static_assert(sizeof(ghw::asm_<"x">)                                         == 1);
static_assert(sizeof(ghw::simd_width<512>)                                   == 1);
static_assert(sizeof(ghw::vendor_intrinsic<VendorBackend::AMD, "v_mfma">)    == 1);

// ── Layer 3: which_dim routing — each family to its axis ──────────────
static_assert(which_dim_v<ghw::cache<CacheOp::Flush>>                        == D::HwInstruction);
static_assert(which_dim_v<ghw::barrier<BarrierArch::X86, BarrierStrength::FullFence>> == D::BarrierStrength);
static_assert(which_dim_v<ghw::tsc<TscMode::SerializedPinned>>               == D::HwInstruction);
static_assert(which_dim_v<ghw::rng<RngSource::PhiloxCounter>>                == D::HwInstruction);
static_assert(which_dim_v<ghw::cpuid<0x00000007u>>                           == D::HwInstruction);
static_assert(which_dim_v<ghw::msr<0x10u>>                                   == D::HwInstruction);
static_assert(which_dim_v<ghw::port_io<0x80u>>                               == D::HwInstruction);
static_assert(which_dim_v<ghw::asm_<"reason">>                               == D::HwInstruction);
static_assert(which_dim_v<ghw::simd_width<256>>                              == D::SimdIsa);
static_assert(which_dim_v<ghw::vendor_intrinsic<VendorBackend::NV, "wgmma">> == D::Representation);

// ── Layer 4: NTTP / type distinctness ─────────────────────────────────
static_assert(!std::is_same_v<ghw::cache<CacheOp::Flush>, ghw::cache<CacheOp::Writeback>>);
static_assert(!std::is_same_v<ghw::cache<CacheOp::Prefetch, 0>, ghw::cache<CacheOp::Prefetch, 3>>);
static_assert(!std::is_same_v<ghw::tsc<TscMode::Raw>, ghw::tsc<TscMode::SerializedPinned>>);
static_assert(!std::is_same_v<ghw::rng<RngSource::RdRand>, ghw::rng<RngSource::RdSeed>>);
static_assert(!std::is_same_v<ghw::msr<0x10u>, ghw::msr<0x11u>>);
static_assert(!std::is_same_v<ghw::asm_<"a">, ghw::asm_<"b">>);
static_assert( std::is_same_v<ghw::asm_<"same">, ghw::asm_<"same">>);
static_assert(!std::is_same_v<ghw::simd_width<256>, ghw::simd_width<512>>);
static_assert(!std::is_same_v<ghw::vendor_intrinsic<VendorBackend::NV, "i">,
                              ghw::vendor_intrinsic<VendorBackend::AMD, "i">>);
// barrier aliases land on distinct (arch, strength) cells.
static_assert(!std::is_same_v<barrier_x86_mfence, barrier_arm_dmb_ish>);
static_assert(!std::is_same_v<barrier_compiler_acquire, barrier_compiler_release>);

// ── Layer 5: validity predicates gate the parametric families ─────────
static_assert( valid_simd_width_v<0>   && valid_simd_width_v<512>);
static_assert(!valid_simd_width_v<100> && !valid_simd_width_v<64>);
static_assert( is_sanctioned_cpuid_leaf_v<0x00000007u>);
static_assert(!is_sanctioned_cpuid_leaf_v<0xDEADBEEFu>);
static_assert( rationale_nonempty_v<"x">);
static_assert(!rationale_nonempty_v<"">);

// ── Layer 6: the five §XXI mints synthesize the right grant types ─────
constexpr eff::TestRunnerCtx ctx{};

static_assert(std::is_same_v<
    decltype(mint_asm_grant<"vpshufb hot probe">(ctx)),
    ghw::asm_<"vpshufb hot probe">>);
static_assert(std::is_same_v<
    decltype(mint_simd_width<256>(ctx)),
    ghw::simd_width<256>>);
static_assert(std::is_same_v<
    decltype(mint_vendor_intrinsic<"wgmma", VendorBackend::NV>(ctx)),
    ghw::vendor_intrinsic<VendorBackend::NV, "wgmma">>);
static_assert(std::is_same_v<
    decltype(mint_tsc_grant<TscMode::SerializedPinned>(ctx, CpuPinProof{})),
    ghw::tsc<TscMode::SerializedPinned>>);
static_assert(std::is_same_v<
    decltype(mint_msr_grant<0xC0000080u>(
        ctx, ::crucible::safety::mint_permission_root<root>())),
    ghw::msr<0xC0000080u>>);

// ── Layer 7: mint concept gates reject the mismatch classes (positive
//    side — the HS14 fixtures witness the negative side at compile-fail) ─
static_assert( CtxFitsAsmMint<eff::TestRunnerCtx, "x">);
static_assert(!CtxFitsAsmMint<eff::TestRunnerCtx, "">);
static_assert(!CtxFitsAsmMint<int, "x">);
static_assert( CtxFitsSimdWidthMint<eff::TestRunnerCtx, 256>);
static_assert(!CtxFitsSimdWidthMint<eff::TestRunnerCtx, 100>);
static_assert( CtxFitsTscMint<eff::TestRunnerCtx, TscMode::SerializedPinned>);
static_assert(!CtxFitsTscMint<eff::TestRunnerCtx, TscMode::NotAllowed>);
static_assert( CtxFitsVendorIntrinsicMint<eff::TestRunnerCtx, "wgmma">);
static_assert(!CtxFitsVendorIntrinsicMint<eff::TestRunnerCtx, "">);

// ── Layer 8: engagement markers route to the three hw axes ────────────
static_assert(which_dim_v<::crucible::fixy::grant::accept_default_strict_for_HwInstruction>
              == D::HwInstruction);
static_assert(which_dim_v<::crucible::fixy::grant::accept_default_strict_for_BarrierStrength>
              == D::BarrierStrength);
static_assert(which_dim_v<::crucible::fixy::grant::accept_default_strict_for_SimdIsa>
              == D::SimdIsa);

// ── Runtime smoke test — non-constant args defeat consteval folding,
//    catching SFINAE / inline-body bugs the static_asserts can mask. ───
inline void runtime_smoke_test() {
    eff::TestRunnerCtx live_ctx{};

    [[maybe_unused]] auto asm_grant   = mint_asm_grant<"runtime smoke asm">(live_ctx);
    [[maybe_unused]] auto width_grant = mint_simd_width<512>(live_ctx);
    [[maybe_unused]] auto vend_grant  =
        mint_vendor_intrinsic<"runtime smoke intrinsic", VendorBackend::AMD>(live_ctx);
    [[maybe_unused]] auto tsc_grant   =
        mint_tsc_grant<TscMode::SerializedPinned>(live_ctx, CpuPinProof{});
    [[maybe_unused]] auto msr_grant   =
        mint_msr_grant<0x10u>(live_ctx, ::crucible::safety::mint_permission_root<root>());

    // Direct grant construction (the non-mint families) round-trips too.
    [[maybe_unused]] ghw::cache<CacheOp::Prefetch, 2> prefetch{};
    [[maybe_unused]] barrier_x86_mfence              fence{};
    [[maybe_unused]] ghw::rng<RngSource::PhiloxCounter> rng_tag{};
    [[maybe_unused]] ghw::cpuid<0x00000001u>            cpuid_tag{};
    [[maybe_unused]] ghw::port_io<0xCF8u>               port_tag{};
}

}  // namespace crucible::fixy::hw::detail::v257_self_test
