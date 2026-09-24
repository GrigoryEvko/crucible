// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::mint_open_view refuses the cold startup context.  Its row admits IO
// but not Block, and a write through an open view flushes object files to
// storage, which blocks.  A context must admit both effects.

#include <crucible/Cipher.h>

using CipherRoot = crucible::fixy::wrap::Path<crucible::fixy::tags::source::External>;

int main() {
    auto cipher = ::crucible::Cipher::open(CipherRoot{"/tmp/crucible_neg_open_view_init_ctx"});
    auto view = cipher.mint_open_view(::crucible::effects::ColdInitCtx{::crucible::effects::testing::init()});
    (void)view;
    return 0;
}
