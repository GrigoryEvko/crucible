#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/fixy/Pipe.h>

#include <optional>
#include <type_traits>

namespace fpipe = crucible::fixy::pipe;
namespace eff = crucible::effects;
namespace cc = crucible::concurrent;

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

inline void body_a(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}
inline void body_b(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

using StageA = cc::Stage<&body_a, eff::HotFgCtx>;
using StageB = cc::Stage<&body_b, eff::HotFgCtx>;

static_assert(fpipe::IsStage<StageA>, "IsStage must accept a Stage<FnPtr, Ctx>.");
static_assert(fpipe::IsStage<StageB>, "IsStage must accept the second Stage.");
static_assert(!fpipe::IsStage<int>, "IsStage must reject bare int.");

static_assert(fpipe::stages_chain<StageA, StageB>, "stages_chain<StageA, StageB> must hold: body_a's output payload "
                                                   "(int) equals body_b's input payload (int).");

static_assert(fpipe::pipeline_chain<StageA, StageB>, "pipeline_chain over (StageA, StageB) must hold.");
static_assert(fpipe::pipeline_chain<StageA>, "pipeline_chain over a single stage holds (no adjacent pairs).");
// The empty pack is not witnessed. The concept requires at least one
// stage, so pipeline_chain<> does not hold at all.

// The hot foreground context carries an empty row, so the union over two
// such stages is empty as well.

static_assert(std::is_same_v<fpipe::pipeline_row_union_t<StageA, StageB>, eff::Row<>>,
              "pipeline_row_union_t over two HotFgCtx stages must equal Row<>.");

static_assert(fpipe::CtxFitsPipeline<eff::HotFgCtx, StageA, StageB>,
              "CtxFitsPipeline must hold for a HotFgCtx coordinator over two "
              "HotFgCtx stages whose union row is Row<>.");

int main() {
    eff::HotFgCtx ctx;
    FakeConsumer<int> in_a;
    FakeProducer<int> out_a;
    FakeConsumer<int> in_b;
    FakeProducer<int> out_b;

    auto sa = fpipe::mint_stage<&body_a>(ctx, std::move(in_a), std::move(out_a));
    auto sb = fpipe::mint_stage<&body_b>(ctx, std::move(in_b), std::move(out_b));
    auto pl = fpipe::mint_pipeline(ctx, std::move(sa), std::move(sb));
    (void)pl;
    return 0;
}
