// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-228 fixture #2 — extras engaging mode<> rejected at
// mint_warm_writer.
//
// `CtxFitsWarmWriterMint<Ctx, Extras...>` requires
// `!detail::extras_engage_mode_v<Extras...>` — the warm-tier stance
// PINS its open-mode axis to `open_mode::WriteTruncate`.  Allowing the
// caller to engage another `grant_fs::mode<>` in Extras would silently
// disagree with the stance posture: the inner `mint_file<...>` call
// would then receive TWO mode<> grants, the OR-fold over
// open_mode_flags would produce undefined semantics
// (O_RDONLY | O_WRONLY ≡ O_RDWR on Linux), and the warm-tier
// `commit_atomic<RenameAt2NoReplace>` discipline would be applied to
// a handle opened with the wrong intent.  Refuse one diagnostic layer
// up.
//
// Mismatch class: extras shadowing a stance-pinned axis
// (extras_engage_mode_v=true).  Distinct from fixture #1
// (ctx-row gap on Effect::Block).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "CtxFitsWarmWriterMint" /
//   "constraints not satisfied" / "extras_engage_mode".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwcd = ::crucible::fixy::wrap::cipher::durable;
    namespace fwfs = ::crucible::fixy::wrap::fs;
    namespace om   = fwfs::open_mode;

    // TestRunnerCtx — Row<Test, Alloc, IO, Block> — admits IO+Block.
    ::crucible::effects::TestRunnerCtx ctx{};

    fwfs::Path<::crucible::fixy::tags::source::Sanitized> path{
        "/tmp/crucible_neg_v228_warm_extras_engage_mode"};

    // Should FAIL: extras engages grant_fs::mode<WriteCreate>, but
    // warm_writer_stance PINS mode to WriteTruncate; the §XXI mint's
    // `!detail::extras_engage_mode_v<Extras...>` clause is false.
    [[maybe_unused]] auto r = fwcd::mint_warm_writer<
        fwfs::grant::mode<om::WriteCreate>
    >(ctx, std::move(path));
    return 0;
}
