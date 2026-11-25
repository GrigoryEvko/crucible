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

void feed_iteration(crucible::test::TraceRig& rig, Iteration iteration) {
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
            metas.set(
                ::fixy::Index<2>::check(idx++).expect("a test reads an element at or past the count of its array"),
                make_meta(fake_ptr(iter, i - 1)));
        metas.set(::fixy::Index<2>::check(idx++).expect("a test reads an element at or past the count of its array"),
                  make_meta(fake_ptr(iter, i)));

        auto ms = rig.append_metas(crucible::test::records_of(metas, n_metas));
        CRUCIBLE_TEST_ASSERT(kTest, ms.is_valid() && "MetaLog overflow");
        CRUCIBLE_TEST_ASSERT(kTest, rig.append_entry(e, ms) && "TraceRing full");
    }
}

void feed_trigger(crucible::test::TraceRig& rig, Iteration iteration) {
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
            metas.set(
                ::fixy::Index<2>::check(idx++).expect("a test reads an element at or past the count of its array"),
                make_meta(fake_ptr(iter, i - 1)));
        metas.set(::fixy::Index<2>::check(idx++).expect("a test reads an element at or past the count of its array"),
                  make_meta(fake_ptr(iter, i)));

        auto ms = rig.append_metas(crucible::test::records_of(metas, n_metas));
        CRUCIBLE_TEST_ASSERT(kTest, ms.is_valid());
        CRUCIBLE_TEST_ASSERT(kTest, rig.append_entry(e, ms));
    }
}

void wait_processed(BackgroundThread const& bt, crucible::test::TraceRig const& rig) {
    const uint64_t target = rig.ring_total_produced();
    uint64_t spins = 0;
    while (bt.total_processed.load_acquire() < target) {
        CRUCIBLE_TEST_ASSERT(kTest, ++spins < 100000000 && "bg thread did not finish processing");
        CRUCIBLE_SPIN_PAUSE;
    }
}

void run_on_first_region(RegionCheck const& check) {
    crucible::test::TraceRig rig{};
    feed_iteration(rig, Iteration{0});
    feed_iteration(rig, Iteration{1});
    feed_trigger(rig, Iteration{2});
    with_background_thread(::foundation::effects::testing::test().alloc, rig.ring_side(), rig.log_side(), {},
                           [&rig, &check](BackgroundThread const& bt, auto stopped) noexcept {
                               auto running = std::move(stopped).start(
                                   ::fixy::InitLoadCtx{::foundation::effects::testing::init()});
                               wait_processed(bt, rig);
                               check(rig, bt, bt.active_region().expect("BackgroundThread did not produce a region"));
                               static_cast<void>(std::move(running).stop());
                           });
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
