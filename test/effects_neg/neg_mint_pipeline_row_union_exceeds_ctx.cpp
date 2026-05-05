// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-026: mint_pipeline rejects a coordinator Ctx that does not
// admit the union of all staged execution-context rows.

#include <crucible/concurrent/Pipeline.h>
#include <crucible/effects/ExecCtx.h>

#include <optional>
#include <utility>

namespace conc = crucible::concurrent;
namespace eff = crucible::effects;

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

inline void pass(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

int main() {
    eff::BgDrainCtx bg_ctx;
    eff::ColdInitCtx init_ctx;
    eff::HotFgCtx coordinator;

    FakeConsumer<int> in0;
    FakeProducer<int> out0;
    auto s0 = conc::mint_stage<&pass>(bg_ctx, std::move(in0), std::move(out0));

    FakeConsumer<int> in1;
    FakeProducer<int> out1;
    auto s1 = conc::mint_stage<&pass>(init_ctx, std::move(in1), std::move(out1));

    auto bad = conc::mint_pipeline(coordinator, std::move(s0), std::move(s1));
    (void)bad;
    return 0;
}
