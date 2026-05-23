// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-226 fixture #7 — zerocopy<Splice> rejected at mint_zerocopy_transfer.
//
// `CtxFitsZerocopyMint<Ctx, Grants...>` requires:
//
//     pack_zerocopy_is_simple_transfer_v<Grants...>
//
// The simple-transfer subset is exactly {Sendfile, CopyFileRange} —
// both expose a `(src_fd, dst_fd, length, off_in, off_out)` shape
// that fits the one-shot mint signature.  `Splice` requires a
// pipe-pair intermediary; `MsgZerocopy` requires a socket + msg
// pre-pinned page tracking.  Neither fits the mint's one-shot
// signature, so they ship as type-level grant enumerators only and
// are gated OUT of the mint surface.  Callers who need them must
// route through a future per-primitive mint family.
//
// Mismatch class: zerocopy axis engaged with non-simple-transfer
// enumerator.  Distinguishes from fixture #6 (sentinel None).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "CtxFitsZerocopyMint" /
//   "constraints not satisfied" / "pack_zerocopy_is_simple_transfer".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwio    = ::crucible::fixy::wrap::io;
    namespace zerocopy = fwio::zerocopy;
    namespace grant    = fwio::grant;

    ::crucible::effects::TestRunnerCtx ctx{};

    // Should FAIL: zerocopy<Splice> engages the axis with a non-simple-
    // transfer enumerator; pack_zerocopy_is_simple_transfer_v<> is false.
    [[maybe_unused]] auto r = fwio::mint_zerocopy_transfer<
        grant::zerocopy<zerocopy::Splice>
    >(ctx,
      /*src_fd=*/-1, /*dst_fd=*/-1, /*length=*/4096,
      /*off_in=*/nullptr, /*off_out=*/nullptr);
    return 0;
}
