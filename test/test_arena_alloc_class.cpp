// The arena's pinned allocation variants return a pointer wrapped in a
// type-level claim about which allocation tier the bytes came from.  A
// caller that fences on the tier then refuses a pointer from a weaker
// one at compile time, where today the same rule is only enforced in
// review.
//
// Two properties have to hold together for that to be worth anything.
// The wrapper must be transparent, so that the pointer it carries
// behaves exactly as the raw allocation would, and the fence must
// actually reject, in both directions: a weaker tier must not pass a
// stronger gate, and a wrapper must not be able to pose as stronger
// than it is.

#include <crucible/Arena.h>
#include <crucible/safety/_AllocClass.h>

#include "test_assert.h"

#include <array>
#include <bit>
#include <cstdio>
#include <cstdint>
#include <type_traits>
#include <utility>

using namespace crucible;
using safety::AllocClass;
using safety::AllocClassTag_v;

struct alignas(64) HotPathStruct {
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t c = 0;
    uint64_t d = 0;
};

struct SmallStruct {
    uint32_t v = 0xDEADBEEFu;
};

static void test_alloc_obj_pinned_returns_arena_pointer() {
    std::printf("  alloc_obj_pinned returns valid arena pointer...\n");

    Arena arena;
    auto bg = effects::testing::bg();

    auto wrapped = arena.alloc_obj_pinned<HotPathStruct>(bg.alloc);
    HotPathStruct* unwrapped = wrapped.peek();

    assert(unwrapped != nullptr);

    unwrapped->a = 1;
    unwrapped->b = 2;
    unwrapped->c = 3;
    unwrapped->d = 4;

    assert(unwrapped->a == 1);
    assert(unwrapped->b == 2);
    assert(unwrapped->c == 3);
    assert(unwrapped->d == 4);

    auto addr = std::bit_cast<uintptr_t>(unwrapped);
    assert((addr & (alignof(HotPathStruct) - 1)) == 0);
}

static void test_alloc_array_pinned_zero_yields_null_wrapper() {
    std::printf("  alloc_array_pinned(0) yields null wrapper...\n");

    Arena arena;
    auto bg = effects::testing::bg();

    // A wrapper around nullptr is well-formed.  The tier is a claim
    // about where the bytes would come from, which stays true of a
    // pointer that addresses none.
    auto wrapped = arena.alloc_array_pinned<int>(bg.alloc, 0);
    assert(wrapped.peek() == nullptr);
}

static void test_alloc_array_pinned_nonzero_yields_writeable_array() {
    std::printf("  alloc_array_pinned(N>0) yields writeable array...\n");

    Arena arena;
    auto bg = effects::testing::bg();

    constexpr size_t N = 16;
    auto wrapped = arena.alloc_array_pinned<uint32_t>(bg.alloc, N);
    uint32_t* arr = wrapped.peek();
    assert(arr != nullptr);

    for (size_t i = 0; i < N; ++i)
        arr[i] = static_cast<uint32_t>(i * 7 + 3);
    for (size_t i = 0; i < N; ++i)
        assert(arr[i] == static_cast<uint32_t>(i * 7 + 3));
}

static void test_alloc_array_nonzero_pinned_returns_nonnull() {
    std::printf("  alloc_array_nonzero_pinned returns non-null...\n");

    Arena arena;
    auto bg = effects::testing::bg();

    auto wrapped = arena.alloc_array_nonzero_pinned<SmallStruct>(bg.alloc, 32);
    SmallStruct* arr = wrapped.peek();
    assert(arr != nullptr);

    // This variant hands back raw storage without running any
    // constructor, so the bytes hold the arena's poison pattern.  The
    // only thing worth reading here is what the test writes first.
    for (size_t i = 0; i < 32; ++i) {
        arr[i].v = static_cast<uint32_t>(i * 13 + 7);
    }
    for (size_t i = 0; i < 32; ++i) {
        assert(arr[i].v == static_cast<uint32_t>(i * 13 + 7));
    }
}

static_assert(decltype(std::declval<Arena&>().alloc_obj_pinned<int>(std::declval<effects::Alloc>()))::tag
                  == AllocClassTag_v::Arena,
              "alloc_obj_pinned must return AllocClass<Arena, T*>.  The tier pin "
              "production call sites fence on is gone.");

static_assert(decltype(std::declval<Arena&>().alloc_array_pinned<int>(std::declval<effects::Alloc>(), size_t{0}))::tag
                  == AllocClassTag_v::Arena,
              "alloc_array_pinned MUST return AllocClass<Arena, T*>.");

static_assert(decltype(std::declval<Arena&>().alloc_array_nonzero_pinned<int>(std::declval<effects::Alloc>(),
                                                                              size_t{1}))::tag
                  == AllocClassTag_v::Arena,
              "alloc_array_nonzero_pinned MUST return AllocClass<Arena, T*>.");

static_assert(
    std::is_same_v<decltype(std::declval<Arena&>().alloc_obj_pinned<HotPathStruct>(std::declval<effects::Alloc>())),
                   AllocClass<AllocClassTag_v::Arena, HotPathStruct*>>,
    "alloc_obj_pinned<HotPathStruct> MUST return EXACTLY "
    "AllocClass<Arena, HotPathStruct*>.  If this fires, the wrapper "
    "return type has drifted (e.g., a refactor changed T → T or wrapped "
    "the pointer differently).");

static_assert(
    std::is_same_v<decltype(std::declval<Arena&>().alloc_array_pinned<int>(std::declval<effects::Alloc>(), size_t{0})),
                   AllocClass<AllocClassTag_v::Arena, int*>>,
    "alloc_array_pinned<int> MUST return EXACTLY AllocClass<Arena, int*>.");

static_assert(std::is_same_v<decltype(std::declval<Arena&>().alloc_array_nonzero_pinned<int>(
                                 std::declval<effects::Alloc>(), size_t{1})),
                             AllocClass<AllocClassTag_v::Arena, int*>>,
              "alloc_array_nonzero_pinned<int> MUST return EXACTLY "
              "AllocClass<Arena, int*>.");

// value_type is the pointer type itself, which is what lets a caller
// name the unwrapped type without unwrapping a value first.
static_assert(std::is_same_v<AllocClass<AllocClassTag_v::Arena, HotPathStruct*>::value_type, HotPathStruct*>);
static_assert(std::is_same_v<AllocClass<AllocClassTag_v::Arena, int*>::value_type, int*>);

// The shape a production fence takes: a constraint on the wrapper's
// tier, evaluated per call.

template <typename W>
concept admissible_at_arena_fence = W::template satisfies<AllocClassTag_v::Arena>;

static_assert(admissible_at_arena_fence<AllocClass<AllocClassTag_v::Arena, int*>>,
              "Arena-tier wrapper MUST pass an Arena-or-stronger fence "
              "(reflexivity at the boundary).  If this fires, the lattice "
              "subsumption is broken at the production-callsite tier.");
static_assert(admissible_at_arena_fence<AllocClass<AllocClassTag_v::Pool, int*>>,
              "Pool-tier wrapper MUST pass an Arena-or-stronger fence "
              "(Pool ⊐ Arena by lattice direction).");
static_assert(admissible_at_arena_fence<AllocClass<AllocClassTag_v::Stack, int*>>,
              "Stack-tier wrapper MUST pass an Arena-or-stronger fence "
              "(Stack = top of lattice; subsumes every consumer).");

// Heap, mmap and huge-page allocations can reach a syscall, which an
// arena bump cannot, so those tiers sit strictly below Arena and a gate
// that demands Arena has to refuse them.

static_assert(!admissible_at_arena_fence<AllocClass<AllocClassTag_v::Heap, int*>>,
              "a Heap-tier wrapper must not pass an Arena-or-stronger fence.  "
              "Heap allocation carries mutex contention and fragmentation that an "
              "arena bump does not, and if this fires the rule keeping heap "
              "allocation off the hot path is no longer enforced at compile "
              "time.");
static_assert(!admissible_at_arena_fence<AllocClass<AllocClassTag_v::Mmap, int*>>,
              "an Mmap-tier wrapper must not pass an Arena-or-stronger fence: "
              "mmap reaches a syscall.");
static_assert(!admissible_at_arena_fence<AllocClass<AllocClassTag_v::HugePage, int*>>,
              "a HugePage-tier wrapper must not pass an Arena-or-stronger fence: "
              "HugePage is the bottom of the lattice and only a consumer that "
              "demands nothing admits it.");

// The other direction matters just as much.  An arena bump occasionally
// takes a new chunk from the heap, so it cannot promise the bounded
// latency a pool-tier consumer is relying on, and an Arena wrapper must
// fail a pool-or-stronger gate.

template <typename W>
concept admissible_at_pool_fence = W::template satisfies<AllocClassTag_v::Pool>;

static_assert(admissible_at_pool_fence<AllocClass<AllocClassTag_v::Pool, int*>>,
              "Pool reflexively passes its own gate.");
static_assert(admissible_at_pool_fence<AllocClass<AllocClassTag_v::Stack, int*>>, "Stack subsumes Pool.");
static_assert(!admissible_at_pool_fence<AllocClass<AllocClassTag_v::Arena, int*>>,
              "an Arena-tier wrapper must not pass a Pool-or-stronger fence.  The "
              "arena's slow-path chunk acquisition breaks the bounded-latency "
              "contract a pool-tier consumer relies on, and if this fires the "
              "per-tier admission ordering has been relaxed in the wrong "
              "direction.");

template <typename W>
concept admissible_at_stack_fence = W::template satisfies<AllocClassTag_v::Stack>;

static_assert(admissible_at_stack_fence<AllocClass<AllocClassTag_v::Stack, int*>>);
static_assert(!admissible_at_stack_fence<AllocClass<AllocClassTag_v::Pool, int*>>);
static_assert(!admissible_at_stack_fence<AllocClass<AllocClassTag_v::Arena, int*>>,
              "an Arena-tier wrapper must not pass a Stack-or-stronger fence.  An "
              "arena pointer costs at least a bump-pointer call, while the Stack "
              "tier means no allocator call at all, and if this fires the "
              "strictest hot-path rule has been weakened.");

static_assert(sizeof(AllocClass<AllocClassTag_v::Arena, int*>) == sizeof(int*),
              "AllocClass<Arena, T*> must be byte-equal to a bare T*.  If this "
              "fires, the wrapper has grown storage of its own and stopped "
              "collapsing to nothing at runtime.");
static_assert(sizeof(AllocClass<AllocClassTag_v::Arena, HotPathStruct*>) == sizeof(HotPathStruct*));
static_assert(sizeof(AllocClass<AllocClassTag_v::Arena, char*>) == sizeof(char*));
static_assert(sizeof(AllocClass<AllocClassTag_v::Arena, void*>) == sizeof(void*));

static void test_pointer_lifetime_through_wrapper() {
    std::printf("  pointer lifetime preserved through AllocClass...\n");

    Arena arena;
    auto bg = effects::testing::bg();

    auto wrapped = arena.alloc_obj_pinned<HotPathStruct>(bg.alloc);
    HotPathStruct* via_peek = wrapped.peek();
    assert(via_peek != nullptr);
    via_peek->a = 0xCAFEBABEull;

    HotPathStruct* via_peek_again = wrapped.peek();
    assert(via_peek == via_peek_again);
    assert(via_peek_again->a == 0xCAFEBABEull);

    // The arena owns the memory, not the wrapper, so a wrapper going
    // out of scope must leave the bytes alone.
    {
        auto& wrapped_ref = wrapped;
        assert(wrapped_ref.peek()->a == 0xCAFEBABEull);
    }
    assert(wrapped.peek()->a == 0xCAFEBABEull);
}

static void test_move_semantics_through_wrapper() {
    std::printf("  move-semantics through AllocClass wrapper...\n");

    Arena arena;
    auto bg = effects::testing::bg();

    auto wrapped = arena.alloc_obj_pinned<HotPathStruct>(bg.alloc);
    HotPathStruct* before = wrapped.peek();

    static_assert(std::is_same_v<decltype(std::move(wrapped).consume()), HotPathStruct*>,
                  "AllocClass::consume() && MUST return T* by value (move "
                  "semantics for a trivially-copyable pointer).");

    HotPathStruct* extracted = std::move(wrapped).consume();
    assert(extracted == before);
    assert(extracted != nullptr);

    // Consuming the wrapper does not end the pointer's life, because
    // the arena is still alive.
    extracted->a = 0xFEEDFACEull;
    assert(extracted->a == 0xFEEDFACEull);
}

// The unwrapped allocation returns nullptr for a count of zero, and
// callers are written to handle that pair.  The wrapper has to preserve
// it for every element type, not just the one checked above.
static void test_null_on_zero_contract() {
    std::printf("  null-on-zero contract preserved through wrapper...\n");

    Arena arena;
    auto bg = effects::testing::bg();

    auto w0 = arena.alloc_array_pinned<int>(bg.alloc, 0);
    assert(w0.peek() == nullptr);

    auto w0c = arena.alloc_array_pinned<char>(bg.alloc, 0);
    assert(w0c.peek() == nullptr);

    auto w0p = arena.alloc_array_pinned<void*>(bg.alloc, 0);
    assert(w0p.peek() == nullptr);
}

static_assert(
    requires { AllocClass<AllocClassTag_v::Arena, int*>{nullptr}; },
    "AllocClass<Arena, T*>{nullptr} MUST be constructible.  "
    "NullSafe + AllocClass are orthogonal axes.");

static_assert(AllocClass<AllocClassTag_v::Arena, int*>{}.peek() == nullptr,
              "AllocClass<Arena, T*> default-construction MUST yield wrapped nullptr.");

using ArenaIntPtr = AllocClass<AllocClassTag_v::Arena, int*>;
static_assert(ArenaIntPtr::satisfies<AllocClassTag_v::Arena>,
              "Arena reflexively satisfies Arena (lattice reflexivity at the "
              "production-instantiated tier).");
static_assert(ArenaIntPtr::satisfies<AllocClassTag_v::Heap>, "Arena ⊒ Heap; satisfies any Heap-or-weaker consumer.");
static_assert(ArenaIntPtr::satisfies<AllocClassTag_v::Mmap>);
static_assert(ArenaIntPtr::satisfies<AllocClassTag_v::HugePage>,
              "Arena must satisfy HugePage, the bottom of the chain.  If this "
              "fires, the downward subsumption direction of the lattice is "
              "broken.");
static_assert(!ArenaIntPtr::satisfies<AllocClassTag_v::Pool>,
              "Arena must not satisfy Pool (the arena's slow-path chunk acquisition "
              "violates Pool's bounded-latency claim).");
static_assert(!ArenaIntPtr::satisfies<AllocClassTag_v::Stack>,
              "Arena must not satisfy Stack (any arena pointer costs at least a "
              "bump-pointer call, and the Stack tier admits no allocator call at "
              "all).");

static void test_relax_arena_to_weaker() {
    std::printf("  relax Arena → Heap (down-the-lattice)...\n");

    Arena arena;
    auto bg = effects::testing::bg();

    auto wrapped = arena.alloc_obj_pinned<int>(bg.alloc);
    int* before = wrapped.peek();

    // The bytes are still arena-allocated afterwards.  Relaxing weakens
    // the claim the wrapper makes, and does nothing to the pointer.
    auto relaxed = std::move(wrapped).relax<AllocClassTag_v::Heap>();
    static_assert(decltype(relaxed)::tag == AllocClassTag_v::Heap);

    assert(relaxed.peek() == before);
}

template <typename W, AllocClassTag_v T_target>
concept can_relax_to = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};

static_assert(can_relax_to<ArenaIntPtr, AllocClassTag_v::Arena>, "Arena → Arena (self) admissible.");
static_assert(can_relax_to<ArenaIntPtr, AllocClassTag_v::Heap>, "Arena → Heap (down-the-lattice) admissible.");
static_assert(can_relax_to<ArenaIntPtr, AllocClassTag_v::HugePage>, "Arena → HugePage (all the way down) admissible.");
static_assert(!can_relax_to<ArenaIntPtr, AllocClassTag_v::Pool>,
              "relaxing Arena to Pool must be rejected.  Claiming pool tier for "
              "an arena allocation would let the value pass a pool-tier fence and "
              "defeat the bounded-latency discipline that fence exists for.");
static_assert(!can_relax_to<ArenaIntPtr, AllocClassTag_v::Stack>,
              "relaxing Arena to Stack must be rejected: Stack is the top claim "
              "of the lattice.");

template <typename W>
    requires(admissible_at_arena_fence<W>)
[[nodiscard]] static int* consume_arena_or_stronger(W&& wrapper) noexcept {
    return std::move(wrapper).consume();
}

static void test_e2e_fence_checked_consumer() {
    std::printf("  end-to-end fence-checked consumer accepts Arena...\n");

    Arena arena;
    auto bg = effects::testing::bg();

    auto wrapped = arena.alloc_array_nonzero_pinned<int>(bg.alloc, 4);
    int* arr = consume_arena_or_stronger(std::move(wrapped));
    assert(arr != nullptr);

    arr[0] = 0xAA;
    arr[1] = 0xBB;
    arr[2] = 0xCC;
    arr[3] = 0xDD;
    assert(arr[0] == 0xAA);
    assert(arr[3] == 0xDD);

    // A stronger tier passes the same fence.  The rejection of a weaker
    // one cannot be witnessed at runtime, and is pinned by the
    // static_asserts instead.
    AllocClass<AllocClassTag_v::Stack, int*> stack_wrapper{arr};
    int* p2 = consume_arena_or_stronger(std::move(stack_wrapper));
    assert(p2 == arr);
}

// A refactor that fast-paths the wrapper could leave the slow path
// unwrapped, so the request below is deliberately larger than one
// block.
static void test_slow_path_preserves_wrapper() {
    std::printf("  slow-path large allocation preserves wrapper...\n");

    Arena arena{/*block_size=*/256};
    auto bg = effects::testing::bg();

    auto wrapped = arena.alloc_array_nonzero_pinned<uint64_t>(bg.alloc, 64);
    uint64_t* arr = wrapped.peek();
    assert(arr != nullptr);

    for (size_t i = 0; i < 64; ++i)
        arr[i] = i * 0xDEADBEEFull + 1;
    for (size_t i = 0; i < 64; ++i)
        assert(arr[i] == i * 0xDEADBEEFull + 1);

    static_assert(decltype(wrapped)::tag == AllocClassTag_v::Arena);
}

static void test_sequential_allocations_distinct() {
    std::printf("  sequential pinned allocations yield distinct pointers...\n");

    Arena arena;
    auto bg = effects::testing::bg();

    auto w1 = arena.alloc_obj_pinned<HotPathStruct>(bg.alloc);
    auto w2 = arena.alloc_obj_pinned<HotPathStruct>(bg.alloc);
    auto w3 = arena.alloc_obj_pinned<HotPathStruct>(bg.alloc);

    assert(w1.peek() != nullptr);
    assert(w2.peek() != nullptr);
    assert(w3.peek() != nullptr);
    assert(w1.peek() != w2.peek());
    assert(w2.peek() != w3.peek());
    assert(w1.peek() != w3.peek());

    // Addresses rise within a block because the arena only bumps.  This
    // holds only while no allocation crosses into a new block, which
    // three small objects cannot do.
    assert(std::bit_cast<uintptr_t>(w1.peek()) < std::bit_cast<uintptr_t>(w2.peek()));
    assert(std::bit_cast<uintptr_t>(w2.peek()) < std::bit_cast<uintptr_t>(w3.peek()));
}

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
