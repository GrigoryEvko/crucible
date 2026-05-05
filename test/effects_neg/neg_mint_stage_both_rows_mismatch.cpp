// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-025: mint_stage checks both payload directions, so hiding
// effectful input and output payloads behind empty HotFgCtx is rejected.

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
using BgPayload = eff::Computation<eff::Row<eff::Effect::Bg>, int>;

inline void stage(FakeConsumer<IoPayload>&&, FakeProducer<BgPayload>&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;
    FakeConsumer<IoPayload> in;
    FakeProducer<BgPayload> out;
    auto bad = conc::mint_stage<&stage>(ctx, std::move(in), std::move(out));
    (void)bad;
    return 0;
}
