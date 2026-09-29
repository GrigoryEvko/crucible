// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::mint_open_view refuses the hot foreground context.  Every write
// through an open view writes object files and flushes them, so the view
// needs a context whose row admits IO and Block, and the hot foreground
// row is empty.

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>

int main() {
    const ::crucible::Cipher cipher;
    auto view = cipher.mint_open_view(::foundation::effects::testing::foreground());
    (void)view;
    return 0;
}
