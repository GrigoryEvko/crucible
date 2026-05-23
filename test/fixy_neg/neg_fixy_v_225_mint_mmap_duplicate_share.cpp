// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-225 fixture #4 — duplicate primary with_share<X> grants rejected.
//
// `CtxFitsMmapMint`'s second uniqueness clause:
//
//     !has_duplicate_primary_share_v<Grants...>
//
// counts only PRIMARY shares (Private/Shared/Anonymous; additive
// Locked/Populate/HugeTLB are NOT primary and stack freely).  Two
// primary tiers is the same kind of ambiguity as duplicate prot — what
// would MAP_SHARED|MAP_PRIVATE even mean? — so refuse it.
//
// Mismatch class: share-axis primary-tier duplicate engagement.
// Distinct from fixture #3 — this fires on the share axis, not prot.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "CtxFitsMmapMint" /
//   "constraints not satisfied" / "has_duplicate_primary_share".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Wrap.h>

struct DupShareRegion {};

int main() {
    namespace fwmm = ::crucible::fixy::wrap::mmap;
    namespace prot  = fwmm::prot;
    namespace share = fwmm::share;
    namespace grant = fwmm::grant;

    ::crucible::effects::TestRunnerCtx ctx{};

    // Should FAIL: Private + Shared in one pack (both primary).
    [[maybe_unused]] auto r = fwmm::mint_mmap<
        DupShareRegion,
        grant::with_prot<prot::ReadOnly>,
        grant::with_share<share::Private>,
        grant::with_share<share::Shared>
    >(ctx, /*fd=*/-1, /*length=*/4096);
    return 0;
}
