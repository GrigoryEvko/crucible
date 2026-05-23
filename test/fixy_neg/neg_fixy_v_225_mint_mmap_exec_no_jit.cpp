// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-225 fixture #2 — `with_prot<Exec>` without `trusted_jit` rejected.
//
// `CtxFitsMmapMint`'s tail clause:
//
//     !has_exec_prot_v<Grants...> || has_trusted_jit_v<Grants...>
//
// engages when the pack contains `with_prot<prot::Exec>` AND requires a
// sibling `trusted_jit` grant.  Without the JIT witness the Exec
// elevation is refused — W^X discipline at the type system.
//
// Mismatch class: Exec prot engagement without the gating witness.
// Distinct from fixture #1 — both prot and share ARE engaged here.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "CtxFitsMmapMint" /
//   "constraints not satisfied" / "has_exec_prot" / "has_trusted_jit".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Wrap.h>

struct JitRegion {};   // dummy Tag

int main() {
    namespace fwmm = ::crucible::fixy::wrap::mmap;
    namespace prot  = fwmm::prot;
    namespace share = fwmm::share;
    namespace grant = fwmm::grant;

    ::crucible::effects::TestRunnerCtx ctx{};

    // Should FAIL: pack engages Exec prot but lacks trusted_jit.
    [[maybe_unused]] auto r = fwmm::mint_mmap<
        JitRegion,
        grant::with_prot<prot::Exec>,
        grant::with_share<share::Private>
    >(ctx, /*fd=*/-1, /*length=*/4096);
    return 0;
}
