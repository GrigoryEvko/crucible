// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Tier 3 commit 1 audit round 2 (CLAUDE.md §XXI HS14): Stage<FnPtr, Ctx>
// is move-only.  The copy constructor is `= delete("Stage holds linear
// Permission tokens via the consumer/producer handles")` — copying
// would duplicate the linear Permission tokens carried by the held
// endpoint handles, defeating the CSL frame rule.
//
// Violation: constructs a Stage via mint_stage, then attempts to
// copy-construct another Stage from it.  The deleted copy ctor
// fires.
//
// Expected diagnostic: "use of deleted function|deleted with|holds
// linear Permission tokens" — the = delete reason string is grep-
// stable and survives toolchain bumps.

#include <crucible/concurrent/Stage.h>
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
    FakeConsumer<int> in;
    FakeProducer<int> out;

    auto stage = conc::mint_stage<&pass_through>(
        ctx, std::move(in), std::move(out));

    auto stage_copy = stage;  // <-- deleted copy ctor fires
    (void)stage_copy;
    return 0;
}
