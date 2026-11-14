// record_into reserves the slot of the trace ring before the metadata log
// takes the records of the op.
//
// The ring and the log here have no consumer, so the ring fills after
// CAPACITY ops and stays full.  A full ring refuses the op, and the log must
// then hold no record of it: a record with no op in the ring is a record that
// no op names.  A reservation that the producer drops leaves the ring as it
// was.  A full log gives the op no metadata index, and the ring still takes
// the op.

#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>
#include <crucible/Vigil.h>
#include <fixy/Core.h>

#include "test_assert.h"
#include "test_harness.h"

#include <cstdint>

using crucible::MetaIndex;
using crucible::MetaLog;
using crucible::TraceRing;

namespace {

TraceRing& test_ring() {
    static TraceRing ring{};
    return ring;
}

MetaLog& test_log() {
    static MetaLog log{::foundation::effects::testing::test().alloc};
    return log;
}

// The records of one op with one input and one output.
using OpMetas = ::fixy::FixedArray<crucible::TensorMeta, 2>;

// The records of a log that fills.  A static object keeps them off the stack.
using FillRun = ::fixy::FixedArray<crucible::TensorMeta, 4096>;
FillRun& fill_run() {
    static FillRun run{};
    return run;
}

[[nodiscard]] TraceRing::Entry entry_with_two_tensors(uint64_t schema) {
    TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{schema};
    entry.shape_hash = crucible::ShapeHash{schema + 1};
    entry.num_inputs = 1;
    entry.num_outputs = 1;
    return entry;
}

[[nodiscard]] bool record(TraceRing& ring, MetaLog& log, const TraceRing::Entry& entry, OpMetas const& metas) {
    return crucible::record_into(ring, log, crucible::test::certify_synthetic_op(entry, metas.view()),
                                 crucible::ScopeHash{}, crucible::CallsiteHash{});
}

// Drains one entry and its metadata index.  The ring holds at least one.
[[nodiscard]] MetaIndex drain_meta_start(TraceRing& ring, TraceRing::Entry& out_entry) {
    ::fixy::FixedArray<TraceRing::Entry, 1> entries{};
    ::fixy::FixedArray<MetaIndex, 1> meta_starts{};
    assert(ring.drain(entries.view(), meta_starts.view()) == 1);
    out_entry = entries.at(::fixy::index<0>());
    return meta_starts.at(::fixy::index<0>());
}

void test_full_ring_leaves_no_record_in_the_log() {
    TraceRing& ring = test_ring();
    MetaLog& log = test_log();
    ring.reset();
    log.reset();
    const OpMetas metas{};

    // No consumer drains the ring, so CAPACITY entries fill it.
    for (uint32_t i = 0; i < TraceRing::CAPACITY; ++i)
        assert(ring.try_append(entry_with_two_tensors(0x100)));

    assert(!record(ring, log, entry_with_two_tensors(0x200), metas) && "a full ring took an op");
    assert(log.head.get() == 0 && "the full ring refused the op, and the log took its records");
    assert(ring.total_produced() == TraceRing::CAPACITY);
}

void test_dropped_reservation_leaves_the_ring() {
    TraceRing& ring = test_ring();
    MetaLog& log = test_log();
    ring.reset();
    log.reset();

    {
        const ::fixy::Option<TraceRing::Reservation> reserved = ring.try_reserve();
        assert(reserved.is_some() && "an empty ring refused a reservation");
    }
    assert(ring.total_produced() == 0 && "a dropped reservation moved the head");

    // The slot of the dropped reservation takes the next op.
    const OpMetas metas{};
    const TraceRing::Entry entry = entry_with_two_tensors(0x300);
    assert(record(ring, log, entry, metas));
    assert(ring.total_produced() == 1);
    TraceRing::Entry drained{};
    const MetaIndex meta_start = drain_meta_start(ring, drained);
    assert(drained.schema_hash == entry.schema_hash);
    assert(meta_start.is_valid() && meta_start.raw() == 0 && "the op did not name its records");
    assert(log.head.get() == 2);
}

void test_op_names_the_records_it_appended() {
    TraceRing& ring = test_ring();
    MetaLog& log = test_log();
    ring.reset();
    log.reset();
    const OpMetas metas{};

    for (uint64_t op = 0; op < 3; ++op)
        assert(record(ring, log, entry_with_two_tensors(0x400 + op), metas));
    for (uint64_t op = 0; op < 3; ++op) {
        TraceRing::Entry drained{};
        const MetaIndex meta_start = drain_meta_start(ring, drained);
        assert(drained.schema_hash == crucible::SchemaHash{0x400 + op});
        assert(meta_start.is_valid() && meta_start.raw() == 2 * op && "an op named the records of another op");
    }
}

void test_full_log_records_the_op_with_no_index() {
    TraceRing& ring = test_ring();
    MetaLog& log = test_log();
    ring.reset();
    log.reset();

    // No consumer releases the log, so the appends fill it.
    FillRun& run = fill_run();
    for (uint32_t i = 0; i < MetaLog::CAPACITY / FillRun::size(); ++i)
        assert(log.try_append(run.view()).is_valid());
    assert(log.size().peek() == MetaLog::CAPACITY);

    const OpMetas metas{};
    assert(record(ring, log, entry_with_two_tensors(0x500), metas) && "a full log made the ring refuse the op");
    TraceRing::Entry drained{};
    assert(!drain_meta_start(ring, drained).is_valid() && "an op of a full log named records");
    assert(drained.schema_hash == crucible::SchemaHash{0x500});
}

}  // namespace

int main() {
    test_full_ring_leaves_no_record_in_the_log();
    test_dropped_reservation_leaves_the_ring();
    test_op_names_the_records_it_appended();
    test_full_log_records_the_op_with_no_index();
    return 0;
}
