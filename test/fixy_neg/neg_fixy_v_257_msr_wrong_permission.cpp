// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-257 mint_msr_grant fixture 2/2 — wrong-tag Permission rejected.
//
// mint_msr_grant consumes a `safety::Permission<hw::root>&&` — the
// privileged-capability tag specific to MSR / port-IO.  A Permission for
// SOME OTHER region (here `other_region`) does NOT bind to the
// `Permission<root>&&` parameter: holding a permission for an unrelated
// region is not authority to read MSRs.
//
// Mismatch class: wrong-tag authority (type mismatch on the consumed
// Permission).  Distinct from neg_fixy_v_257_msr_missing_permission.cpp,
// which omits the argument entirely.
//
// Expected diagnostic: "no matching function" / "could not convert" /
// "cannot bind" / "Permission".

#include <crucible/fixy/Hw.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/permissions/Permission.h>

namespace { struct other_region {}; }

int main() {
    ::crucible::effects::TestRunnerCtx ctx{};
    namespace hw = ::crucible::fixy::hw;
    auto wrong = ::crucible::safety::mint_permission_root<other_region>();
    // Should FAIL: Permission<other_region> is not Permission<hw::root>.
    [[maybe_unused]] auto g = hw::mint_msr_grant<0x10u>(ctx, std::move(wrong));
    return 0;
}
