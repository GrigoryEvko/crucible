// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-228 fixture #6 — extras engaging atomic_write<> rejected at
// mint_head_advancer.
//
// `CtxFitsHeadAdvancerMint<Ctx, Extras...>` requires
// `!detail::extras_engage_atomic_write_v<Extras...>` — the
// HEAD-advance stance PINS its atomicity axis to
// `atomicity::RenameAt2NoReplace` (renameat2 with RENAME_NOREPLACE,
// the only atomic publish that refuses to clobber an existing HEAD).
// Allowing the caller to engage another `grant_fs::atomic_write<>`
// in Extras would silently disagree with the stance posture: the
// head-advancer's inherent `commit_atomic()` always dispatches via
// `head_advance_stance::atomicity_type = RenameAt2NoReplace`, but a
// caller-supplied `atomic_write<LinkAtomic>` extras-grant would
// express the CONTRADICTORY expectation that linkat AT_EMPTY_PATH
// is the publish primitive.  Refuse one diagnostic layer up.
//
// Mismatch class: extras shadowing a stance-pinned axis
// (extras_engage_atomic_write_v=true).  Distinct from fixture #5
// (ctx-row gap on Effect::Block).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "CtxFitsHeadAdvancerMint" /
//   "constraints not satisfied" / "extras_engage_atomic_write".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwcd = ::crucible::fixy::wrap::cipher::durable;
    namespace fwfs = ::crucible::fixy::wrap::fs;
    namespace at   = fwfs::atomicity;

    // TestRunnerCtx — Row<Test, Alloc, IO, Block> — admits IO+Block.
    ::crucible::effects::TestRunnerCtx ctx{};

    fwfs::Path<::crucible::fixy::tags::source::Sanitized> path{
        "/tmp/crucible_neg_v228_head_extras_engage_atomic_write"};

    // Should FAIL: extras engages grant_fs::atomic_write<LinkAtomic>,
    // but head_advance_stance PINS atomicity to RenameAt2NoReplace;
    // the §XXI mint's `!detail::extras_engage_atomic_write_v<Extras...>`
    // clause is false.
    [[maybe_unused]] auto r = fwcd::mint_head_advancer<
        fwfs::grant::atomic_write<at::LinkAtomic>
    >(ctx, std::move(path));
    return 0;
}
