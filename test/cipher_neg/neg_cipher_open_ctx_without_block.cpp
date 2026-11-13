// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::open refuses the cold startup context.  Opening creates the
// object directory and reads HEAD and the log from storage, which blocks.
// The row of the cold startup context admits IO but not Block, so the gate
// refuses it.

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>

int main() {
    const auto source = ::foundation::effects::testing::init();
    const ::fixy::ColdInitCtx startup{source};
    auto cipher = ::crucible::Cipher::open(startup, source.alloc,
                                           ::fixy::mint_tagged<::fixy::tags::source::External>(
                                               ::fixy::TextView{"/tmp/crucible_neg_open_ctx_without_block"}));
    return cipher.is_open() ? 0 : 1;
}
