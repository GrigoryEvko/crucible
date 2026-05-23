// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-225 fixture #3 — duplicate with_prot<X> grants rejected.
//
// `CtxFitsMmapMint`'s uniqueness clause:
//
//     !has_duplicate_prot_v<Grants...>
//
// counts (is_with_prot_v<G> + ... > 1).  Two prot tiers in one mint
// is ambiguous (PROT_READ vs PROT_READ|PROT_WRITE — which wins?  The
// fold OR would yield PROT_READ|PROT_WRITE but the caller's intent is
// unclear).  Refuse it; the caller picks one tier.
//
// Mismatch class: prot-axis duplicate engagement.
// Distinct from fixture #1 (no prot at all) and fixture #2 (Exec gate).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "CtxFitsMmapMint" /
//   "constraints not satisfied" / "has_duplicate_prot".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Wrap.h>

struct DupProtRegion {};

int main() {
    namespace fwmm = ::crucible::fixy::wrap::mmap;
    namespace prot  = fwmm::prot;
    namespace share = fwmm::share;
    namespace grant = fwmm::grant;

    ::crucible::effects::TestRunnerCtx ctx{};

    // Should FAIL: two with_prot<X> grants.
    [[maybe_unused]] auto r = fwmm::mint_mmap<
        DupProtRegion,
        grant::with_prot<prot::ReadOnly>,
        grant::with_prot<prot::ReadWrite>,
        grant::with_share<share::Private>
    >(ctx, /*fd=*/-1, /*length=*/4096);
    return 0;
}
