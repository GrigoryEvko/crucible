#pragma once

// The shared part of test_region_cache: the two variants of the synthetic
// op stream, the helpers that feed them to a Vigil, and the tests of each
// group.  The helpers, the tests of the cache alone and main are in
// test_region_cache.cpp.  Each other source file of the test holds one group
// of tests.

#include <crucible/Vigil.h>

#include <cstdint>

namespace test_region_cache {

inline constexpr uint32_t NUM_OPS = 8;
inline constexpr uint32_t K = crucible::Vigil::ALIGNMENT_K;  // 5

// Both variants share one set of schemas, so only the shapes diverge.
inline constexpr crucible::SchemaHash SCHEMA[NUM_OPS] = {
    crucible::SchemaHash{0x100}, crucible::SchemaHash{0x101}, crucible::SchemaHash{0x102}, crucible::SchemaHash{0x103},
    crucible::SchemaHash{0x104}, crucible::SchemaHash{0x105}, crucible::SchemaHash{0x106}, crucible::SchemaHash{0x107}};

// Variant A holds every tensor at 1024 elements, which is 4096 bytes.
inline constexpr crucible::ShapeHash SHAPE_A[NUM_OPS] = {
    crucible::ShapeHash{0x200}, crucible::ShapeHash{0x201}, crucible::ShapeHash{0x202}, crucible::ShapeHash{0x203},
    crucible::ShapeHash{0x204}, crucible::ShapeHash{0x205}, crucible::ShapeHash{0x206}, crucible::ShapeHash{0x207}};

// Variant B keeps A's first three shapes and changes the rest to 2048
// elements.  Both the shape hash and the tensor shapes differ there, so
// the two variants get different content hashes while sharing a prefix.
inline constexpr crucible::ShapeHash SHAPE_B[NUM_OPS] = {
    crucible::ShapeHash{0x200}, crucible::ShapeHash{0x201}, crucible::ShapeHash{0x202}, crucible::ShapeHash{0x303},
    crucible::ShapeHash{0x304}, crucible::ShapeHash{0x305}, crucible::ShapeHash{0x306}, crucible::ShapeHash{0x307}};

struct OpData {
    crucible::TraceRing::Entry entry{};
    crucible::TensorMeta metas[2]{};
    uint16_t n_metas = 0;
};

void* fake_ptr(uint32_t variant, uint32_t iter, uint32_t op);
crucible::TensorMeta make_meta(void* data_ptr, int64_t size);
OpData make_op(const crucible::ShapeHash* shapes, uint32_t variant, uint32_t iter, uint32_t op_idx);
void feed_record(crucible::Vigil& vigil, const crucible::ShapeHash* shapes, uint32_t variant, uint32_t iter);
void feed_trigger(crucible::Vigil& vigil, const crucible::ShapeHash* shapes, uint32_t variant, uint32_t iter);
void align_and_activate(crucible::Vigil& vigil, const crucible::ShapeHash* shapes, uint32_t variant, uint32_t iter);

// A switch between two cached variants, and a lookup that misses
// (test_region_cache_switch.cpp).
void test_cache_switch_mid_iter();
void test_cache_miss_fallback();

// The data that crosses a switch (test_region_cache_migration.cpp).
void test_cache_data_migration();

// Switches back and forth (test_region_cache_repeated.cpp).
void test_cache_repeated_switching();

}  // namespace test_region_cache
