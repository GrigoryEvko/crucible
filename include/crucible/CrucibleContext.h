#pragma once

// CrucibleContext: Foreground state machine for Tier 1 compiled replay.
//
// Manages the transition between RECORD mode (ops recorded + executed
// eagerly by the Vessel adapter) and COMPILED mode (ops replayed from
// a compiled trace with pre-allocated output storage).
//
// Lifecycle:
//   1. Start in RECORD mode (foreground records, background builds)
//   2. Background thread produces RegionNode with MemoryPlan
//   3. Foreground calls activate(region) → COMPILED mode
//   4. Per-op: advance() → MATCH/DIVERGED/COMPLETE
//   5. On COMPLETE: iteration done, engine auto-resets for next
//   6. On DIVERGED: caller calls deactivate() → back to RECORD
//
// Does NOT own the recording pipeline (TraceRing, MetaLog,
// BackgroundThread). Those are separate components managed by the
// Vessel adapter or test harness. CrucibleContext owns only the
// compiled replay state: PoolAllocator + ReplayEngine.

#include <crucible/Platform.h>
#include <crucible/PoolAllocator.h>
#include <crucible/ReplayEngine.h>
#include <crucible/Types.h>

#include <cassert>
#include <cstdint>
#include <cstring>

namespace crucible {

enum class ContextMode : uint8_t {
  RECORD,    // Foreground records + executes eagerly
  COMPILED,  // Foreground replays with pre-allocated outputs
};

// ═══════════════════════════════════════════════════════════════════
// DispatchResult: Return value from Vigil::dispatch_op()
//
// Tells the Vessel adapter what happened with this op:
//   RECORD   → execute eagerly (normal allocation path)
//   COMPILED → outputs are pre-allocated, use output_ptr(j)
//
// 8 bytes: fits in a single register on x86-64.
// ═══════════════════════════════════════════════════════════════════

struct DispatchResult {
  enum class Action : uint8_t {
    RECORD,    // Execute eagerly (normal allocation)
    COMPILED,  // Execute into pre-allocated output pointers
  };

  Action action = Action::RECORD;
  ReplayStatus status = ReplayStatus::MATCH;
  uint8_t pad[2]{};
  OpIndex op_index;  // position in region (diagnostics); none() for RECORD
};

static_assert(sizeof(DispatchResult) == 8, "DispatchResult: 1+1+2+4 = 8 bytes");

struct CrucibleContext {
  CrucibleContext() = default;

  CrucibleContext(const CrucibleContext&) = delete("owns PoolAllocator with interior pointers");
  CrucibleContext& operator=(const CrucibleContext&) = delete("owns PoolAllocator with interior pointers");
  CrucibleContext(CrucibleContext&&) = delete("PoolAllocator has interior pointers into pool");
  CrucibleContext& operator=(CrucibleContext&&) = delete("PoolAllocator has interior pointers into pool");

  // ── Activate compiled mode ──
  //
  // Takes a RegionNode with a valid MemoryPlan (from BackgroundThread).
  // Materializes the PoolAllocator from the plan, inits the ReplayEngine.
  // External slots must be registered via register_external() before
  // the first advance() call.
  //
  // Returns false if the region has no plan (not ready for Tier 1).
  // The region and its plan must outlive the CrucibleContext (or until
  // deactivate() is called).
  [[nodiscard]] bool activate(const RegionNode* region) {
    assert(region && "null RegionNode");
    if (!region->plan) [[unlikely]]
      return false;

    // If already compiled, deactivate first (re-activation with new region).
    if (mode_ == ContextMode::COMPILED)
      deactivate();

    pool_.init(region->plan);
    engine_.init(region, &pool_);
    active_region_ = region;
    mode_ = ContextMode::COMPILED;
    return true;
  }

  // ── Deactivate: COMPILED → RECORD ──
  //
  // Destroys the PoolAllocator, invalidates the ReplayEngine.
  // Called on divergence or when the caller wants to re-record.
  void deactivate() {
    mode_ = ContextMode::RECORD;
    pool_.destroy();
    active_region_ = nullptr;
    // engine_ still points to freed pool — that's fine, engine is
    // only usable when mode_ == COMPILED, and we just set RECORD.
  }

  // ── Per-op advance (COMPILED mode hot path) ──
  //
  // Checks guards and advances position. Returns:
  //   MATCH:    guards passed, output_ptr()/input_ptr() are valid,
  //             more ops remain in this iteration
  //   DIVERGED: guard failed — position stays, caller should deactivate()
  //   COMPLETE: last op matched and iteration is done — output_ptr()/
  //             input_ptr() are valid for this final op. Engine
  //             auto-resets on the NEXT advance() call.
  //
  // Calling in RECORD mode is a bug (asserts in debug, UB in release).
  [[nodiscard]] CRUCIBLE_INLINE ReplayStatus
  advance(SchemaHash schema_hash, ShapeHash shape_hash) {
    assert(mode_ == ContextMode::COMPILED && "advance() requires COMPILED mode");

    // If the previous advance() returned COMPLETE, the engine is
    // at position num_ops. Reset now before checking the new op.
    if (engine_.is_complete()) [[unlikely]]
      engine_.reset();

    auto status = engine_.advance(schema_hash, shape_hash);

    if (status == ReplayStatus::DIVERGED) [[unlikely]] {
      diverged_count_++;
      return ReplayStatus::DIVERGED;
    }

    // ReplayEngine returns MATCH even for the last op. Check if
    // all ops are now consumed — if so, this was the final op.
    // Don't reset yet — caller may need output_ptr()/input_ptr().
    if (engine_.is_complete()) [[unlikely]] {
      compiled_iterations_++;
      return ReplayStatus::COMPLETE;
    }

    return ReplayStatus::MATCH;
  }

  // ── Output/input pointer forwarding ──
  //
  // Valid after advance() returned MATCH or COMPLETE.
  [[nodiscard]] CRUCIBLE_INLINE void* output_ptr(uint16_t j) const {
    assert(mode_ == ContextMode::COMPILED && "output_ptr() requires COMPILED mode");
    return engine_.output_ptr(j);
  }

  [[nodiscard]] CRUCIBLE_INLINE void* input_ptr(uint16_t j) const {
    assert(mode_ == ContextMode::COMPILED && "input_ptr() requires COMPILED mode");
    return engine_.input_ptr(j);
  }

  // ── Register external slot pointer ──
  //
  // Must be called after activate(), before the first advance(),
  // for each external slot in the MemoryPlan.
  void register_external(SlotId sid, void* ptr) {
    assert(mode_ == ContextMode::COMPILED && "register_external() requires COMPILED mode");
    pool_.register_external(sid, ptr);
  }

  // ── Switch to a different compiled region (mid-iteration safe) ──
  //
  // Switches from the current region to `alt` at divergence position
  // `div_pos`.  For div_pos=0 this is a plain re-activation (no data
  // to migrate).  For div_pos>0:
  //
  //   1. Detach old pool buffer (keeps memory alive, resets allocator)
  //   2. Init new pool from alt's plan
  //   3. Copy only the output slots produced by prefix ops 0..div_pos-1
  //      from old layout offsets to new layout offsets
  //   4. Old buffer freed (RAII via DetachedPool)
  //   5. Engine initialized on new region, advanced past prefix
  //
  // Caller must have verified prefix compatibility: ops 0..div_pos-1
  // have identical schema_hash+shape_hash in both regions.
  //
  // External slots must be re-registered by the caller after success.
  //
  // Returns false if alt has no plan.  On failure, the context is
  // deactivated — caller must handle fallback to RECORDING.
  [[nodiscard]] bool switch_region(const RegionNode* alt, uint32_t div_pos) {
    assert(mode_ == ContextMode::COMPILED && "switch_region requires COMPILED mode");
    assert(alt && "null alternate region");
    if (!alt->plan) [[unlikely]]
      return false;

    // pos=0: no prefix data written — plain re-activation.
    if (div_pos == 0)
      return activate(alt);

    const auto* old_region = active_region_;
    assert(old_region && old_region->plan && "no active region to switch from");

    // Detach old pool: keeps the buffer alive, resets allocator state.
    auto old_pool = pool_.detach();

    // Create new pool from alternate's plan.
    pool_.init(alt->plan);

    // Copy only the prefix output slot data from old → new layout.
    migrate_prefix_slots_(old_region, alt, old_pool.base, div_pos);

    // old_pool destructor frees the detached buffer here.
    // (scope exit, or we can be explicit for clarity)

    // Engine → new region, advance past verified prefix.
    engine_.init(alt, &pool_);
    active_region_ = alt;

    for (uint32_t i = 0; i < div_pos; i++) {
      auto s = engine_.advance(alt->ops[i].schema_hash,
                               alt->ops[i].shape_hash);
      assert(s == ReplayStatus::MATCH || s == ReplayStatus::COMPLETE);
      (void)s;
    }

    return true;
  }

  // ── Queries ──

  [[nodiscard]] ContextMode mode() const { return mode_; }
  [[nodiscard]] bool is_compiled() const { return mode_ == ContextMode::COMPILED; }
  [[nodiscard]] bool is_recording() const { return mode_ == ContextMode::RECORD; }

  [[nodiscard]] uint32_t compiled_iterations() const { return compiled_iterations_; }
  [[nodiscard]] uint32_t diverged_count() const { return diverged_count_; }
  [[nodiscard]] const RegionNode* active_region() const { return active_region_; }

  // Access to sub-components (for diagnostics, not hot path).
  [[nodiscard]] const ReplayEngine& engine() const { return engine_; }
  [[nodiscard]] const PoolAllocator& pool() const { return pool_; }

 private:
  // Copy output slot data for prefix ops (0..div_pos-1) from old pool
  // layout to new pool layout.  Only copies internal slots; external
  // slots keep their existing allocations.  Bitset prevents double-
  // copying aliased slots (in-place ops share SlotIds).
  void migrate_prefix_slots_(
      const RegionNode* old_region,
      const RegionNode* alt,
      const void* old_pool_base,
      uint32_t div_pos)
  {
    const auto* old_plan = old_region->plan;
    const auto* new_plan = alt->plan;

    // 1024-bit bitset: tracks which new-side slots we've already copied.
    static constexpr uint32_t MAX_SLOTS = 1024;
    assert(new_plan->num_slots <= MAX_SLOTS
           && "slot count exceeds migration bitset capacity");
    uint64_t visited[(MAX_SLOTS + 63) / 64]{};

    for (uint32_t i = 0; i < div_pos; i++) {
      const auto& old_te = old_region->ops[i];
      const auto& new_te = alt->ops[i];

      if (!old_te.output_slot_ids || !new_te.output_slot_ids)
        continue;

      assert(old_te.num_outputs == new_te.num_outputs
             && "prefix ops with identical hashes must have equal output counts");

      for (uint16_t j = 0; j < old_te.num_outputs; j++) {
        const SlotId old_sid = old_te.output_slot_ids[j];
        const SlotId new_sid = new_te.output_slot_ids[j];

        if (!old_sid.is_valid() || !new_sid.is_valid()) continue;

        // External slots are registered separately by the caller.
        if (old_plan->slots[old_sid.raw()].is_external ||
            new_plan->slots[new_sid.raw()].is_external)
          continue;

        // Deduplicate aliased outputs (in-place ops).
        const uint32_t ns = new_sid.raw();
        if (visited[ns / 64] & (uint64_t{1} << (ns % 64))) continue;
        visited[ns / 64] |= uint64_t{1} << (ns % 64);

        const uint64_t old_offset = old_plan->slots[old_sid.raw()].offset_bytes;
        const uint64_t new_offset = new_plan->slots[ns].offset_bytes;
        const uint64_t nbytes     = old_plan->slots[old_sid.raw()].nbytes;

        if (nbytes > 0 && old_pool_base) {
          std::memcpy(
              static_cast<char*>(pool_.pool_base()) + new_offset,
              static_cast<const char*>(old_pool_base) + old_offset,
              static_cast<size_t>(nbytes));
        }
      }
    }
  }

  ContextMode mode_ = ContextMode::RECORD;
  [[maybe_unused]] uint8_t pad_[3]{};          // alignment for pool_
  uint32_t compiled_iterations_ = 0;
  uint32_t diverged_count_ = 0;
  const RegionNode* active_region_ = nullptr;
  PoolAllocator pool_;                        // 32B — pre-allocated memory
  ReplayEngine engine_;                       // 24B — per-op walker
};

static_assert(sizeof(CrucibleContext) == 80, "CrucibleContext: 1+3+4+4+8+32+24 padded to 80");

} // namespace crucible
