// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The output payload is a computation engaged at Row<Bg>, and the
// startup context owns Init, Alloc and IO but not Bg.  A context with
// effects of its own, just not the one the payload needs, is the sharper
// case than the empty foreground row.  The startup context is handed its
// capability rather than built from nothing, so the probe goes through
// declval instead of constructing one.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Stage.h>
#include <foundation/effects/Computation.h>

#include <optional>
#include <utility>

namespace {

namespace eff = foundation::effects;

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

using Bad = decltype(fixy::concurrent::mint_stage<&stage>(std::declval<fixy::ColdInitCtx const&>(),
                                                          std::declval<FakeConsumer<int>&&>(),
                                                          std::declval<FakeProducer<BgPayload>&&>()));

}  // namespace

int main() { return 0; }
