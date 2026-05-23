// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-226 fixture #3 — sq_entries<3> rejected at mint_io_uring_ring.
//
// `CtxFitsIoUringMint<Ctx, Grants...>` requires the sole engaged
// `sq_entries<N>` to be a power of two ≤ 32768.  io_uring_setup
// internally rounds N up to the next power of two, but accepting
// a non-pow2 caller would silently disagree with the eventual ring
// size: the caller asked for 3, the kernel gave them 4.  We refuse
// at the type system rather than paper over the mismatch.
//
// Mismatch class: sq_entries engaged but not a power of two.
// Distinct from fixture #1 (missing sq_entries entirely).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "CtxFitsIoUringMint" /
//   "constraints not satisfied" / "sq_entries_is_pow2".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwio  = ::crucible::fixy::wrap::io;
    namespace engine = fwio::engine;
    namespace grant  = fwio::grant;

    ::crucible::effects::TestRunnerCtx ctx{};

    // Should FAIL: sq_entries<3> engages the axis but fails the
    // pow2-ness check (is_pow2_(3) is false).
    [[maybe_unused]] auto r = fwio::mint_io_uring_ring<
        grant::engine<engine::IoUring>,
        grant::sq_entries<3>
    >(ctx);
    return 0;
}
