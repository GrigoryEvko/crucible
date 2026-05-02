// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Tier 3 commit 1 audit round 2 (CLAUDE.md §XXI HS14): Stage::run() is
// rvalue-ref-qualified
//
//   void run() && noexcept;
//
// because running CONSUMES the stage — the held handles are moved
// into the FnPtr invocation and the Stage is in a moved-from state
// after .run() returns.  Calling .run() on an lvalue Stage would
// silently leak the consume semantic (the caller could attempt a
// second .run() expecting valid handles).  The ref-qualifier rejects
// it at compile time.
//
// Violation: calls `stage.run()` on an lvalue (no std::move).  The
// && qualifier fires; only `std::move(stage).run()` is admitted.
//
// Expected diagnostic: "no matching function for call|cannot bind|
// passing.*as.*'this'|discards qualifiers" — the ref-qualifier
// rejection diagnostic family.

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

    stage.run();  // <-- lvalue .run() rejected by && qualifier
    return 0;
}
