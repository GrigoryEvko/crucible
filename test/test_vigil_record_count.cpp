// Vigil::record_op takes the count of the metadata records of an op apart
// from the entry.  The records are the inputs and then the outputs of the
// entry, so a count that disagrees with the entry ends the process before
// the metadata log or the trace ring takes anything.

#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>
#include <crucible/Vigil.h>
#include <fixy/FixedArray.h>

#include "test_abort_probe.h"
#include "test_assert.h"
#include "test_harness.h"

#include <cstdint>

using crucible::MetaLog;
using crucible::TraceRing;
using crucible::Vigil;

namespace {

void test_count_that_disagrees_with_the_entry_aborts() {
    Vigil vigil;
    TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{0x51};
    entry.shape_hash = crucible::ShapeHash{0x52};
    entry.num_inputs = 1;
    entry.num_outputs = 1;
    // The records are the default records.  No region comes from two ops,
    // so no build reads them.
    ::fixy::FixedArray<crucible::TensorMeta, 3> metas{};

    // A count that agrees records the op, and claims this thread.
    assert(vigil.record_op(crucible::test::certify_synthetic_entry(entry), metas.data(), 2));
    const MetaLog& log = vigil.meta_log(vigil.mint_producer_context());
    const uint64_t head_before = log.head.get();
    const uint64_t ring_before = vigil.ring_total_produced();

    // One record too many, one too few, and no record for an op with two
    // tensors.
    assert(crucible::test::aborts(
        [&] { (void)vigil.record_op(crucible::test::certify_synthetic_entry(entry), metas.data(), 3); }));
    assert(crucible::test::aborts(
        [&] { (void)vigil.record_op(crucible::test::certify_synthetic_entry(entry), metas.data(), 1); }));
    assert(crucible::test::aborts(
        [&] { (void)vigil.record_op(crucible::test::certify_synthetic_entry(entry), nullptr, 0); }));
    assert(log.head.get() == head_before && "a refused op put records in the metadata log");
    assert(vigil.ring_total_produced() == ring_before && "a refused op reached the trace ring");

    // The op with a count that agrees records again.
    assert(vigil.record_op(crucible::test::certify_synthetic_entry(entry), metas.data(), 2));
    assert(log.head.get() == head_before + 2);
}

}  // namespace

int main() {
    test_count_that_disagrees_with_the_entry_aborts();
    return 0;
}
