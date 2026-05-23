// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-225 fixture #5 — mint_mmap_anon without with_share<Anonymous>.
//
// `CtxFitsAnonMmapMint` is a strict superset of `CtxFitsMmapMint` that
// additionally requires:
//
//     pack_has_anonymous_v<Grants...>
//
// — i.e., at least one `with_share<share::Anonymous>` in the pack.
// Calling `mint_mmap_anon` with `share::Private` would attempt
// `::mmap(nullptr, ..., MAP_PRIVATE, -1, 0)` which silently degrades to
// a file-backed mapping over fd=-1 (EBADF at runtime).  Refuse it at
// compile time; if the caller truly wants anonymous, they spell it.
//
// Mismatch class: anon-mint without the witnessing Anonymous share tag.
// Distinct from fixtures #1-#4 (which fire on mint_mmap's prot/share
// uniqueness rules) — this fires on the anon variant's extra clause.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "CtxFitsAnonMmapMint" /
//   "constraints not satisfied" / "pack_has_anonymous".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Wrap.h>

struct AnonRegion {};

int main() {
    namespace fwmm = ::crucible::fixy::wrap::mmap;
    namespace prot  = fwmm::prot;
    namespace share = fwmm::share;
    namespace grant = fwmm::grant;

    ::crucible::effects::TestRunnerCtx ctx{};

    // Should FAIL: mint_mmap_anon requires with_share<Anonymous>;
    // pack only has Private.
    [[maybe_unused]] auto r = fwmm::mint_mmap_anon<
        AnonRegion,
        grant::with_prot<prot::ReadOnly>,
        grant::with_share<share::Private>
    >(ctx, /*length=*/4096);
    return 0;
}
