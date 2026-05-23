// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-226 fixture #6 — zerocopy<None> rejected at mint_zerocopy_transfer.
//
// `CtxFitsZerocopyMint<Ctx, Grants...>` requires:
//
//     !pack_zerocopy_is_none_v<Grants...>
//
// `zerocopy::None` is the type-system SENTINEL meaning "no zerocopy
// primitive selected".  Allowing it on the mint surface would mean
// the caller invoked a zerocopy transfer mint that does not actually
// perform zerocopy — a pure footgun.  Callers who genuinely want
// userspace-staged read/write should NOT route through
// `mint_zerocopy_transfer`; they should use `mint_file` + buffered
// read/write per V-224.
//
// Mismatch class: zerocopy axis engaged but with sentinel enumerator.
// Distinct from "no zerocopy engaged at all" (which fails has_zerocopy).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "CtxFitsZerocopyMint" /
//   "constraints not satisfied" / "pack_zerocopy_is_none".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwio    = ::crucible::fixy::wrap::io;
    namespace zerocopy = fwio::zerocopy;
    namespace grant    = fwio::grant;

    ::crucible::effects::TestRunnerCtx ctx{};

    // Should FAIL: zerocopy<None> engages the axis with the sentinel
    // enumerator; pack_zerocopy_is_none_v<> is true and the requires
    // refuses.
    [[maybe_unused]] auto r = fwio::mint_zerocopy_transfer<
        grant::zerocopy<zerocopy::None>
    >(ctx,
      /*src_fd=*/-1, /*dst_fd=*/-1, /*length=*/4096,
      /*off_in=*/nullptr, /*off_out=*/nullptr);
    return 0;
}
