// FOUND-G42 — PoolAllocator AllocClass-pinned production surface.
//
// Verifies the three pinned variants added to PoolAllocator:
//   - slot_ptr_pinned    → AllocClass<Pool, void*>      (hot-path slot)
//   - pool_base_pinned   → AllocClass<Pool, void*>      (safe pinning)
//   - pool_base_huge_pinned → AllocClass<HugePage, void*>  (≥2MB pre)
//
// Layered against the existing test_pool_allocator suite — the raw
// surface (slot_ptr, pool_base) is exercised there; this file proves
// the additive `_pinned` overlay preserves the hot-path semantics
// while pinning allocator tier at the type level.

#include <crucible/PoolAllocator.h>
#include <crucible/safety/AllocClass.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/rt/Registry.h>
#include "test_assert.h"

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

using crucible::PoolAllocator;
using crucible::MemoryPlan;
using crucible::TensorSlot;
using crucible::SlotId;
using crucible::OpIndex;
using crucible::ScalarType;
using crucible::DeviceType;
using crucible::Layout;
using crucible::safety::AllocClass;
using crucible::safety::AllocClassTag_v;

// ── Plan factory shared across tests ─────────────────────────────
static MemoryPlan make_plan(TensorSlot* slots, uint32_t n,
                            uint64_t pool_bytes, uint32_t num_ext) {
  MemoryPlan plan{};
  plan.slots = slots;
  plan.num_slots = n;
  plan.num_external = num_ext;
  plan.pool_bytes = pool_bytes;
  plan.device_type = DeviceType::CPU;
  plan.device_idx = 0;
  return plan;
}

static TensorSlot make_slot(uint64_t off, uint64_t nbytes, uint32_t id,
                            bool external = false) {
  TensorSlot s{};
  s.offset_bytes = off;
  s.nbytes = nbytes;
  s.birth_op = OpIndex{0};
  s.death_op = OpIndex{1};
  s.dtype = ScalarType::Float;
  s.device_type = DeviceType::CPU;
  s.device_idx = 0;
  s.layout = Layout::Strided;
  s.is_external = external;
  s.slot_id = SlotId{id};
  return s;
}

// ── T01 — slot_ptr_pinned returns AllocClass<Pool, void*> at the
//         exact same address as the raw slot_ptr ─────────────────
static void test_slot_ptr_pinned_bit_equality() {
  TensorSlot slots[2]{};
  slots[0] = make_slot(0, 1024, 0);
  slots[1] = make_slot(1024, 1024, 1);
  MemoryPlan plan = make_plan(slots, 2, 2048, 0);

  PoolAllocator pool;
  pool.init(&plan);
  auto pv = pool.mint_initialized_view();

  void* raw_0 = pool.slot_ptr(SlotId{0}, pv);
  void* raw_1 = pool.slot_ptr(SlotId{1}, pv);

  auto pinned_0 = pool.slot_ptr_pinned(SlotId{0}, pv);
  auto pinned_1 = pool.slot_ptr_pinned(SlotId{1}, pv);

  // The wrapper preserves the underlying address bit-for-bit.
  assert(std::move(pinned_0).consume() == raw_0);
  assert(std::move(pinned_1).consume() == raw_1);

  pool.destroy();
}

// ── T02 — type-identity for slot_ptr_pinned ───────────────────────
static void test_slot_ptr_pinned_type_identity() {
  TensorSlot slots[1]{};
  slots[0] = make_slot(0, 256, 0);
  MemoryPlan plan = make_plan(slots, 1, 256, 0);

  PoolAllocator pool;
  pool.init(&plan);
  auto pv = pool.mint_initialized_view();

  using Got = decltype(pool.slot_ptr_pinned(SlotId{0}, pv));
  using Want = AllocClass<AllocClassTag_v::Pool, void*>;
  static_assert(std::is_same_v<Got, Want>,
      "slot_ptr_pinned must return AllocClass<Pool, void*>");
  static_assert(Got::tag == AllocClassTag_v::Pool);

  pool.destroy();
}

// ── T03 — fence-acceptance simulation: only Pool-or-weaker
//         consumers admit a slot pointer ──────────────────────────
template <typename W>
concept admissible_at_pool_fence =
    W::tag == AllocClassTag_v::Pool        ||
    W::tag == AllocClassTag_v::Arena       ||
    W::tag == AllocClassTag_v::Heap        ||
    W::tag == AllocClassTag_v::Mmap        ||
    W::tag == AllocClassTag_v::HugePage;

template <typename W>
concept admissible_at_huge_fence =
    W::tag == AllocClassTag_v::HugePage;

static void test_pool_fence_simulation() {
  // Pool slot is admissible at the Pool fence; it is NOT admissible
  // at the strictest Huge fence (HugePage tier is at the bottom of
  // the lattice — only HugePage values pass through there).
  using Slot = AllocClass<AllocClassTag_v::Pool, void*>;
  static_assert(admissible_at_pool_fence<Slot>);
  static_assert(!admissible_at_huge_fence<Slot>);

  // A HugePage value (from pool_base_huge_pinned) IS admissible at
  // the Huge fence.
  using Huge = AllocClass<AllocClassTag_v::HugePage, void*>;
  static_assert(admissible_at_huge_fence<Huge>);
}

// ── T04 — negative witnesses: Stack value won't compile-time match
//         a Pool slot; HugePage won't match an Arena slot ─────────
static void test_negative_tier_witnesses() {
  using PoolSlot   = AllocClass<AllocClassTag_v::Pool,     void*>;
  using StackSlot  = AllocClass<AllocClassTag_v::Stack,    void*>;
  using ArenaSlot  = AllocClass<AllocClassTag_v::Arena,    void*>;
  using HeapSlot   = AllocClass<AllocClassTag_v::Heap,     void*>;
  using MmapSlot   = AllocClass<AllocClassTag_v::Mmap,     void*>;
  using HugeSlot   = AllocClass<AllocClassTag_v::HugePage, void*>;

  // satisfies<Required>: producer (Self) must be at-or-stronger
  // than consumer's required tier (Required).  Lattice direction:
  // HugePage(weakest) ⊑ Mmap ⊑ Heap ⊑ Arena ⊑ Pool ⊑ Stack(strongest).

  // PoolSlot satisfies Pool, Arena, Heap, Mmap, HugePage (weaker tiers).
  static_assert( PoolSlot::satisfies<AllocClassTag_v::Pool>);
  static_assert( PoolSlot::satisfies<AllocClassTag_v::Arena>);
  static_assert( PoolSlot::satisfies<AllocClassTag_v::Heap>);
  static_assert( PoolSlot::satisfies<AllocClassTag_v::Mmap>);
  static_assert( PoolSlot::satisfies<AllocClassTag_v::HugePage>);
  // PoolSlot does NOT satisfy Stack (stronger tier).
  static_assert(!PoolSlot::satisfies<AllocClassTag_v::Stack>);

  // HugeSlot satisfies only HugePage; weaker than every other tier.
  static_assert( HugeSlot::satisfies<AllocClassTag_v::HugePage>);
  static_assert(!HugeSlot::satisfies<AllocClassTag_v::Mmap>);
  static_assert(!HugeSlot::satisfies<AllocClassTag_v::Heap>);
  static_assert(!HugeSlot::satisfies<AllocClassTag_v::Arena>);
  static_assert(!HugeSlot::satisfies<AllocClassTag_v::Pool>);
  static_assert(!HugeSlot::satisfies<AllocClassTag_v::Stack>);

  // StackSlot satisfies every tier (top of lattice).
  static_assert( StackSlot::satisfies<AllocClassTag_v::Stack>);
  static_assert( StackSlot::satisfies<AllocClassTag_v::Pool>);
  static_assert( StackSlot::satisfies<AllocClassTag_v::Arena>);
  static_assert( StackSlot::satisfies<AllocClassTag_v::Heap>);
  static_assert( StackSlot::satisfies<AllocClassTag_v::Mmap>);
  static_assert( StackSlot::satisfies<AllocClassTag_v::HugePage>);

  // Suppress unused-typedef warnings.
  (void)sizeof(ArenaSlot);
  (void)sizeof(HeapSlot);
  (void)sizeof(MmapSlot);
}

// ── T05 — layout invariant: pinned wrapper is zero-cost ──────────
static void test_layout_invariant() {
  static_assert(sizeof(AllocClass<AllocClassTag_v::Pool, void*>) ==
                sizeof(void*));
  static_assert(sizeof(AllocClass<AllocClassTag_v::HugePage, void*>) ==
                sizeof(void*));
}

// ── T06 — pool_base_pinned at small pool (page_align=ALIGNMENT) ──
static void test_pool_base_pinned_small_pool() {
  TensorSlot slots[1]{};
  slots[0] = make_slot(0, 1024, 0);
  MemoryPlan plan = make_plan(slots, 1, 1024, 0);

  PoolAllocator pool;
  pool.init(&plan);

  auto pinned = pool.pool_base_pinned();
  static_assert(std::is_same_v<decltype(pinned),
      AllocClass<AllocClassTag_v::Pool, void*>>);

  // The wrapped pointer matches pool_base() exactly.
  void* raw = pool.pool_base();
  assert(std::move(pinned).consume() == raw);
  assert(raw != nullptr);

  // 256B (PoolAllocator::ALIGNMENT) at minimum.
  assert(reinterpret_cast<uintptr_t>(raw) % PoolAllocator::ALIGNMENT == 0);

  pool.destroy();
}

// ── T07 — pool_base_huge_pinned at ≥2MB pool ─────────────────────
static void test_pool_base_huge_pinned() {
  // Allocate exactly kHugePageBytes (2MB) — triggers huge-page-
  // aligned allocation in PoolAllocator::init.
  constexpr uint64_t HP = crucible::rt::kHugePageBytes;
  TensorSlot slots[1]{};
  slots[0] = make_slot(0, HP / 2, 0);  // half the pool, well under HP
  MemoryPlan plan = make_plan(slots, 1, HP, 0);

  PoolAllocator pool;
  pool.init(&plan);

  // The pre() requires pool_bytes ≥ kHugePageBytes — verified by
  // constructing the AllocClass<HugePage, void*>.
  auto pinned = pool.pool_base_huge_pinned();
  static_assert(std::is_same_v<decltype(pinned),
      AllocClass<AllocClassTag_v::HugePage, void*>>);

  void* raw = pool.pool_base();
  void* wrapped = std::move(pinned).consume();
  assert(wrapped == raw);
  assert(raw != nullptr);
  // 2MB-aligned (HugePage promise).
  assert(reinterpret_cast<uintptr_t>(raw) % HP == 0);

  pool.destroy();
}

// ── T08 — relax DOWN-the-lattice (admissible) — Pool → Heap ──────
static void test_pool_relax_to_weaker() {
  TensorSlot slots[1]{};
  slots[0] = make_slot(0, 256, 0);
  MemoryPlan plan = make_plan(slots, 1, 256, 0);

  PoolAllocator pool;
  pool.init(&plan);
  auto pv = pool.mint_initialized_view();

  auto pinned = pool.slot_ptr_pinned(SlotId{0}, pv);
  // Going down the lattice (Pool → Heap) is admissible.  Going UP
  // (Pool → Stack) is forbidden — the negative test proving that
  // direction lives in test/safety_neg/neg_alloc_class_relax_to_stronger.cpp.
  auto relaxed = std::move(pinned).relax<AllocClassTag_v::Heap>();
  static_assert(std::is_same_v<decltype(relaxed),
      AllocClass<AllocClassTag_v::Heap, void*>>);

  pool.destroy();
}

// ── T09 — type-level chain composition through three tiers ───────
//         Verifies satisfies<> behaves transitively in production
//         use: a slot_ptr_pinned result can flow into any consumer
//         declaring requires Pool / Arena / Heap / Mmap / HugePage.
static void test_chain_composition() {
  using P = AllocClass<AllocClassTag_v::Pool, void*>;
  // Pool ⊐ Arena ⊐ Heap ⊐ Mmap ⊐ HugePage.  Pool satisfies all.
  static_assert(P::satisfies<AllocClassTag_v::Pool>);
  static_assert(P::satisfies<AllocClassTag_v::Arena>);
  static_assert(P::satisfies<AllocClassTag_v::Heap>);
  static_assert(P::satisfies<AllocClassTag_v::Mmap>);
  static_assert(P::satisfies<AllocClassTag_v::HugePage>);

  using H = AllocClass<AllocClassTag_v::HugePage, void*>;
  // HugePage is the weakest; satisfies only itself.
  static_assert( H::satisfies<AllocClassTag_v::HugePage>);
  static_assert(!H::satisfies<AllocClassTag_v::Mmap>);
}

// ── T10 — end-to-end fence-checked consumer (production-like) ────
template <typename Slot>
    requires (Slot::template satisfies<AllocClassTag_v::Pool>)
static uintptr_t pool_consumer(Slot slot) {
  void* p = std::move(slot).consume();
  return reinterpret_cast<uintptr_t>(p);
}

static void test_e2e_fence_checked_consumer() {
  TensorSlot slots[1]{};
  slots[0] = make_slot(0, 1024, 0);
  MemoryPlan plan = make_plan(slots, 1, 1024, 0);

  PoolAllocator pool;
  pool.init(&plan);
  auto pv = pool.mint_initialized_view();

  // Pool tier slot flows through the Pool-fence consumer cleanly.
  auto slot_pinned = pool.slot_ptr_pinned(SlotId{0}, pv);
  uintptr_t addr = pool_consumer(std::move(slot_pinned));
  assert(addr != 0);

  pool.destroy();
}

// ── T11 — pool_base_pinned at large pool (huge-page eligible) ──
//         pool_base_pinned returns Pool tier even when the
//         underlying allocation is huge-page aligned (Pool is the
//         SAFE pinning — it always holds).
static void test_pool_base_pinned_with_huge_alignment() {
  constexpr uint64_t HP = crucible::rt::kHugePageBytes;
  TensorSlot slots[1]{};
  slots[0] = make_slot(0, HP / 2, 0);
  MemoryPlan plan = make_plan(slots, 1, HP, 0);

  PoolAllocator pool;
  pool.init(&plan);

  // pool_base_pinned returns Pool tier (the floor — safe when
  // pool_bytes is large enough for huge-page alignment too).
  auto pool_pinned = pool.pool_base_pinned();
  static_assert(std::is_same_v<decltype(pool_pinned),
      AllocClass<AllocClassTag_v::Pool, void*>>);

  // pool_base_huge_pinned returns HugePage tier (the strict
  // promise).  The same address, two different type-level claims.
  auto huge_pinned = pool.pool_base_huge_pinned();
  static_assert(std::is_same_v<decltype(huge_pinned),
      AllocClass<AllocClassTag_v::HugePage, void*>>);

  void* p1 = std::move(pool_pinned).consume();
  void* p2 = std::move(huge_pinned).consume();
  assert(p1 == p2);
  assert(reinterpret_cast<uintptr_t>(p1) % HP == 0);

  pool.destroy();
}

int main() {
  test_slot_ptr_pinned_bit_equality();
  test_slot_ptr_pinned_type_identity();
  test_pool_fence_simulation();
  test_negative_tier_witnesses();
  test_layout_invariant();
  test_pool_base_pinned_small_pool();
  test_pool_base_huge_pinned();
  test_pool_relax_to_weaker();
  test_chain_composition();
  test_e2e_fence_checked_consumer();
  test_pool_base_pinned_with_huge_alignment();
  std::puts("ok");
  return 0;
}
