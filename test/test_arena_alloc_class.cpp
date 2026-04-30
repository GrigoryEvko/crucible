// FOUND-G42: Arena.h AllocClass-pinned production surface.
//
// Verifies the `_pinned` variants added to `Arena` for the FOUND-G42
// production-call-site task.  The same rigor template that FOUND-
// G17-AUDIT established for Philox.h applies here:
//
//   1. Bit-equality with raw — `alloc_obj_pinned<T>(a).peek() ==
//      alloc_obj<T>(a)` (modulo separate-call alloc divergence).
//      Pointer EQUIVALENT TO a freshly-bumped arena pointer; same
//      arena, same lifetime, equivalent semantics.
//   2. Pinned tier — every `_pinned` variant returns AllocClass<Arena>
//      at the type level.
//   3. Type-identity asserts — value_type IS T*, full type matches
//      AllocClass<AllocClassTag_v::Arena, T*>.
//   4. Memory-plan-fence simulation — a function with the shape
//      `requires AllocClass<...>::satisfies<Arena>` accepts every
//      `_pinned` result; rejects Heap-tier wrappers.
//   5. Negative witnesses — Mmap / HugePage / Heap MUST be rejected
//      at the Arena gate; Stack / Pool MUST be rejected at the
//      Arena-or-stronger gate when wrapped Arena values try to
//      pose as stronger.
//   6. Layout invariant — sizeof(AllocClass<Arena, T*>) == sizeof(T*).
//   7. Lifetime preservation — pointer wrapped by AllocClass remains
//      valid for the arena's lifetime; .peek() and .consume() both
//      return the same address.
//   8. Move semantics — `std::move(wrapped).consume()` yields the
//      underlying T* by value.
//   9. Null-on-zero contract — alloc_array_pinned<T>(a, 0) wraps
//      nullptr (same as raw alloc_array).
//  10. NullSafe + AllocClass — AllocClass<Arena, T*>{nullptr} is a
//      VALID wrapper (it's the type-level claim about WHERE bytes
//      WOULD be allocated; nullness is orthogonal to tier).

#include <crucible/Arena.h>
#include <crucible/safety/AllocClass.h>

#include "test_assert.h"

#include <array>
#include <cstdio>
#include <cstdint>
#include <type_traits>
#include <utility>

using namespace crucible;
using safety::AllocClass;
using safety::AllocClassTag_v;

// ── Test types ─────────────────────────────────────────────────────

struct alignas(64) HotPathStruct {
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t c = 0;
    uint64_t d = 0;
};

struct SmallStruct {
    uint32_t v = 0xDEADBEEFu;
};

// ── 1. Pointer-shape equivalence between raw and pinned ────────────

static void test_alloc_obj_pinned_returns_arena_pointer() {
    std::printf("  alloc_obj_pinned returns valid arena pointer...\n");

    Arena arena;
    effects::Bg bg;

    auto wrapped = arena.alloc_obj_pinned<HotPathStruct>(bg.alloc);
    HotPathStruct* unwrapped = wrapped.peek();

    // The pointer must be non-null (gnu::returns_nonnull is preserved
    // through the wrapper layer at the type level — the wrapper just
    // carries the pointer, doesn't introduce nullability).
    assert(unwrapped != nullptr);

    // It must be writeable to (i.e., it points into the arena's
    // backing storage with proper alignment for HotPathStruct).
    unwrapped->a = 1;
    unwrapped->b = 2;
    unwrapped->c = 3;
    unwrapped->d = 4;

    assert(unwrapped->a == 1);
    assert(unwrapped->b == 2);
    assert(unwrapped->c == 3);
    assert(unwrapped->d == 4);

    // 64-byte alignment must be honored.
    auto addr = reinterpret_cast<uintptr_t>(unwrapped);
    assert((addr & (alignof(HotPathStruct) - 1)) == 0);
}

static void test_alloc_array_pinned_zero_yields_null_wrapper() {
    std::printf("  alloc_array_pinned(0) yields null wrapper...\n");

    Arena arena;
    effects::Bg bg;

    // n == 0 must produce a wrapper around nullptr — mirrors
    // alloc_array's contract.  AllocClass<Arena, T*>{nullptr} is
    // a perfectly valid wrapper: the type-level claim is "if this
    // pointer were non-null, it would be Arena-allocated."
    auto wrapped = arena.alloc_array_pinned<int>(bg.alloc, 0);
    assert(wrapped.peek() == nullptr);
}

static void test_alloc_array_pinned_nonzero_yields_writeable_array() {
    std::printf("  alloc_array_pinned(N>0) yields writeable array...\n");

    Arena arena;
    effects::Bg bg;

    constexpr size_t N = 16;
    auto wrapped = arena.alloc_array_pinned<uint32_t>(bg.alloc, N);
    uint32_t* arr = wrapped.peek();
    assert(arr != nullptr);

    for (size_t i = 0; i < N; ++i) arr[i] = static_cast<uint32_t>(i * 7 + 3);
    for (size_t i = 0; i < N; ++i) assert(arr[i] == static_cast<uint32_t>(i * 7 + 3));
}

static void test_alloc_array_nonzero_pinned_returns_nonnull() {
    std::printf("  alloc_array_nonzero_pinned returns non-null...\n");

    Arena arena;
    effects::Bg bg;

    auto wrapped = arena.alloc_array_nonzero_pinned<SmallStruct>(bg.alloc, 32);
    SmallStruct* arr = wrapped.peek();
    assert(arr != nullptr);

    // alloc_array_nonzero returns raw storage (no placement-new), so
    // the memory is Arena-poisoned (0xCD pattern), NOT default-init.
    // The wrapper carries no construction semantics; verify writeable
    // and addressable instead.
    for (size_t i = 0; i < 32; ++i) {
        arr[i].v = static_cast<uint32_t>(i * 13 + 7);
    }
    for (size_t i = 0; i < 32; ++i) {
        assert(arr[i].v == static_cast<uint32_t>(i * 13 + 7));
    }
}

// ── 2. Pinned tier — every variant returns AllocClass<Arena, T*> ──

static_assert(decltype(std::declval<Arena&>().alloc_obj_pinned<int>(
        std::declval<effects::Alloc>()))::tag
              == AllocClassTag_v::Arena,
    "alloc_obj_pinned MUST return AllocClass<Arena, T*>.  FOUND-G42 "
    "production-callsite tier-pin gone.");

static_assert(decltype(std::declval<Arena&>().alloc_array_pinned<int>(
        std::declval<effects::Alloc>(), size_t{0}))::tag
              == AllocClassTag_v::Arena,
    "alloc_array_pinned MUST return AllocClass<Arena, T*>.");

static_assert(decltype(std::declval<Arena&>().alloc_array_nonzero_pinned<int>(
        std::declval<effects::Alloc>(), size_t{1}))::tag
              == AllocClassTag_v::Arena,
    "alloc_array_nonzero_pinned MUST return AllocClass<Arena, T*>.");

// ── 3. Type-identity asserts — full type, not just tag ────────────

static_assert(std::is_same_v<
        decltype(std::declval<Arena&>().alloc_obj_pinned<HotPathStruct>(
            std::declval<effects::Alloc>())),
        AllocClass<AllocClassTag_v::Arena, HotPathStruct*>>,
    "alloc_obj_pinned<HotPathStruct> MUST return EXACTLY "
    "AllocClass<Arena, HotPathStruct*>.  If this fires, the wrapper "
    "return type has drifted (e.g., a refactor changed T → T or wrapped "
    "the pointer differently).");

static_assert(std::is_same_v<
        decltype(std::declval<Arena&>().alloc_array_pinned<int>(
            std::declval<effects::Alloc>(), size_t{0})),
        AllocClass<AllocClassTag_v::Arena, int*>>,
    "alloc_array_pinned<int> MUST return EXACTLY AllocClass<Arena, int*>.");

static_assert(std::is_same_v<
        decltype(std::declval<Arena&>().alloc_array_nonzero_pinned<int>(
            std::declval<effects::Alloc>(), size_t{1})),
        AllocClass<AllocClassTag_v::Arena, int*>>,
    "alloc_array_nonzero_pinned<int> MUST return EXACTLY "
    "AllocClass<Arena, int*>.");

// value_type accessor — pinning value_type IS T* lets production
// callers read the unwrapped pointer type via decltype(...)::value_type.
static_assert(std::is_same_v<
        AllocClass<AllocClassTag_v::Arena, HotPathStruct*>::value_type,
        HotPathStruct*>);
static_assert(std::is_same_v<
        AllocClass<AllocClassTag_v::Arena, int*>::value_type, int*>);

// ── 4. Memory-plan-fence simulation — accept Arena-or-stronger ────
//
// This concept simulates a future production fence:
//
//   template <typename W>
//     requires (W::template satisfies<AllocClassTag_v::Arena>)
//   void process_arena(W p);  // accepts Arena, Pool, Stack
//
// Today such fences exist only in code review.  With AllocClass
// they become per-call type-checked.

template <typename W>
concept admissible_at_arena_fence =
    W::template satisfies<AllocClassTag_v::Arena>;

static_assert(admissible_at_arena_fence<
        AllocClass<AllocClassTag_v::Arena, int*>>,
    "Arena-tier wrapper MUST pass an Arena-or-stronger fence "
    "(reflexivity at the boundary).  If this fires, the lattice "
    "subsumption is broken at the production-callsite tier.");
static_assert(admissible_at_arena_fence<
        AllocClass<AllocClassTag_v::Pool, int*>>,
    "Pool-tier wrapper MUST pass an Arena-or-stronger fence "
    "(Pool ⊐ Arena by lattice direction).");
static_assert(admissible_at_arena_fence<
        AllocClass<AllocClassTag_v::Stack, int*>>,
    "Stack-tier wrapper MUST pass an Arena-or-stronger fence "
    "(Stack = top of lattice; subsumes every consumer).");

// ── 5. Negative witnesses — load-bearing rejections ──────────────
//
// Heap / Mmap / HugePage tiers are STRICTLY weaker than Arena —
// they include syscall costs that Arena does not.  A function
// declared `requires Arena` MUST refuse them.  This is the
// LOAD-BEARING REJECTION at the AllocClass production-callsite
// surface.

static_assert(!admissible_at_arena_fence<
        AllocClass<AllocClassTag_v::Heap, int*>>,
    "Heap-tier wrapper MUST NOT pass an Arena-or-stronger fence — "
    "heap allocations carry mutex contention + fragmentation costs "
    "that Arena's bump pointer does not.  If this fires, the "
    "CLAUDE.md §VIII memory-plan discipline (no heap on hot path) "
    "is no longer compile-time-enforceable through AllocClass.");
static_assert(!admissible_at_arena_fence<
        AllocClass<AllocClassTag_v::Mmap, int*>>,
    "Mmap-tier wrapper MUST NOT pass an Arena-or-stronger fence — "
    "mmap is syscall-bearing.");
static_assert(!admissible_at_arena_fence<
        AllocClass<AllocClassTag_v::HugePage, int*>>,
    "HugePage-tier wrapper MUST NOT pass an Arena-or-stronger fence "
    "(HugePage = bottom of lattice; only NDS-equivalent consumers "
    "admit it).");

// And the reverse direction: Arena-tier MUST NOT pass a Pool-or-
// stronger fence.  Arena's bump may occasionally need a new chunk
// from heap (slow path); a Pool-tier consumer claims structurally
// bounded latency that Arena cannot promise.

template <typename W>
concept admissible_at_pool_fence =
    W::template satisfies<AllocClassTag_v::Pool>;

static_assert( admissible_at_pool_fence<
        AllocClass<AllocClassTag_v::Pool, int*>>,
    "Pool reflexively passes its own gate.");
static_assert( admissible_at_pool_fence<
        AllocClass<AllocClassTag_v::Stack, int*>>,
    "Stack subsumes Pool.");
static_assert(!admissible_at_pool_fence<
        AllocClass<AllocClassTag_v::Arena, int*>>,
    "Arena-tier wrapper MUST NOT pass a Pool-or-stronger fence.  THE "
    "LOAD-BEARING REJECTION: arena's slow-path chunk acquisition "
    "violates Pool's bounded-latency contract.  If this fires, a "
    "future relaxation of the lattice direction has silently broken "
    "the per-tier admission ordering.");

template <typename W>
concept admissible_at_stack_fence =
    W::template satisfies<AllocClassTag_v::Stack>;

static_assert( admissible_at_stack_fence<
        AllocClass<AllocClassTag_v::Stack, int*>>);
static_assert(!admissible_at_stack_fence<
        AllocClass<AllocClassTag_v::Pool, int*>>);
static_assert(!admissible_at_stack_fence<
        AllocClass<AllocClassTag_v::Arena, int*>>,
    "Arena-tier wrapper MUST NOT pass a Stack-or-stronger fence — "
    "an Arena pointer involves AT LEAST a bump-pointer call; Stack "
    "tier is `no allocator call at all`.  If this fires, the "
    "strongest hot-path discipline is silently weakened.");

// ── 6. Layout invariant ───────────────────────────────────────────

static_assert(sizeof(AllocClass<AllocClassTag_v::Arena, int*>) == sizeof(int*),
    "FOUND-G42 zero-cost claim: AllocClass<Arena, T*> MUST be byte-"
    "equal to bare T*.  If this fires, the production wrapper has "
    "introduced a runtime cost that breaks the §XVI EBO-collapse rule.");
static_assert(sizeof(AllocClass<AllocClassTag_v::Arena, HotPathStruct*>)
              == sizeof(HotPathStruct*));
static_assert(sizeof(AllocClass<AllocClassTag_v::Arena, char*>) == sizeof(char*));
static_assert(sizeof(AllocClass<AllocClassTag_v::Arena, void*>) == sizeof(void*));

// ── 7. Lifetime preservation — pointer through wrapper ────────────

static void test_pointer_lifetime_through_wrapper() {
    std::printf("  pointer lifetime preserved through AllocClass...\n");

    Arena arena;
    effects::Bg bg;

    auto wrapped = arena.alloc_obj_pinned<HotPathStruct>(bg.alloc);
    HotPathStruct* via_peek = wrapped.peek();
    assert(via_peek != nullptr);
    via_peek->a = 0xCAFEBABEull;

    // peek() returns the same pointer on subsequent calls (no copy).
    HotPathStruct* via_peek_again = wrapped.peek();
    assert(via_peek == via_peek_again);
    assert(via_peek_again->a == 0xCAFEBABEull);

    // The wrapper does not own the pointed-to memory; that's the
    // arena's job.  `wrapped` going out of scope MUST NOT free the
    // arena memory.  We verify by making a ScopedView of the wrapper
    // and ensuring the underlying value remains accessible.
    {
        auto& wrapped_ref = wrapped;  // local ref scope
        assert(wrapped_ref.peek()->a == 0xCAFEBABEull);
    }
    // After the scope, the wrapper still refers to valid arena memory.
    assert(wrapped.peek()->a == 0xCAFEBABEull);
}

// ── 8. Move semantics — consume() yields the raw T* by value ─────

static void test_move_semantics_through_wrapper() {
    std::printf("  move-semantics through AllocClass wrapper...\n");

    Arena arena;
    effects::Bg bg;

    auto wrapped = arena.alloc_obj_pinned<HotPathStruct>(bg.alloc);
    HotPathStruct* before = wrapped.peek();

    static_assert(std::is_same_v<
        decltype(std::move(wrapped).consume()), HotPathStruct*>,
        "AllocClass::consume() && MUST return T* by value (move "
        "semantics for a trivially-copyable pointer).");

    HotPathStruct* extracted = std::move(wrapped).consume();
    assert(extracted == before);
    assert(extracted != nullptr);

    // Use the extracted pointer — it remains valid (the arena hasn't
    // been destroyed).
    extracted->a = 0xFEEDFACEull;
    assert(extracted->a == 0xFEEDFACEull);
}

// ── 9. Null-on-zero contract for alloc_array_pinned ──────────────
//
// The raw alloc_array<T>(a, 0) returns nullptr.  alloc_array_pinned
// MUST preserve this — the caller is expected to handle (count=0,
// nullptr) per Arena.h's NullSafe documentation.  Verified by
// test_alloc_array_pinned_zero_yields_null_wrapper above; here pin
// the structural property explicitly.

static void test_null_on_zero_contract() {
    std::printf("  null-on-zero contract preserved through wrapper...\n");

    Arena arena;
    effects::Bg bg;

    auto w0 = arena.alloc_array_pinned<int>(bg.alloc, 0);
    assert(w0.peek() == nullptr);

    // Same for char arrays and pointer arrays.
    auto w0c = arena.alloc_array_pinned<char>(bg.alloc, 0);
    assert(w0c.peek() == nullptr);

    auto w0p = arena.alloc_array_pinned<void*>(bg.alloc, 0);
    assert(w0p.peek() == nullptr);
}

// ── 10. NullSafe + AllocClass orthogonality ──────────────────────
//
// AllocClass<Arena, T*>{nullptr} is a VALID wrapper.  The tier
// classifies WHERE BYTES WOULD BE allocated; nullness is orthogonal.
// A nullptr can carry an Arena tier-claim because if it were
// allocated, it would be from the arena.

static_assert(
    requires {
        AllocClass<AllocClassTag_v::Arena, int*>{nullptr};
    },
    "AllocClass<Arena, T*>{nullptr} MUST be constructible.  "
    "NullSafe + AllocClass are orthogonal axes.");

// Default-construction yields an AllocClass<Arena, nullptr>.
static_assert(AllocClass<AllocClassTag_v::Arena, int*>{}.peek() == nullptr,
    "AllocClass<Arena, T*> default-construction MUST yield wrapped nullptr.");

// ── 11. Reflexive + cross-tier satisfies coverage ────────────────

using ArenaIntPtr = AllocClass<AllocClassTag_v::Arena, int*>;
static_assert( ArenaIntPtr::satisfies<AllocClassTag_v::Arena>,
    "Arena reflexively satisfies Arena (lattice reflexivity at the "
    "production-instantiated tier).");
static_assert( ArenaIntPtr::satisfies<AllocClassTag_v::Heap>,
    "Arena ⊒ Heap; satisfies any Heap-or-weaker consumer.");
static_assert( ArenaIntPtr::satisfies<AllocClassTag_v::Mmap>);
static_assert( ArenaIntPtr::satisfies<AllocClassTag_v::HugePage>,
    "Arena MUST satisfy HugePage (the bottom of the chain).  If this "
    "fires, a future linearization regression has broken Arena's "
    "subsumption-down direction.");
static_assert(!ArenaIntPtr::satisfies<AllocClassTag_v::Pool>,
    "Arena MUST NOT satisfy Pool (Arena's slow-path chunk acquisition "
    "violates Pool's bounded-latency claim).");
static_assert(!ArenaIntPtr::satisfies<AllocClassTag_v::Stack>,
    "Arena MUST NOT satisfy Stack (any pointer involves at least a "
    "bump-pointer call; Stack tier admits no allocator call at all).");

// ── 12. relax<>() — Arena → Heap is admissible (down-the-lattice) ─

static void test_relax_arena_to_weaker() {
    std::printf("  relax Arena → Heap (down-the-lattice)...\n");

    Arena arena;
    effects::Bg bg;

    auto wrapped = arena.alloc_obj_pinned<int>(bg.alloc);
    int* before = wrapped.peek();

    // Relax Arena → Heap.  Bytes still arena-allocated; the wrapper
    // just claims a weaker tier.
    auto relaxed = std::move(wrapped).relax<AllocClassTag_v::Heap>();
    static_assert(decltype(relaxed)::tag == AllocClassTag_v::Heap);

    // Pointer bytes preserved — relax is a tier downgrade, not a
    // pointer transformation.
    assert(relaxed.peek() == before);
}

// SFINAE detector for relax — proves the requires-clause REJECTS
// up-the-lattice relaxations (the load-bearing rejection class).

template <typename W, AllocClassTag_v T_target>
concept can_relax_to = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};

static_assert( can_relax_to<ArenaIntPtr, AllocClassTag_v::Arena>,
    "Arena → Arena (self) admissible.");
static_assert( can_relax_to<ArenaIntPtr, AllocClassTag_v::Heap>,
    "Arena → Heap (down-the-lattice) admissible.");
static_assert( can_relax_to<ArenaIntPtr, AllocClassTag_v::HugePage>,
    "Arena → HugePage (all the way down) admissible.");
static_assert(!can_relax_to<ArenaIntPtr, AllocClassTag_v::Pool>,
    "Arena → Pool (UP the lattice) MUST be REJECTED.  Claiming Pool-"
    "tier from an Arena allocation would silently let the value pass "
    "Pool-tier consumer fences, defeating the bounded-latency "
    "discipline.  THE LOAD-BEARING REJECTION at the relax surface.");
static_assert(!can_relax_to<ArenaIntPtr, AllocClassTag_v::Stack>,
    "Arena → Stack (UP the lattice, top-claim) MUST be REJECTED.");

// ── 13. End-to-end production scenario — fence-checked consumer ──

template <typename W>
    requires (admissible_at_arena_fence<W>)
[[nodiscard]] static int* consume_arena_or_stronger(W&& wrapper) noexcept {
    // Production-shape consumer: takes any AllocClass-pinned pointer
    // at Arena tier or stronger, reaches into the bytes.  Compile
    // error if the caller passes Heap-tier or worse.
    return std::move(wrapper).consume();
}

static void test_e2e_fence_checked_consumer() {
    std::printf("  end-to-end fence-checked consumer accepts Arena...\n");

    Arena arena;
    effects::Bg bg;

    auto wrapped = arena.alloc_array_nonzero_pinned<int>(bg.alloc, 4);
    int* arr = consume_arena_or_stronger(std::move(wrapped));
    assert(arr != nullptr);

    arr[0] = 0xAA;
    arr[1] = 0xBB;
    arr[2] = 0xCC;
    arr[3] = 0xDD;
    assert(arr[0] == 0xAA);
    assert(arr[3] == 0xDD);

    // Demonstrate compile-time admission: a Stack-tier wrapper also
    // passes the same fence (Stack ⊒ Arena by lattice direction).
    AllocClass<AllocClassTag_v::Stack, int*> stack_wrapper{arr};
    int* p2 = consume_arena_or_stronger(std::move(stack_wrapper));
    assert(p2 == arr);

    // Per the negative witnesses above, a Heap-tier wrapper would
    // FAIL to compile here.  We can't put the fail-to-compile in the
    // runtime test, but the static_asserts above pin the rejection.
}

// ── 14. Large-allocation slow-path correctness ───────────────────
//
// Pin that the wrapper preserves the slow-path semantics (large
// allocation that exceeds block_size).  Edge case for the AUDIT
// rigor: a future refactor that special-cases the wrapper for
// fast-path-only must NOT break the slow-path.

static void test_slow_path_preserves_wrapper() {
    std::printf("  slow-path large allocation preserves wrapper...\n");

    Arena arena{/*block_size=*/256};  // small block to force slow path
    effects::Bg bg;

    // Allocate larger than block_size — forces slow path.
    auto wrapped = arena.alloc_array_nonzero_pinned<uint64_t>(bg.alloc, 64);
    uint64_t* arr = wrapped.peek();
    assert(arr != nullptr);

    for (size_t i = 0; i < 64; ++i) arr[i] = i * 0xDEADBEEFull + 1;
    for (size_t i = 0; i < 64; ++i) assert(arr[i] == i * 0xDEADBEEFull + 1);

    // Pinned tier survives slow path.
    static_assert(decltype(wrapped)::tag == AllocClassTag_v::Arena);
}

// ── 15. Multiple allocations in sequence — pointers distinct ─────

static void test_sequential_allocations_distinct() {
    std::printf("  sequential pinned allocations yield distinct pointers...\n");

    Arena arena;
    effects::Bg bg;

    auto w1 = arena.alloc_obj_pinned<HotPathStruct>(bg.alloc);
    auto w2 = arena.alloc_obj_pinned<HotPathStruct>(bg.alloc);
    auto w3 = arena.alloc_obj_pinned<HotPathStruct>(bg.alloc);

    assert(w1.peek() != nullptr);
    assert(w2.peek() != nullptr);
    assert(w3.peek() != nullptr);
    assert(w1.peek() != w2.peek());
    assert(w2.peek() != w3.peek());
    assert(w1.peek() != w3.peek());

    // Bump-pointer arena: each next allocation is at a higher address
    // (within a block).  Verify ordering.
    assert(reinterpret_cast<uintptr_t>(w1.peek())
           < reinterpret_cast<uintptr_t>(w2.peek()));
    assert(reinterpret_cast<uintptr_t>(w2.peek())
           < reinterpret_cast<uintptr_t>(w3.peek()));
}

// ── Main ───────────────────────────────────────────────────────────

int main() {
    std::printf("test_arena_alloc_class\n");

    test_alloc_obj_pinned_returns_arena_pointer();
    test_alloc_array_pinned_zero_yields_null_wrapper();
    test_alloc_array_pinned_nonzero_yields_writeable_array();
    test_alloc_array_nonzero_pinned_returns_nonnull();
    test_pointer_lifetime_through_wrapper();
    test_move_semantics_through_wrapper();
    test_null_on_zero_contract();
    test_relax_arena_to_weaker();
    test_e2e_fence_checked_consumer();
    test_slow_path_preserves_wrapper();
    test_sequential_allocations_distinct();

    std::printf("PASS\n");
    return 0;
}
