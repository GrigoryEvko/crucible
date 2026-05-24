// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-091 fixture #1/4 — Path<External> rejected at
// mint_durable_truncate_file (delegated through mint_file).
//
// `mint_durable_truncate_file<Ctx>(ctx, Path<Sanitized>, mode_t)` is a
// composite mint that delegates to the underlying `mint_file<...>` with
// a pinned grant stack (mode<WriteTruncate> + durable<Fsync> +
// atomic_write<LinkAtomic>).  The Sanitized path tag is preserved at
// the durable mint's surface — passing Path<External> directly bypasses
// the V-031/V-233 sanitize_path_no_dotdot discharge.
//
// Mismatch class: Path source-tag mismatch (External ≠ Sanitized) at
// the OUTER durable-mint signature, before delegation to mint_file.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "could not convert" / "cannot convert" /
//   "conversion from".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Source.h>           // fixy::tags::source::*
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwfs = ::crucible::fixy::wrap::fs;

    ::crucible::effects::TestRunnerCtx ctx{};

    // Trust-boundary path — NOT sanitized.
    fwfs::Path<::crucible::fixy::tags::source::External> external_path{
        "/tmp/crucible_neg_v226_durable_truncate_external"};

    // Should FAIL: mint_durable_truncate_file's signature takes
    // `Path<Sanitized>` — passing Path<External> is a tag-mismatch
    // refusal at the type system, before any delegation to mint_file.
    [[maybe_unused]] auto r =
        fwfs::mint_durable_truncate_file(ctx, std::move(external_path));
    return 0;
}
