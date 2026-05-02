// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Tier 3 commit 2 audit round 2 (CLAUDE.md §XXI HS14): Pipeline::run()
// is rvalue-ref-qualified
//
//   void run() && noexcept;
//
// because running CONSUMES the pipeline — each stage is moved into
// its own jthread's body, where std::move(stage).run() invokes the
// FnPtr.  After Pipeline.run() returns, the held stages are
// moved-from; calling .run() twice or expecting the held stages to
// remain valid would be a soundness error.
//
// Violation: calls `pipeline.run()` on an lvalue (no std::move).
// The && qualifier rejects; only `std::move(pipeline).run()` is
// admitted.
//
// Expected diagnostic: "no matching function for call|cannot bind|
// passing.*as.*'this'|discards qualifiers".

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

    FakeConsumer<int> in; FakeProducer<int> out;
    auto stage = conc::mint_stage<&pass_through>(ctx, std::move(in), std::move(out));

    auto pipeline = conc::mint_pipeline(ctx, std::move(stage));

    pipeline.run();  // <-- lvalue .run() rejected by && qualifier
    return 0;
}
