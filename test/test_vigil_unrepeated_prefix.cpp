// A stream that starts with ops that never occur again still compiles its
// loop, and the background pipeline holds a bounded trace while it searches.

#include <crucible/BackgroundThread.h>
#include <crucible/IterationDetector.h>
#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>
#include <crucible/Vigil.h>
#include <fixy/FixedArray.h>

#include "test_assert.h"
#include "test_harness.h"
#include <bit>
#include <cstdint>

using crucible::BackgroundThread;
using crucible::IterationDetector;
using crucible::MetaIndex;
using crucible::MetaLog;
using crucible::TraceRing;
using crucible::Vigil;

namespace {

crucible::TensorMeta make_meta(uint64_t address) noexcept {
    crucible::TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = crucible::tensor_dim(1024);
    meta.strides[0] = crucible::tensor_dim(1);
    meta.dtype = crucible::ScalarType::Float;
    meta.device_type = crucible::DeviceType::CPU;
    meta.device_idx = 0;
    meta.data_ptr = crucible::external_data_ptr(std::bit_cast<void*>(address));
    return meta;
}

// Records one op with one input and one output.  Returns false when the
// trace ring refused the op.
bool record(Vigil& vigil, uint64_t schema, uint64_t shape, uint64_t input_address, uint64_t output_address) {
    TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{schema};
    entry.shape_hash = crucible::ShapeHash{shape};
    entry.num_inputs = 1;
    entry.num_outputs = 1;
    ::fixy::FixedArray<crucible::TensorMeta, 2> metas{};
    metas[0] = make_meta(input_address);
    metas[1] = make_meta(output_address);
    return vigil.record_op(crucible::test::certify_synthetic_entry(entry), metas.data(), 2);
}

// Records ops that never occur again, then the eight-op loop until it
// publishes a region or until 400 iterations.  Returns the iterations of the
// loop that the region took, or zero when no region came.
uint32_t iterations_to_region(Vigil& vigil, uint64_t prefix_ops) {
    for (uint64_t i = 0; i < prefix_ops; ++i) {
        (void)record(vigil, 0x1000000 + i, 0x2000000 + i, 0x100000 + i * 0x100, 0x100080 + i * 0x100);
        if ((i & 0x3fff) == 0) vigil.flush();
    }
    vigil.flush();
    for (uint32_t iteration = 0; iteration < 400; ++iteration) {
        const uint64_t base = 0x40000000 + uint64_t{iteration} * 0x10000;
        for (uint64_t op = 0; op < 8; ++op)
            (void)record(vigil, 0x100 + op, 0x200 + op, base + op * 0x100, base + (op + 1) * 0x100);
        vigil.flush();
        if (vigil.active_region() != nullptr) return iteration + 1;
    }
    return 0;
}

void test_prefix_that_never_recurs_publishes_a_region() {
    // Five ops before the loop, as an initialization records them.  The
    // region comes a few iterations after the loop starts.
    Vigil vigil;
    const uint32_t iterations = iterations_to_region(vigil, 5);
    assert(iterations != 0 && "the loop after the prefix published no region");
    assert(iterations <= 8);
    assert(vigil.bg_detector_boundaries() > 0);
}

// The trace ring and the metadata log of the pipeline test.  Each is a
// function-local object, made at the first call.
TraceRing& pipeline_ring() {
    static TraceRing ring{};
    return ring;
}

MetaLog& pipeline_log() {
    static MetaLog log{};
    return log;
}

void test_detect_stage_holds_a_bounded_trace() {
    // Ops that never occur again give no period.  The detect stage holds
    // only the ops that a future iteration can claim: at most half of the
    // history of the detector and K ops more.
    BackgroundThread bt;
    TraceRing& ring = pipeline_ring();
    bt.start(&ring, &pipeline_log());
    constexpr uint64_t kOps = uint64_t{3} * IterationDetector::HISTORY_MAX_CAPACITY;
    for (uint64_t i = 0; i < kOps; ++i) {
        TraceRing::Entry entry{};
        entry.schema_hash = crucible::SchemaHash{0x3000000 + i};
        entry.shape_hash = crucible::ShapeHash{0x4000000 + i};
        while (!ring.try_append(entry, MetaIndex::none())) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }
    const uint64_t target = ring.total_produced();
    while (bt.total_processed.load_acquire() < target) {
        CRUCIBLE_SPIN_PAUSE;
    }
    assert(bt.pending_op_count() <= IterationDetector::HISTORY_MAX_CAPACITY / 2 + IterationDetector::K);
    assert(bt.detector.pos_ - bt.detector.base_ <= IterationDetector::HISTORY_MAX_CAPACITY);
    bt.stop();
}

}  // namespace

int main() {
    test_prefix_that_never_recurs_publishes_a_region();
    test_detect_stage_holds_a_bounded_trace();
    return 0;
}
