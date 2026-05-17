// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Pipe fixture #2: mint_pipeline via fixy:: alias rejects
// when the variadic pack contains a non-Stage element.
//
// Violation: passes a bare int as one of the pipeline stages.
// `pipeline_chain<int>` is false; CtxFitsPipeline rejects.  Routing
// through `fixy::pipe::mint_pipeline` must reject identically.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsPipeline / pipeline_chain / IsStage.

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Pipe.h>

#include <optional>
#include <utility>

namespace eff   = crucible::effects;
namespace fpipe = crucible::fixy::pipe;

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

inline void pass_through(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;

    FakeConsumer<int> in;
    FakeProducer<int> out;
    auto stage = fpipe::mint_stage<&pass_through>(
        ctx, std::move(in), std::move(out));

    int not_a_stage = 42;

    auto bad = fpipe::mint_pipeline(ctx, std::move(stage), not_a_stage);
    (void)bad;
    return 0;
}
