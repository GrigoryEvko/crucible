// The build stage of the pipeline copies the records of a work before the
// publish stage releases them.
//
// The detect stage cuts a work, and then a commit marker whose floor passes
// the records of the work.  The build stage copies the records of the work
// (copy_run of MetaLog.h), and then it sends the marker on.  The publish stage
// moves the tail of the log to the floor of the marker, and the producer then
// writes new records into the slots of the work.  The channel between the two
// stages is FIFO, and its push and its pop are a release and an acquire.  So
// the copy comes before the release of the tail, and the release comes before
// each write of the producer into the slots.
//
// The records of the run fill the log two times, so the second lap writes into
// each slot of the first lap.  The callback of the first region holds the
// publish stage until the producer meets a full log.  After that, the producer
// writes the second lap only into the slots that each release frees, at once.
//
// Each record holds the number of its op and its place in the op, and the
// callsite hash of the entry of each op holds the number of the op.  The
// callback reads the copy of each op of each region: each record must equal
// the record that the producer wrote for the op of the entry.  A copy after the
// release can hold a record of the next lap.
//
// Under the tsan preset, a release before the copy gives the copy and the
// write of the producer into a copied slot no order, and ThreadSanitizer stops
// the test.  It reports two accesses with no happens-before order, also when
// the two do not overlap in time, so the report does not depend on the
// timing of the copy.

#include <crucible/BackgroundThread.h>
#include <crucible/IterationDetector.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <crucible/ValidatedOp.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include "trace_rig.h"

#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes, and
// which owns Block and Nondet for the flush.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

using crucible::BackgroundThread;
using crucible::MetaIndex;
using crucible::MetaLog;
using crucible::TensorMeta;
using crucible::TraceRing;

// Each op holds the most inputs and outputs that an op can hold, so the run
// passes the capacity of the log with few ops.
constexpr uint16_t kInputsPerOp = crucible::kMaxOpInputs;
constexpr uint16_t kOutputsPerOp = crucible::kMaxOpOutputs;
constexpr uint32_t kRecordsPerOp = crucible::kMaxOpRecords;
constexpr uint32_t kOpsPerIteration = 8;

// The records of the run fill the log two times.
constexpr uint32_t kOpsPerLap = MetaLog::CAPACITY / kRecordsPerOp;
constexpr uint32_t kLaps = 2;
constexpr uint32_t kOps = kLaps * kOpsPerLap;

constexpr uint64_t kSchemaBase = 0xC000;
constexpr uint64_t kShapeBase = 0xD000;

// The number of an op in the run, from zero.
using OpNumber = ::fixy::Id<struct OpNumberTag, uint32_t>;

// The place of a record in its op: the inputs, and then the outputs.
using RecordPlace = ::fixy::Index<kRecordsPerOp>;
using OpRecords = ::fixy::FixedArray<TensorMeta, kRecordsPerOp>;

// An op holds its records, or it holds no tensor.
enum class OpTensors : uint8_t {
    held,
    none,
};

// The record at a place of an op.  Three lanes hold the number of the op and
// the place, so a record of another op or another place differs in each of
// them.  The record holds no data pointer, so the build gives it no slot, and
// a region has no memory plan.
[[nodiscard]] TensorMeta record_of(OpNumber op, RecordPlace place) noexcept {
    const int64_t number = ::fixy::widen<int64_t>(op.raw());
    const int64_t at = ::fixy::narrow<int64_t>(place.raw()).expect("a place of a record fits in 64 signed bits");
    TensorMeta meta{};
    meta.ndim = 2;
    meta.sizes[0] = crucible::tensor_dim(number);
    meta.sizes[1] = crucible::tensor_dim(at);
    meta.strides[0] = crucible::tensor_dim(number * kRecordsPerOp + at + 1);
    meta.strides[1] = crucible::tensor_dim(1);
    meta.dtype = crucible::ScalarType::Float;
    meta.device_type = crucible::DeviceType::CPU;
    meta.device_idx = 0;
    meta.layout = crucible::Layout::Strided;
    return meta;
}

// The records of an op, in the order of their places.
[[nodiscard]] OpRecords records_of(OpNumber op) noexcept {
    OpRecords records{};
    for (const RecordPlace place : records.indices())
        records.set(place, record_of(op, place));
    return records;
}

// The entry of an op.  The place of the op in its iteration gives its schema
// and its shape, so each iteration repeats one signature.  The callsite hash
// holds the number of the op, one more than the number, and the detector
// reads no callsite hash.
[[nodiscard]] TraceRing::Entry entry_of(OpNumber op, OpTensors tensors) noexcept {
    const uint64_t position = op.raw() % kOpsPerIteration;
    TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{kSchemaBase + position};
    entry.shape_hash = crucible::ShapeHash{kShapeBase + position};
    if (tensors == OpTensors::held) {
        entry.num_inputs = kInputsPerOp;
        entry.num_outputs = kOutputsPerOp;
    }
    return entry;
}

[[nodiscard]] crucible::CallsiteHash callsite_of(OpNumber op) noexcept {
    return crucible::CallsiteHash{uint64_t{op.raw()} + 1};
}

// The callback of each published region.  It runs on the thread of the
// publish stage, and the test reads its counts after the stop, which joins
// that thread.
struct CopyCheck {
    // The producer raises it when an append meets a full log.  The callback
    // of the first region waits for it, so the publish stage releases no
    // record before the log is full.
    ::fixy::Atomic<bool> has_met_full{false};
    ::fixy::Atomic<bool> is_first_region{true};

    ::fixy::Atomic<uint64_t> regions{0u};
    ::fixy::Atomic<uint64_t> records{0u};
    ::fixy::Atomic<uint64_t> wrong_records{0u};
    ::fixy::Atomic<uint64_t> wrong_ops{0u};
    // One more than the number of the first op of the last checked region,
    // so zero means no region.
    ::fixy::Atomic<uint64_t> first_op_end{0u};

    void on_region_ready(::foundation::effects::Bg const&, BackgroundThread::PublishStage const&,
                         crucible::PublishedRegion const& ready) noexcept;
};

// The count of the records of the copy that differ from the records that the
// producer wrote for the op.
[[nodiscard]] uint64_t count_wrong_records(crucible::TraceEntry const& op, OpNumber number) noexcept {
    const OpRecords expected = records_of(number);
    uint64_t wrong = 0;
    uint32_t place = 0;
    const auto compare = [&expected, &wrong, &place](TensorMeta const& copied) noexcept {
        const RecordPlace at = RecordPlace::check(place).expect("an op holds at most kRecordsPerOp records");
        if (!(copied == expected.at(at))) ++wrong;
        ++place;
    };
    for (TensorMeta const& copied : op.input_metas())
        compare(copied);
    for (TensorMeta const& copied : op.output_metas())
        compare(copied);
    return wrong;
}

void CopyCheck::on_region_ready(::foundation::effects::Bg const&, BackgroundThread::PublishStage const&,
                                crucible::PublishedRegion const& ready) noexcept {
    if (is_first_region.load_acquire()) {
        is_first_region.store_release(false);
        while (!has_met_full.load_acquire()) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }
    const crucible::DagRef<crucible::RegionNode const> region =
        crucible::ConstRegionLink{ready.region}.expect("the publish stage gives a record of a region");

    uint64_t wrong_op_count = region->num_ops() == kOpsPerIteration ? 0u : 1u;
    uint64_t wrong_record_count = 0;
    const uint64_t first_number = region->op_at(0).callsite_hash.raw() - 1;
    for (uint32_t index = 0; index < region->num_ops(); ++index) {
        crucible::TraceEntry const& op = region->op_at(index);
        const OpNumber number{
            ::fixy::narrow<uint32_t>(op.callsite_hash.raw() - 1).expect("the number of an op fits in 32 bits")};
        const bool is_in_order = uint64_t{number.raw()} == first_number + index;
        const bool has_its_entry = op.schema_hash == entry_of(number, OpTensors::held).schema_hash
                                && op.num_inputs() == kInputsPerOp && op.num_outputs() == kOutputsPerOp;
        if (!is_in_order || !has_its_entry) {
            ++wrong_op_count;
            continue;
        }
        wrong_record_count += count_wrong_records(op, number);
    }
    // The publish stage publishes the regions in the order of their ops.
    if (first_number + 1 <= first_op_end.load_acquire()) ++wrong_op_count;
    first_op_end.store_release(first_number + 1);

    (void)regions.fetch_add_acq_rel(1u);
    (void)records.fetch_add_acq_rel(uint64_t{region->num_ops()} * kRecordsPerOp);
    (void)wrong_records.fetch_add_acq_rel(wrong_record_count);
    (void)wrong_ops.fetch_add_acq_rel(wrong_op_count);
}

// Appends the entry of an op, and waits while the ring is full.
void append_op(crucible::test::TraceRig& rig, OpNumber op, OpTensors tensors, MetaIndex start) noexcept {
    const TraceRing::Entry entry = entry_of(op, tensors);
    while (!rig.append_entry(entry, start, crucible::ScopeHash{0}, callsite_of(op))) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// Records each op of the run.  The records of an op wait for room in the log,
// and each wait raises has_met_full.  Gives the count of the appends that met
// a full log.
[[nodiscard]] uint64_t record_run(crucible::test::TraceRig& rig, CopyCheck& check) noexcept {
    uint64_t full_waits = 0;
    for (uint32_t raw = 0; raw < kOps; ++raw) {
        const OpNumber op{raw};
        const OpRecords records = records_of(op);
        MetaIndex start = rig.append_metas(records.view());
        while (!start.is_valid()) {
            check.has_met_full.store_release(true);
            ++full_waits;
            CRUCIBLE_SPIN_PAUSE;
            start = rig.append_metas(records.view());
        }
        append_op(rig, op, OpTensors::held, start);
    }
    // The first ops of the next iteration end the last iteration.  They hold
    // no tensor.
    for (uint32_t raw = kOps; raw < kOps + crucible::IterationDetector::K; ++raw)
        append_op(rig, OpNumber{raw}, OpTensors::none, MetaIndex::none());
    return full_waits;
}

}  // namespace

int main() {
    // The check and the CallRef of its callback come before the frame of the
    // thread, so the two outlive the join of the pipeline thread.
    CopyCheck check{};
    const BackgroundThread::RegionReadyCall on_ready =
        BackgroundThread::RegionReadyCall::bind<&CopyCheck::on_region_ready>(check);
    crucible::test::TraceRig rig{};
    const uint64_t full_waits = crucible::with_background_thread(
        ::foundation::effects::testing::test().alloc, rig.ring_side(), rig.log_side(),
        {.on_region_ready = ::fixy::some(::fixy::erase_brand(::fixy::to(on_ready)))},
        [&rig, &check](BackgroundThread const& bt, auto stopped) noexcept {
            auto running = std::move(stopped).start(::fixy::InitLoadCtx{::foundation::effects::testing::init()});
            const uint64_t waits = record_run(rig, check);
            CRUCIBLE_TEST_ASSERT(kTest, bt.flush(kTest).is_ok());
            static_cast<void>(std::move(running).stop());
            return waits;
        });

    CRUCIBLE_TEST_ASSERT(kTest, full_waits > 0 && "the producer never met a full log");
    CRUCIBLE_TEST_ASSERT(kTest, rig.log_total_produced() == MetaIndex{uint64_t{kLaps} * MetaLog::CAPACITY});
    CRUCIBLE_TEST_ASSERT(kTest, rig.log_total_released() == rig.log_total_produced()
                                    && "the floor of the last commit marker did not release each record");
    CRUCIBLE_TEST_ASSERT(kTest, check.wrong_ops.load_acquire() == 0
                                    && "a region held an op out of order, or an op that the producer did not record");
    CRUCIBLE_TEST_ASSERT(kTest, check.wrong_records.load_acquire() == 0
                                    && "a copy of the build held a record that the producer did not write for its op");
    CRUCIBLE_TEST_ASSERT(kTest, check.regions.load_acquire() >= kOps / kOpsPerIteration / 2
                                    && "the pipeline published too few regions to check the copies");
    CRUCIBLE_TEST_ASSERT(kTest, check.first_op_end.load_acquire() > kOpsPerLap
                                    && "no checked region held the records of the second lap");

    ::fixy::check::pass(kTest,
                        "test_background_thread_copy_before_release: PASSED (regions={}, records={}, full "
                        "waits={})\n",
                        check.regions.load_acquire(), check.records.load_acquire(), full_waits);
    return 0;
}
