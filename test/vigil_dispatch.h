#pragma once

// The shared part of test_vigil_dispatch: the synthetic op stream, the
// helpers that feed it to a Vigil, and the tests of each group.  The
// helpers, the compile-time checks and main are in test_vigil_dispatch.cpp.
// Each other source file of the test holds one group of tests.

#include <crucible/Vigil.h>

#include <cstdint>

namespace test_vigil_dispatch {

inline constexpr uint32_t NUM_OPS = 8;
inline constexpr uint32_t K = crucible::Vigil::ALIGNMENT_K;

inline constexpr crucible::SchemaHash SCHEMA[NUM_OPS] = {
    crucible::SchemaHash{0x100}, crucible::SchemaHash{0x101}, crucible::SchemaHash{0x102}, crucible::SchemaHash{0x103},
    crucible::SchemaHash{0x104}, crucible::SchemaHash{0x105}, crucible::SchemaHash{0x106}, crucible::SchemaHash{0x107}};
inline constexpr crucible::ShapeHash SHAPE[NUM_OPS] = {
    crucible::ShapeHash{0x200}, crucible::ShapeHash{0x201}, crucible::ShapeHash{0x202}, crucible::ShapeHash{0x203},
    crucible::ShapeHash{0x204}, crucible::ShapeHash{0x205}, crucible::ShapeHash{0x206}, crucible::ShapeHash{0x207}};

struct OpData {
    crucible::TraceRing::Entry entry{};
    crucible::TensorMeta metas[2]{};
    uint16_t n_metas = 0;
};

void* fake_ptr(uint32_t iter, uint32_t op);
crucible::TensorMeta make_meta(void* data_ptr);
OpData make_op(uint32_t iter, uint32_t op_idx);
void feed_record(crucible::Vigil& vigil, uint32_t iter);
void feed_trigger(crucible::Vigil& vigil, uint32_t iter);

// Callers get back an engine sitting at position 0 and ready for whole
// compiled iterations.
//
// Reaching that state takes two phases.  The first K ops still return
// RECORD: each one extends a sliding-window match against the head of
// the region, and only the last of them confirms the match and
// activates.  The remaining ops of that iteration then run compiled,
// which is what leaves the engine back at position 0.
void align_and_activate(crucible::Vigil& vigil, uint32_t iter);

// The producer thread gate (test_vigil_dispatch_threads.cpp).
void test_second_producer_is_rejected();
void test_cold_gates_reject_a_context_on_another_thread();
void test_second_thread_cannot_claim_the_brand();

// Replay, divergence and recovery through dispatch_op
// (test_vigil_dispatch_replay.cpp).
void test_dispatch_basic();
void test_dispatch_divergence();
void test_dispatch_recovery();

// The pool behind the compiled iterations (test_vigil_dispatch_pool.cpp).
void test_dispatch_data_flow();
void test_dispatch_pool_bounds();

// dispatch_op_pure (test_vigil_dispatch_pure.cpp and
// test_vigil_dispatch_pure_recovery.cpp).
void test_dispatch_pure_matches_dispatch_op();
void test_dispatch_pure_divergence_and_recovery();

}  // namespace test_vigil_dispatch
