// TraceRing::try_record reserves the slot of the trace ring before the
// metadata log takes the records of the op.
//
// The ring and the log here have no consumer, so the ring fills after
// CAPACITY ops and stays full.  A full ring refuses the op, and the log must
// then hold no record of it: a record with no op in the ring is a record that
// no op names.  An op with no records takes no metadata index.  A full log
// gives the op no metadata index, and the ring still takes the op.  Each
// test holds a ring and a log of its own.  The reservation is private to the
// doors of the ring, so no test holds one (the fixtures
// neg_tracering_reservation_named and neg_tracering_try_reserve_called).

#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>
#include <crucible/Vigil.h>
#include <fixy/Core.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include "test_harness.h"
#include "trace_rig.h"

#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::MetaIndex;
using crucible::MetaLog;
using crucible::TraceRing;
using crucible::test::LocalLog;
using crucible::test::LocalRing;

namespace {

// The records of one op with one input and one output.
using OpMetas = ::fixy::FixedArray<crucible::TensorMeta, 2>;

// The records of a log that fills.  The run is on the heap, off the stack.
constexpr uint32_t kFillRecords = 4096;

[[nodiscard]] TraceRing::Entry entry_with_two_tensors(uint64_t schema) {
    TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{schema};
    entry.shape_hash = crucible::ShapeHash{schema + 1};
    entry.num_inputs = 1;
    entry.num_outputs = 1;
    return entry;
}

[[nodiscard]] bool record(LocalRing& ring, LocalLog& log, const TraceRing::Entry& entry, OpMetas const& metas) {
    return crucible::test::with_synthetic_op(entry, metas, 2, [&ring, &log](crucible::ValidatedOp const& op) {
        return crucible::test::record_local(ring, log, op);
    });
}

// Drains one entry and its metadata index.  The ring holds at least one.
[[nodiscard]] MetaIndex drain_meta_start(LocalRing& ring, TraceRing::Entry& out_entry) {
    auto [entries, entries_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<TraceRing::Entry, 1>);
    auto [meta_starts, meta_starts_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<MetaIndex, 1>);
    uint32_t const drained =
        ring.with_consumer([&entries, &entries_key, &meta_starts, &meta_starts_key](TraceRing::Consumer& end) noexcept {
            return entries.with_mut(entries_key, [&end, &meta_starts, &meta_starts_key](auto entry_run) noexcept {
                return meta_starts.with_mut(meta_starts_key, [&end, &entry_run](auto meta_run) noexcept {
                    return TraceRing::drain(end, std::move(entry_run), std::move(meta_run));
                });
            });
        });
    CRUCIBLE_TEST_ASSERT(kTest, drained == 1);
    out_entry = entries.at(::fixy::index<0>());
    return meta_starts.at(::fixy::index<0>());
}

[[nodiscard]] uint64_t ring_produced(LocalRing const& ring) {
    return ring.with_owner([](TraceRing const& owner) noexcept { return owner.total_produced(); });
}

[[nodiscard]] uint64_t log_produced(LocalLog const& log) {
    return log.with_owner([](MetaLog const& owner) noexcept { return owner.total_produced().raw(); });
}

void test_full_ring_leaves_no_record_in_the_log() {
    LocalRing ring{};
    LocalLog log{::foundation::effects::testing::test().alloc};
    const OpMetas metas{};

    // No consumer drains the ring, so CAPACITY entries fill it.
    ring.with_producer([](TraceRing::Producer& end) noexcept {
        for (uint32_t i = 0; i < TraceRing::CAPACITY; ++i)
            CRUCIBLE_TEST_ASSERT(kTest, TraceRing::try_append(end, entry_with_two_tensors(0x100)));
    });

    CRUCIBLE_TEST_ASSERT(kTest, !record(ring, log, entry_with_two_tensors(0x200), metas) && "a full ring took an op");
    CRUCIBLE_TEST_ASSERT(kTest, log_produced(log) == 0 && "the full ring refused the op, and the log took its records");
    CRUCIBLE_TEST_ASSERT(kTest, ring_produced(ring) == TraceRing::CAPACITY);
}

void test_op_with_no_records_takes_no_index() {
    LocalRing ring{};
    LocalLog log{::foundation::effects::testing::test().alloc};

    TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{0x300};
    entry.shape_hash = crucible::ShapeHash{0x301};
    auto const sealed = crucible::test::seal_entry(entry);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::test::record_local(ring, log, crucible::test::certify_synthetic_op(sealed)));
    CRUCIBLE_TEST_ASSERT(kTest, ring_produced(ring) == 1 && log_produced(log) == 0);
    TraceRing::Entry drained{};
    CRUCIBLE_TEST_ASSERT(kTest, !drain_meta_start(ring, drained).is_valid() && "an op with no records named records");
    CRUCIBLE_TEST_ASSERT(kTest, drained.schema_hash == entry.schema_hash);

    // The next op with records takes the first records of the log.
    const OpMetas metas{};
    CRUCIBLE_TEST_ASSERT(kTest, record(ring, log, entry_with_two_tensors(0x310), metas));
    const MetaIndex meta_start = drain_meta_start(ring, drained);
    CRUCIBLE_TEST_ASSERT(kTest, meta_start.is_valid() && meta_start.raw() == 0 && "the op did not name its records");
    CRUCIBLE_TEST_ASSERT(kTest, log_produced(log) == 2);
}

void test_op_names_the_records_it_appended() {
    LocalRing ring{};
    LocalLog log{::foundation::effects::testing::test().alloc};
    const OpMetas metas{};

    for (uint64_t op = 0; op < 3; ++op)
        CRUCIBLE_TEST_ASSERT(kTest, record(ring, log, entry_with_two_tensors(0x400 + op), metas));
    for (uint64_t op = 0; op < 3; ++op) {
        TraceRing::Entry drained{};
        const MetaIndex meta_start = drain_meta_start(ring, drained);
        CRUCIBLE_TEST_ASSERT(kTest, drained.schema_hash == crucible::SchemaHash{0x400 + op});
        CRUCIBLE_TEST_ASSERT(kTest, meta_start.is_valid() && meta_start.raw() == 2 * op
                                        && "an op named the records of another op");
    }
}

void test_full_log_records_the_op_with_no_index() {
    LocalRing ring{};
    LocalLog log{::foundation::effects::testing::test().alloc};

    // No consumer releases the log, so the appends fill it.
    const ::fixy::HeapArray<crucible::TensorMeta> run =
        ::fixy::mint_heap_array(::foundation::effects::testing::test().alloc, kFillRecords, crucible::TensorMeta{});
    crucible::MetaRun const records = crucible::mint_checked_meta_run(run.view());
    log.with_producer([records](MetaLog::Producer& end) noexcept {
        for (uint32_t i = 0; i < MetaLog::CAPACITY / kFillRecords; ++i)
            CRUCIBLE_TEST_ASSERT(kTest, MetaLog::try_append(end, records).is_valid());
    });
    CRUCIBLE_TEST_ASSERT(kTest, log.with_owner([](MetaLog const& owner) noexcept { return owner.size().peek(); })
                                    == MetaLog::CAPACITY);

    const OpMetas metas{};
    CRUCIBLE_TEST_ASSERT(kTest, record(ring, log, entry_with_two_tensors(0x500), metas)
                                    && "a full log made the ring refuse the op");
    TraceRing::Entry drained{};
    CRUCIBLE_TEST_ASSERT(kTest, !drain_meta_start(ring, drained).is_valid() && "an op of a full log named records");
    CRUCIBLE_TEST_ASSERT(kTest, drained.schema_hash == crucible::SchemaHash{0x500});
}

}  // namespace

int main() {
    test_full_ring_leaves_no_record_in_the_log();
    test_op_with_no_records_takes_no_index();
    test_op_names_the_records_it_appended();
    test_full_log_records_the_op_with_no_index();
    return 0;
}
