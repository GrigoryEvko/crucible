#pragma once

// ── crucible::concurrent::Pipeline<Stages...> ────────────────────────
//
// Tier 3 commit 2 — composes N Stage<auto FnPtr_i, Ctx_i>'s into a
// chain where the output payload type of stage_i equals the input
// payload type of stage_{i+1}.  Built on Stage (Tier 3 commit 1,
// concurrent/Stage.h); together they ship the integration substrate's
// Tier 3 row per CLAUDE.md §XXI.
//
// ── What this header ships ──────────────────────────────────────────
//
//   IsStage<T>                      — recognizer for Stage<FnPtr, Ctx>
//                                     specializations.
//
//   stages_chain<S1, S2>            — pairwise compatibility: output
//                                     value of S1 == input value of S2
//                                     (after cv-ref strip, via
//                                     Stage::input_value_type /
//                                     Stage::output_value_type).
//
//   pipeline_chain<Stages...>       — N-ary fold of stages_chain over
//                                     adjacent pairs.  Vacuously true
//                                     for N ≤ 1.
//
//   CtxFitsPipeline<Ctx, Stages...> — single concept gate for
//                                     mint_pipeline.  Conjunction of
//                                     IsExecCtx<Ctx>,
//                                     (IsStage<Stages> && ...), and
//                                     pipeline_chain<Stages...>, and
//                                     pipeline_row_union_t<Stages...>
//                                     admitted by Ctx::row_type.
//
//   Pipeline<Stages...>             — value-typed bundle of N stages,
//                                     held in a std::tuple.
//                                     Move-only.  &&-qualified .run()
//                                     spawns one std::jthread per
//                                     stage and joins via std::array
//                                     destructor (the same RAII
//                                     pattern as permission_fork).
//
//   mint_pipeline<>(ctx, stages...) — Universal Mint Pattern factory;
//                                     ctx-bound flavor; single-concept
//                                     gate; [[nodiscard]] noexcept;
//                                     consumes each stage by move.
//
// ── Why .run() must spawn threads ───────────────────────────────────
//
// Each Stage's body (the FnPtr the user wrote) IS the drain loop —
// it spins on try_pop until upstream closes its channel, processes
// each message, and writes downstream via try_push.  Sequential
// invocation of N stages would deadlock: stage_0's drain loop blocks
// waiting for stage_1 to consume from the channel between them, but
// stage_1 hasn't started yet.
//
// Pipeline.run() therefore spawns N jthreads — one per stage — and
// joins them via the std::array<jthread, N> destructor at fn epilogue.
// This is identical in shape to permission_fork (safety/PermissionFork.h);
// the only difference is what's being forked: there it's permissions
// over disjoint regions, here it's already-bundled stages over already-
// connected channels.
//
// Cost: N pthread_create + N pthread_join per Pipeline::run() call.
// For long-lived pipelines (the typical Crucible shape — a Vigil's
// background drain pipeline, a kernel-compile pool's stage chain) the
// thread spawn cost amortizes to zero.
//
// ── Stage-level fit checks happen at Stage mint ─────────────────────
//
// Each Stage was minted via mint_stage<FnPtr_i>(ctx_i, in_i, out_i),
// at which point CtxFitsStage<FnPtr_i, Ctx_i> validated the per-stage
// invariants.  The endpoint handles in_i and out_i themselves came
// from Endpoint mints (Tier 2) that already validated
// SubstrateFitsCtxResidency.  Pipeline's remaining jobs are:
//   * verify the CHAIN invariant (output_i ≡ input_{i+1}); and
//   * verify coordinator row admission.  The union of all stage rows,
//     pipeline_row_union_t<Stages...>, must be a Subrow of the
//     coordinator Ctx::row_type.
//
// The row-union gate is pinned by
// test/effects_neg/neg_mint_pipeline_{row_union_exceeds_ctx,
// chain_payload_compat_ok_row_mismatch,capability_propagation}.cpp and
// emits safety::diag::EffectRowMismatch at the mint boundary.
//
// ── Universal Mint Pattern compliance ───────────────────────────────
//
//   * Name: mint_pipeline (mint_<noun>, §XXI rule).
//   * First parameter: Ctx const& (ctx-bound mint flavor, §XXI).
//   * Single authorization boundary: ctx + pipeline_chain in the
//     requires clause; coordinator row admission via EffectRowMismatch
//     static assertion before constructing the Pipeline.
//   * [[nodiscard]] constexpr noexcept (no allocation in mint itself;
//     the jthread allocations happen at .run() time).
//   * Returns concrete Pipeline<Stages...> — never type-erased.
//   * Discoverable via `grep "mint_pipeline"`.
//   * HS14 negative-compile fixtures alongside.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — chain-compatibility checked structurally; mismatched
//              payload types fail at the requires-clause boundary.
//   InitSafe — pure type-level construction; tuple of moved-in stages.
//   MemSafe  — Pipeline owns the N stages by value; jthread array on
//              the stack joins via dtor; no heap allocation by
//              Pipeline itself (jthreads are themselves heap-backed
//              but managed by std::jthread).
//   BorrowSafe — Pipeline is move-only; each Stage in the tuple is
//              also move-only; copies would duplicate the linear
//              Permission tokens carried by Stage's handles.
//   ThreadSafe — channels between stages are responsible for
//              cross-thread ordering (PermissionedSpscChannel et al.);
//              Pipeline only orchestrates the spawn/join.
//   LeakSafe — RAII jthread join at .run() epilogue guarantees no
//              orphan threads; std::array destructor joins in reverse
//              order (immaterial to correctness — bodies must all
//              complete before .run() returns).
//   DetSafe  — same (FnPtr_i, Ctx_i, handle pairings) → same
//              Pipeline type and same body invocations.
//
// Runtime cost: sizeof(Pipeline<S1, S2, ...>) ≈ sum of sizeof(S_i).
// Per .run() call: N pthread_create + N pthread_join (Linux ~5-15 μs
// each).  Right primitive for "N stages, long bodies" — NOT for
// "thousands of micro-stages" (use ChaseLevDeque + ThreadPool then).
//
// ── References ──────────────────────────────────────────────────────
//
//   concurrent/Stage.h             — Tier 3 commit 1 (the unit being
//                                    composed)
//   safety/PipelineStage.h         — FOUND-D19 (the FnPtr shape)
//   safety/PermissionFork.h        — analogous RAII fork-join pattern
//   CLAUDE.md §XXI                 — Universal Mint Pattern
//   CLAUDE.md §IX                  — concurrency cost ordering
//   CLAUDE.md §XVIII HS14          — neg-compile fixture requirement

#include <crucible/Platform.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/concurrent/Stage.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/diag/RowMismatch.h>

#include <array>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// ═════════════════════════════════════════════════════════════════════
// ── IsStage<T> ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Class-template specialization recognizer for Stage<FnPtr, Ctx>.
// Detail-namespaced trait + user-facing concept.

namespace detail {

template <class T>
struct is_stage : std::false_type {};

template <auto FnPtr, class Ctx>
struct is_stage<Stage<FnPtr, Ctx>> : std::true_type {};

}  // namespace detail

template <class T>
concept IsStage = detail::is_stage<std::remove_cvref_t<T>>::value;

// ═════════════════════════════════════════════════════════════════════
// ── stages_chain<S1, S2> ───────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Pairwise compatibility: stage S1's output payload type equals
// stage S2's input payload type.  Both must be IsStage; the equality
// uses Stage's exposed input_value_type / output_value_type aliases
// (cv-ref-stripped at extraction time per FOUND-D19's
// pipeline_stage_input_value_t / pipeline_stage_output_value_t).

template <class S1, class S2>
concept stages_chain =
    IsStage<S1>
 && IsStage<S2>
 && std::is_same_v<
        typename std::remove_cvref_t<S1>::output_value_type,
        typename std::remove_cvref_t<S2>::input_value_type>;

// ═════════════════════════════════════════════════════════════════════
// ── pipeline_chain<Stages...> ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// N-ary fold over adjacent stage pairs.  Empty pack and single-stage
// pack are vacuously chain-compatible (no adjacent pairs to check).
// For N ≥ 2: every adjacent (i, i+1) must satisfy stages_chain.

namespace detail {

template <class Tuple, std::size_t... Is>
consteval bool pipeline_chain_check(std::index_sequence<Is...>) noexcept {
    if constexpr (sizeof...(Is) == 0) {
        return true;  // ≤ 1 stage — vacuously chained
    } else {
        return ((stages_chain<
                    std::tuple_element_t<Is,     Tuple>,
                    std::tuple_element_t<Is + 1, Tuple>>) && ...);
    }
}

}  // namespace detail

template <class... Stages>
concept pipeline_chain =
    sizeof...(Stages) >= 1
 && (IsStage<Stages> && ...)
 && detail::pipeline_chain_check<std::tuple<std::remove_cvref_t<Stages>...>>(
        std::make_index_sequence<(sizeof...(Stages) > 0 ? sizeof...(Stages) - 1 : 0)>{});

// ═════════════════════════════════════════════════════════════════════
// ── CtxFitsPipeline<Ctx, Stages...> ────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Soundness gate for mint_pipeline.  Conjunction of:
//
//   1. IsExecCtx<Ctx> — Ctx is a well-formed ExecCtx with the four
//      static facts.  (Ctx is the COORDINATOR ctx for Pipeline, not
//      a per-stage ctx — those live inside each Stage.)
//
//   2. pipeline_chain<Stages...> — IsStage on each, plus adjacent-pair
//      payload-type compatibility folded across the pack.
//
//   3. pipeline_row_union_t<Stages...> ⊆ Ctx::row_type — the
//      coordinator must admit every effect row represented by the
//      staged execution contexts it is about to run.
//
// Per-stage CtxFitsStage was checked at each Stage's mint boundary;
// re-checking payload rows here would be redundant.

namespace detail {

inline void mint_pipeline_row_admission_anchor() noexcept {}

template <class... Stages>
struct pipeline_row_union_impl;

template <>
struct pipeline_row_union_impl<> {
    using type = ::crucible::effects::Row<>;
};

template <class Stage0, class... Rest>
struct pipeline_row_union_impl<Stage0, Rest...> {
    using stage_row = typename std::remove_cvref_t<Stage0>::ctx_type::row_type;
    using rest_row = typename pipeline_row_union_impl<Rest...>::type;
    using type = ::crucible::effects::row_union_t<stage_row, rest_row>;
};

}  // namespace detail

template <class... Stages>
using pipeline_row_union_t =
    typename detail::pipeline_row_union_impl<std::remove_cvref_t<Stages>...>::type;

template <class Ctx, class... Stages>
concept CtxFitsPipeline =
    ::crucible::effects::IsExecCtx<Ctx>
 && pipeline_chain<Stages...>
 && ::crucible::effects::Subrow<pipeline_row_union_t<Stages...>,
                                typename Ctx::row_type>;

// ═════════════════════════════════════════════════════════════════════
// ── Pipeline<Stages...> ────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <class... Stages>
    requires pipeline_chain<Stages...>
class Pipeline {
public:
    static constexpr std::size_t arity = sizeof...(Stages);

    // ── Construction (used by mint_pipeline; not user-facing) ─────
    [[nodiscard]] explicit constexpr Pipeline(Stages&&... stages) noexcept
        : stages_{std::forward<Stages>(stages)...}
    {}

    // ── Move-only (held Stages are move-only) ──────────────────────
    Pipeline(Pipeline const&) = delete("Pipeline holds move-only Stages, each of which holds linear Permission tokens via its consumer/producer handles");
    Pipeline& operator=(Pipeline const&) = delete("Pipeline holds move-only Stages, each of which holds linear Permission tokens via its consumer/producer handles");
    Pipeline(Pipeline&&) noexcept = default;
    Pipeline& operator=(Pipeline&&) noexcept = default;

    // ── run() — spawns N jthreads, one per stage; joins via dtor ──
    //
    // &&-qualified: running CONSUMES the Pipeline.  Each stage is
    // moved into its own jthread's body, where `std::move(stage).run()`
    // invokes the FnPtr.  The std::array<jthread, N>'s destructor
    // joins all threads at fn epilogue, in reverse-of-construction
    // order (immaterial — all bodies must complete before .run()
    // returns).
    //
    // Identical RAII shape to permission_fork (safety/PermissionFork.h)
    // but parameterized by N stages instead of N permission tags.

    void run() && noexcept {
        std::move(*this).run_impl_(std::index_sequence_for<Stages...>{});
    }

    // ── Accessor (introspection only — does not consume) ───────────
    template <std::size_t I>
        requires (I < sizeof...(Stages))
    [[nodiscard]] constexpr auto&       stage()       &  noexcept { return std::get<I>(stages_); }

    template <std::size_t I>
        requires (I < sizeof...(Stages))
    [[nodiscard]] constexpr auto const& stage() const &  noexcept { return std::get<I>(stages_); }

private:
    template <std::size_t... Is>
    void run_impl_(std::index_sequence<Is...>) && noexcept {
        // Build N jthreads in-place; each captures-by-move its stage
        // and invokes std::move(stage).run() in its body.  The array's
        // destructor (at this fn's epilogue) joins all threads.
        [[maybe_unused]] std::array<std::jthread, sizeof...(Is)> threads = {
            std::jthread{
                [stage = std::move(std::get<Is>(stages_))]
                (std::stop_token) mutable noexcept {
                    std::move(stage).run();
                }
            }...
        };
        // ~std::array runs here, joining each jthread.
    }

    std::tuple<Stages...> stages_;
};

// ═════════════════════════════════════════════════════════════════════
// ── mint_pipeline<>(ctx, stages...) — Universal Mint Pattern ───────
// ═════════════════════════════════════════════════════════════════════
//
// Ctx-bound mint factory per CLAUDE.md §XXI.  Each stage is consumed
// by move into the constructed Pipeline.  The single concept gate
// CtxFitsPipeline<Ctx, Stages...> validates IsExecCtx + IsStage-pack
// + chain-compatibility; failure produces a substitution diagnostic
// at the call site.

template <::crucible::effects::IsExecCtx Ctx, class... Stages>
    requires pipeline_chain<std::remove_cvref_t<Stages>...>
[[nodiscard]] constexpr auto mint_pipeline(
    Ctx const& /*ctx*/,
    Stages&&... stages) noexcept
{
    using ctx_row = typename Ctx::row_type;
    using required_row = pipeline_row_union_t<std::remove_cvref_t<Stages>...>;
    using offending_row =
        ::crucible::effects::row_difference_t<required_row, ctx_row>;

    CRUCIBLE_ROW_MISMATCH_ASSERT(
        (::crucible::effects::Subrow<required_row, ctx_row>),
        EffectRowMismatch,
        &::crucible::concurrent::detail::mint_pipeline_row_admission_anchor,
        ctx_row,
        required_row,
        offending_row);

    return Pipeline<std::remove_cvref_t<Stages>...>{
        std::forward<Stages>(stages)...
    };
}

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Pin admit/reject behavior across canonical Stage compositions:
// matched chains admit, mismatched chains reject; non-Stage elements
// reject; non-IsExecCtx ctx rejects.

namespace detail::pipeline_self_test {

namespace eff = ::crucible::effects;
namespace saf = ::crucible::safety::extract;

// Reuse Stage's self-test fixtures (FakeConsumer/FakeProducer +
// stage_pass_through / stage_transform_int_to_float).
using namespace ::crucible::concurrent::detail::stage_self_test;

// Additional fixture: a float-to-double transform stage to chain
// after the int-to-float one.
inline void stage_transform_float_to_double(FakeConsumer<float>&&,
                                            FakeProducer<double>&&) noexcept {}
static_assert(saf::PipelineStage<&stage_transform_float_to_double>);

// Fixture stages of distinct shapes for chain checks.
using S_int_to_int       = Stage<&stage_pass_through,            eff::HotFgCtx>;
using S_int_to_float     = Stage<&stage_transform_int_to_float,  eff::HotFgCtx>;
using S_float_to_double  = Stage<&stage_transform_float_to_double, eff::HotFgCtx>;
using S_bg_int_to_int    = Stage<&stage_pass_through,            eff::BgDrainCtx>;
using S_init_int_to_int  = Stage<&stage_pass_through,            eff::ColdInitCtx>;

// IsStage admits / rejects appropriately.
static_assert( IsStage<S_int_to_int>);
static_assert( IsStage<S_int_to_float>);
static_assert( IsStage<S_float_to_double>);
static_assert(!IsStage<int>);
static_assert(!IsStage<eff::HotFgCtx>);

// stages_chain admits matched pairs, rejects mismatched.
static_assert( stages_chain<S_int_to_int,      S_int_to_int>);       // int→int, int→int
static_assert( stages_chain<S_int_to_float,    S_float_to_double>);  // int→float, float→double
static_assert(!stages_chain<S_int_to_int,      S_float_to_double>);  // int→int, float→double (mismatch)
static_assert(!stages_chain<S_int_to_float,    S_int_to_int>);       // int→float, int→int (mismatch)
static_assert(!stages_chain<int,               S_int_to_int>);       // non-Stage
static_assert(!stages_chain<S_int_to_int,      int>);                // non-Stage

// pipeline_chain folds correctly.
static_assert( pipeline_chain<S_int_to_int>);                                              // N=1, vacuous
static_assert( pipeline_chain<S_int_to_int, S_int_to_int>);                                // N=2, matched
static_assert( pipeline_chain<S_int_to_int, S_int_to_float, S_float_to_double>);           // N=3, transforms align
static_assert( pipeline_chain<S_bg_int_to_int, S_int_to_int>);                             // context row is separate from payload chain
static_assert(!pipeline_chain<S_int_to_int, S_float_to_double>);                           // N=2, mismatch
static_assert(!pipeline_chain<S_int_to_int, S_int_to_int, S_float_to_double>);             // N=3, last pair mismatch
static_assert(!pipeline_chain<int>);                                                       // non-Stage
static_assert(!pipeline_chain<>);                                                          // empty pack

static_assert(eff::Subrow<pipeline_row_union_t<S_int_to_int>, eff::Row<>>);
static_assert(eff::Subrow<
    pipeline_row_union_t<S_bg_int_to_int>,
    eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>);
static_assert(eff::Subrow<
    pipeline_row_union_t<S_bg_int_to_int, S_init_int_to_int>,
    eff::Row<eff::Effect::Bg, eff::Effect::Alloc,
             eff::Effect::Init, eff::Effect::IO>>);

// CtxFitsPipeline conjunction.
static_assert( CtxFitsPipeline<eff::HotFgCtx,   S_int_to_int>);
static_assert( CtxFitsPipeline<eff::BgDrainCtx, S_int_to_float, S_float_to_double>);
static_assert( CtxFitsPipeline<eff::BgDrainCtx, S_bg_int_to_int, S_int_to_int>);
static_assert(!CtxFitsPipeline<int,             S_int_to_int>);                            // non-Ctx
static_assert(!CtxFitsPipeline<eff::HotFgCtx,   S_int_to_int, S_float_to_double>);         // mismatch
static_assert(!CtxFitsPipeline<eff::HotFgCtx,   int>);                                     // non-Stage
static_assert(!CtxFitsPipeline<eff::HotFgCtx,   S_bg_int_to_int>);                         // coordinator row too narrow
static_assert(!CtxFitsPipeline<eff::ColdInitCtx, S_bg_int_to_int>);                        // Init ctx does not admit Bg

// Pipeline<...> type-level invariants.
using P1 = Pipeline<S_int_to_int>;
using P2 = Pipeline<S_int_to_int, S_int_to_int>;
using P3 = Pipeline<S_int_to_int, S_int_to_float, S_float_to_double>;

static_assert(P1::arity == 1);
static_assert(P2::arity == 2);
static_assert(P3::arity == 3);

// Move-only enforcement.
static_assert(!std::is_copy_constructible_v<P1>);
static_assert(!std::is_copy_assignable_v<P1>);
static_assert( std::is_move_constructible_v<P1>);
static_assert( std::is_move_assignable_v<P1>);

}  // namespace detail::pipeline_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Per the project's runtime-smoke-test discipline.  Constructs a
// 2-stage pipeline of pass-through stages, runs it (each stage body
// is a no-op so the jthreads complete immediately), and confirms the
// move/run cycle works end-to-end.

inline bool pipeline_runtime_smoke_test() noexcept {
    namespace dst = detail::stage_self_test;
    namespace eff = ::crucible::effects;

    eff::HotFgCtx ctx;

    // Stage 0: int → int pass-through.
    dst::FakeConsumer<int> in0;
    dst::FakeProducer<int> out0;
    auto stage0 = mint_stage<&dst::stage_pass_through>(
        ctx, std::move(in0), std::move(out0));

    // Stage 1: int → int pass-through (chain-compatible with stage 0).
    dst::FakeConsumer<int> in1;
    dst::FakeProducer<int> out1;
    auto stage1 = mint_stage<&dst::stage_pass_through>(
        ctx, std::move(in1), std::move(out1));

    auto pipeline = mint_pipeline(ctx, std::move(stage0), std::move(stage1));

    // Move it once to exercise the move ctor.
    auto pipeline_moved = std::move(pipeline);

    // Run consumes it; bodies are no-ops so jthreads exit immediately.
    std::move(pipeline_moved).run();

    return true;
}

// ═════════════════════════════════════════════════════════════════════
// ── Real-channel integration smoke (audit round 3) ─────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The pipeline_runtime_smoke_test() above uses local FakeConsumer /
// FakeProducer fixtures — sufficient to exercise the structural
// composition but it never proves that real PermissionedSpscChannel
// ProducerHandle / ConsumerHandle types actually flow through the
// Tier 1 → Tier 2 → Tier 3 chain.
//
// This test mints a real PermissionedSpscChannel, splits its Whole
// permission into Producer + Consumer, constructs the typed handles,
// passes those handles into mint_stage, composes into a Pipeline of
// 1, and runs it.  The stage body is a no-op (returns immediately
// without draining); the test proves the COMPOSITION compiles and
// the jthread spawn/join sequence completes — NOT that the body
// drains the channel.
//
// What this pins:
//   * IsConsumerHandle<PermissionedSpscChannel<...>::ConsumerHandle>
//     actually holds (proven by mint_stage accepting it as the
//     consumer slot of a real PipelineStage-shape FnPtr).
//   * IsProducerHandle<PermissionedSpscChannel<...>::ProducerHandle>
//     actually holds.
//   * Stage's typename binding (consumer_handle_type / producer_handle_type)
//     resolves correctly when FnPtr's parameters are nested types of a
//     class template.
//   * Pipeline<Stage<FnPtr, Ctx>> with N=1 runs cleanly via the same
//     jthread-spawn/array-join code path used for N≥2.

namespace detail::pipeline_real_smoke {

struct InTag  {};
struct OutTag {};

using ChIn  = PermissionedSpscChannel<int, 64, InTag>;
using ChOut = PermissionedSpscChannel<int, 64, OutTag>;

// Real PipelineStage-shape body — no-op forwarder that returns
// immediately without draining (so the jthread completes promptly
// and the smoke test doesn't hang).
inline void real_stage_body(typename ChIn::ConsumerHandle&&,
                            typename ChOut::ProducerHandle&&) noexcept {}

// Compile-time witnesses — the structural facts the test exercises
// at runtime must also hold at compile time.
static_assert(::crucible::safety::extract::is_consumer_handle_v<typename ChIn::ConsumerHandle>);
static_assert(::crucible::safety::extract::is_producer_handle_v<typename ChOut::ProducerHandle>);
static_assert(::crucible::safety::extract::PipelineStage<&real_stage_body>);

}  // namespace detail::pipeline_real_smoke

inline bool pipeline_real_integration_smoke_test() noexcept {
    namespace eff  = ::crucible::effects;
    namespace saf  = ::crucible::safety;
    using namespace detail::pipeline_real_smoke;

    // Two real channels: one for the stage's input drain, one for
    // its output push (the body never actually drives them; this
    // test exercises the COMPOSITION only).
    ChIn  ch_in;
    ChOut ch_out;

    // Mint + split permissions for both channels.
    auto whole_in = saf::mint_permission_root<spsc_tag::Whole<InTag>>();
    auto [pp_in, cp_in] = saf::mint_permission_split<
        spsc_tag::Producer<InTag>, spsc_tag::Consumer<InTag>>(std::move(whole_in));

    auto whole_out = saf::mint_permission_root<spsc_tag::Whole<OutTag>>();
    auto [pp_out, cp_out] = saf::mint_permission_split<
        spsc_tag::Producer<OutTag>, spsc_tag::Consumer<OutTag>>(std::move(whole_out));

    // Construct the handles the stage body will own.  pp_in (input-
    // side producer) and cp_out (output-side consumer) are the
    // "external" sides — left as raw permission tokens that destruct
    // at scope exit; in a real pipeline a driver / downstream consumer
    // would convert them to handles.
    auto consumer_h = ch_in.consumer(std::move(cp_in));
    auto producer_h = ch_out.producer(std::move(pp_out));

    // Mint the stage with REAL handles — exercises Stage's typename
    // binding against PermissionedSpscChannel nested handle types.
    eff::HotFgCtx ctx;
    auto stage = mint_stage<&real_stage_body>(
        ctx, std::move(consumer_h), std::move(producer_h));

    // Compose into a 1-stage pipeline (vacuously chain-compatible);
    // run it (jthread spawn → no-op body → jthread join).
    auto pipeline = mint_pipeline(ctx, std::move(stage));
    std::move(pipeline).run();

    // Suppress unused-permission warnings.  In a real pipeline these
    // would be converted to handles owned by upstream/downstream
    // actors.
    (void)pp_in;
    (void)cp_out;
    return true;
}

}  // namespace crucible::concurrent
