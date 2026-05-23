// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-257 mint_msr_grant fixture 1/2 — missing Permission<root>.
//
// mint_msr_grant's signature is `(Ctx const&, Permission<root>&&)`.
// MSR access is ring-0 privileged: minting the grant SPENDS a Root
// authority token.  Calling without the Permission<root> leaves no
// matching overload — privilege cannot be conjured from nothing.
//
// Mismatch class: missing authority argument (arity / overload
// resolution).  Distinct from neg_fixy_v_257_msr_wrong_permission.cpp,
// which supplies a Permission of the WRONG tag.
//
// Expected diagnostic: "no matching function" / "too few arguments" /
// "Permission".

#include <crucible/fixy/Hw.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    ::crucible::effects::TestRunnerCtx ctx{};
    namespace hw = ::crucible::fixy::hw;
    // Should FAIL: no Permission<root> argument supplied.
    [[maybe_unused]] auto g = hw::mint_msr_grant<0x10u>(ctx);
    return 0;
}
