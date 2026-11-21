// The test is several source files of one executable, so that no
// translation unit holds every test:
//
//   end_to_end.h                  the shared part
//   this file                     the helpers and main
//   ..._replay.cpp                a region, its replay and a divergence
//   ..._pool.cpp                  the pool behind the replay
//   ..._multi_iteration.cpp       a second boundary

#include "end_to_end.h"

#include <crucible/CrucibleContext.h>
#include <crucible/BackgroundThread.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include "test_harness.h"
#include <bit>
#include <cstdint>
#include <cstdio>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;

namespace test_end_to_end {

TensorMeta make_meta(void* data_ptr) {
    TensorMeta m{};
    m.ndim = 1;
    m.sizes[0] = ::crucible::tensor_dim(1024);
    m.strides[0] = ::crucible::tensor_dim(1);
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CPU;
    m.device_idx = 0;
    m.layout = Layout::Strided;
    m.data_ptr = external_data_ptr(data_ptr);
    return m;
}

void* fake_ptr(uint32_t iter, uint32_t op) {
    return std::bit_cast<void*>(static_cast<std::uintptr_t>((iter + 1) * 0x100000 + (op + 1) * 0x1000));
}

void feed_iteration(TraceRing& ring, MetaLog& meta_log, Iteration iteration) {
    const uint32_t iter = iteration.raw();
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        TraceRing::Entry e{};
        e.schema_hash = SCHEMA[i];
        e.shape_hash = SHAPE[i];
        e.num_inputs = (i == 0) ? 0 : 1;
        e.num_outputs = 1;

        const auto n_metas = static_cast<uint16_t>(e.num_inputs + e.num_outputs);
        ::fixy::FixedArray<TensorMeta, 2> metas{};
        uint16_t idx = 0;
        if (i > 0)
            metas.with_element(
                ::fixy::Index<2>::check(idx++).expect("a test reads an element at or past the count of its array"),
                [meta = make_meta(fake_ptr(iter, i - 1))](TensorMeta& record) noexcept { record = meta; });
        metas.with_element(
            ::fixy::Index<2>::check(idx++).expect("a test reads an element at or past the count of its array"),
            [meta = make_meta(fake_ptr(iter, i))](TensorMeta& record) noexcept { record = meta; });

        auto ms = meta_log.try_append(crucible::test::records_of(metas, n_metas));
        CRUCIBLE_TEST_ASSERT(kTest, ms.is_valid() && "MetaLog overflow");
        CRUCIBLE_TEST_ASSERT(kTest, ring.try_append(e, ms) && "TraceRing full");
    }
}

void feed_trigger(TraceRing& ring, MetaLog& meta_log, Iteration iteration) {
    const uint32_t iter = iteration.raw();
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        TraceRing::Entry e{};
        e.schema_hash = SCHEMA[i];
        e.shape_hash = SHAPE[i];
        e.num_inputs = (i == 0) ? 0 : 1;
        e.num_outputs = 1;

        const auto n_metas = static_cast<uint16_t>(e.num_inputs + e.num_outputs);
        ::fixy::FixedArray<TensorMeta, 2> metas{};
        uint16_t idx = 0;
        if (i > 0)
            metas.with_element(
                ::fixy::Index<2>::check(idx++).expect("a test reads an element at or past the count of its array"),
                [meta = make_meta(fake_ptr(iter, i - 1))](TensorMeta& record) noexcept { record = meta; });
        metas.with_element(
            ::fixy::Index<2>::check(idx++).expect("a test reads an element at or past the count of its array"),
            [meta = make_meta(fake_ptr(iter, i))](TensorMeta& record) noexcept { record = meta; });

        auto ms = meta_log.try_append(crucible::test::records_of(metas, n_metas));
        CRUCIBLE_TEST_ASSERT(kTest, ms.is_valid());
        CRUCIBLE_TEST_ASSERT(kTest, ring.try_append(e, ms));
    }
}

void wait_processed(BackgroundThread& bt, TraceRing& ring) {
    const uint64_t target = ring.total_produced();
    uint64_t spins = 0;
    while (bt.total_processed.load_acquire() < target) {
        CRUCIBLE_TEST_ASSERT(kTest, ++spins < 100000000 && "bg thread did not finish processing");
        CRUCIBLE_SPIN_PAUSE;
    }
}

}  // namespace test_end_to_end

int main() {
    using namespace test_end_to_end;
    ::fixy::report(::fixy::Sink::Out, "test_end_to_end:\n");
    test_pipeline_basic();
    test_pipeline_divergence();
    test_pipeline_data_flow();
    test_pipeline_pool_bounds();
    test_pipeline_multi_iteration();
    ::fixy::check::pass(kTest, "test_end_to_end: all tests passed\n");
    return 0;
}
