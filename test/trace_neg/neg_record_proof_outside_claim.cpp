// Negative-compile test.  This file must fail to compile.
//
// Only the producer claim of the Vigils makes the proof of the record path
// (RecordProof of crucible/ForegroundCtx.h).  A caller outside the claim that
// built the proof itself would open the producer side of the trace ring of a
// Vigil while the Vigil records, and two producers would write the same head.

#include <crucible/TraceRing.h>
#include <crucible/Vigil.h>

namespace neg_fixture {

[[maybe_unused]] static void build_the_proof_outside_the_claim() noexcept {
    const crucible::RecordProof proof{};
    static_cast<void>(proof);
}

}  // namespace neg_fixture

int main() { return 0; }
