// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::open refuses a bare string for the root.  The root comes from
// an operator, so it crosses a trust boundary, and the caller must state
// that with a path tagged External.  The value constructor of Tagged is
// private and mint_tagged is its door, so no string converts to a path by
// itself.  The context here is valid, so the refusal names the path.

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::TestRunnerCtx store_ctx{::foundation::effects::testing::test()};
    [[maybe_unused]] auto cipher = ::crucible::Cipher::open(store_ctx, "/tmp/crucible_neg_open_bare_string");
    return 0;
}
