#pragma once

// ── crucible::safety::AllocClass<AllocClassTag_v Tag, T> ────────────
//
// Type-pinned allocator-strategy wrapper.  A value of type T whose
// permitted allocator (HugePage ⊑ Mmap ⊑ Heap ⊑ Arena ⊑ Pool ⊑
// Stack) is fixed at the type level via the non-type template
// parameter Tag.  SIXTH chain wrapper from 28_04_2026_effects.md
// §4.3.6 (FOUND-G39) — first wrapper of Month-2 second-pass after
// the Month-2 first-pass close.
//
// Refines `Effect::Alloc` cap-tag from a single capability into
// per-allocator latency tiers.  Composes orthogonally with the
// five sister chain wrappers (DetSafe / HotPath / Wait / MemOrder
// / Progress) via wrapper-nesting per 28_04 §4.7.
//
// THE LOAD-BEARING USE CASE: type-fences CLAUDE.md §VIII memory-
// plan discipline at every call site.  A function declared
// `requires AllocClass::satisfies<Arena>` rejects callees carrying
// `AllocClassTag_v::Heap` (or worse Mmap/HugePage) at compile
// time — replacing today's review-only enforcement with a per-call
// type-fence.
//
//   Substrate: Graded<ModalityKind::Absolute,
//                     AllocClassLattice::At<Tag>,
//                     T>
//   Regime:    1 (zero-cost EBO collapse — At<Tag>::element_type
//                 is empty, sizeof(AllocClass<Tag, T>) == sizeof(T))
//
//   Use cases (per 28_04 §4.3.6 + CLAUDE.md §VIII):
//     - Hot-path scratch buffers — declared `Stack`
//     - PoolAllocator::alloc_slot return type — declared `Pool`
//     - Arena::alloc_obj return type — declared `Arena`
//     - TraceRing/MetaLog backing storage at init — declared `Heap`
//     - Cipher cold-tier mmap regions — declared `Mmap`
//     - TraceRing huge-page-backed buffers — declared `HugePage`
//
//   The bug class caught: a function declared `Stack` accidentally
//   calling `malloc`.  Today caught by perf regression (the tail
//   latency spikes); with the wrapper, becomes a compile error at
//   the call boundary.  Equivalent for `Arena`-pinned hot-path
//   functions accidentally invoking jemalloc.
//
//   Axiom coverage:
//     TypeSafe — AllocClassTag_v is a strong scoped enum;
//                cross-tag mismatches are compile errors via the
//                relax<WeakerTag>() and satisfies<RequiredTag>
//                gates.
//     ThreadSafe — composes with HotPath / Wait / MemOrder for
//                  full hot-path admission fencing per CLAUDE.md
//                  §VIII (no malloc on hot path) + §IX (latency
//                  hierarchy).
//     MemSafe — defaulted copy/move; T's move semantics carry
//               through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//   Runtime cost:
//     sizeof(AllocClass<Tag, T>) == sizeof(T).
//
// ── Why Modality::Absolute ─────────────────────────────────────────
//
// An allocator-strategy pin is a STATIC property of WHICH ALLOCATOR
// the function uses.  Mirrors the five sister chain wrappers — all
// Absolute over At<>-pinned grades.
//
// ── Tag-conversion API: relax + satisfies ──────────────────────────
//
// AllocClass subsumption-direction (per AllocClassLattice.h):
//
//   Bottom = HugePage (weakest claim — uses heaviest setup).
//   Top    = Stack    (strongest claim — no allocator at all).
//
// For USE, the direction is REVERSED:
//
//   A producer at a STRONGER tag (Stack) satisfies a consumer at a
//   WEAKER tag (Arena).  Stronger no-allocation claim serves
//   weaker requirement.  An AllocClass<Stack, T> can be relaxed
//   to AllocClass<Arena, T> — the stack-only value trivially
//   satisfies the Arena-acceptance gate.
//
//   The converse is forbidden: an AllocClass<Heap, T> CANNOT
//   become an AllocClass<Stack, T> — the heap-allocated value
//   carries an allocator-call dependency that the stack discipline
//   forbids.  No `tighten()` method exists.
//
// API:
//   - relax<WeakerTag>() &  / && — convert to a less-strict tag;
//                                  compile error if WeakerTag > Tag.
//   - satisfies<RequiredTag>     — static predicate.
//   - tag (static constexpr)     — the pinned AllocClassTag_v
//                                  value.
//
// SEMANTIC NOTE on chain LINEARIZATION: per AllocClassLattice.h
// divergence (2), Pool ⊐ Arena reflects the spec's cost claim —
// Pool is structurally bounded (preallocated freelist), while
// Arena's bump can occasionally need a new chunk from heap.  The
// chain is for ADMISSION GATING, not a strict cost guarantee in
// every scenario.
//
// `Graded::weaken` on the substrate goes UP the lattice — that
// operation has NO MEANINGFUL SEMANTICS for a type-pinned tag and
// would be the LOAD-BEARING BUG: a Heap-tier value claiming Stack
// compliance would defeat the malloc-on-hot-path discipline.
// Hidden by the wrapper.
//
// See FOUND-G38 (algebra/lattices/AllocClassLattice.h) for the
// underlying substrate; 28_04_2026_effects.md §4.3.6 for the
// production-call-site rationale; CLAUDE.md §VIII for the memory-
// plan discipline this wrapper type-fences.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/AllocClassLattice.h>

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the AllocClassTag enum into the safety:: namespace under
// `AllocClassTag_v`.  No name collision — the wrapper class is
// `AllocClass`, not `AllocClassTag`.
using ::crucible::algebra::lattices::AllocClassLattice;
using AllocClassTag_v = ::crucible::algebra::lattices::AllocClassTag;

template <AllocClassTag_v Tag, typename T>
class [[nodiscard]] AllocClass {
public:
    using value_type   = T;
    using lattice_type = AllocClassLattice::At<Tag>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    static constexpr AllocClassTag_v tag = Tag;

private:
    graded_type impl_;

public:

    constexpr AllocClass() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit AllocClass(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit AllocClass(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    constexpr AllocClass(const AllocClass&)            = default;
    constexpr AllocClass(AllocClass&&)                 = default;
    constexpr AllocClass& operator=(const AllocClass&) = default;
    constexpr AllocClass& operator=(AllocClass&&)      = default;
    ~AllocClass()                                      = default;

    [[nodiscard]] friend constexpr bool operator==(
        AllocClass const& a, AllocClass const& b) noexcept(
        noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek() == b.peek();
    }

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }

    [[nodiscard]] constexpr T const& peek() const& noexcept {
        return impl_.peek();
    }

    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        return impl_.peek_mut();
    }

    constexpr void swap(AllocClass& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(AllocClass& a, AllocClass& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    template <AllocClassTag_v RequiredTag>
    static constexpr bool satisfies = AllocClassLattice::leq(RequiredTag, Tag);

    template <AllocClassTag_v WeakerTag>
        requires (AllocClassLattice::leq(WeakerTag, Tag))
    [[nodiscard]] constexpr AllocClass<WeakerTag, T> relax() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return AllocClass<WeakerTag, T>{this->peek()};
    }

    template <AllocClassTag_v WeakerTag>
        requires (AllocClassLattice::leq(WeakerTag, Tag))
    [[nodiscard]] constexpr AllocClass<WeakerTag, T> relax() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return AllocClass<WeakerTag, T>{
            std::move(impl_).consume()};
    }
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace alloc_class {
    template <typename T> using Stack    = AllocClass<AllocClassTag_v::Stack,    T>;
    template <typename T> using Pool     = AllocClass<AllocClassTag_v::Pool,     T>;
    template <typename T> using Arena    = AllocClass<AllocClassTag_v::Arena,    T>;
    template <typename T> using Heap     = AllocClass<AllocClassTag_v::Heap,     T>;
    template <typename T> using Mmap     = AllocClass<AllocClassTag_v::Mmap,     T>;
    template <typename T> using HugePage = AllocClass<AllocClassTag_v::HugePage, T>;
}  // namespace alloc_class

// ── Layout invariants ───────────────────────────────────────────────
namespace detail::alloc_class_layout {

template <typename T> using StackA = AllocClass<AllocClassTag_v::Stack, T>;
template <typename T> using ArenaA = AllocClass<AllocClassTag_v::Arena, T>;
template <typename T> using HeapA  = AllocClass<AllocClassTag_v::Heap,  T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(StackA, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(StackA, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(StackA, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ArenaA, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ArenaA, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HeapA,  int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HeapA,  double);

}  // namespace detail::alloc_class_layout

static_assert(sizeof(AllocClass<AllocClassTag_v::Stack,    int>)    == sizeof(int));
static_assert(sizeof(AllocClass<AllocClassTag_v::Pool,     int>)    == sizeof(int));
static_assert(sizeof(AllocClass<AllocClassTag_v::Arena,    int>)    == sizeof(int));
static_assert(sizeof(AllocClass<AllocClassTag_v::Heap,     int>)    == sizeof(int));
static_assert(sizeof(AllocClass<AllocClassTag_v::Mmap,     int>)    == sizeof(int));
static_assert(sizeof(AllocClass<AllocClassTag_v::HugePage, int>)    == sizeof(int));
static_assert(sizeof(AllocClass<AllocClassTag_v::Stack,    double>) == sizeof(double));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::alloc_class_self_test {

using StackInt    = AllocClass<AllocClassTag_v::Stack,    int>;
using PoolInt     = AllocClass<AllocClassTag_v::Pool,     int>;
using ArenaInt    = AllocClass<AllocClassTag_v::Arena,    int>;
using HeapInt     = AllocClass<AllocClassTag_v::Heap,     int>;
using MmapInt     = AllocClass<AllocClassTag_v::Mmap,     int>;
using HugePageInt = AllocClass<AllocClassTag_v::HugePage, int>;

inline constexpr StackInt a_default{};
static_assert(a_default.peek() == 0);
static_assert(a_default.tag == AllocClassTag_v::Stack);

inline constexpr StackInt a_explicit{42};
static_assert(a_explicit.peek() == 42);

inline constexpr StackInt a_in_place{std::in_place, 7};
static_assert(a_in_place.peek() == 7);

static_assert(StackInt::tag    == AllocClassTag_v::Stack);
static_assert(PoolInt::tag     == AllocClassTag_v::Pool);
static_assert(ArenaInt::tag    == AllocClassTag_v::Arena);
static_assert(HeapInt::tag     == AllocClassTag_v::Heap);
static_assert(MmapInt::tag     == AllocClassTag_v::Mmap);
static_assert(HugePageInt::tag == AllocClassTag_v::HugePage);

// Chain reminder (per AllocClassLattice):
//   HugePage ⊑ Mmap ⊑ Heap ⊑ Arena ⊑ Pool ⊑ Stack
// satisfies<R> means leq(R, Self).  A wrapper at tier T satisfies
// all tiers ≤ T (self + everything DOWN the chain).

// Stack (chain top) satisfies every consumer.  THE LOAD-BEARING
// POSITIVE TEST.
static_assert(StackInt::satisfies<AllocClassTag_v::Stack>);
static_assert(StackInt::satisfies<AllocClassTag_v::Pool>);
static_assert(StackInt::satisfies<AllocClassTag_v::Arena>);
static_assert(StackInt::satisfies<AllocClassTag_v::Heap>);
static_assert(StackInt::satisfies<AllocClassTag_v::Mmap>);
static_assert(StackInt::satisfies<AllocClassTag_v::HugePage>);

// Pool satisfies Pool + Arena + Heap + Mmap + HugePage; FAILS on Stack.
static_assert( PoolInt::satisfies<AllocClassTag_v::Pool>);
static_assert( PoolInt::satisfies<AllocClassTag_v::Arena>);
static_assert( PoolInt::satisfies<AllocClassTag_v::Heap>);
static_assert( PoolInt::satisfies<AllocClassTag_v::Mmap>);
static_assert( PoolInt::satisfies<AllocClassTag_v::HugePage>);
static_assert(!PoolInt::satisfies<AllocClassTag_v::Stack>,
    "Pool MUST NOT satisfy Stack — Stack is the strongest no-"
    "allocation claim, Pool still calls into a freelist.");

// Arena satisfies Arena + Heap + Mmap + HugePage; FAILS on Pool, Stack.
static_assert( ArenaInt::satisfies<AllocClassTag_v::Arena>);
static_assert( ArenaInt::satisfies<AllocClassTag_v::Heap>);
static_assert( ArenaInt::satisfies<AllocClassTag_v::Mmap>);
static_assert( ArenaInt::satisfies<AllocClassTag_v::HugePage>);
static_assert(!ArenaInt::satisfies<AllocClassTag_v::Pool>,
    "Arena MUST NOT satisfy Pool — Pool is structurally bounded "
    "(preallocated freelist), Arena's bump can occasionally hit "
    "the cold-path new-chunk acquisition.");
static_assert(!ArenaInt::satisfies<AllocClassTag_v::Stack>);

// Heap satisfies Heap + Mmap + HugePage; FAILS on Arena, Pool, Stack —
// THIS IS THE LOAD-BEARING REJECTION for hot-path admission gates
// per CLAUDE.md §VIII (no malloc on hot path).
static_assert( HeapInt::satisfies<AllocClassTag_v::Heap>);
static_assert( HeapInt::satisfies<AllocClassTag_v::Mmap>);
static_assert( HeapInt::satisfies<AllocClassTag_v::HugePage>);
static_assert(!HeapInt::satisfies<AllocClassTag_v::Arena>,
    "Heap MUST NOT satisfy Arena — this is the load-bearing "
    "rejection that hot-path admission gates depend on (CLAUDE.md "
    "§VIII no-malloc-on-hot-path discipline).  If this fires, "
    "jemalloc calls can silently flow into hot-path TraceRing / "
    "MetaLog / Vigil call sites, breaking the per-call shape "
    "budget (jemalloc ~50-200ns vs Arena bump ~2-3ns).");
static_assert(!HeapInt::satisfies<AllocClassTag_v::Pool>);
static_assert(!HeapInt::satisfies<AllocClassTag_v::Stack>);

// HugePage (chain bottom) satisfies only HugePage.
static_assert( HugePageInt::satisfies<AllocClassTag_v::HugePage>);
static_assert(!HugePageInt::satisfies<AllocClassTag_v::Mmap>);
static_assert(!HugePageInt::satisfies<AllocClassTag_v::Heap>);
static_assert(!HugePageInt::satisfies<AllocClassTag_v::Arena>);
static_assert(!HugePageInt::satisfies<AllocClassTag_v::Pool>);
static_assert(!HugePageInt::satisfies<AllocClassTag_v::Stack>);

// ── relax<WeakerTag> — DOWN-the-lattice conversion ───────────────
inline constexpr auto from_stack_to_pool =
    StackInt{42}.relax<AllocClassTag_v::Pool>();
static_assert(from_stack_to_pool.peek() == 42);
static_assert(from_stack_to_pool.tag == AllocClassTag_v::Pool);

inline constexpr auto from_stack_to_hugepage =
    StackInt{99}.relax<AllocClassTag_v::HugePage>();
static_assert(from_stack_to_hugepage.peek() == 99);
static_assert(from_stack_to_hugepage.tag == AllocClassTag_v::HugePage);

inline constexpr auto from_arena_to_heap =
    ArenaInt{7}.relax<AllocClassTag_v::Heap>();
static_assert(from_arena_to_heap.peek() == 7);

inline constexpr auto from_pool_to_self =
    PoolInt{8}.relax<AllocClassTag_v::Pool>();
static_assert(from_pool_to_self.peek() == 8);

template <typename W, AllocClassTag_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert( can_relax<StackInt,    AllocClassTag_v::Pool>);     // ✓ down
static_assert( can_relax<StackInt,    AllocClassTag_v::HugePage>); // ✓ down (full chain)
static_assert( can_relax<StackInt,    AllocClassTag_v::Stack>);    // ✓ self
static_assert( can_relax<ArenaInt,    AllocClassTag_v::Heap>);     // ✓ down
static_assert( can_relax<ArenaInt,    AllocClassTag_v::Arena>);    // ✓ self
static_assert(!can_relax<ArenaInt,    AllocClassTag_v::Pool>,        // ✗ up
    "relax<Pool> on an Arena-pinned wrapper MUST be rejected — "
    "Pool is structurally bounded; Arena's bump can fail cold-path.");
static_assert(!can_relax<ArenaInt,    AllocClassTag_v::Stack>);    // ✗ up
static_assert(!can_relax<HeapInt,     AllocClassTag_v::Arena>,     // ✗ up
    "relax<Arena> on a Heap-pinned wrapper MUST be rejected — "
    "THIS IS THE LOAD-BEARING REJECTION for the no-malloc-on-hot-"
    "path discipline.");
static_assert(!can_relax<HeapInt,     AllocClassTag_v::Stack>);
static_assert(!can_relax<HugePageInt, AllocClassTag_v::Mmap>);     // ✗ up
// HugePage reflexivity at the bottom.
static_assert( can_relax<HugePageInt, AllocClassTag_v::HugePage>); // ✓ self at bottom

// ── Diagnostic forwarders ─────────────────────────────────────────
static_assert(StackInt::value_type_name().ends_with("int"));
static_assert(StackInt::lattice_name()    == "AllocClassLattice::At<Stack>");
static_assert(PoolInt::lattice_name()     == "AllocClassLattice::At<Pool>");
static_assert(ArenaInt::lattice_name()    == "AllocClassLattice::At<Arena>");
static_assert(HeapInt::lattice_name()     == "AllocClassLattice::At<Heap>");
static_assert(MmapInt::lattice_name()     == "AllocClassLattice::At<Mmap>");
static_assert(HugePageInt::lattice_name() == "AllocClassLattice::At<HugePage>");

// ── swap, peek_mut, equality ─────────────────────────────────────
[[nodiscard]] consteval bool swap_exchanges_within_same_tag() noexcept {
    StackInt a{10};
    StackInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_tag());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    StackInt a{10};
    StackInt b{20};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    StackInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    StackInt a{42};
    StackInt b{42};
    StackInt c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

struct NoEqualityT {
    int v{0};
    NoEqualityT() = default;
    explicit NoEqualityT(int x) : v{x} {}
    NoEqualityT(NoEqualityT&&) = default;
    NoEqualityT& operator=(NoEqualityT&&) = default;
    NoEqualityT(NoEqualityT const&) = delete;
    NoEqualityT& operator=(NoEqualityT const&) = delete;
};

template <typename W>
concept can_equality_compare = requires(W const& a, W const& b) {
    { a == b } -> std::convertible_to<bool>;
};

static_assert( can_equality_compare<StackInt>);
static_assert(!can_equality_compare<AllocClass<AllocClassTag_v::Stack, NoEqualityT>>);

static_assert(!std::is_copy_constructible_v<AllocClass<AllocClassTag_v::Stack, NoEqualityT>>,
    "AllocClass<Tag, T> must transitively inherit T's copy-deletion.");
static_assert(std::is_move_constructible_v<AllocClass<AllocClassTag_v::Stack, NoEqualityT>>);

// ── relax reflexivity + move-only ────────────────────────────────
[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    StackInt a{99};
    auto b = a.relax<AllocClassTag_v::Stack>();
    return b.peek() == 99 && b.tag == AllocClassTag_v::Stack;
}
static_assert(relax_to_self_is_identity());

struct MoveOnlyT {
    int v{0};
    constexpr MoveOnlyT() = default;
    constexpr explicit MoveOnlyT(int x) : v{x} {}
    constexpr MoveOnlyT(MoveOnlyT&&) = default;
    constexpr MoveOnlyT& operator=(MoveOnlyT&&) = default;
    MoveOnlyT(MoveOnlyT const&) = delete;
    MoveOnlyT& operator=(MoveOnlyT const&) = delete;
};

template <typename W, AllocClassTag_v T_target>
concept can_relax_rvalue = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};
template <typename W, AllocClassTag_v T_target>
concept can_relax_lvalue = requires(W const& w) {
    { w.template relax<T_target>() };
};

using StackMoveOnly = AllocClass<AllocClassTag_v::Stack, MoveOnlyT>;
static_assert( can_relax_rvalue<StackMoveOnly, AllocClassTag_v::Pool>,
    "relax<>() && MUST work for move-only T.");
static_assert(!can_relax_lvalue<StackMoveOnly, AllocClassTag_v::Pool>,
    "relax<>() const& on move-only T MUST be rejected.");

[[nodiscard]] consteval bool relax_move_only_works() noexcept {
    StackMoveOnly src{MoveOnlyT{77}};
    auto dst = std::move(src).relax<AllocClassTag_v::Pool>();
    return dst.peek().v == 77 && dst.tag == AllocClassTag_v::Pool;
}
static_assert(relax_move_only_works());

// ── Stable name + aliases ─────────────────────────────────────────
static_assert(StackInt::value_type_name().size() > 0);
static_assert(StackInt::lattice_name().starts_with("AllocClassLattice::At<"));

static_assert(alloc_class::Stack<int>::tag    == AllocClassTag_v::Stack);
static_assert(alloc_class::Pool<int>::tag     == AllocClassTag_v::Pool);
static_assert(alloc_class::Arena<int>::tag    == AllocClassTag_v::Arena);
static_assert(alloc_class::Heap<int>::tag     == AllocClassTag_v::Heap);
static_assert(alloc_class::Mmap<int>::tag     == AllocClassTag_v::Mmap);
static_assert(alloc_class::HugePage<int>::tag == AllocClassTag_v::HugePage);

static_assert(std::is_same_v<alloc_class::Stack<double>,
                             AllocClass<AllocClassTag_v::Stack, double>>);

// ── TWO admission-gate simulations — LOAD-BEARING ────────────────
//
// (1) Hot-path admission gate (CLAUDE.md §VIII no-malloc):
//     declares `requires AllocClass::satisfies<Arena>`.  Admits
//     Stack/Pool/Arena (the per-call-shape-respecting tiers);
//     rejects Heap/Mmap/HugePage.
template <typename W>
concept is_hot_path_alloc_admissible =
    W::template satisfies<AllocClassTag_v::Arena>;

static_assert( is_hot_path_alloc_admissible<StackInt>);
static_assert( is_hot_path_alloc_admissible<PoolInt>);
static_assert( is_hot_path_alloc_admissible<ArenaInt>,
    "Arena-tier value MUST pass the hot-path alloc gate (Arena is "
    "the boundary).");
static_assert(!is_hot_path_alloc_admissible<HeapInt>,
    "Heap-tier value MUST be REJECTED at the hot-path alloc gate "
    "— THIS IS THE LOAD-BEARING TEST for CLAUDE.md §VIII no-"
    "malloc-on-hot-path discipline.");
static_assert(!is_hot_path_alloc_admissible<MmapInt>);
static_assert(!is_hot_path_alloc_admissible<HugePageInt>);

// (2) Stack-only gate (strictest — for inlinable hot-loop bodies).
template <typename W>
concept is_stack_only_admissible =
    W::template satisfies<AllocClassTag_v::Stack>;

static_assert( is_stack_only_admissible<StackInt>);
static_assert(!is_stack_only_admissible<PoolInt>,
    "Pool MUST be REJECTED at the strict stack-only gate — Pool "
    "still calls into a freelist (~2-5ns), not free.");
static_assert(!is_stack_only_admissible<ArenaInt>);
static_assert(!is_stack_only_admissible<HeapInt>);

// ── Runtime smoke test ─────────────────────────────────────────────
inline void runtime_smoke_test() {
    StackInt a{};
    StackInt b{42};
    StackInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    if (StackInt::tag != AllocClassTag_v::Stack) {
        std::abort();
    }

    StackInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    StackInt sx{1};
    StackInt sy{2};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    StackInt source{77};
    auto relaxed_copy = source.relax<AllocClassTag_v::Pool>();
    auto relaxed_move = std::move(source).relax<AllocClassTag_v::Heap>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    [[maybe_unused]] bool s1 = StackInt::satisfies<AllocClassTag_v::Arena>;
    [[maybe_unused]] bool s2 = HeapInt::satisfies<AllocClassTag_v::Stack>;

    StackInt eq_a{42};
    StackInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    StackInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    alloc_class::Stack<int>    alias_stack{123};
    alloc_class::Arena<int>    alias_arena{456};
    alloc_class::HugePage<int> alias_huge{789};
    [[maybe_unused]] auto sv = alias_stack.peek();
    [[maybe_unused]] auto av = alias_arena.peek();
    [[maybe_unused]] auto hv = alias_huge.peek();

    [[maybe_unused]] bool can_stack_pass = is_hot_path_alloc_admissible<StackInt>;
    [[maybe_unused]] bool can_heap_pass  = is_hot_path_alloc_admissible<HeapInt>;
    [[maybe_unused]] bool can_strict     = is_stack_only_admissible<StackInt>;
}

}  // namespace detail::alloc_class_self_test

}  // namespace crucible::safety
