// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-091 fixture #3/4 — Path<External> rejected at
// mint_durable_append_file (delegated through mint_file).
//
// `mint_durable_append_file<Ctx>(ctx, Path<Sanitized>, mode_t)` is the
// append-only sibling of mint_durable_truncate_file (grant stack
// mode<WriteAppend> + durable<Fdatasync> + with_flag<DataSync>).  Same
// Sanitized path requirement at the §XXI boundary.
//
// Mismatch class: Path source-tag mismatch (External ≠ Sanitized).
// Companion to fixture #1 (truncate form) — both use the SAME path-tag
// gate; the gate fires regardless of the underlying grant pack.
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

    fwfs::Path<::crucible::fixy::tags::source::External> external_path{
        "/tmp/crucible_neg_v226_durable_append_external"};

    // Should FAIL: same path-tag mismatch as fixture #1, but at
    // mint_durable_append_file's signature.
    [[maybe_unused]] auto r =
        fwfs::mint_durable_append_file(ctx, std::move(external_path));
    return 0;
}
