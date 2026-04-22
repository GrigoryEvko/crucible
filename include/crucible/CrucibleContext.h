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
//
// Discriminated variant: the two Actions carry fundamentally different
// follow-on state, and the existing "one big struct with union-shaped
// fields" encoded the discrimination via convention (Action::RECORD
// means op_index is meaningless / none()).  Helper accessors now make
// the access-with-wrong-action case a contract fire.
//
// Layout preserved (sizeof == 8) — the helpers don't add storage,
// they gate access through pre() contracts.  The `action` field IS
// the discriminant; `status` and `op_index` are the COMPILED-arm
// payload (RECORD has no meaningful payload).
struct DispatchResult {
  enum class Action : uint8_t {
    RECORD,    // Execute eagerly (normal allocation)
    COMPILED,  // Execute into pre-allocated output pointers
  };

  Action action = Action::RECORD;
  ReplayStatus status = ReplayStatus::MATCH;
  uint8_t pad[2]{};
  OpIndex op_index;  // position in region (diagnostics); none() for RECORD

  // Discriminated accessors.  Accessing the COMPILED payload when
  // action == RECORD is a bug: status and op_index carry no meaning.
  // Routing through these accessors makes the caller prove they
  // checked the action first.
  [[nodiscard, gnu::pure]] bool is_record()   const noexcept { return action == Action::RECORD; }
  [[nodiscard, gnu::pure]] bool is_compiled() const noexcept { return action == Action::COMPILED; }

  // COMPILED-only payload accessors.  pre() fires if the caller
  // reaches for status/op_index without first confirming COMPILED.
  [[nodiscard, gnu::pure]] ReplayStatus compiled_status() const noexcept
      pre (action == Action::COMPILED)
  {
    return status;
  }
  [[nodiscard, gnu::pure]] OpIndex compiled_op_index() const noexcept
      pre (action == Action::COMPILED)
  {
    return op_index;
  }

  // Factories make the legitimate constructions explicit.
  [[nodiscard]] static DispatchResult record() noexcept {
    return DispatchResult{};  // defaults are RECORD / MATCH / none()
  }
  [[nodiscard]] static DispatchResult compiled(
      ReplayStatus s, OpIndex idx) noexcept {
    return DispatchResult{.action = Action::COMPILED,
                          .status = s,
                          .pad    = {},
                          .op_index = idx};
  }
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
  //
  // CRUCIBLE_HOT + gnu::flatten: this function is called per-op in
  // COMPILED mode — same cadence as Vigil::dispatch_op.  gnu::hot
  // promotes it to .text.hot; gnu::flatten tells the compiler to
  // inline every call inside the function body (the engine_.advance
  // call and the counter .bump() calls), eliminating the frame on
  // the hot path.
  [[nodiscard, gnu::flatten]] CRUCIBLE_HOT ReplayStatus
  advance(SchemaHash schema_hash, ShapeHash shape_hash)
      pre (mode_ == ContextMode::COMPILED)
  {
    // Exhaustive switch: a new ReplayStatus variant added to the enum
    // would previously have silently been routed to the DIVERGED branch
    // (the old if/else-if fell through anything that wasn't MATCH or
    // COMPLETE).  Now -Werror=switch fires, forcing an explicit
    // decision on what the new status means for compiled_iterations_ /
    // diverged_count_ bookkeeping.
    switch (engine_.advance(schema_hash, shape_hash)) {
      case ReplayStatus::MATCH:
        return ReplayStatus::MATCH;
      case ReplayStatus::COMPLETE: {
        compiled_iterations_.bump();
        // Auto-reset for next iteration. current_ survives reset
        // so output_ptr/input_ptr remain valid until next advance().
        engine_.reset();
        return ReplayStatus::COMPLETE;
      }
      case ReplayStatus::DIVERGED:
        diverged_count_.bump();
        return ReplayStatus::DIVERGED;
      default:
        std::unreachable();
    }
  }

  // ── Output/input pointer forwarding ──
  //
  // CRUCIBLE_HOT: called once per op-output/op-input after every
  // successful advance().  Binds ATen output tensors to their
  // memory-plan slots.  Not gnu::flatten — the delegate already
  // always_inlines the engine call.
  [[nodiscard]] CRUCIBLE_HOT void* output_ptr(uint16_t j) const CRUCIBLE_LIFETIMEBOUND
      pre (mode_ == ContextMode::COMPILED)
  {
    return engine_.output_ptr(j);
  }

  [[nodiscard]] CRUCIBLE_HOT void* input_ptr(uint16_t j) const CRUCIBLE_LIFETIMEBOUND
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

  [[nodiscard, gnu::flatten]] CRUCIBLE_HOT ReplayStatus
  advance(SchemaHash schema_hash, ShapeHash shape_hash,
          CompiledView const&)
  {
    auto av = engine_.mint_active_view();
    switch (engine_.advance(schema_hash, shape_hash, av)) {
      case ReplayStatus::MATCH:
        return ReplayStatus::MATCH;
      case ReplayStatus::COMPLETE:
        compiled_iterations_.bump();
        engine_.reset(av);
        return ReplayStatus::COMPLETE;
      case ReplayStatus::DIVERGED:
        diverged_count_.bump();
        return ReplayStatus::DIVERGED;
      default:
        std::unreachable();
    }
  }

  [[nodiscard]] CRUCIBLE_HOT void* output_ptr(uint16_t j, CompiledView const&) const
      CRUCIBLE_LIFETIMEBOUND
  {
    // mint_active_view is const; previous const_cast removal was a
    // legacy artefact from a pre-const factory.
    auto av = engine_.mint_active_view();
    return engine_.output_ptr(j, av);
  }

  [[nodiscard]] CRUCIBLE_HOT void* input_ptr(uint16_t j, CompiledView const&) const
      CRUCIBLE_LIFETIMEBOUND
  {
    auto av = engine_.mint_active_view();
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
  // Migration bitset capacity.  Public constant because it is a real
  // structural limit of the migrate_prefix_slots_ algorithm: the
  // visited[] bitset is sized at this bound, and a num_slots > this
  // would silently miss tracking visits and re-copy slots.  The
  // MemoryPlan creation path enforces num_slots <= PoolAllocator's
  // own kMaxNumSlots (1 M) which is much larger; this 1 K migration
  // bound is the tighter local constraint and lifted to a contract
  // pre() on this function.
  static constexpr uint32_t MIGRATION_MAX_SLOTS = 1024;

  void migrate_prefix_slots_(
      const RegionNode* old_region,
      const RegionNode* alt,
      const void* old_pool_base,
      uint32_t div_pos)
      CRUCIBLE_NO_THREAD_SAFETY
      pre (old_region != nullptr)
      pre (alt        != nullptr)
      pre (old_region->plan != nullptr)
      pre (alt->plan        != nullptr)
      // The migration bitset is fixed-size; reject plans the bitset
      // can't track.  Lifted from the legacy runtime assert.
      pre (alt->plan->num_slots <= MIGRATION_MAX_SLOTS)
  {
    const auto* old_plan = old_region->plan;
    const auto* new_plan = alt->plan;
    // Propagate the contract bound to the optimizer so the
    // visited[] indexing math below uses the tight upper bound
    // rather than the field's full uint32_t range.
    [[assume(new_plan->num_slots <= MIGRATION_MAX_SLOTS)]];
    uint64_t visited[(MIGRATION_MAX_SLOTS + 63) / 64]{};

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
