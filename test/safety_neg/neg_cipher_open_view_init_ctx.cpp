// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::mint_open_view refuses the cold startup context.  Its row admits
// IO but not Block, and a write through an open view flushes object files
// to storage, which blocks.  A context must admit both effects.

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>

int main() {
    const ::crucible::Cipher cipher;
    auto view = cipher.mint_open_view(::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    (void)view;
    return 0;
}
