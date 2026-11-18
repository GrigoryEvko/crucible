// The two values of the detector that a Vigil gives, read while the
// background pipeline runs.
//
// The detect stage writes the detector with no order.  A read of the
// detector itself by the foreground while the pipeline runs is a data race,
// and ThreadSanitizer reports it.  The Vigil reads a snapshot that the detect
// stage stores in an atomic cell after each batch.  Under the tsan preset, a
// race report ends the test.  In each build, the test checks that the
// snapshot after a flush counts the boundaries of the loop and holds its
// period.

#include <crucible/TraceRing.h>
#include <crucible/Vigil.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include "test_harness.h"
#include <cstdint>

using crucible::Vigil;

namespace {

// The ops of one iteration of the loop, and its count of iterations.
constexpr uint64_t kPeriod = 8;
constexpr uint64_t kIterations = 64;

// The tensor of each op.  The detector reads the schemas and the shapes, so
// the tensor needs no data address.
crucible::TensorMeta make_meta() noexcept {
    crucible::TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = crucible::tensor_dim(1024);
    meta.strides[0] = crucible::tensor_dim(1);
    meta.dtype = crucible::ScalarType::Float;
    meta.device_type = crucible::DeviceType::CPU;
    meta.device_idx = 0;
    meta.data_ptr = crucible::external_data_ptr(nullptr);
    return meta;
}

// Records the op at the position of the loop.  Each iteration has the same
// schemas and shapes.
void record(Vigil& vigil, uint64_t position) {
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{0x100 + position % kPeriod};
    entry.shape_hash = crucible::ShapeHash{0x200 + position % kPeriod};
    entry.num_inputs = 1;
    entry.num_outputs = 1;
    const ::fixy::FixedArray<crucible::TensorMeta, 2> metas{make_meta(), make_meta()};
    (void)vigil.record_op(crucible::test::certify_synthetic_op(entry, metas, 2));
}

// Reads the two values after each op, with no flush, while the detect stage
// reads the ops of the loop.  After the flush, the snapshot holds each op of
// the loop.  No reset restarts the detector, so its count of boundaries only
// rises, and the last count is the largest.
void test_detector_reads_while_the_pipeline_runs(::fixy::TestRunnerCtx const& test) {
    Vigil vigil;
    uint32_t largest_seen = 0;
    uint64_t confirmed_reads = 0;
    for (uint64_t position = 0; position < kPeriod * kIterations; ++position) {
        record(vigil, position);
        largest_seen = ::fixy::max(largest_seen, vigil.bg_detector_boundaries());
        if (vigil.bg_detector_confirmed()) ++confirmed_reads;
    }
    vigil.flush();
    CRUCIBLE_TEST_ASSERT(test, vigil.bg_detector_confirmed());
    CRUCIBLE_TEST_ASSERT(test, vigil.bg_detector_boundaries() > 0);
    CRUCIBLE_TEST_ASSERT(test, vigil.bg_detector_boundaries() >= largest_seen);
    ::fixy::check::pass(test, "  the detector reported {} boundaries, and {} reads saw a period\n",
                        vigil.bg_detector_boundaries(), confirmed_reads);
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_detector_reads_while_the_pipeline_runs(test);
    return 0;
}
