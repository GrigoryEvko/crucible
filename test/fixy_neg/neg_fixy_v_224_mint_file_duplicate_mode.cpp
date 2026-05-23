// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-224 fixture #3 — duplicate mode<> grants rejected at mint_file.
//
// `CtxFitsFileMint<Ctx, Grants...>` requires
// `!has_duplicate_mode_v<Grants...>` — engaging TWO mode tags in the
// same Grants pack is unambiguous programmer error: the OR-fold over
// open_mode_flags would produce a flag set whose semantics are
// undefined (O_RDONLY | O_WRONLY ≡ O_RDWR on Linux, but the caller
// expressed BOTH ReadOnly + WriteTruncate as if they could compose).
// Refuse the mint instead of letting the silent OR happen.
//
// Mismatch class: Grants-pack engagement ceiling
// (has_duplicate_mode_v=true).  Distinct from fixture #2 (no mode at
// all) — this fires on engaging too many.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "constraints not satisfied" / "no matching function" /
//   "CtxFitsFileMint" / "has_duplicate_mode".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Source.h>           // fixy::tags::source::*
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwfs = ::crucible::fixy::wrap::fs;
    namespace om   = fwfs::open_mode;

    ::crucible::effects::TestRunnerCtx ctx{};

    fwfs::Path<::crucible::fixy::tags::source::Sanitized> path{
        "/tmp/crucible_neg_v224_duplicate_mode"};

    // Should FAIL: two mode<> grants in the same pack —
    // has_duplicate_mode_v is true, so CtxFitsFileMint's
    // `!has_duplicate_mode_v<Grants...>` clause is false; the
    // requires-clause refuses the instantiation.
    [[maybe_unused]] auto r = fwfs::mint_file<
        fwfs::grant::mode<om::ReadOnly>,
        fwfs::grant::mode<om::WriteTruncate>
    >(ctx, std::move(path));
    return 0;
}
