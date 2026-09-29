// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::open refuses the cold startup context.  Opening creates the
// object directory and reads HEAD and the log from storage, which blocks.
// The row of the cold startup context admits IO but not Block, so the gate
// refuses it.

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::ColdInitCtx startup{::foundation::effects::testing::init()};
    auto cipher =
        ::crucible::Cipher::open(startup, ::fixy::mint_tagged<::fixy::tags::source::External>(
                                              std::filesystem::path{"/tmp/crucible_neg_open_ctx_without_block"}));
    return cipher.is_open() ? 0 : 1;
}
