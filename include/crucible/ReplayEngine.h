#pragma once

// ReplayEngine: Walks a compiled RegionNode one op at a time,
// verifying guards and providing pre-allocated output pointers.
//
// Used by CrucibleContext during COMPILED mode. For each op the
// Vessel adapter intercepts:
//   1. advance(schema_hash, shape_hash) → guard check + position advance
//   2. If MATCH: output_ptr(j) → pre-allocated storage for output j
//   3. Caller executes the op writing into that storage (Tier 1: eager)
//
// On divergence: returns DIVERGED, position stays at the failed op.
// On completion: returns COMPLETE after all ops consumed.
// Reset: reset() rewinds to op 0 for the next iteration.

#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>
#include <crucible/PoolAllocator.h>

#include <cassert>
#include <cstdint>

namespace crucible {

enum class ReplayStatus : uint8_t {
  MATCH,     // Op matches compiled trace, outputs ready in pool
  DIVERGED,  // Guard failed — schema or shape mismatch
  COMPLETE,  // All ops in region consumed (iteration done)
};

struct ReplayEngine {
  ReplayEngine() = default;

  ReplayEngine(const ReplayEngine&) = delete("ReplayEngine tracks mutable position state");
  ReplayEngine& operator=(const ReplayEngine&) = delete("ReplayEngine tracks mutable position state");

  // ── Init: bind to a compiled region + pool ──
  //
  // The region and pool must outlive the ReplayEngine.
  // Call reset() after init to start from op 0 (redundant but explicit).
  void init(const RegionNode* region, const PoolAllocator* pool) {
    assert(region && "null RegionNode");
    assert(pool && pool->is_initialized() && "pool not initialized");
    ops_ = region->ops;
    num_ops_ = region->num_ops;
    pool_ = pool;
    op_index_ = 0;
  }

  // ── Reset: rewind to op 0 for the next iteration ──
  //
  // Same compiled region, same pool. Just restart the walk.
  void reset() { op_index_ = 0; }

  // ── Hot path: advance one op ──
  //
  // Checks the guard at the current position against the live op's
  // schema_hash and shape_hash. On match: increments position,
  // caller queries output_ptr()/input_ptr(). On mismatch: position
  // stays (caller can inspect diverged_op_index()).
  //
  // Returns COMPLETE when all ops have been matched.
  [[nodiscard]] CRUCIBLE_INLINE ReplayStatus
  advance(uint64_t schema_hash, uint64_t shape_hash) {
    if (op_index_ >= num_ops_) [[unlikely]]
      return ReplayStatus::COMPLETE;

    const auto& entry = ops_[op_index_];

    // Hard guard: op identity must match exactly.
    if (entry.schema_hash != schema_hash) [[unlikely]]
      return ReplayStatus::DIVERGED;

    // Hard guard: tensor geometry must match exactly.
    // Different shapes → different kernels, different memory plan.
    if (entry.shape_hash != shape_hash) [[unlikely]]
      return ReplayStatus::DIVERGED;

    op_index_++;
    return ReplayStatus::MATCH;
  }

  // ── Output pointer for output j of the last matched op ──
  //
  // Valid only after advance() returned MATCH.
  // Returns nullptr for outputs with no assigned slot (empty tensors).
  [[nodiscard]] CRUCIBLE_INLINE void* output_ptr(uint16_t j) const {
    assert(op_index_ > 0 && "no matched entry — call advance() first");
    const auto& entry = ops_[op_index_ - 1];
    assert(j < entry.num_outputs && "output index out of bounds");
    SlotId sid = entry.output_slot_ids[j];
    return sid.is_valid() ? pool_->slot_ptr(sid) : nullptr;
  }

  // ── Input pointer for input j of the last matched op ──
  //
  // Valid only after advance() returned MATCH.
  // Returns nullptr for inputs with no assigned slot.
  [[nodiscard]] CRUCIBLE_INLINE void* input_ptr(uint16_t j) const {
    assert(op_index_ > 0 && "no matched entry — call advance() first");
    const auto& entry = ops_[op_index_ - 1];
    assert(j < entry.num_inputs && "input index out of bounds");
    SlotId sid = entry.input_slot_ids[j];
    return sid.is_valid() ? pool_->slot_ptr(sid) : nullptr;
  }

  // ── Queries ──

  // The last matched TraceEntry (valid after MATCH).
  [[nodiscard]] const TraceEntry& current_entry() const {
    assert(op_index_ > 0 && "no matched entry");
    return ops_[op_index_ - 1];
  }

  // Position of the last matched op (valid after MATCH).
  [[nodiscard]] uint32_t matched_op_index() const {
    assert(op_index_ > 0 && "no matched entry");
    return op_index_ - 1;
  }

  // Position where divergence occurred (valid after DIVERGED).
  [[nodiscard]] uint32_t diverged_op_index() const { return op_index_; }

  // Number of ops successfully matched so far.
  [[nodiscard]] uint32_t ops_matched() const { return op_index_; }

  // Total ops in the compiled region.
  [[nodiscard]] uint32_t num_ops() const { return num_ops_; }

  // All ops consumed?
  [[nodiscard]] bool is_complete() const { return op_index_ >= num_ops_; }

  // Is the engine bound to a region?
  [[nodiscard]] bool is_initialized() const { return ops_ != nullptr; }

 private:
  const TraceEntry* ops_ = nullptr;       // region's ops array (not owned)
  const PoolAllocator* pool_ = nullptr;   // slot_ptr() source (not owned)
  uint32_t num_ops_ = 0;                  // region's op count
  uint32_t op_index_ = 0;                 // next position to check
};

static_assert(sizeof(ReplayEngine) == 24, "ReplayEngine: 2 ptrs + 2×u32");

} // namespace crucible
