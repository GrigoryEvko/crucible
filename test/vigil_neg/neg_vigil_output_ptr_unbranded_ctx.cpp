// Negative-compile test.  This file must fail to compile.
//
// An output pointer of the replayed op is read only by the thread that
// holds this Vigil's producer claim, and mint_producer_context is the
// route to that context.  A context that names no claim is refused.

#include <crucible/Vigil.h>

int main() {
    crucible::Vigil vigil = crucible::mint_vigil(::fixy::InitLoadCtx{::foundation::effects::testing::init()}, {});
    auto const out = vigil.output_ptr(::foundation::effects::testing::foreground(), crucible::OutputPosition{0});
    (void)out;
    return 0;
}
