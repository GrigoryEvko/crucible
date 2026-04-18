#pragma once

// ReplayEngine: Walks a compiled RegionNode one op at a time,
// verifying guards and providing pre-allocated output pointers.
//
// Cursor-based design: a sliding pointer (cursor_) advances through
// the ops array. Every advance is a single pointer increment (+96B),
// replacing the old index*96 multiply (lea+shl+lea = 3 dependent
// instructions). Pre-cached expected hashes at offsets 0 and 8
// give the guard comparison zero pointer-chase latency.
//
// Hot path assembly (MATCH, non-last op):
//
//     cmp  rsi, [rdi + 0]      ; schema_hash vs expected_schema_
//     jne  .diverged
//     cmp  rdx, [rdi + 8]      ; shape_hash vs expected_shape_
//     jne  .diverged
//     mov  rax, [rdi + 16]     ; load cursor_
//     mov  [rdi + 32], rax     ; store current_ (for output_ptr)
//     prefetcht0 [rax + 64]    ; slot IDs for output_ptr()
//     add  rax, 96             ; ++cursor_
//     cmp  rax, [rdi + 24]     ; cursor_ == end_?
//     je   .complete
//     mov  rcx, [rax]          ; pre-cache next schema
//     mov  [rdi], rcx
//     mov  rcx, [rax + 8]      ; pre-cache next shape
//     mov  [rdi + 8], rcx
//     mov  [rdi + 16], rax     ; store cursor_
//     xor  eax, eax            ; return MATCH
//     ret

#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>
#include <crucible/PoolAllocator.h>

#include <cassert>
#include <cstdint>

namespace crucible {

enum class ReplayStatus : uint8_t {
  MATCH,     // Op matches compiled trace, outputs ready in pool
  DIVERGED,  // Guard failed -- schema or shape mismatch
  COMPLETE,  // All ops in region consumed (iteration done)
};

struct ReplayEngine {
  ReplayEngine() = default;

  ReplayEngine(const ReplayEngine&) = delete("ReplayEngine tracks mutable cursor position");
  ReplayEngine& operator=(const ReplayEngine&) = delete("ReplayEngine tracks mutable cursor position");
  ReplayEngine(ReplayEngine&&) = delete("non-owning pointers would alias with moved-from cursor");
  ReplayEngine& operator=(ReplayEngine&&) = delete("non-owning pointers would alias with moved-from cursor");

  // ── Init: bind to a compiled region + pool ──
  void init(const RegionNode* region, const PoolAllocator* pool)
      CRUCIBLE_NO_THREAD_SAFETY {
    assert(region && "null RegionNode");
    assert(pool && pool->is_initialized() && "pool not initialized");
    ops_ = region->ops;
    end_ = region->ops + region->num_ops;
    cursor_ = ops_;
    current_ = nullptr;
    slot_table_ = pool->table();
    pool_ = pool;
    // Prime the cache: load first entry's guard values.
    if (ops_ != end_) [[likely]] {
      expected_schema_ = ops_[0].schema_hash;
      expected_shape_ = ops_[0].shape_hash;
    } else {
      expected_schema_ = SchemaHash{};
      expected_shape_ = ShapeHash{};
    }
  }

  // ── Reset: rewind cursor to first op for the next iteration ──
  //
  // current_ is NOT cleared: it still points to the last matched
  // entry so that output_ptr()/input_ptr() remain valid after
  // CrucibleContext returns COMPLETE and internally resets.
  void reset() {
    cursor_ = ops_;
    if (ops_ != end_) [[likely]] {
      expected_schema_ = ops_[0].schema_hash;
      expected_shape_ = ops_[0].shape_hash;
    }
  }

  // ── Hot path: advance one op ──
  //
  // Two L1d comparisons at fixed struct offsets (0, 8). No pointer chase.
  // On match: stores current_, increments cursor_, pre-caches NEXT entry.
  // Returns COMPLETE for the last matched op directly.
  //
  // Caller must reset() after COMPLETE before calling advance() again.
  [[nodiscard]] CRUCIBLE_INLINE ReplayStatus
  advance(SchemaHash schema_hash, ShapeHash shape_hash)
#if CRUCIBLE_HAS_CONTRACTS
      pre (cursor_ < end_)
#endif
  {

    // Guard 1: op identity. L1d load at offset 0.
    if (schema_hash != expected_schema_) [[unlikely]]
      return ReplayStatus::DIVERGED;

    // Guard 2: tensor geometry. L1d load at offset 8 (same cache line).
    if (shape_hash != expected_shape_) [[unlikely]]
      return ReplayStatus::DIVERGED;

    // ── Match confirmed ──
    current_ = cursor_;

    // Prefetch current entry's second cache line (output_slot_ids at
    // offset 88). The caller will almost certainly call output_ptr().
    __builtin_prefetch(reinterpret_cast<const char*>(cursor_) + 64, 0, 3);

    // Advance cursor to next entry. Single add (vs old lea+shl+lea).
    ++cursor_;

    if (cursor_ == end_) [[unlikely]]
      return ReplayStatus::COMPLETE;

    // Pre-cache NEXT entry's guard hashes for the next advance() call.
    // This load may miss L1d for large regions (>300 ops), but it's
    // after the comparison — pipelined with the caller's output_ptr().
    expected_schema_ = cursor_->schema_hash;
    expected_shape_ = cursor_->shape_hash;
    return ReplayStatus::MATCH;
  }

  // ── Output pointer for output j of the last matched op ──
  //
  // current_ points to the matched entry (set during advance, survives
  // reset). The prefetch during advance() brought the second cache line
  // (containing output_slot_ids) into L1d.
  [[nodiscard]] CRUCIBLE_INLINE void* output_ptr(uint16_t j) const CRUCIBLE_LIFETIMEBOUND
#if CRUCIBLE_HAS_CONTRACTS
      pre (current_ != nullptr)
      pre (j < current_->num_outputs)
      pre (current_->output_slot_ids != nullptr)
#endif
  {
    SlotId sid = current_->output_slot_ids[j];
    return sid.is_valid() ? slot_table_[sid.raw()] : nullptr;
  }

  // ── Input pointer for input j of the last matched op ──
  [[nodiscard]] CRUCIBLE_INLINE void* input_ptr(uint16_t j) const CRUCIBLE_LIFETIMEBOUND
#if CRUCIBLE_HAS_CONTRACTS
      pre (current_ != nullptr)
      pre (j < current_->num_inputs)
      pre (current_->input_slot_ids != nullptr)
#endif
  {
    SlotId sid = current_->input_slot_ids[j];
    return sid.is_valid() ? slot_table_[sid.raw()] : nullptr;
  }

  // ── Queries ──

  [[nodiscard]] const TraceEntry& current_entry() const CRUCIBLE_LIFETIMEBOUND {
    assert(current_ && "no matched entry");
    return *current_;
  }

  [[nodiscard]] OpIndex matched_op_index() const {
    assert(current_ && "no matched entry");
    return OpIndex{static_cast<uint32_t>(current_ - ops_)};
  }

  [[nodiscard]] OpIndex diverged_op_index() const {
    return OpIndex{static_cast<uint32_t>(cursor_ - ops_)};
  }

  [[nodiscard]] uint32_t ops_matched() const {
    return static_cast<uint32_t>(cursor_ - ops_);
  }

  [[nodiscard]] uint32_t num_ops() const {
    return static_cast<uint32_t>(end_ - ops_);
  }

  [[nodiscard]] bool is_complete() const { return cursor_ == end_; }
  [[nodiscard]] bool is_initialized() const { return ops_ != nullptr; }

 private:
  // ── Layout: all fields in ONE 64-byte cache line ──
  //
  // Offsets 0-7:   expected_schema_  (advance reads first)
  // Offsets 8-15:  expected_shape_   (advance reads second)
  // Offsets 16-23: cursor_           (advance: read + increment + write)
  // Offsets 24-31: end_              (advance: completion check)
  // Offsets 32-39: current_          (advance: write; output_ptr: read)
  // Offsets 40-47: slot_table_       (output_ptr: index into)
  // Offsets 48-55: ops_              (reset + diagnostics)
  // Offsets 56-63: pool_             (diagnostics only)
  //
  // Total: 64 bytes = exactly one cache line.
  //
  // vs old index-based layout: eliminated op_index_ (imul for *96),
  // num_ops_ (use end_ pointer), and the lea+shl+lea addressing chain.
  // Kept current_ for output_ptr survival across reset().
  SchemaHash expected_schema_{};                // 8B — pre-cached guard value
  ShapeHash expected_shape_{};                  // 8B — pre-cached guard value
  const TraceEntry* cursor_ = nullptr;          // 8B — current position in ops
  const TraceEntry* end_ = nullptr;             // 8B — one past last op
  const TraceEntry* current_ = nullptr;         // 8B — last matched entry
  void* const* slot_table_ = nullptr;           // 8B — pool slot pointer table
  const TraceEntry* ops_ = nullptr;             // 8B — base for reset + diagnostics
  const PoolAllocator* pool_ = nullptr;         // 8B — kept for diagnostics
};

static_assert(sizeof(ReplayEngine) == 64, "ReplayEngine: 8 × 8B = 64 bytes (one cache line)");

} // namespace crucible
