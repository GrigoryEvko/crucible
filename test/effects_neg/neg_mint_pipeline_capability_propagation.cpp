// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-026: a stage that needed a capability-carrying payload is
// minted under an admitting stage Ctx, but the pipeline coordinator
// must still admit the staged row before running it.

#include <crucible/concurrent/Pipeline.h>
#include <crucible/effects/Capability.h>
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

using Payload = eff::Capability<eff::Effect::Alloc, eff::Bg>;

inline void cap_stage(FakeConsumer<Payload>&&, FakeProducer<Payload>&&) noexcept {}

int main() {
    eff::BgDrainCtx stage_ctx;
    eff::HotFgCtx coordinator;

    FakeConsumer<Payload> in;
    FakeProducer<Payload> out;
    auto stage = conc::mint_stage<&cap_stage>(stage_ctx, std::move(in), std::move(out));

    auto bad = conc::mint_pipeline(coordinator, std::move(stage));
    (void)bad;
    return 0;
}
