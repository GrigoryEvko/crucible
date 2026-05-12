#pragma once

// ═══════════════════════════════════════════════════════════════════
// RecipePool — intern NumericalRecipe instances by structural equality.
//
// Per FORGE.md §19.2, every IR002 KernelNode eventually references a
// NumericalRecipe via pointer, not by value.  Two kernels with the
// same semantic fields must share the same `const NumericalRecipe*`
// so that:
//
//   - Kernel-level CSE (FORGE.md §E.5, §18.6) dedupes via recipe
//     pointer equality
//   - KernelContentHash (FORGE.md §18.6) composes the recipe pointer
//     directly instead of hashing fields again
//   - L1 federation cache keys (FORGE.md §23.2) benefit from the
//     canonical-pointer identity
//
// RecipePool is the interning substrate.  One per process, owned by
// a caller-provided Arena (same ownership pattern as ExprPool and
// RecipeRegistry's internal store).
//
// ─── Safety ─────────────────────────────────────────────────────────
//
//   InitSafe    — all fields NSDMI'd; slots_ allocated at construction
//   TypeSafe    — stores RecipeHash + const NumericalRecipe*; never
//                 returns bare uint64_t
//   NullSafe    — intern() returns a guaranteed-non-null pointer into
//                 the arena; gnu::returns_nonnull exposes the proof
//   MemSafe     — arena-owned; copy/move deleted with reasons;
//                 recipes are immutable post-intern
//   BorrowSafe  — single-threaded by convention (CRUCIBLE_NO_THREAD_SAFETY);
//                 no locking on the intern path.  Registry uses this
//                 at startup only; compile paths will wrap in
//                 Raft-coordinated init per FORGE.md §20.3.
//   ThreadSafe  — N/A single-threaded.  Document the contract; enforce
//                 via debug-only producer-thread check (future work).
//   LeakSafe    — Arena bulk-frees on destruction
//   DetSafe     — intern identity is deterministic given the sequence
//                 of intern() calls; hash is platform-independent;
//                 rehash is order-preserving
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/Platform.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Post.h>
#include <crucible/safety/Pre.h>
#include <crucible/safety/Refined.h>

#include <bit>
#include <cstddef>
#include <cstdint>

namespace crucible {

class CRUCIBLE_OWNER RecipePool {
 public:
  using Capacity = safety::PowerOfTwo<uint32_t>;
  using Size     = safety::Monotonic<uint32_t>;

  // Initial slot capacity.  Must be a power of two and ≥ 8.  Load
  // factor is capped at 50%, so `initial_capacity` accommodates
  // `initial_capacity / 2` distinct recipes before the first resize.
  //
  // Defaults to 32 — covers the 5-15 recipes a typical ML model
  // pins (FORGE.md §19.2) with no growth.  Registries bootstrapping
  // with ~8 starter recipes fit comfortably.
  [[gnu::cold]] explicit RecipePool(Arena& arena CRUCIBLE_LIFETIMEBOUND,
                                    effects::Alloc a,
                                    uint32_t initial_capacity = 32)
      noexcept
      // CONTRACT-109: pow2 invariant discharges through the named
      // predicate `crucible::decide::is_power_of_two_le` (CONTRACT-050
      // catalog) — checks `initial_capacity` is a power of two AND
      // `≤ UINT32_MAX` (the latter trivially true for uint32_t but
      // pinned via the same predicate for cite-discipline consistency
      // with future width-parametric pool variants).  The `>= 8`
      // lower-bound stays as a separate pre because it's an
      // application-level minimum (load-factor sanity) not a
      // structural invariant the catalog covers.  Both pre clauses
      // are pure-parameter — no class member access — so P2900 pre()
      // is sufficient (no consteval-bypass exposure).
      pre (initial_capacity >= 8)
      pre (::crucible::decide::is_power_of_two_le<std::uint32_t>(
          initial_capacity, UINT32_MAX))
      : arena_{&arena}
      , capacity_{initial_capacity}
      , size_{0}
  {
    slots_ = arena.alloc_array_nonzero<Slot>(a, initial_capacity);
    for (uint32_t i = 0; i < initial_capacity; ++i) {
      slots_[i] = Slot{};  // NSDMI: recipe=nullptr
    }
    // CONTRACT-109-POST: construction-state invariant — after the
    // ctor runs, the three load-bearing fields agree:
    //   (1) capacity_ matches the requested initial_capacity (set
    //       via member-init list above; post catches a future
    //       refactor that rounds up internally — ExprPool does this
    //       and would need a different shape).
    //   (2) slots_ is non-null (alloc_array_nonzero guarantees this
    //       for a non-zero count, and pre asserts initial_capacity
    //       >= 8).
    //   (3) size_ is zero (NSDMI in the field declaration; post
    //       catches a future refactor that pre-populates entries).
    // Routes through CRUCIBLE_POST because the predicates reference
    // class members through `this->`; P2900 `post (r:...)` is
    // consteval-bypass-vulnerable per the GCC 16.1.1 family.  Void
    // return: first arg `0` is the conventional sentinel.  Under
    // NDEBUG these collapse to `[[assume]]` for downstream intern()
    // optimizer.
    CRUCIBLE_POST(0, capacity_.value() == initial_capacity);
    CRUCIBLE_POST(0, slots_ != nullptr);
    CRUCIBLE_POST(0, size_.get() == 0);
  }

  // Interior pointers into arena_ would dangle if this pool moved.
  RecipePool(const RecipePool&)            = delete("RecipePool owns interior pointers into arena_");
  RecipePool& operator=(const RecipePool&) = delete("RecipePool owns interior pointers into arena_");
  RecipePool(RecipePool&&)                 = delete("interior pointers would dangle");
  RecipePool& operator=(RecipePool&&)      = delete("interior pointers would dangle");

  // Intern a recipe.  If a recipe with identical SEMANTIC fields
  // (everything except `hash`) is already in the pool, returns the
  // existing canonical pointer.  Otherwise allocates a new
  // NumericalRecipe in the arena, populates its `hash` field via
  // compute_recipe_hash (the input's hash is ignored — pool is the
  // authority), inserts into the probing table, returns the new
  // pointer.
  //
  // Pointer identity semantics:
  //   pool.intern(a) == pool.intern(b)
  //     iff
  //   a and b have identical semantic fields (modulo `hash`).
  //
  // Cost: ~tens of ns on hit (hash + linear probe + pointer load).
  // ~100 ns on miss (hash + probe + arena alloc + field copy).
  //
  // Non-null return: the pool never returns nullptr.  Arena OOM
  // aborts via std::abort per the Crucible OOM-is-unrecoverable
  // discipline; table-full triggers grow.
  [[nodiscard, gnu::returns_nonnull]] const NumericalRecipe* intern(
      effects::Alloc a, const NumericalRecipe& fields)
      CRUCIBLE_LIFETIMEBOUND
      CRUCIBLE_NO_THREAD_SAFETY
  {
    const RecipeHash h = compute_recipe_hash(fields);
    const uint64_t hv = h.raw();

    // Probe existing slots for a match.
    const uint32_t cap = capacity_.value();
    const uint32_t mask = cap - 1;
    uint32_t idx = static_cast<uint32_t>(hv) & mask;
    for (uint32_t probe = 0; probe < cap; ++probe) {
      const uint32_t i = (idx + probe) & mask;
      Slot& s = slots_[i];
      if (s.recipe == nullptr) {
        // Empty slot — grow if we'd exceed 50% load after insert,
        // then restart.  Otherwise install here.
        if ((size_.get() + 1) * 2 > cap) [[unlikely]] {
          grow_(a);
          return intern(a, fields);  // restart probing in the new table
        }
        return install_(a, i, fields, h);
      }
      if (s.hash == hv && semantic_equal_(*s.recipe, fields)) {
        return s.recipe;  // hit
      }
    }
    // Unreachable under the 50%-load invariant: probe eventually
    // finds an empty slot or exceeds the grow threshold.
    std::abort();
  }

  // ─── Diagnostics ─────────────────────────────────────────────────

  [[nodiscard, gnu::pure]] uint32_t size() const noexcept { return size_.get(); }
  [[nodiscard, gnu::pure]] uint32_t capacity() const noexcept { return capacity_.value(); }

 private:
  struct Slot {
    uint64_t hash = 0;   // only meaningful if recipe != nullptr
    const NumericalRecipe* recipe = nullptr;  // nullptr = empty
  };

  [[nodiscard, gnu::pure]] static constexpr bool semantic_equal_(
      const NumericalRecipe& a, const NumericalRecipe& b) noexcept
  {
    // Compare every semantic field.  NOT the `hash` field — pool
    // authority means the stored `hash` reflects the semantic fields
    // by construction, and the input's `hash` is ignored.
    return a.accum_dtype    == b.accum_dtype
        && a.out_dtype      == b.out_dtype
        && a.reduction_algo == b.reduction_algo
        && a.rounding       == b.rounding
        && a.scale_policy   == b.scale_policy
        && a.softmax        == b.softmax
        && a.determinism    == b.determinism
        && a.flags          == b.flags;
  }

  // Install a fresh recipe at slot index `i`.  Allocates from arena,
  // copies the fields, writes the authoritative hash, registers.
  [[nodiscard, gnu::returns_nonnull]] const NumericalRecipe* install_(
      effects::Alloc a, uint32_t i,
      const NumericalRecipe& fields, RecipeHash h)
  {
    // alloc_obj returns uninitialized storage; placement-new with
    // fields + authoritative hash.  NumericalRecipe is an aggregate,
    // so this zero-initializes any future padding and sets every
    // field explicitly.
    NumericalRecipe* r = arena_->alloc_obj<NumericalRecipe>(a);
    *r = fields;
    r->hash = h;

    slots_[i] = Slot{.hash = h.raw(), .recipe = r};
    size_.bump();
    return r;
  }

  // Double the table capacity, rehash every existing entry.
  // Called when the 50% load threshold would be exceeded.
  [[gnu::cold, gnu::noinline]]
  void grow_(effects::Alloc a) {
    const uint32_t old_cap = capacity_.value();
    const uint32_t old_size = size_.get();
    Slot* old_slots = slots_;
    const uint32_t new_cap = old_cap * 2;

    slots_ = arena_->alloc_array_nonzero<Slot>(a, new_cap);
    for (uint32_t i = 0; i < new_cap; ++i) {
      slots_[i] = Slot{};
    }
    capacity_ = Capacity{new_cap};

    const uint32_t new_mask = new_cap - 1;
    uint32_t reinserted = 0;
    for (uint32_t i = 0; i < old_cap; ++i) {
      Slot& s = old_slots[i];
      if (s.recipe == nullptr) continue;
      uint32_t idx = static_cast<uint32_t>(s.hash) & new_mask;
      for (uint32_t probe = 0; probe < new_cap; ++probe) {
        const uint32_t j = (idx + probe) & new_mask;
        if (slots_[j].recipe == nullptr) {
          slots_[j] = s;
          ++reinserted;
          break;
        }
      }
    }
    CRUCIBLE_POST(0, reinserted == old_size);
    size_.advance(reinserted);
    // Old slots remain in the arena until arena destruction — small
    // one-time leak of (old_cap) slot bytes, acceptable at the rate
    // of ~log2(N) resizes per pool lifetime.
  }

  Arena*   arena_;
  Slot*    slots_;
  Capacity capacity_;
  Size     size_;
};

} // namespace crucible
