#pragma once

// The shared part of test_vigil: the rig of the divergence tests, which the
// tests of the producer role use too, and the tests of each group.  The
// persistence test and main are in test_vigil.cpp.  Each other source file
// of the test holds one group of tests.

#include <crucible/Vigil.h>

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────
// A divergence must leave no published region behind
//
// While the context is compiled, dispatch_op takes the replay branch and
// never reads the publication slot.  A region the background thread
// publishes during that window therefore sits there unobserved.  It was cut
// from entries recorded before the divergence, so a divergence that leaves
// it in place hands it to the very next dispatch, which starts aligning
// against the trace the divergence just invalidated.  Alignment records
// nothing, so the background thread never receives the new trace either.
//
// The window opens deterministically below.  The ring is stuffed through
// record_op while the context is compiled, so the background thread
// publishes a second region the foreground cannot see.  flush() then leaves
// the ring empty and the background thread idle, and the diverging op is
// deliberately not recorded, so no further region can appear between the
// divergence and the dispatch that follows it.
// ─────────────────────────────────────────────────────────────────────
namespace divrig {

inline constexpr uint32_t NUM_OPS = 8;
inline constexpr uint32_t K = crucible::Vigil::ALIGNMENT_K;

inline constexpr crucible::SchemaHash SCHEMA[NUM_OPS] = {
    crucible::SchemaHash{0x100}, crucible::SchemaHash{0x101}, crucible::SchemaHash{0x102}, crucible::SchemaHash{0x103},
    crucible::SchemaHash{0x104}, crucible::SchemaHash{0x105}, crucible::SchemaHash{0x106}, crucible::SchemaHash{0x107}};
inline constexpr crucible::ShapeHash SHAPE[NUM_OPS] = {
    crucible::ShapeHash{0x200}, crucible::ShapeHash{0x201}, crucible::ShapeHash{0x202}, crucible::ShapeHash{0x203},
    crucible::ShapeHash{0x204}, crucible::ShapeHash{0x205}, crucible::ShapeHash{0x206}, crucible::ShapeHash{0x207}};

// A schema that appears in no region, so the divergence it causes finds no
// alternate in the region cache and falls through to the recording reset.
inline constexpr crucible::SchemaHash FOREIGN_SCHEMA = crucible::SchemaHash{0x9999};

struct OpData {
    crucible::TraceRing::Entry entry{};
    crucible::TensorMeta metas[2]{};
    uint16_t n_metas = 0;
};

OpData op_at(uint32_t iter, uint32_t op_idx);

// Two whole iterations and one signature-length head, which is the shortest
// input that makes the detector confirm a boundary and publish a region.
void feed_one_region(crucible::Vigil& vigil, uint32_t first_iter);

}  // namespace divrig

namespace test_vigil {

// A divergence drops what the foreground has not observed
// (test_vigil_divergence.cpp).
void test_divergence_drops_the_unobserved_region();
void test_divergence_drops_a_half_finished_alignment();

// The producer role and its surface (test_vigil_producer.cpp).
void test_record_op_claims_the_producer_role();
void test_producer_surface_takes_the_claim_context();

// A region keeps the tensor metadata that the foreground recorded, after the
// foreground writes over the metadata log (test_vigil_metadata.cpp).
void test_region_keeps_its_metadata_after_the_log_wraps();

}  // namespace test_vigil
