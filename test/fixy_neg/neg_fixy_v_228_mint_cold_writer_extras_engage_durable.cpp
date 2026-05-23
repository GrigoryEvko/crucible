// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-228 fixture #4 — extras engaging durable<> rejected at
// mint_cold_writer.
//
// `CtxFitsColdWriterMint<Ctx, Extras...>` requires
// `!detail::extras_engage_durable_v<Extras...>` — the cold-tier
// stance PINS its sync-op axis to `sync_op::Fsync` (full meta+data
// sync per write).  Allowing the caller to engage another
// `grant_fs::durable<>` in Extras would silently disagree with the
// stance posture: the cold-writer's inherent `sync()` member always
// dispatches via `cold_writer_stance::sync_op_type = Fsync`, but a
// caller-supplied `durable<Fdatasync>` extras-grant would express the
// CONTRADICTORY expectation that fdatasync suffices.  Refuse one
// diagnostic layer up.
//
// Mismatch class: extras shadowing a stance-pinned axis
// (extras_engage_durable_v=true).  Distinct from fixture #3
// (ctx-row gap on Effect::Block).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "CtxFitsColdWriterMint" /
//   "constraints not satisfied" / "extras_engage_durable".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwcd = ::crucible::fixy::wrap::cipher::durable;
    namespace fwfs = ::crucible::fixy::wrap::fs;
    namespace so   = fwfs::sync_op;

    // TestRunnerCtx — Row<Test, Alloc, IO, Block> — admits IO+Block.
    ::crucible::effects::TestRunnerCtx ctx{};

    fwfs::Path<::crucible::fixy::tags::source::Sanitized> path{
        "/tmp/crucible_neg_v228_cold_extras_engage_durable"};

    // Should FAIL: extras engages grant_fs::durable<Fdatasync>, but
    // cold_writer_stance PINS sync-op to Fsync; the §XXI mint's
    // `!detail::extras_engage_durable_v<Extras...>` clause is false.
    [[maybe_unused]] auto r = fwcd::mint_cold_writer<
        fwfs::grant::durable<so::Fdatasync>
    >(ctx, std::move(path));
    return 0;
}
