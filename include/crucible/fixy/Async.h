#pragma once

// ── crucible::fixy::async — async-copy / mbarrier grant surface (FIXY-V-270) ─
//
// The WMEM epic's accelerator-async-pipeline grant family.  Where V-269's
// grant::hw::scope<> pins a memory-VISIBILITY scope for a fence, this header
// surfaces the two PTX async-pipeline primitives that the visibility scope
// coordinates:
//
//   * cp.async / TMA bulk copy  — a multi-STAGE pipelined fill of shared
//     memory at an accelerator memory scope (Cta / Cluster / Gpu).
//   * mbarrier.arrive / .wait   — the shared-memory barrier object that
//     signals async-copy completion and gates the consuming warp.
//
// These are the type-level tokens V-273 (sessions/AsyncPipelineSession.h)
// and V-274 (fixy/AsyncPipeline.h: mint_async_pipeline) compose into the
// producer-fills / consumer-drains binary protocol.  This header ships only
// the GRANT surface + the three §XXI ctx-bound mints; the session lives
// downstream.
//
// ── The three grant tag families (all final : grant_base, EBO = 0) ────
//
//   grant::async::copy<Stages, Scope, Bytes>  → Synchronization
//   grant::async::mbarrier_arrive<Scope>      → Synchronization
//   grant::async::mbarrier_wait<Scope>        → Synchronization
//
// ── On the Synchronization axis (DimensionAxis::Synchronization = 20) ──
//
// Synchronization was a WRAPPER-ONLY axis until now: the V-084 Wait<> /
// MemOrder<> surfaces are value-carrying Graded wrappers with NO Fn<...>
// slot and NO which_dim grant tag (only the generic
// accept_default_strict_for<Synchronization> engagement marker).  V-270
// ships the FIRST real which_dim-routed grant tags on this axis.  They are
// pure DECLARATION tags (a binding/site declares "I issue an async copy at
// scope S with N stages of B bytes"), NOT Fn<...> positional slots — so
// there is NO CollisionCatalog rule and NO ValidComposition wiring (the
// async grants are consumed by the V-273/274 session machinery, not by the
// Fn<> aggregator).
//
// ── The accelerator-scope gate (reuses V-265) ────────────────────────
//
// cp.async and mbarrier are accelerator-only constructs: cp.async fills
// shared memory visible at Cta scope minimum; mbarrier is a shared-memory
// barrier object addressed at .shared::cta / .shared::cluster.  An async
// copy "at ARM Inner scope" or "at System scope" is nonsensical.  Every
// mint therefore gates on `async_scope_realizable(Scope)` ==
// `mem_scope_is_accel(Scope)` (the V-265 trunk classifier): the scope must
// be accel-trunk (Warp / Cta / Cluster / Gpu).  The shared sentinels
// (Thread ⊥, System ⊤) and the ARM trunk (Inner / Outer) are rejected.
//
// ── Three §XXI ctx-bound mint factories (CLAUDE.md §XXI) ──────────────
//
//   mint_async_copy<Stages, Scope, Bytes>(ctx) → copy<Stages, Scope, Bytes>
//   mint_mbarrier_arrive<Scope>(ctx)           → mbarrier_arrive<Scope>
//   mint_mbarrier_wait<Scope>(ctx)             → mbarrier_wait<Scope>
//
// Each is `[[nodiscard]] constexpr noexcept`, takes `Ctx const&` first, and
// gates on ONE concept per §XXI's single-concept rule.  copy adds Stages>=1
// (a 0-stage pipeline is degenerate — there is nothing to overlap) and
// Bytes>0 (a 0-byte transfer copies nothing); both mbarrier mints gate on
// scope alone.  The grant tags themselves are PURE (any cell is
// instantiable for diagnosis / self-test); the mint is the authorization
// boundary (mirrors V-269's scope<> tag-vs-mint split).
//
// ── Axiom coverage (CLAUDE.md §II) ────────────────────────────────────
//
//   InitSafe   — every tag is `final` empty struct, NSDMI-trivial.
//   TypeSafe   — Stages (uint8_t) / Bytes (uint32_t) NTTPs + MemoryScope
//                scoped enum; cross-axis mixing is a compile error.
//   NullSafe   — no raw pointer surface; mints return value-type grants.
//   MemSafe    — zero-state tags; nothing owned, nothing freed.
//   BorrowSafe — ctx passed by const-ref; no shared state.
//   ThreadSafe — every factory is pure / stateless / constexpr.
//   LeakSafe   — zero-state tags; no resources to leak.
//   DetSafe    — same grants + same ctx → same tag types on any platform.
//
// ── HS14 fixtures (≥2 per NEW mint → 6 fixtures in test/fixy_neg/) ────
//
//   mint_async_copy      : ARM-trunk scope        + Stages==0
//   mint_mbarrier_arrive : ARM-trunk scope        + System (⊤) scope
//   mint_mbarrier_wait   : ARM-trunk scope        + non-ctx

#include <crucible/fixy/Grant.h>                          // grant_base, which_dim, IsGrantTag
#include <crucible/fixy/Dim.h>                            // dim::DimensionAxis
#include <crucible/algebra/lattices/MemoryScopeLattice.h>  // MemoryScope (V-265)
#include <crucible/effects/ExecCtx.h>                     // effects::IsExecCtx

#include <cstdint>
#include <type_traits>

namespace crucible::fixy::async {

// Re-export of the V-265 MemoryScope visibility carrier so an async grant
// cites the canonical lattice enum.
using MemoryScope = ::crucible::algebra::lattices::MemoryScope;

// An async-copy / mbarrier grant is realizable iff its scope is on the
// accelerator trunk (Warp / Cta / Cluster / Gpu) — cp.async and mbarrier
// are GPU-only constructs.  Reuses the V-265 trunk classifier; the shared
// sentinels (Thread ⊥ / System ⊤) and the ARM trunk are rejected.
[[nodiscard]] constexpr bool async_scope_realizable(MemoryScope scope) noexcept {
    return ::crucible::algebra::lattices::mem_scope_is_accel(scope);
}

}  // namespace crucible::fixy::async

// ═════════════════════════════════════════════════════════════════════
// ── grant tag families (crucible::fixy::grant::async) ─────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::grant::async {

namespace fa = ::crucible::fixy::async;

// (1) copy<Stages, Scope, Bytes> — async multi-stage shared-memory fill
//     (cp.async / TMA bulk copy).  Stages = pipeline depth, Scope = accel
//     visibility scope, Bytes = per-stage transfer size.  PURE tag: any
//     cell is instantiable for diagnosis; the Stages>=1 / Bytes>0 /
//     accel-scope gate lives in mint_async_copy.
template <std::uint8_t Stages, fa::MemoryScope Scope, std::uint32_t Bytes>
struct copy final : grant_base {};

// (2) mbarrier_arrive<Scope> — mbarrier.arrive at an accel scope.
template <fa::MemoryScope Scope>
struct mbarrier_arrive final : grant_base {};

// (3) mbarrier_wait<Scope> — mbarrier.try_wait at an accel scope.
template <fa::MemoryScope Scope>
struct mbarrier_wait final : grant_base {};

}  // namespace crucible::fixy::grant::async

// ── which_dim routing — CR-09 locked namespace ───────────────────────

namespace crucible::fixy::grant {

namespace fa = ::crucible::fixy::async;

template <std::uint8_t Stages, fa::MemoryScope Scope, std::uint32_t Bytes>
struct which_dim<async::copy<Stages, Scope, Bytes>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Synchronization> {};

template <fa::MemoryScope Scope>
struct which_dim<async::mbarrier_arrive<Scope>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Synchronization> {};

template <fa::MemoryScope Scope>
struct which_dim<async::mbarrier_wait<Scope>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Synchronization> {};

// Engagement marker for the Synchronization axis (V-270 ships the first
// which_dim-routed grants on it, so the named alias lands here).
using accept_default_strict_for_Synchronization =
    accept_default_strict_for<dim::DimensionAxis::Synchronization>;

}  // namespace crucible::fixy::grant

// ═════════════════════════════════════════════════════════════════════
// ── Canonical aliases + the three §XXI mint factories ─────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::async {

namespace ga = ::crucible::fixy::grant::async;

// ── Copy aliases — pin the canonical accel scopes, leave Stages/Bytes ─
template <std::uint8_t Stages, std::uint32_t Bytes>
using copy_cta = ga::copy<Stages, MemoryScope::Cta, Bytes>;       // cp.async → .shared::cta
template <std::uint8_t Stages, std::uint32_t Bytes>
using copy_cluster = ga::copy<Stages, MemoryScope::Cluster, Bytes>;  // TMA → .shared::cluster

// ── mbarrier aliases — the cta / cluster barrier shapes ───────────────
using mbarrier_arrive_cta     = ga::mbarrier_arrive<MemoryScope::Cta>;
using mbarrier_wait_cta       = ga::mbarrier_wait<MemoryScope::Cta>;
using mbarrier_arrive_cluster = ga::mbarrier_arrive<MemoryScope::Cluster>;
using mbarrier_wait_cluster   = ga::mbarrier_wait<MemoryScope::Cluster>;

// ── §XXI ctx-fit concepts — ONE concept per mint ─────────────────────

// Base: a valid ExecCtx is the floor for every async-grant mint.
template <typename Ctx>
concept CtxFitsAsyncGrant = ::crucible::effects::IsExecCtx<Ctx>;

// async copy requires a non-degenerate pipeline (Stages>=1), a non-empty
// transfer (Bytes>0), and an accelerator-realizable scope.
template <typename Ctx, std::uint8_t Stages, MemoryScope Scope, std::uint32_t Bytes>
concept CtxFitsAsyncCopyMint =
    CtxFitsAsyncGrant<Ctx>
    && (Stages >= 1)
    && (Bytes > 0)
    && async_scope_realizable(Scope);

// mbarrier arrive/wait require only an accelerator-realizable scope.
template <typename Ctx, MemoryScope Scope>
concept CtxFitsMbarrierMint =
    CtxFitsAsyncGrant<Ctx> && async_scope_realizable(Scope);

// ── mint_async_copy<Stages, Scope, Bytes>(ctx) → copy<Stages, Scope, Bytes> ─
template <std::uint8_t Stages, MemoryScope Scope, std::uint32_t Bytes,
          ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsAsyncCopyMint<Ctx, Stages, Scope, Bytes>
[[nodiscard]] constexpr ga::copy<Stages, Scope, Bytes>
mint_async_copy(Ctx const&) noexcept {
    return {};
}

// ── mint_mbarrier_arrive<Scope>(ctx) → mbarrier_arrive<Scope> ─────────
template <MemoryScope Scope, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsMbarrierMint<Ctx, Scope>
[[nodiscard]] constexpr ga::mbarrier_arrive<Scope>
mint_mbarrier_arrive(Ctx const&) noexcept {
    return {};
}

// ── mint_mbarrier_wait<Scope>(ctx) → mbarrier_wait<Scope> ─────────────
template <MemoryScope Scope, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsMbarrierMint<Ctx, Scope>
[[nodiscard]] constexpr ga::mbarrier_wait<Scope>
mint_mbarrier_wait(Ctx const&) noexcept {
    return {};
}

}  // namespace crucible::fixy::async

// ═════════════════════════════════════════════════════════════════════
// ── Self-test (compile-time) ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::async::detail::v270_self_test {

namespace ga = ::crucible::fixy::grant::async;
using ::crucible::fixy::grant::IsGrantTag;
using ::crucible::fixy::grant::which_dim_v;
using D = ::crucible::fixy::dim::DimensionAxis;
namespace eff = ::crucible::effects;

// ── Layer 1: every grant tag is a final + grant_base + cv-ref-free marker ─
static_assert(IsGrantTag<ga::copy<2, MemoryScope::Cta, 16>>);
static_assert(IsGrantTag<ga::mbarrier_arrive<MemoryScope::Cta>>);
static_assert(IsGrantTag<ga::mbarrier_wait<MemoryScope::Cluster>>);

// ── Layer 2: sizeof — EBO-collapsible (1 byte standalone) ─────────────
static_assert(sizeof(ga::copy<4, MemoryScope::Gpu, 256>)        == 1);
static_assert(sizeof(ga::mbarrier_arrive<MemoryScope::Warp>)    == 1);
static_assert(sizeof(ga::mbarrier_wait<MemoryScope::Cta>)       == 1);

// ── Layer 3: which_dim routing — every family to Synchronization ──────
static_assert(which_dim_v<ga::copy<2, MemoryScope::Cta, 16>>    == D::Synchronization);
static_assert(which_dim_v<ga::mbarrier_arrive<MemoryScope::Cta>> == D::Synchronization);
static_assert(which_dim_v<ga::mbarrier_wait<MemoryScope::Cta>>  == D::Synchronization);
static_assert(which_dim_v<copy_cta<2, 16>>                      == D::Synchronization);
static_assert(which_dim_v<mbarrier_arrive_cluster>              == D::Synchronization);

// ── Layer 4: NTTP / type distinctness ─────────────────────────────────
static_assert(!std::is_same_v<ga::copy<2, MemoryScope::Cta, 16>,
                              ga::copy<4, MemoryScope::Cta, 16>>);   // Stages
static_assert(!std::is_same_v<ga::copy<2, MemoryScope::Cta, 16>,
                              ga::copy<2, MemoryScope::Cluster, 16>>); // Scope
static_assert(!std::is_same_v<ga::copy<2, MemoryScope::Cta, 16>,
                              ga::copy<2, MemoryScope::Cta, 32>>);    // Bytes
static_assert(!std::is_same_v<ga::mbarrier_arrive<MemoryScope::Cta>,
                              ga::mbarrier_wait<MemoryScope::Cta>>);  // arrive ≠ wait
static_assert(!std::is_same_v<mbarrier_arrive_cta, mbarrier_arrive_cluster>);
static_assert( std::is_same_v<copy_cta<2, 16>, ga::copy<2, MemoryScope::Cta, 16>>);

// ── Layer 5: the accel-scope realizability predicate truth table ──────
static_assert( async_scope_realizable(MemoryScope::Warp));
static_assert( async_scope_realizable(MemoryScope::Cta));
static_assert( async_scope_realizable(MemoryScope::Cluster));
static_assert( async_scope_realizable(MemoryScope::Gpu));
static_assert(!async_scope_realizable(MemoryScope::Thread));   // ⊥ sentinel
static_assert(!async_scope_realizable(MemoryScope::System));   // ⊤ sentinel
static_assert(!async_scope_realizable(MemoryScope::Inner));    // ARM trunk
static_assert(!async_scope_realizable(MemoryScope::Outer));    // ARM trunk

// ── Layer 6: the three §XXI mints synthesize the right grant types ────
constexpr eff::TestRunnerCtx ctx{};

static_assert(std::is_same_v<
    decltype(mint_async_copy<2, MemoryScope::Cta, 16>(ctx)),
    ga::copy<2, MemoryScope::Cta, 16>>);
static_assert(std::is_same_v<
    decltype(mint_mbarrier_arrive<MemoryScope::Cluster>(ctx)),
    ga::mbarrier_arrive<MemoryScope::Cluster>>);
static_assert(std::is_same_v<
    decltype(mint_mbarrier_wait<MemoryScope::Cta>(ctx)),
    ga::mbarrier_wait<MemoryScope::Cta>>);

// ── Layer 7: mint concept gates reject the mismatch classes (positive
//    side — the HS14 fixtures witness the negative side at compile-fail) ─
static_assert( CtxFitsAsyncCopyMint<eff::TestRunnerCtx, 2, MemoryScope::Cta, 16>);
static_assert(!CtxFitsAsyncCopyMint<eff::TestRunnerCtx, 0, MemoryScope::Cta, 16>);  // Stages==0
static_assert(!CtxFitsAsyncCopyMint<eff::TestRunnerCtx, 2, MemoryScope::Cta, 0>);   // Bytes==0
static_assert(!CtxFitsAsyncCopyMint<eff::TestRunnerCtx, 2, MemoryScope::Inner, 16>); // ARM scope
static_assert(!CtxFitsAsyncCopyMint<int, 2, MemoryScope::Cta, 16>);                 // non-ctx
static_assert( CtxFitsMbarrierMint<eff::TestRunnerCtx, MemoryScope::Cta>);
static_assert( CtxFitsMbarrierMint<eff::TestRunnerCtx, MemoryScope::Gpu>);
static_assert(!CtxFitsMbarrierMint<eff::TestRunnerCtx, MemoryScope::System>);       // ⊤ sentinel
static_assert(!CtxFitsMbarrierMint<eff::TestRunnerCtx, MemoryScope::Inner>);        // ARM scope
static_assert(!CtxFitsMbarrierMint<int, MemoryScope::Cta>);                         // non-ctx

// ── Layer 8: engagement marker routes to the Synchronization axis ─────
static_assert(which_dim_v<::crucible::fixy::grant::accept_default_strict_for_Synchronization>
              == D::Synchronization);

// ── Runtime smoke test — non-constant args defeat consteval folding,
//    catching SFINAE / inline-body bugs the static_asserts can mask. ───
inline void runtime_smoke_test() {
    eff::TestRunnerCtx live_ctx{};

    [[maybe_unused]] auto copy_grant    =
        mint_async_copy<3, MemoryScope::Cta, 128>(live_ctx);
    [[maybe_unused]] auto arrive_grant  =
        mint_mbarrier_arrive<MemoryScope::Cta>(live_ctx);
    [[maybe_unused]] auto wait_grant    =
        mint_mbarrier_wait<MemoryScope::Cluster>(live_ctx);

    // Direct alias construction round-trips too.
    [[maybe_unused]] copy_cta<2, 16>     dbuf{};
    [[maybe_unused]] mbarrier_arrive_cta arr{};
    [[maybe_unused]] mbarrier_wait_cta   wt{};
}

}  // namespace crucible::fixy::async::detail::v270_self_test
