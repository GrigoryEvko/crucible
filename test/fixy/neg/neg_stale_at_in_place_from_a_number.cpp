// at_in_place takes its grade from a stamp of a source.  A number in the
// place of the stamp is a grade that no source vouches for, so the call
// does not compile.

#include <fixy/Ctx.h>
#include <fixy/Stale.h>
#include <foundation/effects/Ctx.h>

#include <cstdint>

int main() {
    fixy::SequenceSource const source =
        fixy::mint_sequence_source(fixy::ColdInitCtx{::foundation::effects::testing::init()});
    fixy::Stale<int> const claimed = fixy::Stale<int>::at_in_place(source, std::uint64_t{0}, 7);
    return claimed.peek();
}
