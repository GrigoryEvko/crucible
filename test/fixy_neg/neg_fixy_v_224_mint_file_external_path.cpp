// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-224 fixture #1 — Path<External> rejected at mint_file.
//
// `mint_file<Grants...>(ctx, Path<Sanitized>)` takes a sanitized path
// at the §XXI boundary.  Passing a `Path<source::External>` directly
// (the trust-boundary surface where operator bytes arrive — not yet
// sanitized) must red at the call site so callers cannot bypass the
// V-031/V-233 sanitize_path_no_dotdot discharge.
//
// Mismatch class: Path source-tag mismatch (External ≠ Sanitized).
// Distinct from fixture #6 (ctx lacks Block) — this fires on the
// path parameter's tag, not on the ctx row.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "could not convert" / "cannot convert" /
//   "conversion from".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Source.h>           // fixy::tags::source::*
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwfs = ::crucible::fixy::wrap::fs;
    namespace om   = fwfs::open_mode;

    ::crucible::effects::TestRunnerCtx ctx{};

    // Trust-boundary path — NOT sanitized.
    fwfs::Path<::crucible::fixy::tags::source::External> external_path{
        "/tmp/crucible_neg_v224_external"};

    // Should FAIL: mint_file's signature is
    // `mint_file(Ctx const&, Path<Sanitized>, mode_t)` — passing
    // Path<External> is a tag-mismatch refusal at the type system.
    [[maybe_unused]] auto r =
        fwfs::mint_file<fwfs::grant::mode<om::ReadOnly>>(ctx, std::move(external_path));
    return 0;
}
