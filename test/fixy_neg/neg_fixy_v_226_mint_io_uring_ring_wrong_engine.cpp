// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-226 fixture #2 — engine<Synchronous> rejected at mint_io_uring_ring.
//
// `CtxFitsIoUringMint<Ctx, Grants...>` requires the engaged engine to
// be exactly `engine::IoUring`.  `engine_is_io_uring_v<Grants...>`
// folds across the Grants pack and asserts the lone engine matches.
// `engine<Synchronous>` engages the axis but with the WRONG enumerator,
// so the requires-clause refuses.
//
// Mismatch class: engine engaged with non-IoUring enumerator.
// Distinguishes from fixture #1 (empty pack — no engine engagement at all).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "CtxFitsIoUringMint" /
//   "constraints not satisfied" / "engine_is_io_uring".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwio  = ::crucible::fixy::wrap::io;
    namespace engine = fwio::engine;
    namespace grant  = fwio::grant;

    ::crucible::effects::TestRunnerCtx ctx{};

    // Should FAIL: engine<Synchronous> engages the engine axis but with
    // the wrong enumerator; engine_is_io_uring_v<> is false.
    [[maybe_unused]] auto r = fwio::mint_io_uring_ring<
        grant::engine<engine::Synchronous>,
        grant::sq_entries<128>
    >(ctx);
    return 0;
}
