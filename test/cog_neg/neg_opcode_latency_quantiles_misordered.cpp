// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The second of two fixtures for the OpcodeLatencyTable gates.
//
// cog::OrderedLatencyQuantiles refines a latency triple by
// cog::quantile_ordered, which holds when p50 <= p99 <= p999.  The
// mint checks the predicate.  In a constant expression the failed
// check is not a constant, so the constexpr variable below cannot be
// initialized, and the build stops here.
//
// A triple out of order comes from corrupt storage, from an import
// under a different histogram convention, or from a calibrator that
// mislabelled the fields.  Every consumer that compares throughput
// envelopes across Cogs relies on the order.
//
// The first fixture, neg_opcode_latency_rejects_psu_rail.cpp, tests
// the concept gate HasOpcodeTable.  This one tests the data invariant.

#include <crucible/cog/OpcodeLatencyTable.h>

namespace cog = crucible::cog;

// p50 = 99 is more than p99 = 50, and p99 is more than p999 = 30.
constexpr cog::OrderedLatencyQuantiles BAD_QUANTILES_FIXTURE =
    ::fixy::mint_refined<cog::quantile_ordered>(cog::LatencyQuantiles{99u, 50u, 30u});

int main() { return BAD_QUANTILES_FIXTURE.value().p50_ns == 99u ? 0 : 1; }
