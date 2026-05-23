// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-224 fixture #5 — Path<External> target rejected at commit_atomic.
//
// `commit_atomic<Atomicity>(ctx, Path<Sanitized> tmp, Path<Sanitized> target)`
// takes BOTH the source AND the target as Path<Sanitized>.  This
// catches the "caller sanitized the tmp path but forgot to sanitize
// the target" regression — the V-031/V-233 sanitize discharge applies
// to EVERY path crossing the syscall surface, not just the source.
//
// Mismatch class: Path target-tag mismatch (External ≠ Sanitized).
// Distinct from fixture #1 (path tag on mint_file's input) — this
// fires on commit_atomic's TARGET parameter, demonstrating the gate
// applies to every Path-typed parameter independently.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "could not convert" / "cannot convert" /
//   "conversion from".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Source.h>           // fixy::tags::source::*
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwfs = ::crucible::fixy::wrap::fs;
    namespace at_  = fwfs::atomicity;

    ::crucible::effects::TestRunnerCtx ctx{};

    fwfs::Path<::crucible::fixy::tags::source::Sanitized> tmp_path{
        "/tmp/crucible_neg_v224_tmp"};
    // Target is External — operator-supplied, NOT sanitized.
    fwfs::Path<::crucible::fixy::tags::source::External> external_target{
        "/tmp/crucible_neg_v224_target"};

    // Should FAIL: commit_atomic's target parameter is
    // Path<Sanitized>; passing Path<External> is tag-mismatch.
    [[maybe_unused]] auto r = fwfs::commit_atomic<at_::Rename>(
        ctx, std::move(tmp_path), std::move(external_target));
    return 0;
}
