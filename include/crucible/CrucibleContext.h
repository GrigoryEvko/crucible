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
#include <crucible/safety/Mutation.h>
#include <crucible/safety/ScopedView.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>

namespace crucible {

enum class ContextMode : uint8_t {
  RECORD,    // Foreground records + executes eagerly
  COMPILED,  // Foreground replays with pre-allocated outputs
};

// State tag for ScopedView.  COMPILED is the only mode that gates
// methods (advance / output_ptr / input_ptr / register_external all
// pre(mode_ == COMPILED)).  RECORD has no mode-specific methods —
// dispatch_op records via Vigil's own ring, no ctx_ method is RECORD-
// only.  DIVERGED is a transient status returned by advance(), not a
// stable state you hold a view against.
namespace ctx_mode {
    struct Compiled  {};
}

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
      CRUCIBLE_NO_THREAD_SAFETY
      pre (region != nullptr)
  {
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
  advance(SchemaHash schema_hash, ShapeHash shape_hash)
      pre (mode_ == ContextMode::COMPILED)
  {
    auto status = engine_.advance(schema_hash, shape_hash);

    if (status == ReplayStatus::MATCH) [[likely]]
      return ReplayStatus::MATCH;

    if (status == ReplayStatus::COMPLETE) [[unlikely]] {
      compiled_iterations_.bump();
      // Auto-reset for next iteration. current_ survives reset
      // so output_ptr/input_ptr remain valid until next advance().
      engine_.reset();
      return ReplayStatus::COMPLETE;
    }

    // DIVERGED
    diverged_count_.bump();
    return ReplayStatus::DIVERGED;
  }

  // ── Output/input pointer forwarding ──
  [[nodiscard]] CRUCIBLE_INLINE void* output_ptr(uint16_t j) const CRUCIBLE_LIFETIMEBOUND
      pre (mode_ == ContextMode::COMPILED)
  {
    return engine_.output_ptr(j);
  }

  [[nodiscard]] CRUCIBLE_INLINE void* input_ptr(uint16_t j) const CRUCIBLE_LIFETIMEBOUND
      pre (mode_ == ContextMode::COMPILED)
  {
    return engine_.input_ptr(j);
  }

  // ── Register external slot pointer ──
  //
  // Untyped legacy entry point — routes through the typed overload
  // below by minting the CompiledView locally.  pool_'s register_external
  // has no legacy untyped overload any more, so this one-liner is the
  // bridge for callers that haven't been migrated to pass a view.
  void register_external(SlotId sid, crucible::safety::NonNull<void*> ptr)
      pre (mode_ == ContextMode::COMPILED)
  {
    register_external(sid, ptr, mint_compiled_view());
  }

  // ── Switch to a different compiled region (mid-iteration safe) ──
  [[nodiscard]] bool switch_region(const RegionNode* alt, uint32_t div_pos)
      CRUCIBLE_NO_THREAD_SAFETY
      pre (mode_ == ContextMode::COMPILED)
      pre (alt != nullptr)
  {
    if (!alt->plan) [[unlikely]]
      return false;

    if (div_pos == 0)
      return activate(alt);

    const auto* old_region = active_region_;
    assert(old_region && old_region->plan && "no active region to switch from");

    // Pool is currently initialized (we're in COMPILED mode and the
    // active_region_ != nullptr check above confirmed it).  Mint the
    // InitializedView, use the typed detach overload.  After detach
    // the pool is empty; pv is now semantically stale, but its
    // destructor fires at scope exit before any further pool use.
    auto pv = pool_.mint_initialized_view();
    auto old_pool = pool_.detach(pv);
    pool_.init(alt->plan);
    migrate_prefix_slots_(old_region, alt, old_pool.base, div_pos);

    engine_.init(alt, &pool_);
    active_region_ = alt;

    // Engine just init'd → mint an ActiveView and use the typed advance
    // overload so the prefix-replay matches the rest of the codebase
    // (Vigil dispatch_op and our own typed advance).
    auto av = engine_.mint_active_view();
    for (uint32_t i = 0; i < div_pos; i++) {
      auto s = engine_.advance(alt->ops[i].schema_hash,
                               alt->ops[i].shape_hash, av);
      assert(s == ReplayStatus::MATCH || s == ReplayStatus::COMPLETE);
      (void)s;
    }

    return true;
  }

  // ── Queries ──

  [[nodiscard]] ContextMode mode() const { return mode_; }
  [[nodiscard]] bool is_compiled() const { return mode_ == ContextMode::COMPILED; }
  [[nodiscard]] bool is_recording() const { return mode_ == ContextMode::RECORD; }

  // ── ScopedView predicates + factories ────────────────────────────
  //
  // Mode-specific methods accept a CompiledView proof that the ctx is
  // in COMPILED; the existing pre()-checked overloads are retained for
  // gradual migration.

  using CompiledView  = crucible::safety::ScopedView<CrucibleContext,
                                                      ctx_mode::Compiled>;

  [[nodiscard]] friend constexpr bool view_ok(
      CrucibleContext const& c, std::type_identity<ctx_mode::Compiled>) noexcept {
    return c.mode_ == ContextMode::COMPILED;
  }

  [[nodiscard]] CRUCIBLE_INLINE CompiledView mint_compiled_view() const noexcept
      pre (mode_ == ContextMode::COMPILED)
  {
    return crucible::safety::mint_view<ctx_mode::Compiled>(*this);
  }

  // ── Typed mode-specific overloads ────────────────────────────────

  // The typed overloads thread proofs all the way down: a CompiledView
  // on the context implies the engine is initialized AND the pool is
  // initialized (activate() inits both synchronously).  Mint the inner
  // ActiveView / InitializedView from the outer CompiledView and pass
  // them to the engine + pool typed methods, so the entire dispatch
  // chain is type-state-checked end-to-end.

  [[nodiscard]] CRUCIBLE_INLINE ReplayStatus
  advance(SchemaHash schema_hash, ShapeHash shape_hash,
          CompiledView const&)
  {
    auto av = engine_.mint_active_view();
    auto status = engine_.advance(schema_hash, shape_hash, av);
    if (status == ReplayStatus::MATCH) [[likely]]
      return ReplayStatus::MATCH;
    if (status == ReplayStatus::COMPLETE) [[unlikely]] {
      compiled_iterations_.bump();
      engine_.reset(av);
      return ReplayStatus::COMPLETE;
    }
    diverged_count_.bump();
    return ReplayStatus::DIVERGED;
  }

  [[nodiscard]] CRUCIBLE_INLINE void* output_ptr(uint16_t j, CompiledView const&) const
      CRUCIBLE_LIFETIMEBOUND
  {
    auto av = const_cast<ReplayEngine&>(engine_).mint_active_view();
    return engine_.output_ptr(j, av);
  }

  [[nodiscard]] CRUCIBLE_INLINE void* input_ptr(uint16_t j, CompiledView const&) const
      CRUCIBLE_LIFETIMEBOUND
  {
    auto av = const_cast<ReplayEngine&>(engine_).mint_active_view();
    return engine_.input_ptr(j, av);
  }

  CRUCIBLE_INLINE void register_external(SlotId sid,
                                          crucible::safety::NonNull<void*> ptr,
                                          CompiledView const&)
  {
    auto pv = pool_.mint_initialized_view();
    pool_.register_external(sid, ptr, pv);
  }

  [[nodiscard]] uint32_t compiled_iterations() const { return compiled_iterations_.get(); }
  [[nodiscard]] uint32_t diverged_count() const { return diverged_count_.get(); }
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
  // Both counters are structurally monotonic: incremented on iteration
  // boundaries / divergence events, never reset.  Wrapped so the
  // invariant is type-enforced (overflow is the only way to violate it
  // and bump() catches that via contract).
  crucible::safety::Monotonic<uint32_t> compiled_iterations_ {0}; // 4B  — offset 68
  crucible::safety::Monotonic<uint32_t> diverged_count_      {0}; // 4B  — offset 72
  [[maybe_unused]] uint8_t pad2_[4]{};        // 4B  — offset 76
  const RegionNode* active_region_ = nullptr; // 8B  — offset 80
  PoolAllocator pool_;                        // 32B — offset 88
};

static_assert(sizeof(CrucibleContext) == 120,
              "CrucibleContext: 64 engine + 24 cold + 32 pool = 120");

// Tier 2 opt-in: CrucibleContext must not hold a ScopedView field —
// views mustn't outlive the stack frame that minted them.
static_assert(crucible::safety::no_scoped_view_field_check<CrucibleContext>());

} // namespace crucible
