// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-226 fixture #4 — duplicate engine<IoUring> rejected.
//
// `CtxFitsIoUringMint<Ctx, Grants...>` requires `!has_duplicate_engine_v<>`:
// engaging more than one grant on the engine axis (even if both are
// `engine::IoUring`) is rejected because the type system cannot decide
// which copy is authoritative.  This mirrors the V-225
// `!has_duplicate_prot_v<>` discipline.
//
// Mismatch class: engine axis engaged twice.
// Distinguishes from fixture #2 (engine engaged once with wrong enumerator).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "CtxFitsIoUringMint" /
//   "constraints not satisfied" / "has_duplicate_engine".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwio  = ::crucible::fixy::wrap::io;
    namespace engine = fwio::engine;
    namespace grant  = fwio::grant;

    ::crucible::effects::TestRunnerCtx ctx{};

    // Should FAIL: two engine<IoUring> grants in the pack triggers
    // has_duplicate_engine_v<>; even though both name the SAME
    // enumerator, the type system refuses the redundant engagement.
    [[maybe_unused]] auto r = fwio::mint_io_uring_ring<
        grant::engine<engine::IoUring>,
        grant::engine<engine::IoUring>,
        grant::sq_entries<128>
    >(ctx);
    return 0;
}
