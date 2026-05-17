// ── test_fixy_pipe_chain — FIXY-AUDIT-C2 sentinel ──────────────────
//
// Positive-compile witness for the pipeline coherence helpers
// re-exported under fixy::pipe::
//
//   - IsStage<T>                        — Stage<FnPtr,Ctx> recognizer
//   - stages_chain<S1, S2>              — adjacent-pair I/O equality
//   - pipeline_chain<Stages...>         — N-ary fold
//   - pipeline_row_union_t<Stages...>   — union of stage ctx rows
//   - CtxFitsPipeline<Ctx, Stages...>   — single concept gate
//
// Builds a 2-stage pipeline (Stage<&body_a> -> Stage<&body_b>) where
// body_a's output type matches body_b's input type, then asserts
//   1. each Stage satisfies IsStage,
//   2. stages_chain<S1, S2> holds,
//   3. pipeline_chain_v<S1, S2> holds,
//   4. pipeline_row_union_t<S1, S2> equals the expected Row.
// Task #1426.

#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Pipe.h>

#include <optional>
#include <type_traits>

namespace fpipe = crucible::fixy::pipe;
namespace eff   = crucible::effects;
namespace cc    = crucible::concurrent;

// ─── 1. Stub consumer / producer types ─────────────────────────────

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

// ─── 2. Two compatible-payload bodies ─────────────────────────────
//
// body_a:  Consumer<int> -> Producer<int>  (output is int)
// body_b:  Consumer<int> -> Producer<int>  (input  is int)

inline void body_a(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}
inline void body_b(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

using StageA = cc::Stage<&body_a, eff::HotFgCtx>;
using StageB = cc::Stage<&body_b, eff::HotFgCtx>;

// ─── 3. IsStage recognizer ─────────────────────────────────────────

static_assert(fpipe::IsStage<StageA>,
    "IsStage must accept a Stage<FnPtr, Ctx>.");
static_assert(fpipe::IsStage<StageB>,
    "IsStage must accept the second Stage.");
static_assert(!fpipe::IsStage<int>,
    "IsStage must reject bare int.");

// ─── 4. stages_chain — adjacent-pair coherence ────────────────────

static_assert(fpipe::stages_chain<StageA, StageB>,
    "stages_chain<StageA, StageB> must hold: body_a's output payload "
    "(int) equals body_b's input payload (int).");

// ─── 5. pipeline_chain — N-ary fold over adjacent pairs ───────────

static_assert(fpipe::pipeline_chain<StageA, StageB>,
    "pipeline_chain over (StageA, StageB) must hold.");
static_assert(fpipe::pipeline_chain<StageA>,
    "pipeline_chain over a single stage holds (no adjacent pairs).");
// Note: pipeline_chain<> (empty pack) is REJECTED by the substrate
// concept which requires `sizeof...(Stages) >= 1`; the docstring at
// line 318 of concurrent/Pipeline.h calls it "vacuously chain-
// compatible" but the concept body requires N >= 1.  We don't
// witness the empty case here.

// ─── 6. pipeline_row_union_t — union of each stage's ctx::row_type ─
//
// HotFgCtx has row_type == Row<>, so the union over two HotFgCtx
// stages is Row<>.

static_assert(std::is_same_v<
    fpipe::pipeline_row_union_t<StageA, StageB>,
    eff::Row<>>,
    "pipeline_row_union_t over two HotFgCtx stages must equal Row<>.");

// ─── 7. CtxFitsPipeline — single concept gate ─────────────────────

static_assert(fpipe::CtxFitsPipeline<eff::HotFgCtx, StageA, StageB>,
    "CtxFitsPipeline must hold for a HotFgCtx coordinator over two "
    "HotFgCtx stages whose union row is Row<>.");

// ─── 8. Runtime construction round-trip (sanity) ──────────────────

int main() {
    eff::HotFgCtx ctx;
    FakeConsumer<int> in_a;
    FakeProducer<int> out_a;
    FakeConsumer<int> in_b;
    FakeProducer<int> out_b;

    auto sa = fpipe::mint_stage<&body_a>(ctx,
        std::move(in_a), std::move(out_a));
    auto sb = fpipe::mint_stage<&body_b>(ctx,
        std::move(in_b), std::move(out_b));
    auto pl = fpipe::mint_pipeline(ctx, std::move(sa), std::move(sb));
    (void)pl;
    return 0;
}
