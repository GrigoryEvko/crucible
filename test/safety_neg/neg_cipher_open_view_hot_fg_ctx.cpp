// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::mint_open_view refuses the hot foreground context.  Every write
// through an open view writes object files and flushes them, so the view needs
// a context whose row admits IO and Block, and the hot foreground row is
// empty.

#include <crucible/Cipher.h>

using CipherRoot = crucible::fixy::wrap::Path<crucible::fixy::tags::source::External>;

int main() {
    auto cipher = ::crucible::Cipher::open(CipherRoot{"/tmp/crucible_neg_open_view_hot_fg_ctx"});
    auto view = cipher.mint_open_view(::crucible::effects::HotFgCtx{});
    (void)view;
    return 0;
}
