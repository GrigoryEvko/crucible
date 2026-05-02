#pragma once

// ── crucible::concurrent::mint_stage_from_endpoints ─────────────────
//
// Tier 2 → Tier 3 bridge factory: composes an Endpoint pair
// (consumer-side + producer-side) directly into a Stage<auto FnPtr,
// Ctx>, without the user having to manually call Endpoint::into_handle()
// on each side.
//
// Motivation: the Endpoint owns the substrate-fit-validated typed view;
// the Stage owns the pipeline-stage-shaped invocation primitive.
// Without an explicit bridge, users had to choose ONE — going through
// Endpoint loses Stage composition; going through Stage directly
// loses Endpoint's substrate-fit validation.  This header closes that
// composition gap.
//
// ── What this header ships ──────────────────────────────────────────
//
//   IsConsumerEndpoint<E>     — recognizes Endpoint<S, Direction::Consumer, Ctx>
//                               specializations.
//
//   IsProducerEndpoint<E>     — recognizes Endpoint<S, Direction::Producer, Ctx>
//                               specializations.
//
//   StageHandlesMatchEndpoints<FnPtr, ConsumerEp, ProducerEp>
//                             — soundness gate: the endpoint pair's
//                               extracted handle types match exactly
//                               what FnPtr's PipelineStage signature
//                               declares.  Distinct from CtxFitsStage
//                               (which checks FnPtr+Ctx) and from
//                               IsConsumerEndpoint/IsProducerEndpoint
//                               (which check shape only).
//
//   CtxFitsStageFromEndpoints<FnPtr, Ctx, ConsumerEp, ProducerEp>
//                             — full single-concept gate for
//                               mint_stage_from_endpoints.  Conjunction
//                               of CtxFitsStage<FnPtr, Ctx>,
//                               IsConsumerEndpoint, IsProducerEndpoint,
//                               and StageHandlesMatchEndpoints.
//
//   mint_stage_from_endpoints<auto FnPtr>(ctx, in_ep, out_ep)
//                             — Universal Mint Pattern factory.
//                               Consumes both endpoints by &&,
//                               extracts handles via into_handle(),
//                               forwards to mint_stage<FnPtr>(ctx,
//                               in_handle, out_handle).
//
// ── Universal Mint Pattern compliance ───────────────────────────────
//
//   * Name: mint_stage_from_endpoints (mint_<noun>, §XXI rule).
//   * First parameter: Ctx const& (ctx-bound mint flavor).
//   * Single concept gate: CtxFitsStageFromEndpoints<...>.
//   * [[nodiscard]] constexpr noexcept (pure structural composition).
//   * Returns concrete Stage<FnPtr, Ctx> — never type-erased.
//   * Discoverable via `grep "mint_stage_from_endpoints"`.
//   * 2 HS14 negative-compile fixtures alongside (in
//     test/effects_neg/neg_mint_stage_from_endpoints_*).
//
// ── Why free function in a separate header (not method on Endpoint) ─
//
// Mirrors the bridges/EndpointMint.h discipline: keeping bridge
// factories out of concurrent/Endpoint.h prevents pulling Stage.h
// (and PipelineStage / SignatureTraits) into every Endpoint user.
// Callers who don't compose Endpoints into Stages never see Stage.h.
//
// ── Composition shape ───────────────────────────────────────────────
//
//   // 1. Mint endpoints (substrate-fit validated by mint_endpoint)
//   auto in_ep  = mint_endpoint<Ch, Direction::Consumer>(ctx, in_handle);
//   auto out_ep = mint_endpoint<Ch2, Direction::Producer>(ctx, out_handle);
//
//   // 2. Bridge to stage (handle-type compatibility validated)
//   auto stage = mint_stage_from_endpoints<&body>(
//       ctx, std::move(in_ep), std::move(out_ep));
//
//   // 3. Compose into pipeline (chain compatibility validated)
//   auto pipe = mint_pipeline(ctx, std::move(stage));
//
//   // 4. Run (jthread spawn + RAII join)
//   std::move(pipe).run();
//
// Three concept gates fire in sequence:
//   * mint_endpoint:                SubstrateFitsCtxResidency
//   * mint_stage_from_endpoints:    CtxFitsStage + handle-type match
//   * mint_pipeline:                pipeline_chain
//
// Each gate runs ONCE at its mint boundary; downstream uses are
// concept-free (full speed, no per-call check).
//
// ── References ──────────────────────────────────────────────────────
//
//   concurrent/Endpoint.h           — Tier 2 source (Endpoint type +
//                                     into_handle() consume accessor)
//   concurrent/Stage.h              — Tier 3 target (Stage type +
//                                     mint_stage factory)
//   concurrent/Pipeline.h           — Tier 3 composition (Pipeline of
//                                     Stages)
//   bridges/EndpointMint.h          — sibling bridge factories
//                                     (Endpoint → recording / crash
//                                     bridges)
//   CLAUDE.md §XXI                  — Universal Mint Pattern
//   CLAUDE.md §XVIII HS14           — neg-compile fixture requirement

#include <crucible/Platform.h>
#include <crucible/concurrent/Endpoint.h>
#include <crucible/concurrent/Stage.h>

#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// ═════════════════════════════════════════════════════════════════════
// ── Endpoint specialization recognizers ───────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

template <class T>
struct is_endpoint : std::false_type {};

template <class Substr, Direction Dir, ::crucible::effects::IsExecCtx Ctx>
struct is_endpoint<Endpoint<Substr, Dir, Ctx>> : std::true_type {
    static constexpr Direction direction = Dir;
};

}  // namespace detail

template <class E>
concept IsEndpoint = detail::is_endpoint<std::remove_cvref_t<E>>::value;

template <class E>
concept IsConsumerEndpoint =
    IsEndpoint<E>
 && (detail::is_endpoint<std::remove_cvref_t<E>>::direction == Direction::Consumer);

template <class E>
concept IsProducerEndpoint =
    IsEndpoint<E>
 && (detail::is_endpoint<std::remove_cvref_t<E>>::direction == Direction::Producer);

// ═════════════════════════════════════════════════════════════════════
// ── StageHandlesMatchEndpoints<FnPtr, ConsumerEp, ProducerEp> ──────
// ═════════════════════════════════════════════════════════════════════
//
// Verifies that the endpoint pair's handle_type aliases match the
// types FnPtr's PipelineStage signature declares.  Without this
// check, the user could feed a Channel<int>::ConsumerHandle into a
// FnPtr expecting Channel<float>::ConsumerHandle, and the failure
// would only surface deep inside mint_stage's internal parameter
// binding.  With the check, the mismatch produces a clean
// CtxFitsStageFromEndpoints concept-failure diagnostic at the
// mint_stage_from_endpoints call site.

template <auto FnPtr, class ConsumerEp, class ProducerEp>
concept StageHandlesMatchEndpoints =
    ::crucible::safety::extract::PipelineStage<FnPtr>
 && std::is_same_v<
        typename std::remove_cvref_t<ConsumerEp>::handle_type,
        std::remove_reference_t<
            ::crucible::safety::extract::param_type_t<FnPtr, 0>>>
 && std::is_same_v<
        typename std::remove_cvref_t<ProducerEp>::handle_type,
        std::remove_reference_t<
            ::crucible::safety::extract::param_type_t<FnPtr, 1>>>;

// ═════════════════════════════════════════════════════════════════════
// ── CtxFitsStageFromEndpoints — full soundness gate ────────────────
// ═════════════════════════════════════════════════════════════════════

template <auto FnPtr, class Ctx, class ConsumerEp, class ProducerEp>
concept CtxFitsStageFromEndpoints =
    CtxFitsStage<FnPtr, Ctx>
 && IsConsumerEndpoint<ConsumerEp>
 && IsProducerEndpoint<ProducerEp>
 && StageHandlesMatchEndpoints<FnPtr, ConsumerEp, ProducerEp>;

// ═════════════════════════════════════════════════════════════════════
// ── mint_stage_from_endpoints<auto FnPtr>(ctx, in_ep, out_ep) ──────
// ═════════════════════════════════════════════════════════════════════
//
// Token mint per CLAUDE.md §XXI ctx-bound flavor.  Consumes both
// endpoints by &&, extracts their held handles via into_handle(),
// forwards to mint_stage<FnPtr> for the actual Stage construction.
//
// Why FnPtr is non-deducible: same rationale as mint_stage —
// FnPtr appears only in the requires-clause and does not occur in
// any deduced parameter type, so callers spell
// `mint_stage_from_endpoints<&my_body>(ctx, std::move(in_ep), std::move(out_ep))`
// explicitly.
//
// ── Eight-axiom audit ───────────────────────────────────────────────
//
//   InitSafe — pure structural composition; no fields, no storage.
//              The returned Stage's NSDMI / aggregate-init handles
//              its own InitSafe story.
//   TypeSafe — single concept gate (CtxFitsStageFromEndpoints) pins
//              every cross-tier compatibility axis at construction.
//              Once it passes, the four conjuncts (CtxFitsStage,
//              IsConsumerEndpoint, IsProducerEndpoint,
//              StageHandlesMatchEndpoints) are all true and the
//              returned Stage is structurally guaranteed valid.
//   NullSafe — no pointers crossed in the bridge.  Endpoint owns a
//              non-null handle pointer by precondition (mint_endpoint
//              never returns an Endpoint whose handle_ is null);
//              into_handle() moves the value out and nulls Endpoint's
//              internal pointer.  No raw pointer is exposed at any
//              point in the bridge boundary.
//   MemSafe  — both endpoints are consumed by &&, never copied.
//              into_handle() on each Endpoint extracts its held
//              handle by move; the resulting handles are then
//              consumed by move into mint_stage.  No double-extract,
//              no use-after-extract (Endpoints are gone after the
//              call returns).
//   BorrowSafe — single ownership flow throughout.  in_ep/out_ep are
//              consumed; their handles are consumed; Stage owns them
//              for its lifetime.  No aliased mutation possible by
//              construction.
//   ThreadSafe — pure structural; no atomics, no synchronization.
//              The returned Stage's run() &&  is the boundary at which
//              jthread spawn occurs (in mint_pipeline + Pipeline::run).
//   LeakSafe — endpoints' handles are RAII-bound by Stage's lifetime;
//              Stage's destructor (defaulted) destroys both handles.
//              Pipeline::run's RAII jthread join ensures the stage
//              completes before destructors fire.
//   DetSafe  — pure structural composition emits no kernels, no FP,
//              no data flow.  Determinism is the responsibility of
//              the FnPtr body and the substrate's recipe pinning.

template <auto FnPtr,
          ::crucible::effects::IsExecCtx Ctx,
          class ConsumerEp,
          class ProducerEp>
    requires CtxFitsStageFromEndpoints<FnPtr, Ctx, ConsumerEp, ProducerEp>
[[nodiscard]] constexpr auto mint_stage_from_endpoints(
    Ctx const&    ctx,
    ConsumerEp&&  in_ep,
    ProducerEp&&  out_ep) noexcept
{
    return mint_stage<FnPtr>(
        ctx,
        std::move(in_ep).into_handle(),
        std::move(out_ep).into_handle());
}

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::stage_endpoint_bridge_self_test {

namespace eff = ::crucible::effects;
namespace saf = ::crucible::safety;

struct UTag1 {};
struct UTag2 {};

using Ch1 = PermissionedSpscChannel<int, 64, UTag1>;
using Ch2 = PermissionedSpscChannel<int, 64, UTag2>;

using ConsEp = Endpoint<Ch1, Direction::Consumer, eff::HotFgCtx>;
using ProdEp = Endpoint<Ch2, Direction::Producer, eff::HotFgCtx>;

// IsConsumerEndpoint / IsProducerEndpoint admit / reject correctly.
static_assert( IsEndpoint<ConsEp>);
static_assert( IsEndpoint<ProdEp>);
static_assert(!IsEndpoint<int>);

static_assert( IsConsumerEndpoint<ConsEp>);
static_assert(!IsConsumerEndpoint<ProdEp>);
static_assert(!IsConsumerEndpoint<int>);

static_assert( IsProducerEndpoint<ProdEp>);
static_assert(!IsProducerEndpoint<ConsEp>);
static_assert(!IsProducerEndpoint<int>);

// Stage body matching the Endpoint pair's handle types.
inline void int_stage_body(typename Ch1::ConsumerHandle&&,
                           typename Ch2::ProducerHandle&&) noexcept {}

static_assert(saf::extract::PipelineStage<&int_stage_body>);

// StageHandlesMatchEndpoints admits the matching pair.
static_assert(StageHandlesMatchEndpoints<&int_stage_body, ConsEp, ProdEp>);

// CtxFitsStageFromEndpoints admits the full chain under HotFgCtx.
static_assert(CtxFitsStageFromEndpoints<&int_stage_body, eff::HotFgCtx, ConsEp, ProdEp>);

// Negative cases:
//   * Non-Endpoint argument
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, eff::HotFgCtx, int,    ProdEp>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, eff::HotFgCtx, ConsEp, int>);
//   * Direction-swapped endpoints
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, eff::HotFgCtx, ProdEp, ConsEp>);
//   * Non-IsExecCtx
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, int,           ConsEp, ProdEp>);

// ── Payload-type mismatch coverage (StageHandlesMatchEndpoints axis) ──
// Belt-and-suspenders: static_assert proof of payload-mismatch
// rejection at HEADER level (the runtime fixture neg_mint_stage_from_
// endpoints_handle_mismatch proves the same axis at the test layer;
// this static_assert proves it independently, so a refactor that
// breaks StageHandlesMatchEndpoints' payload-type discrimination
// would fail the header's own consistency check, NOT just the test).

struct UTagFloat {};
using ChFloat   = PermissionedSpscChannel<float, 64, UTagFloat>;
using FloatConsEp = Endpoint<ChFloat, Direction::Consumer, eff::HotFgCtx>;
using FloatProdEp = Endpoint<ChFloat, Direction::Producer, eff::HotFgCtx>;

// FloatConsEp's handle_type is Channel<float>::ConsumerHandle, but
// int_stage_body expects Channel<int>::ConsumerHandle on slot 0.
// Direction matches; FnPtr shape matches; Ctx fits — only the
// handle-payload-type axis disagrees.  Bridge must reject.
static_assert( IsConsumerEndpoint<FloatConsEp>);
static_assert( IsProducerEndpoint<FloatProdEp>);
static_assert(!StageHandlesMatchEndpoints<&int_stage_body, FloatConsEp, ProdEp>);
static_assert(!StageHandlesMatchEndpoints<&int_stage_body, ConsEp, FloatProdEp>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, eff::HotFgCtx, FloatConsEp, ProdEp>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, eff::HotFgCtx, ConsEp, FloatProdEp>);

// Conversely: the matching int pair MUST satisfy
// StageHandlesMatchEndpoints (positive control to avoid a false
// "everything rejects" pathology).
static_assert( StageHandlesMatchEndpoints<&int_stage_body, ConsEp, ProdEp>);

}  // namespace detail::stage_endpoint_bridge_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Exercises the full Tier 2 → Tier 3 bridge: mint_endpoint (Tier 2)
// for both sides, then mint_stage_from_endpoints to compose into a
// Stage, then mint_pipeline + run().  Distinct from
// pipeline_real_integration_smoke_test which calls mint_stage
// DIRECTLY on the channel handles (skipping Endpoint).  This smoke
// proves the Endpoint-mediated path also works end-to-end.

inline bool stage_endpoint_bridge_smoke_test() noexcept {
    namespace eff = ::crucible::effects;
    namespace saf = ::crucible::safety;
    using namespace detail::stage_endpoint_bridge_self_test;

    Ch1 ch_in;
    Ch2 ch_out;

    // Mint + split permissions for both channels.
    auto whole_in = saf::mint_permission_root<spsc_tag::Whole<UTag1>>();
    auto [pp_in, cp_in] = saf::mint_permission_split<
        spsc_tag::Producer<UTag1>, spsc_tag::Consumer<UTag1>>(std::move(whole_in));

    auto whole_out = saf::mint_permission_root<spsc_tag::Whole<UTag2>>();
    auto [pp_out, cp_out] = saf::mint_permission_split<
        spsc_tag::Producer<UTag2>, spsc_tag::Consumer<UTag2>>(std::move(whole_out));

    // Construct handles.
    auto consumer_h = ch_in.consumer(std::move(cp_in));
    auto producer_h = ch_out.producer(std::move(pp_out));

    // Mint Endpoints — substrate-fit validation runs here.
    eff::HotFgCtx ctx;
    auto in_ep  = mint_endpoint<Ch1, Direction::Consumer>(ctx, consumer_h);
    auto out_ep = mint_endpoint<Ch2, Direction::Producer>(ctx, producer_h);

    // Bridge to Stage — handle-type compatibility validated.
    auto stage = mint_stage_from_endpoints<&int_stage_body>(
        ctx, std::move(in_ep), std::move(out_ep));

    // Compose into pipeline + run.
    auto pipeline = mint_pipeline(ctx, std::move(stage));
    std::move(pipeline).run();

    // Suppress unused-permission warnings.
    (void)pp_in;
    (void)cp_out;
    return true;
}

}  // namespace crucible::concurrent
