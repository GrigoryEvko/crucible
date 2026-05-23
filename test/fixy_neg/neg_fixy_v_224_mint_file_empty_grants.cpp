// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-224 fixture #2 — empty Grants pack rejected at mint_file.
//
// `CtxFitsFileMint<Ctx, Grants...>` requires `has_mode_v<Grants...>`
// — every mint_file call site MUST engage exactly one open-mode tier
// (`mode<om::ReadOnly>` / `mode<om::WriteCreate>` / …).  A call with
// an empty Grants pack (zero grants, zero modes) silently defaults
// to O_RDONLY at the POSIX layer, which is precisely the trap §XXI's
// type-level proof obligation exists to prevent.
//
// Mismatch class: Grants-pack engagement gap (has_mode_v=false).
// Distinct from fixture #3 (duplicate mode) — this fires on the
// engagement floor, not the engagement ceiling.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "constraints not satisfied" / "no matching function" /
//   "CtxFitsFileMint" / "has_mode".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Source.h>           // fixy::tags::source::*
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwfs = ::crucible::fixy::wrap::fs;

    ::crucible::effects::TestRunnerCtx ctx{};

    fwfs::Path<::crucible::fixy::tags::source::Sanitized> path{
        "/tmp/crucible_neg_v224_empty_grants"};

    // Should FAIL: mint_file<> — empty Grants pack does NOT engage
    // any mode<X>, so CtxFitsFileMint's has_mode_v predicate is
    // false; the requires-clause refuses the instantiation.
    [[maybe_unused]] auto r = fwfs::mint_file<>(ctx, std::move(path));
    return 0;
}
