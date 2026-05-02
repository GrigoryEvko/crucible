#pragma once

// ── crucible::concurrent::Stage<auto FnPtr, Ctx> ─────────────────────
//
// Tier 3 keystone of the integration stack — bundles a PipelineStage-
// shaped function pointer (FOUND-D19, safety/PipelineStage.h) with its
// owning ExecCtx and the pair of typed endpoint handles it will consume
// on `.run()`.  One Stage = one node in a future Pipeline<Stages...>
// chain.
//
// ── What the spec says (CLAUDE.md §XXI) ─────────────────────────────
//
//   🚧 Tier 3 | mint_stage<auto FnPtr>(ctx, in, out)
//             | PipelineStage<FnPtr> ∧ CtxFitsStage<FnPtr, Ctx>
//             | Stage<FnPtr, Ctx>
//
// This file ships the Tier 3 ROW.  Pipeline<Stages...> + mint_pipeline
// follow in a separate header (concurrent/Pipeline.h).
//
// ── Mental model ────────────────────────────────────────────────────
//
// A PipelineStage is a void function taking
//   (ConsumerHandle&&, ProducerHandle&&)
// per FOUND-D19.  In a pipeline of N stages connected by N-1 channels,
// stage_i drains channel_{i-1} (consumer side) and writes to channel_i
// (producer side).  Stage<FnPtr, Ctx> bundles:
//
//   * the FnPtr identifying which stage body to invoke
//   * the Ctx representing where the body runs (HotFgCtx, BgDrainCtx,
//     KernelCompileCtx, ColdInitCtx, …)
//   * the consumer handle (drains from the previous channel)
//   * the producer handle (writes to the next channel)
//
// Construction is via mint_stage<FnPtr>(ctx, in, out) per the Universal
// Mint Pattern (CLAUDE.md §XXI).  The mint's `requires` clause binds
// the FnPtr to the PipelineStage shape AND verifies the ctx is a
// well-formed ExecCtx.
//
// SUBSTRATE-FIT NOT RE-VALIDATED HERE.  The two handles passed in came
// from already-validated Endpoints (Tier 2, concurrent/Endpoint.h), each
// of which checked SubstrateFitsCtxResidency<Substrate, Ctx> at its own
// mint boundary.  Re-checking substrate fit at the Stage level would be
// redundant and would require a `handle_substrate_t<H>` metafunction
// the codebase does not yet provide.  The chain-level invariant
// (output type of stage_i == input type of stage_{i+1}) lives in
// Pipeline's gate, not Stage's.
//
// ── Composition with prior tiers ────────────────────────────────────
//
//   Tier 1 (concurrent/SubstrateCtxFit.h) — substrate ↔ ctx residency
//     gates.  Validated when Endpoints were minted.
//   Tier 2 (concurrent/Endpoint.h)        — typed ctx-aware view of a
//     single substrate handle.  Stage CONSUMES Endpoint's
//     `.into_*_handle()` outputs (or raw handles obtained equivalently).
//   Tier 3 (THIS HEADER)                  — composes a PipelineStage-
//     shaped function with its ctx and its pair of typed handles.
//   Tier 3.b (concurrent/Pipeline.h, follow-up) — chain of Stages with
//     pipeline_chain<Stages...> compatibility check.
//
// ── Universal Mint Pattern compliance ───────────────────────────────
//
//   * Name: mint_stage  (mint_<noun>, §XXI rule).
//   * First parameter: Ctx const& (ctx-bound mint, §XXI flavor).
//   * Single concept gate: CtxFitsStage<FnPtr, Ctx>.
//   * [[nodiscard]] constexpr noexcept — pure structural composition,
//     no allocation.
//   * Returns concrete Stage<FnPtr, Ctx> — never type-erased.
//   * Discoverable via `grep "mint_stage"`.
//   * 2 HS14 negative-compile fixtures shipped alongside (see
//     test/effects_neg/neg_mint_stage_*).
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — FnPtr's parameter signature pins the handle types
//              statically; mismatched handles fail the parameter-type
//              binding at the mint call site.
//   InitSafe — pure type-level construction; the held handles are
//              move-constructed into the Stage's storage.
//   MemSafe  — Stage owns the two handles by value (stored in-place);
//              no external references; destructor cleans up via the
//              handles' own destructors.
//   BorrowSafe — Stage is move-only (the held Permission tokens in the
//              handles are linear; copy would duplicate them).
//   ThreadSafe — Stage holds no atomics; ordering is the underlying
//              channels' responsibility.
//   LeakSafe — RAII; handles released at Stage destruction.
//   DetSafe  — same (FnPtr, Ctx, handles) → same Stage type and
//              same FnPtr invocation.
//
// Runtime cost: sizeof(Stage<FnPtr, Ctx>) ≈ sizeof(ConsumerHandle) +
// sizeof(ProducerHandle) + sizeof(Ctx).  Ctx is EBO-collapsed via
// [[no_unique_address]] (~1 byte for context tags).  `.run()` inlines
// to a direct FnPtr call with the two handles moved in — byte-identical
// to bare `FnPtr(std::move(in), std::move(out))` under -O3.
//
// ── References ──────────────────────────────────────────────────────
//
//   safety/PipelineStage.h         — FOUND-D19 shape recognizer
//   safety/SignatureTraits.h       — arity_v / param_type_t / return_type_t
//   concurrent/Endpoint.h          — Tier 2 source of typed handles
//   concurrent/SubstrateCtxFit.h   — Tier 1 substrate ↔ ctx gates
//   effects/ExecCtx.h              — IsExecCtx concept
//   CLAUDE.md §XXI                 — Universal Mint Pattern
//   CLAUDE.md §XVIII HS14          — neg-compile fixture requirement

#include <crucible/Platform.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/PipelineStage.h>
#include <crucible/safety/SignatureTraits.h>

#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// ═════════════════════════════════════════════════════════════════════
// ── CtxFitsStage<auto FnPtr, Ctx> ──────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Soundness gate for mint_stage.  Conjunction of:
//
//   1. PipelineStage<FnPtr> — FnPtr has the canonical pipeline-stage
//      shape (arity 2, param 0 is consumer handle &&, param 1 is
//      producer handle &&, return void) per FOUND-D19.
//
//   2. IsExecCtx<Ctx> — Ctx is a well-formed ExecCtx with the four
//      static facts (residency_tier, cap_type, row_type, locality_hint).
//
// Substrate-level residency fit is NOT checked here — see header
// docstring "SUBSTRATE-FIT NOT RE-VALIDATED HERE" for rationale.
// Pipeline-level chain consistency lives in pipeline_chain<Stages...>
// (concurrent/Pipeline.h, follow-up).

template <auto FnPtr, class Ctx>
concept CtxFitsStage =
    ::crucible::safety::extract::PipelineStage<FnPtr>
 && ::crucible::effects::IsExecCtx<Ctx>;

// ═════════════════════════════════════════════════════════════════════
// ── Stage<auto FnPtr, Ctx> ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <auto FnPtr, class Ctx>
    requires CtxFitsStage<FnPtr, Ctx>
class Stage {
public:
    // ── Type-level facts (all static, zero runtime cost) ──────────
    using ctx_type             = Ctx;
    using consumer_handle_type = std::remove_reference_t<
        ::crucible::safety::extract::param_type_t<FnPtr, 0>>;
    using producer_handle_type = std::remove_reference_t<
        ::crucible::safety::extract::param_type_t<FnPtr, 1>>;
    using input_value_type     =
        ::crucible::safety::extract::pipeline_stage_input_value_t<FnPtr>;
    using output_value_type    =
        ::crucible::safety::extract::pipeline_stage_output_value_t<FnPtr>;

    static constexpr auto fn_ptr = FnPtr;

    static constexpr bool is_value_preserving =
        ::crucible::safety::extract::pipeline_stage_is_value_preserving_v<FnPtr>;

    // ── Construction (used by mint_stage; not user-facing) ─────────
    //
    // Marked explicit + nodiscard.  User code goes through mint_stage,
    // which checks CtxFitsStage at the call boundary.  Direct
    // construction is permitted (no friend declaration to add) but the
    // mint factory is the canonical authorization point per §XXI.
    [[nodiscard]] explicit constexpr Stage(
        Ctx const&             ctx,
        consumer_handle_type&& in,
        producer_handle_type&& out) noexcept
        : ctx_{ctx}
        , in_{std::move(in)}
        , out_{std::move(out)}
    {}

    // ── Move-only (handles hold linear Permission tokens) ──────────
    Stage(Stage const&) = delete("Stage holds linear Permission tokens via the consumer/producer handles");
    Stage& operator=(Stage const&) = delete("Stage holds linear Permission tokens via the consumer/producer handles");
    Stage(Stage&&) noexcept = default;
    Stage& operator=(Stage&&) noexcept = default;

    // ── run() — invokes the stage body, consuming the held handles ─
    //
    // &&-qualified: running a Stage CONSUMES it.  After .run() returns,
    // the handles have been moved into FnPtr and the Stage is in a
    // moved-from state (destructor still runs cleanly — handles are
    // moved-from but valid).
    //
    // The stage body is responsible for the drain loop, the per-call
    // try_pop / try_push policy, and termination (when its consumer
    // observes the upstream channel close).  Stage itself is purely
    // structural.

    void run() && noexcept {
        FnPtr(std::move(in_), std::move(out_));
    }

    // ── Accessors ──────────────────────────────────────────────────
    [[nodiscard]] constexpr Ctx const& ctx() const noexcept { return ctx_; }

    [[nodiscard]] constexpr consumer_handle_type&       in()       &  noexcept { return in_; }
    [[nodiscard]] constexpr consumer_handle_type const& in() const &  noexcept { return in_; }

    [[nodiscard]] constexpr producer_handle_type&       out()       &  noexcept { return out_; }
    [[nodiscard]] constexpr producer_handle_type const& out() const &  noexcept { return out_; }

private:
    [[no_unique_address]] Ctx     ctx_;
    consumer_handle_type           in_;
    producer_handle_type           out_;
};

// ═════════════════════════════════════════════════════════════════════
// ── mint_stage<auto FnPtr>(ctx, in, out) — Universal Mint Pattern ─
// ═════════════════════════════════════════════════════════════════════
//
// Token-mint factory per CLAUDE.md §XXI ctx-bound flavor.  Authority
// derives from CtxFitsStage<FnPtr, Ctx>; the held handles are
// move-consumed into the Stage.
//
// Why FnPtr is non-deducible:
// FnPtr appears only in the requires-clause and in the deduced handle
// types — there is no parameter whose type is "function-pointer of FnPtr"
// from which deduction could pin FnPtr.  Callers therefore spell
// `mint_stage<&my_stage_body>(ctx, in, out)` explicitly, which is the
// load-bearing site that pins WHICH stage body runs.

template <auto FnPtr, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsStage<FnPtr, Ctx>
[[nodiscard]] constexpr auto mint_stage(
    Ctx const&                                                       ctx,
    typename Stage<FnPtr, Ctx>::consumer_handle_type&& in,
    typename Stage<FnPtr, Ctx>::producer_handle_type&& out) noexcept
{
    return Stage<FnPtr, Ctx>{ctx, std::move(in), std::move(out)};
}

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// These pin the integration with the FOUND-D19 PipelineStage shape:
// every ConsumerHandle / ProducerHandle pair recognized by D05/D06 is
// admitted; non-stage shapes are rejected.

namespace detail::stage_self_test {

namespace eff = ::crucible::effects;
namespace saf = ::crucible::safety::extract;

// ── Fixture handles satisfying IsConsumerHandle / IsProducerHandle ─
// (mirror the patterns used by D05/D06 self-tests — try_pop returns
// std::optional<T>, try_push takes T const& and returns bool).

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

static_assert(::crucible::safety::extract::is_consumer_handle_v<FakeConsumer<int>>);
static_assert(::crucible::safety::extract::is_producer_handle_v<FakeProducer<int>>);

// ── Pipeline-stage-shape function: pass-through int relay ─────────
inline void stage_pass_through(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}
static_assert(saf::PipelineStage<&stage_pass_through>);

// ── Pipeline-stage-shape function: int → float transform ──────────
inline void stage_transform_int_to_float(FakeConsumer<int>&&, FakeProducer<float>&&) noexcept {}
static_assert(saf::PipelineStage<&stage_transform_int_to_float>);

// ── Non-PipelineStage candidates ──────────────────────────────────
inline void stage_wrong_arity(FakeConsumer<int>&&) noexcept {}
inline int  stage_returns_int(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept { return 0; }
inline void stage_wrong_param_kind(FakeConsumer<int>&, FakeProducer<int>&&) noexcept {}

static_assert(!saf::PipelineStage<&stage_wrong_arity>);
static_assert(!saf::PipelineStage<&stage_returns_int>);
static_assert(!saf::PipelineStage<&stage_wrong_param_kind>);

// ── CtxFitsStage admits / rejects appropriately ───────────────────
static_assert( CtxFitsStage<&stage_pass_through,           eff::HotFgCtx>);
static_assert( CtxFitsStage<&stage_pass_through,           eff::BgDrainCtx>);
static_assert( CtxFitsStage<&stage_transform_int_to_float, eff::HotFgCtx>);
static_assert(!CtxFitsStage<&stage_wrong_arity,            eff::HotFgCtx>);
static_assert(!CtxFitsStage<&stage_returns_int,            eff::HotFgCtx>);
static_assert(!CtxFitsStage<&stage_pass_through,           int>);  // int isn't an ExecCtx

// ── Stage<...> type-level invariants ───────────────────────────────
using S1 = Stage<&stage_pass_through, eff::HotFgCtx>;

static_assert(std::is_same_v<typename S1::ctx_type, eff::HotFgCtx>);
static_assert(std::is_same_v<typename S1::consumer_handle_type, FakeConsumer<int>>);
static_assert(std::is_same_v<typename S1::producer_handle_type, FakeProducer<int>>);
static_assert(std::is_same_v<typename S1::input_value_type,  int>);
static_assert(std::is_same_v<typename S1::output_value_type, int>);
static_assert(S1::is_value_preserving);
static_assert(S1::fn_ptr == &stage_pass_through);

using S2 = Stage<&stage_transform_int_to_float, eff::BgDrainCtx>;
static_assert(std::is_same_v<typename S2::input_value_type,  int>);
static_assert(std::is_same_v<typename S2::output_value_type, float>);
static_assert(!S2::is_value_preserving);

// ── Move-only enforcement ──────────────────────────────────────────
static_assert(!std::is_copy_constructible_v<S1>);
static_assert(!std::is_copy_assignable_v<S1>);
static_assert( std::is_move_constructible_v<S1>);
static_assert( std::is_move_assignable_v<S1>);

}  // namespace detail::stage_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Per the project's runtime-smoke-test discipline (auto-memory:
// feedback_algebra_runtime_smoke_test_discipline) every header that
// ships consteval / SFINAE machinery must include a runtime call site
// to defeat compiler eliding bugs that pure static_assert can mask.

inline bool stage_runtime_smoke_test() noexcept {
    namespace dst = detail::stage_self_test;
    namespace eff = ::crucible::effects;

    eff::HotFgCtx ctx;
    dst::FakeConsumer<int> in;
    dst::FakeProducer<int> out;

    auto stage = mint_stage<&dst::stage_pass_through>(
        ctx, std::move(in), std::move(out));

    // Move it once to exercise the move ctor.
    auto stage_moved = std::move(stage);

    // Run consumes it (the body is a no-op so this is safe).
    std::move(stage_moved).run();

    return true;
}

}  // namespace crucible::concurrent
