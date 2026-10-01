#pragma once

// The shared part of test_end_to_end: the synthetic workload, the helpers
// that feed it to the trace ring and wait for the background thread, and
// the tests of each group.  The helpers and main are in test_end_to_end.cpp.
// Each other source file of the test holds one group of tests.

#include <crucible/BackgroundThread.h>
#include <crucible/CrucibleContext.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace test_end_to_end {

// The views of the replay chain are minted on the thread that holds a
// Vigil's producer claim.  This test drives the context without a Vigil,
// so it takes that context from the test door.
inline constexpr crucible::VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<crucible::Vigil>();

// The synthetic workload is a linear chain: op 0 has no input, and
// every later op consumes exactly the tensor its predecessor produced.
// Each tensor is a contiguous 1D float of 1024 elements, which is where
// the literal 4096 in the fill and bounds checks comes from.

inline constexpr uint32_t NUM_OPS = 8;

inline constexpr crucible::SchemaHash SCHEMA[NUM_OPS] = {
    crucible::SchemaHash{0x100}, crucible::SchemaHash{0x101}, crucible::SchemaHash{0x102}, crucible::SchemaHash{0x103},
    crucible::SchemaHash{0x104}, crucible::SchemaHash{0x105}, crucible::SchemaHash{0x106}, crucible::SchemaHash{0x107}};
inline constexpr crucible::ShapeHash SHAPE[NUM_OPS] = {
    crucible::ShapeHash{0x200}, crucible::ShapeHash{0x201}, crucible::ShapeHash{0x202}, crucible::ShapeHash{0x203},
    crucible::ShapeHash{0x204}, crucible::ShapeHash{0x205}, crucible::ShapeHash{0x206}, crucible::ShapeHash{0x207}};

crucible::TensorMeta make_meta(void* data_ptr);

// These addresses are never dereferenced.  They serve only as hash keys
// for the producer-consumer match, so any distinct non-null value does.
void* fake_ptr(uint32_t iter, uint32_t op);

void feed_iteration(crucible::TraceRing* ring, crucible::MetaLog* meta_log, uint32_t iter);

// K is the length of the detector's signature, so feeding exactly the
// first K ops of an iteration is what presents it with a repeat.
void feed_trigger(crucible::TraceRing* ring, crucible::MetaLog* meta_log, uint32_t iter);

// The feeding thread has no other handshake with the background thread,
// so it spins on the processed counter until the drain has caught up
// with everything produced so far.  The spin cap turns a stalled
// background thread into a failed assertion instead of a hang.
void wait_processed(crucible::BackgroundThread& bt, crucible::TraceRing& ring);

// A region, its replay and a divergence (test_end_to_end_replay.cpp).
void test_pipeline_basic();
void test_pipeline_divergence();

// The pool behind the replay (test_end_to_end_pool.cpp).
void test_pipeline_data_flow();
void test_pipeline_pool_bounds();

// A second boundary (test_end_to_end_multi_iteration.cpp).
void test_pipeline_multi_iteration();

}  // namespace test_end_to_end
