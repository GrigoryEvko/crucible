// at_in_place reads the distance from the stamp to the current position of
// the source.  A stamp alone has no current position, so the call without
// the source does not compile.

#include <fixy/Ctx.h>
#include <fixy/Stale.h>
#include <foundation/effects/Ctx.h>

int main() {
    fixy::SequenceSource const source =
        fixy::mint_sequence_source(fixy::ColdInitCtx{::foundation::effects::testing::init()});
    fixy::SequenceStamp const sampled = source.stamp();
    fixy::Stale<int> const claimed = fixy::Stale<int>::at_in_place(sampled, 7);
    return claimed.peek();
}
