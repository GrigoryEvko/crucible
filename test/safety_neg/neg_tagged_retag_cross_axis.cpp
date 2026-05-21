// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: invoking `Tagged<T, Tag>::retag<NewTag>()` for a
// cross-axis phantom transition that is NOT in V-023's catalog
// (source::* to trust::*).
//
// FIXY-V-024 wires `Tagged::retag()` with `requires RetagAllowed<
// Tag, NewTag>`.  The requires-clause consults the V-023 catalog;
// cross-axis transitions (source::* → trust::*, source::* →
// access::*, etc.) are NEVER admitted — laundering provenance into
// verification status or access mode is meaningless across axes.
//
// V-022 sister fixtures (neg_retag_policy_*.cpp) probe the
// RetagAllowed concept GATE directly via a free template constraint.
// This V-024 fixture exercises the CONSUMER level — the wired-in
// requires-clause on the production Tagged::retag() method.  Both
// must reject the same transition; the consumer-level fixture
// witnesses that the V-024 wire-up actually fires.

#include <crucible/safety/Tagged.h>

namespace ns = crucible::safety;

int main() {
    // Construct a Tagged with a source::* phantom.
    ns::Tagged<int, ns::source::External> external{42};

    // Cross-axis retag — not in V-023's catalog.  Requires-clause
    // on Tagged::retag() fires here.
    auto verified = std::move(external).retag<ns::trust::Verified>();
    (void)verified;
    return 0;
}
