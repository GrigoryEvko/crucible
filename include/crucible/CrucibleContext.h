#pragma once

// CrucibleContext: Foreground state machine for Tier 1 compiled replay.
//
// Layout: engine_ is at offset 0 so the engine's hot cache line IS
// the context's hot cache line. mode_ lives at offset 56 (same line).
// All hot-path data in one 64-byte cache line:
//
//   [0..55]  engine_ (expected hashes, current_, slot_table_, ops_, counters)
//   [56]     mode_   (ContextMode: RECORD or COMPILED)
//   [57..63] pad + compiled_iterations_

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

// DispatchResult: Return value from Vigil::dispatch_op()
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
  [[nodiscard]] bool activate(const RegionNode* region)
      CRUCIBLE_NO_THREAD_SAFETY {
    assert(region && "null RegionNode");
    if (!region->plan) [[unlikely]]
      return false;

    if (mode_ == ContextMode::COMPILED)
      deactivate();

    pool_.init(region->plan);
    engine_.init(region, &pool_);
    active_region_ = region;
    mode_ = ContextMode::COMPILED;
    return true;
  }

  // ── Deactivate: COMPILED -> RECORD ──
  void deactivate() {
    mode_ = ContextMode::RECORD;
    pool_.destroy();
    active_region_ = nullptr;
  }

  // ── Per-op advance (COMPILED mode hot path) ──
  //
  // ReplayEngine now returns COMPLETE directly for the last matched op,
  // so we don't need a separate is_complete() check after every advance().
  //
  // Hot path (MATCH, non-last): two L1d comparisons + increment + pre-cache.
  // Cold path (COMPLETE): engine auto-resets for the next iteration.
  [[nodiscard]] CRUCIBLE_INLINE ReplayStatus
  advance(SchemaHash schema_hash, ShapeHash shape_hash) {
    assert(mode_ == ContextMode::COMPILED && "advance() requires COMPILED mode");

    auto status = engine_.advance(schema_hash, shape_hash);

    if (status == ReplayStatus::MATCH) [[likely]]
      return ReplayStatus::MATCH;

    if (status == ReplayStatus::COMPLETE) [[unlikely]] {
      compiled_iterations_++;
      // Auto-reset for next iteration. current_ survives reset
      // so output_ptr/input_ptr remain valid until next advance().
      engine_.reset();
      return ReplayStatus::COMPLETE;
    }

    // DIVERGED
    diverged_count_++;
    return ReplayStatus::DIVERGED;
  }

  // ── Output/input pointer forwarding ──
  [[nodiscard]] CRUCIBLE_INLINE void* output_ptr(uint16_t j) const CRUCIBLE_LIFETIMEBOUND {
    assert(mode_ == ContextMode::COMPILED && "output_ptr() requires COMPILED mode");
    return engine_.output_ptr(j);
  }

  [[nodiscard]] CRUCIBLE_INLINE void* input_ptr(uint16_t j) const CRUCIBLE_LIFETIMEBOUND {
    assert(mode_ == ContextMode::COMPILED && "input_ptr() requires COMPILED mode");
    return engine_.input_ptr(j);
  }

  // ── Register external slot pointer ──
  void register_external(SlotId sid, void* ptr) {
    assert(mode_ == ContextMode::COMPILED && "register_external() requires COMPILED mode");
    pool_.register_external(sid, ptr);
  }

  // ── Switch to a different compiled region (mid-iteration safe) ──
  [[nodiscard]] bool switch_region(const RegionNode* alt, uint32_t div_pos)
      CRUCIBLE_NO_THREAD_SAFETY {
    assert(mode_ == ContextMode::COMPILED && "switch_region requires COMPILED mode");
    assert(alt && "null alternate region");
    if (!alt->plan) [[unlikely]]
      return false;

    if (div_pos == 0)
      return activate(alt);

    const auto* old_region = active_region_;
    assert(old_region && old_region->plan && "no active region to switch from");

    auto old_pool = pool_.detach();
    pool_.init(alt->plan);
    migrate_prefix_slots_(old_region, alt, old_pool.base, div_pos);

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
  [[nodiscard]] const RegionNode* active_region() const CRUCIBLE_LIFETIMEBOUND { return active_region_; }

  [[nodiscard]] const ReplayEngine& engine() const CRUCIBLE_LIFETIMEBOUND { return engine_; }
  [[nodiscard]] const PoolAllocator& pool() const CRUCIBLE_LIFETIMEBOUND { return pool_; }

 private:
  void migrate_prefix_slots_(
      const RegionNode* old_region,
      const RegionNode* alt,
      const void* old_pool_base,
      uint32_t div_pos)
      CRUCIBLE_NO_THREAD_SAFETY {
    const auto* old_plan = old_region->plan;
    const auto* new_plan = alt->plan;

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

        if (old_plan->slots[old_sid.raw()].is_external ||
            new_plan->slots[new_sid.raw()].is_external)
          continue;

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

  // ── Layout: engine at offset 0 so its cache line IS our cache line ──
  //
  // Cache line 0 (hot): engine_ (64B = exactly one cache line)
  //   [0..63]  engine_ (expected hashes, cursor_, end_, current_, slot_table_, ops_, pool_)
  //
  // Cache line 1: mode_ + counters + active_region_
  //   [64]     mode_
  //   [65..67] pad
  //   [68..71] compiled_iterations_
  //   [72..75] diverged_count_
  //   [76..79] pad2
  //   [80..87] active_region_
  //
  // Cache line 2: pool_ (32B) + pad to 128B
  //   [88..119] pool_
  //   [120..127] pad
  ReplayEngine engine_;                       // 64B — at offset 0 (one cache line)
  ContextMode mode_ = ContextMode::RECORD;    // 1B  — offset 64
  [[maybe_unused]] uint8_t pad_[3]{};         // 3B  — offset 65
  uint32_t compiled_iterations_ = 0;          // 4B  — offset 68
  uint32_t diverged_count_ = 0;               // 4B  — offset 72
  [[maybe_unused]] uint8_t pad2_[4]{};        // 4B  — offset 76
  const RegionNode* active_region_ = nullptr; // 8B  — offset 80
  PoolAllocator pool_;                        // 32B — offset 88
};

static_assert(sizeof(CrucibleContext) == 120,
              "CrucibleContext: 64 engine + 24 cold + 32 pool = 120");

} // namespace crucible
