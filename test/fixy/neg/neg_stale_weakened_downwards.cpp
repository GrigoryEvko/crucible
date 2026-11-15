// weaken moves the grade towards more stale and nowhere else.  A fresher
// grade in a constant expression reaches the CRUCIBLE_PRE trap inside
// Stale::weaken, so the static_assert below has a non-constant
// condition, and the expansion note names the macro in Stale.h.

#include <fixy/Ctx.h>
#include <fixy/Stale.h>
#include <foundation/effects/Ctx.h>

namespace {

[[nodiscard]] constexpr int under_test() noexcept {
    fixy::SequenceSource source = fixy::mint_sequence_source(fixy::ColdInitCtx{::foundation::effects::testing::init()});
    fixy::SequenceStamp const sampled = source.stamp();
    for (int step = 0; step < 8; ++step)
        static_cast<void>(source.advance());
    fixy::Stale<int> eight_behind = fixy::Stale<int>::at(1, source, sampled);
    fixy::Stale<int> fresher = eight_behind.weaken(::foundation::algebra::lattices::staleness::at(3));
    return fresher.peek();
}

static_assert(under_test() == 1);

}  // namespace

int main() { return 0; }
