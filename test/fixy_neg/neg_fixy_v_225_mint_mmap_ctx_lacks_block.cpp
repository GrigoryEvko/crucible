// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-225 fixture #6 — ColdInitCtx rejected at mint_mmap (no Block).
//
// `CtxAdmitsIoBlock<Ctx>` requires the caller's ExecCtx row to admit
// BOTH `Effect::IO` AND `Effect::Block`.  Even though mmap is a memory
// management call, it can park the caller (kernel page-cache pressure,
// NUMA-remote page faults, MAP_LOCKED hitting RLIMIT_MEMLOCK).  We
// treat it as IO+Block like every other filesystem-touching syscall.
//
// `ColdInitCtx` is `Row<Init, Alloc, IO>` — admits IO but NOT Block.
// A process-init mint that only declares IO cannot map durably-paged
// memory because Block would lift the "init phase MUST NOT park on
// page fault" discipline.
//
// Mismatch class: ctx-row engagement gap (lacks Effect::Block).
// Mirrors FIXY-V-224 fixture #6 (same kind of gap on mint_file).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "constraints not satisfied" / "no matching function" /
//   "CtxFitsMmapMint" / "CtxAdmitsIoBlock" / "row_contains" / "Block".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Wrap.h>

struct InitRegion {};

int main() {
    namespace fwmm = ::crucible::fixy::wrap::mmap;
    namespace prot  = fwmm::prot;
    namespace share = fwmm::share;
    namespace grant = fwmm::grant;

    // ColdInitCtx — Row<Init, Alloc, IO> — admits IO but NOT Block.
    ::crucible::effects::ColdInitCtx ctx{};

    // Should FAIL: mint_mmap's CtxFitsMmapMint folds in
    // CtxAdmitsIoBlock<Ctx>; ColdInitCtx's row lacks Effect::Block.
    [[maybe_unused]] auto r = fwmm::mint_mmap_anon<
        InitRegion,
        grant::with_prot<prot::ReadOnly>,
        grant::with_share<share::Anonymous>
    >(ctx, /*length=*/4096);
    return 0;
}
