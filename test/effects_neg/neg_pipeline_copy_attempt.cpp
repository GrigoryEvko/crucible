// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Tier 3 commit 2 audit round 2 (CLAUDE.md §XXI HS14): Pipeline<Stages...>
// is move-only.  The copy constructor is `= delete("Pipeline holds
// move-only Stages, each of which holds linear Permission tokens via
// its consumer/producer handles")` — copying would duplicate the
// linear Permission tokens transitively held by every Stage in the
// tuple.
//
// Violation: constructs a Pipeline via mint_pipeline, then attempts
// to copy-construct another Pipeline from it.  The deleted copy
// ctor fires.
//
// Expected diagnostic: "use of deleted function|deleted with|
// holds move-only Stages" — the = delete reason string is grep-
// stable.

#include <crucible/concurrent/Pipeline.h>
#include <crucible/effects/ExecCtx.h>

#include <optional>
#include <utility>

namespace conc = crucible::concurrent;
namespace eff  = crucible::effects;

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

    FakeConsumer<int> in0; FakeProducer<int> out0;
    auto stage0 = conc::mint_stage<&pass_through>(ctx, std::move(in0), std::move(out0));

    FakeConsumer<int> in1; FakeProducer<int> out1;
    auto stage1 = conc::mint_stage<&pass_through>(ctx, std::move(in1), std::move(out1));

    auto pipeline = conc::mint_pipeline(ctx, std::move(stage0), std::move(stage1));

    auto pipeline_copy = pipeline;  // <-- deleted copy ctor fires
    (void)pipeline_copy;
    return 0;
}
