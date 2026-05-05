// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-025: mint_stage rejects a stage whose input payload row is not
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

using IoPayload = eff::Computation<eff::Row<eff::Effect::IO>, int>;

inline void stage(FakeConsumer<IoPayload>&&, FakeProducer<int>&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;
    FakeConsumer<IoPayload> in;
    FakeProducer<int> out;
    auto bad = conc::mint_stage<&stage>(ctx, std::move(in), std::move(out));
    (void)bad;
    return 0;
}
