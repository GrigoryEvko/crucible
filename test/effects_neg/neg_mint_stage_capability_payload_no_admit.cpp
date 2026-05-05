// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-025: Capability<E, S> payloads carry Row<E>; a Ctx whose row
// omits E cannot host the stage.

#include <crucible/concurrent/Stage.h>
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

inline void stage(FakeConsumer<Payload>&&, FakeProducer<int>&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;
    FakeConsumer<Payload> in;
    FakeProducer<int> out;
    auto bad = conc::mint_stage<&stage>(ctx, std::move(in), std::move(out));
    (void)bad;
    return 0;
}
