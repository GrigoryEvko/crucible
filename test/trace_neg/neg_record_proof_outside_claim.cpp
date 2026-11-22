// Negative-compile test.  This file must fail to compile.
//
// Only the producer claim of the Vigils makes the proof of the record path
// (RecordProof of crucible/ForegroundCtx.h).  A caller outside the claim that
// built the proof itself would publish the head of the trace ring with no
// check while a second producer writes the same head.

#include <crucible/TraceRing.h>

namespace neg_fixture {

[[maybe_unused]] static void commit_without_the_claim(crucible::TraceRing& ring,
                                                      crucible::TraceRing::Reservation reserved,
                                                      crucible::TraceRing::Entry const& entry) noexcept {
    const crucible::RecordProof proof{};
    ring.commit_proven(proof, static_cast<crucible::TraceRing::Reservation&&>(reserved), entry,
                       crucible::MetaIndex::none(), crucible::ScopeHash{}, crucible::CallsiteHash{},
                       crucible::OpDepth{});
}

}  // namespace neg_fixture

int main() { return 0; }
