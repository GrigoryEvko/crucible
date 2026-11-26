// Negative-compile test.  This file must fail to compile.
//
// The proof of a matched op from the Ref of an entry.  Only the engine makes
// a proof, in the step of an advance that matched.  A proof of an entry that
// no advance matched would let the doors of an address read an op with no
// test of the match.

#include <crucible/ReplayEngine.h>

int main() {
    const crucible::TraceEntry entry{};
    const crucible::MatchedOp op{::fixy::erase_brand(::fixy::to(entry))};
    return static_cast<int>(op.entry().num_inputs());
}
