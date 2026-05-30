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
//   EndpointPack<Endpoints...>
//                             — type-list wrapper for GAPS-085's
//                               variadic handle matcher.
//
//   StageHandlesMatchEndpointsExtended<FnPtr, Inputs, Outputs>
//                             — fan-in/fan-out gate over endpoint
//                               packs.  Inputs must match FnPtr's
//                               leading consumer-handle parameters;
//                               Outputs must match the trailing
//                               producer-handle parameters.
//
//   mint_mpmc_stage_from_endpoints<auto FnPtr>(ctx, endpoints...)
//                             — variadic bridge factory.  The input /
//                               output boundary is inferred from
//                               StageArity<FnPtr>; leading endpoints
//                               are consumers, trailing endpoints are
//                               producers.
//
//   mint_swmr_stage<auto FnPtr>(ctx, in_ep, writer)
//                             — snapshot publication bridge for the
//                               1-input / SWMR-writer fan-out source
//                               pattern.  Downstream graph consumers
//                               read through the corresponding
//                               snapshot reader handles.
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
#include <crucible/safety/Decide.h>

#include <cstddef>
#include <tuple>
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

template <class... Endpoints>
struct EndpointPack {};

namespace detail {

template <auto FnPtr, class Inputs, class Outputs>
struct stage_handles_match_endpoints_extended : std::false_type {};

template <auto FnPtr,
          class... ConsumerEps,
          class... ProducerEps>
struct stage_handles_match_endpoints_extended<
    FnPtr,
    EndpointPack<ConsumerEps...>,
    EndpointPack<ProducerEps...>> {
private:
    using extract = ::crucible::safety::extract::StageArity<FnPtr>;
    using consumer_tuple = std::tuple<ConsumerEps...>;
    using producer_tuple = std::tuple<ProducerEps...>;

    template <class Endpoint, std::size_t I>
    static consteval bool consumer_endpoint_matches_param() noexcept {
        using endpoint = std::remove_cvref_t<Endpoint>;
        if constexpr (I >= ::crucible::safety::extract::arity_v<FnPtr>) {
            return false;
        } else if constexpr (!IsEndpoint<endpoint>) {
            return false;
        } else if constexpr (is_endpoint<endpoint>::direction
                             != Direction::Consumer) {
            return false;
        } else {
            return std::is_same_v<
                typename endpoint::handle_type,
                std::remove_reference_t<
                    ::crucible::safety::extract::param_type_t<
                        FnPtr, I>>>;
        }
    }

    template <class Endpoint, std::size_t I>
    static consteval bool producer_endpoint_matches_param() noexcept {
        using endpoint = std::remove_cvref_t<Endpoint>;
        if constexpr (I >= ::crucible::safety::extract::arity_v<FnPtr>) {
            return false;
        } else if constexpr (!IsEndpoint<endpoint>) {
            return false;
        } else if constexpr (is_endpoint<endpoint>::direction
                             != Direction::Producer) {
            return false;
        } else {
            return std::is_same_v<
                typename endpoint::handle_type,
                std::remove_reference_t<
                    ::crucible::safety::extract::param_type_t<
                        FnPtr, I>>>;
        }
    }

    template <std::size_t... Is>
    static consteval bool input_types_match(std::index_sequence<Is...>) noexcept {
        return (consumer_endpoint_matches_param<
            std::tuple_element_t<Is, consumer_tuple>, Is>() && ...);
    }

    template <std::size_t... Is>
    static consteval bool output_types_match(std::index_sequence<Is...>) noexcept {
        constexpr std::size_t offset = extract::input_count;
        return (producer_endpoint_matches_param<
            std::tuple_element_t<Is, producer_tuple>, offset + Is>() && ...);
    }

    static consteval bool compute() noexcept {
        if constexpr (!::crucible::safety::extract::VariadicPipelineStage<
                          FnPtr>) {
            return false;
        } else if constexpr (extract::input_count != sizeof...(ConsumerEps)
                          || extract::output_count != sizeof...(ProducerEps)) {
            return false;
        } else {
            return input_types_match(
                       std::make_index_sequence<sizeof...(ConsumerEps)>{})
                && output_types_match(
                       std::make_index_sequence<sizeof...(ProducerEps)>{});
        }
    }

public:
    static constexpr bool value = compute();
};

}  // namespace detail

template <auto FnPtr, class Inputs, class Outputs>
concept StageHandlesMatchEndpointsExtended =
    detail::stage_handles_match_endpoints_extended<
        FnPtr, Inputs, Outputs>::value;

namespace detail {

template <class Tuple, class Seq>
struct endpoint_pack_from_tuple_indices;

template <class Tuple, std::size_t... Is>
struct endpoint_pack_from_tuple_indices<Tuple, std::index_sequence<Is...>> {
    using type = EndpointPack<std::tuple_element_t<Is, Tuple>...>;
};

template <std::size_t Offset, class Tuple, class Seq>
struct endpoint_pack_from_tuple_offset_indices;

template <std::size_t Offset, class Tuple, std::size_t... Is>
struct endpoint_pack_from_tuple_offset_indices<
    Offset,
    Tuple,
    std::index_sequence<Is...>> {
    using type = EndpointPack<std::tuple_element_t<Offset + Is, Tuple>...>;
};

template <std::size_t N, class... Endpoints>
using endpoint_take_pack_t =
    typename endpoint_pack_from_tuple_indices<
        std::tuple<Endpoints...>,
        std::make_index_sequence<N>>::type;

template <std::size_t N, class... Endpoints>
using endpoint_drop_pack_t =
    typename endpoint_pack_from_tuple_offset_indices<
        N,
        std::tuple<Endpoints...>,
        std::make_index_sequence<sizeof...(Endpoints) - N>>::type;

template <auto FnPtr, class Ctx, class... Endpoints>
struct mpmc_stage_from_endpoints_gate {
private:
    using arity = ::crucible::safety::extract::StageArity<FnPtr>;

    static consteval bool compute() noexcept {
        if constexpr (!::crucible::safety::extract::VariadicPipelineStage<
                          FnPtr>
                   || !::crucible::effects::IsExecCtx<Ctx>
                   || sizeof...(Endpoints)
                        != ::crucible::safety::extract::arity_v<FnPtr>) {
            return false;
        } else {
            using inputs = endpoint_take_pack_t<
                arity::input_count,
                Endpoints...>;
            using outputs = endpoint_drop_pack_t<
                arity::input_count,
                Endpoints...>;
            return CtxFitsVariadicStage<FnPtr, Ctx>
                && StageHandlesMatchEndpointsExtended<FnPtr, inputs, outputs>;
        }
    }

public:
    static constexpr bool value = compute();
};

template <auto FnPtr, class Ctx, class Tuple, std::size_t... Is>
[[nodiscard]] constexpr auto
move_input_handles_from_endpoint_tuple(Tuple& endpoints,
                                       std::index_sequence<Is...>) noexcept {
    return std::tuple{
        std::move(std::get<Is>(endpoints)).into_handle()...
    };
}

template <auto FnPtr, class Ctx, class Tuple, std::size_t... Is>
[[nodiscard]] constexpr auto
move_output_handles_from_endpoint_tuple(Tuple& endpoints,
                                        std::index_sequence<Is...>) noexcept {
    constexpr std::size_t offset =
        ::crucible::safety::extract::StageArity<FnPtr>::input_count;
    return std::tuple{
        std::move(std::get<offset + Is>(endpoints)).into_handle()...
    };
}

template <auto FnPtr, class Ctx, class Tuple>
[[nodiscard]] constexpr auto
make_mpmc_stage_from_endpoint_tuple(Ctx const& ctx,
                                    Tuple& endpoints) noexcept {
    using arity = ::crucible::safety::extract::StageArity<FnPtr>;
    auto inputs = move_input_handles_from_endpoint_tuple<FnPtr, Ctx>(
        endpoints,
        std::make_index_sequence<arity::input_count>{});
    auto outputs = move_output_handles_from_endpoint_tuple<FnPtr, Ctx>(
        endpoints,
        std::make_index_sequence<arity::output_count>{});
    using stage_type = MpmcStage<
        FnPtr,
        Ctx,
        decltype(inputs),
        decltype(outputs)>;
    return stage_type{ctx, std::move(inputs), std::move(outputs)};
}

// fix-03: sole authorized constructor of SwmrStage (its ctor is private +
// friends this factory).  Forward-declared in Stage.h so SwmrStage's class
// body can friend it without seeing CtxFitsSwmrStageFromEndpoint (which is
// defined below, after Stage.h is included).  Intentionally unconstrained —
// the row-admission gate runs in mint_swmr_stage BEFORE this is called.
template <auto FnPtr, class Ctx>
[[nodiscard]] constexpr auto
make_swmr_stage(Ctx const& ctx,
                std::remove_reference_t<
                    ::crucible::safety::extract::param_type_t<FnPtr, 0>>&& in,
                std::remove_reference_t<
                    ::crucible::safety::extract::param_type_t<FnPtr, 1>>&& writer)
    noexcept {
    return SwmrStage<FnPtr, Ctx>{ctx, std::move(in), std::move(writer)};
}

inline void mpmc_stage_row_admission_anchor_() noexcept {}
inline void swmr_stage_row_admission_anchor_() noexcept {}

template <auto FnPtr, class Ctx, class ConsumerEp, class Writer>
struct swmr_stage_from_endpoint_gate {
private:
    static consteval bool compute() noexcept {
        using consumer_ep = std::remove_cvref_t<ConsumerEp>;
        using writer = std::remove_cvref_t<Writer>;
        if constexpr (!CtxFitsSwmrPublishStage<FnPtr, Ctx>
                   || !IsConsumerEndpoint<consumer_ep>
                   || !::crucible::safety::extract::is_swmr_writer_v<
                          writer>) {
            return false;
        } else {
            return std::is_same_v<
                    typename consumer_ep::handle_type,
                    std::remove_reference_t<
                        ::crucible::safety::extract::param_type_t<
                            FnPtr, 0>>>
                && std::is_same_v<
                    writer,
                    std::remove_reference_t<
                        ::crucible::safety::extract::param_type_t<
                            FnPtr, 1>>>;
        }
    }

public:
    static constexpr bool value = compute();
};

}  // namespace detail

template <auto FnPtr, class Ctx, class... Endpoints>
concept CtxFitsMpmcStageFromEndpoints =
    detail::mpmc_stage_from_endpoints_gate<
        FnPtr,
        Ctx,
        std::remove_cvref_t<Endpoints>...>::value;

template <auto FnPtr, class Ctx, class ConsumerEp, class Writer>
concept CtxFitsSwmrStageFromEndpoint =
    detail::swmr_stage_from_endpoint_gate<
        FnPtr,
        Ctx,
        ConsumerEp,
        Writer>::value;

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
// ── mint_mpmc_stage_from_endpoints<auto FnPtr>(ctx, endpoints...) ──
// ═════════════════════════════════════════════════════════════════════

template <auto FnPtr,
          ::crucible::effects::IsExecCtx Ctx,
          class... Endpoints>
    requires CtxFitsMpmcStageFromEndpoints<FnPtr, Ctx, Endpoints...>
[[nodiscard]] constexpr auto mint_mpmc_stage_from_endpoints(
    Ctx const& ctx,
    Endpoints&&... endpoints) noexcept
{
    using ctx_row = typename Ctx::row_type;
    using required_row = variadic_stage_row_union_t<FnPtr>;
    using offending_row =
        ::crucible::effects::row_difference_t<required_row, ctx_row>;

    CRUCIBLE_ROW_MISMATCH_ASSERT(
        (::crucible::decide::row_subset<required_row, ctx_row>()),
        EffectRowMismatch,
        &::crucible::concurrent::detail::mpmc_stage_row_admission_anchor_,
        ctx_row,
        required_row,
        offending_row);

    std::tuple<std::remove_cvref_t<Endpoints>...> endpoint_tuple{
        std::forward<Endpoints>(endpoints)...
    };
    return detail::make_mpmc_stage_from_endpoint_tuple<FnPtr>(
        ctx,
        endpoint_tuple);
}

template <auto FnPtr,
          ::crucible::effects::IsExecCtx Ctx,
          class ConsumerEp,
          class Writer>
    requires CtxFitsSwmrStageFromEndpoint<FnPtr, Ctx, ConsumerEp, Writer>
[[nodiscard]] constexpr auto mint_swmr_stage(
    Ctx const& ctx,
    ConsumerEp&& in_ep,
    Writer&& writer) noexcept
{
    using ctx_row = typename Ctx::row_type;
    using required_row = swmr_stage_row_union_t<FnPtr>;
    using offending_row =
        ::crucible::effects::row_difference_t<required_row, ctx_row>;

    CRUCIBLE_ROW_MISMATCH_ASSERT(
        (::crucible::decide::row_subset<required_row, ctx_row>()),
        EffectRowMismatch,
        &::crucible::concurrent::detail::swmr_stage_row_admission_anchor_,
        ctx_row,
        required_row,
        offending_row);

    // fix-03: construct via the friended detail factory (SwmrStage's ctor
    // is now private).  The row admission above is the load-bearing §XXI
    // gate; direct `SwmrStage<FnPtr, Ctx>{...}` construction is rejected.
    return detail::make_swmr_stage<FnPtr>(
        ctx,
        std::move(in_ep).into_handle(),
        std::forward<Writer>(writer));
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

inline void fan_in_body(typename Ch1::ConsumerHandle&&,
                        typename Ch1::ConsumerHandle&&,
                        typename Ch2::ProducerHandle&&) noexcept {}
inline void fan_out_body(typename Ch1::ConsumerHandle&&,
                         typename Ch2::ProducerHandle&&,
                         typename Ch2::ProducerHandle&&) noexcept {}

static_assert(saf::extract::VariadicPipelineStage<&fan_in_body>);
static_assert(!saf::extract::PipelineStage<&fan_in_body>);
static_assert(StageHandlesMatchEndpointsExtended<
    &fan_in_body,
    EndpointPack<ConsEp, ConsEp>,
    EndpointPack<ProdEp>>);
static_assert(!StageHandlesMatchEndpointsExtended<
    &fan_in_body,
    EndpointPack<ConsEp>,
    EndpointPack<ProdEp>>);
static_assert(!StageHandlesMatchEndpointsExtended<
    &fan_in_body,
    EndpointPack<ConsEp, ConsEp, ConsEp>,
    EndpointPack<ProdEp>>);
static_assert(!StageHandlesMatchEndpointsExtended<
    &fan_in_body,
    EndpointPack<ConsEp, int>,
    EndpointPack<ProdEp>>);

static_assert(saf::extract::VariadicPipelineStage<&fan_out_body>);
static_assert(!saf::extract::PipelineStage<&fan_out_body>);
static_assert(StageHandlesMatchEndpointsExtended<
    &fan_out_body,
    EndpointPack<ConsEp>,
    EndpointPack<ProdEp, ProdEp>>);
static_assert(!StageHandlesMatchEndpointsExtended<
    &fan_out_body,
    EndpointPack<ConsEp, ProdEp>,
    EndpointPack<ProdEp>>);
static_assert(!StageHandlesMatchEndpointsExtended<
    &fan_out_body,
    EndpointPack<ConsEp>,
    EndpointPack<ProdEp, ProdEp, ProdEp>>);

}  // namespace detail::stage_endpoint_bridge_self_test

}  // namespace crucible::concurrent
