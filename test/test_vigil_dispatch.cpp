// Integration tests for compiled replay across the whole pipeline, from
// the trace ring through the background thread and back out of
// dispatch_op.  Nothing here is mocked, so a failure can come from any
// stage and the assertions say which one.
//
// The test is several source files of one executable, so that no
// translation unit holds every test:
//
//   vigil_dispatch.h                      the shared part
//   this file                             the helpers, the compile-time
//                                         checks and main
//   ..._threads.cpp                       the producer thread gate
//   ..._replay.cpp                        replay, divergence and recovery
//   ..._pool.cpp                          the pool behind the replay
//   ..._pure.cpp                          dispatch_op_pure against
//                                         dispatch_op
//   ..._pure_recovery.cpp                 dispatch_op_pure through a
//                                         divergence and a recovery

#include "vigil_dispatch.h"

#include <crucible/Vigil.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>
#include "test_harness.h"
#include "test_assert.h"
#include <bit>
#include <cstdint>
#include <cstdio>

using namespace crucible;

namespace test_vigil_dispatch {

void* fake_ptr(uint32_t iter, uint32_t op) {
    return std::bit_cast<void*>(static_cast<std::uintptr_t>((iter + 1) * 0x100000 + (op + 1) * 0x1000));
}

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

OpData make_op(uint32_t iter, uint32_t op_idx) {
    OpData d;
    d.entry.schema_hash = SCHEMA[op_idx];
    d.entry.shape_hash = SHAPE[op_idx];
    d.entry.num_inputs = (op_idx == 0) ? 0 : 1;
    d.entry.num_outputs = 1;

    uint16_t idx = 0;
    if (op_idx > 0) d.metas[idx++] = make_meta(fake_ptr(iter, op_idx - 1));
    d.metas[idx++] = make_meta(fake_ptr(iter, op_idx));
    d.n_metas = static_cast<uint16_t>(d.entry.num_inputs + d.entry.num_outputs);
    return d;
}

void feed_record(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(iter, i);
        (void)vigil.record_op(crucible::test::certify_synthetic_entry(d.entry), d.metas, d.n_metas);
    }
}

void feed_trigger(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        auto d = make_op(iter, i);
        (void)vigil.record_op(crucible::test::certify_synthetic_entry(d.entry), d.metas, d.n_metas);
    }
}

void align_and_activate(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < K; i++) {
        // The mode reports the replay, not the publication, so it stays
        // RECORDING through every alignment op before the last one.
        assert(!vigil.is_compiled() && "the mode must not report a replay before the context activates");
        auto d = make_op(iter, i);
        auto r = vigil.dispatch_op(crucible::test::certify_synthetic_entry(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::RECORD && "alignment ops should return RECORD");
    }
    assert(vigil.context().is_compiled() && "CrucibleContext should be compiled after K alignment ops");
    assert(vigil.is_compiled() && "the mode must report the replay once the context activates");

    for (uint32_t i = K; i < NUM_OPS; i++) {
        auto d = make_op(iter, i);
        auto r = vigil.dispatch_op(crucible::test::certify_synthetic_entry(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }
}

// True when a call of dispatch_op_pure with a context of type Ctx compiles.
template <class Ctx>
concept can_dispatch_pure_with = requires(Vigil& vigil, Ctx const& ctx, TraceRing::ValidatedEntryPtr entry) {
    vigil.dispatch_op_pure(ctx, entry, nullptr, 0u);
};

namespace {
struct Stranger {};
}  // namespace

// Only the context of a Vigil's producer claim passes.  The unbranded
// foreground context has the empty row too, so its refusal shows that the
// brand, and not the row alone, is what the entry asks for.
static_assert(can_dispatch_pure_with<VigilFgCtx>);
static_assert(!can_dispatch_pure_with<::fixy::HotFgCtx>);
static_assert(!can_dispatch_pure_with<decltype(::foundation::effects::testing::foreground<Stranger>())>);
static_assert(!can_dispatch_pure_with<::fixy::BgLoadCtx>);
static_assert(!can_dispatch_pure_with<::fixy::BgDrainCtx>);
static_assert(!can_dispatch_pure_with<::fixy::InitLoadCtx>);
static_assert(!can_dispatch_pure_with<::fixy::TestRunnerCtx>);

// The accessors of the COMPILED arm are constexpr.  A valid read runs in the
// constant context in which neg_dispatch_result_compiled_status_on_record and
// neg_dispatch_result_compiled_op_index_on_record fail, so each of those
// fixtures fails on its precondition and not on the context.
constexpr ReplayStatus compiled_status_witness = [] {
    DispatchResult result = DispatchResult::compiled(ReplayStatus::COMPLETE, OpIndex{7});
    return result.compiled_status();
}();
static_assert(compiled_status_witness == ReplayStatus::COMPLETE);

constexpr OpIndex compiled_op_index_witness = [] {
    DispatchResult result = DispatchResult::compiled(ReplayStatus::MATCH, OpIndex{7});
    return result.compiled_op_index();
}();
static_assert(compiled_op_index_witness == OpIndex{7});
static_assert(DispatchResult::record().is_record() && !DispatchResult::record().is_compiled());

}  // namespace test_vigil_dispatch

int main() {
    using namespace test_vigil_dispatch;
    std::printf("test_vigil_dispatch:\n");
    test_second_producer_is_rejected();
    test_cold_gates_reject_a_context_on_another_thread();
    test_second_thread_cannot_claim_the_brand();
    test_dispatch_basic();
    test_dispatch_divergence();
    test_dispatch_recovery();
    test_dispatch_data_flow();
    test_dispatch_pool_bounds();
    test_dispatch_pure_matches_dispatch_op();
    test_dispatch_pure_divergence_and_recovery();
    std::printf("test_vigil_dispatch: all tests passed\n");
    return 0;
}
