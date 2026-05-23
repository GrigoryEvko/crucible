// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-225 fixture #1 — empty Grants pack rejected at mint_mmap.
//
// `CtxFitsMmapMint<Ctx>` requires the Grants pack to engage BOTH
// (a) at least one `with_prot<X>` and (b) at least one primary
// `with_share<X>` (Private/Shared/Anonymous).  An empty pack fails
// both checks: `has_prot_grant_v<>` is false, `has_primary_share_grant_v<>`
// is false, so the requires-clause refuses the instantiation.
//
// Mismatch class: missing prot + missing primary share engagement.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "CtxFitsMmapMint" /
//   "constraints not satisfied" / "has_prot_grant" / "has_primary_share".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Wrap.h>

struct TestRegion {};   // dummy Tag

int main() {
    namespace fwmm = ::crucible::fixy::wrap::mmap;

    ::crucible::effects::TestRunnerCtx ctx{};

    // Should FAIL: empty Grants pack fails has_prot_grant_v +
    // has_primary_share_grant_v predicates in CtxFitsMmapMint.
    [[maybe_unused]] auto r = fwmm::mint_mmap<TestRegion>(
        ctx, /*fd=*/-1, /*length=*/4096);
    return 0;
}
