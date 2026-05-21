// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-120b negative fixture #8 (HS14 ≥2 floor, mint #4 of 4):
// `mint_quarantine_policy` IsExecCtx-half failure routed through the
// `fixy::warden::` re-export (Warden.h:156).
//
// The substrate template declares Ctx with the concept-introducer
// `effects::IsExecCtx Ctx`; passing a struct that fails the
// structural IsExecCtx concept (no row_type, no Effect aggregation)
// triggers a template-parameter constraint violation BEFORE the
// requires-clause is evaluated.  Diagnoses the second of two
// orthogonal soundness gates (sibling fixture #7 names the Init-row
// half) — together they prove the using-decl preserves BOTH.
//
// Expected diagnostic: "no matching function" / "constraints not
// satisfied" / "IsExecCtx" / "NotAnExecCtx" / "row_type".

#include <crucible/fixy/Warden.h>

namespace test_fixy_warden_quarantine_policy_not_exec_ctx {

struct NotAnExecCtx {};

}  // namespace test_fixy_warden_quarantine_policy_not_exec_ctx

int main() {
    using NotAnExecCtx =
        test_fixy_warden_quarantine_policy_not_exec_ctx::NotAnExecCtx;
    auto policy = crucible::fixy::warden::mint_quarantine_policy<
        NotAnExecCtx, 2>(NotAnExecCtx{});
    (void)policy;
    return 0;
}
