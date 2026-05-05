// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-025: mint_stage rejects a stage whose output payload row is not
// admitted by the stage Ctx.

#include <crucible/concurrent/Stage.h>
#include <crucible/effects/Computation.h>
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

using BgPayload = eff::Computation<eff::Row<eff::Effect::Bg>, int>;

inline void stage(FakeConsumer<int>&&, FakeProducer<BgPayload>&&) noexcept {}

int main() {
    eff::ColdInitCtx ctx;
    FakeConsumer<int> in;
    FakeProducer<BgPayload> out;
    auto bad = conc::mint_stage<&stage>(ctx, std::move(in), std::move(out));
    (void)bad;
    return 0;
}
