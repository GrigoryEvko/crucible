#pragma once

// ── crucible::algebra::lattices::AllocClassLattice ──────────────────
//
// Six-tier total-order chain lattice over the allocator-strategy
// cost spectrum.  The grading axis underlying the AllocClass wrapper
// from 28_04_2026_effects.md §4.3.6.  Refines `Effect::Alloc` cap-
// tag into per-allocator latency tiers.
//
// THE LOAD-BEARING USE CASE: type-fences the discipline today
// enforced only by review and perf regression — a function declared
// `requires AllocClass::satisfies<Arena>` rejects callees carrying
// `AllocClassTag::Heap` (or worse Mmap/HugePage) at compile time.
// Today TraceRing / MetaLog / Vigil hot paths rely on reviewers
// catching accidental `malloc` calls; AllocClass makes the
// discipline a per-call type-fence.
//
// Composes orthogonally with the five sister chain lattices via
// wrapper-nesting per 28_04 §4.7.  The canonical foreground hot-
// path stack-only allocator:
//
//     HotPath<Hot, AllocClass<Stack, T>>
//
// ── The classification ──────────────────────────────────────────────
//
// Each tier names a CLASS of allocation strategy a function uses.
// A function declared at tier T promises to use ONLY allocators at
// tier T or stronger (cheaper / lower latency).
//
//     Stack    — Stack allocation only.  Compiler-determined
//                 lifetime, no allocator call at all.  ~0-1ns
//                 (instruction-level).  The strongest allocation-
//                 cost claim.  Production: hot-path scratch
//                 buffers, std::array<T, N> with small N, structs
//                 by value.
//     Pool     — Pool allocator with preallocated chunks.  ~2-5ns
//                 amortized (freelist grab + slot reuse).  No
//                 fragmentation, no syscall, bounded latency.
//                 Production: PoolAllocator::alloc_slot for
//                 fixed-size slot reuse (e.g. ConductorTensorImpl).
//     Arena    — Bump-pointer arena.  ~2-3ns per alloc; bulk-free
//                 at epoch boundary.  Occasionally hits cold path
//                 for new chunk acquisition (~50ns when chunk
//                 exhausted).  Production: Arena::alloc_obj for
//                 DAG/graph nodes.
//     Heap     — jemalloc/malloc/operator new.  ~50-200ns under
//                 contention; possibly acquires lock.  Hot-path
//                 banned per CLAUDE.md §VIII (memory plan rules).
//                 Production: long-lived owned buffers (TraceRing
//                 backing storage, MetaLog buffers — but only at
//                 init, not per-iteration).
//     Mmap     — mmap(2) syscall.  ~10-50μs cold, then page-fault
//                 cost on first touch.  Production: huge backing
//                 buffers, file-backed regions (Cipher cold tier).
//     HugePage — mmap with MAP_HUGETLB or transparent huge page
//                 hint.  ~20-100μs initial setup; then 2MB-aligned
//                 page table entries.  Production: TraceRing huge-
//                 page backing for long-running deployments per
//                 CLAUDE.md §VIII.
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class AllocClassTag ∈ the six tiers above.
// Order:   HugePage ⊑ Mmap ⊑ Heap ⊑ Arena ⊑ Pool ⊑ Stack.
//
// Bottom = HugePage (weakest hot-path-friendliness claim;
//                    syscall-bearing setup cost).
// Top    = Stack    (strongest claim — no allocator call at all;
//                    admissible everywhere).
// Join   = max      (the more-restricted of two providers).
// Meet   = min      (the more-permissive of two providers).
//
// ── Direction convention ────────────────────────────────────────────
//
// Stronger guarantee = HIGHER in the lattice.  `leq(weak, strong)`
// reads "a weaker-budget consumer is satisfied by a stronger-budget
// provider" — a Stack-tier function can be invoked from any
// consumer site because it makes the strongest "I'm hot-path
// friendly" claim about allocation cost.
//
// This is the Crucible-standard subsumption-up direction, shared
// with the five sister chain lattices.
//
// ── DIVERGENCE FROM 28_04_2026_effects.md §4.3.6 SPEC ──────────────
//
// (1) ENUM ORDINAL INVERSION.  Spec puts Stack=0 ... HugePage=5
//     (mirroring "lower ordinal = cheaper").  This implementation
//     INVERTS to HugePage=0 ... Stack=5 to keep the chain direction
//     uniform with the four sister chain lattices that required
//     inversion (DetSafe/HotPath/Wait/MemOrder).  The SEMANTIC
//     contract from the spec ("Hot-path concept gates admit only
//     {Stack, Pool, Arena}") is preserved exactly:
//
//       AllocClass<Stack>::satisfies<Arena>  = leq(Arena, Stack)
//                                             = true ✓
//       AllocClass<Heap>::satisfies<Arena>   = leq(Arena, Heap)
//                                             = false ✓
//       AllocClass<Stack>::satisfies<Stack>  = true ✓
//       AllocClass<Mmap>::satisfies<Pool>    = false ✓
//
// (2) LINEARIZATION OF Pool vs Arena.  Spec puts Pool=1 < Arena=2,
//     i.e., Pool is CHEAPER than Arena in the spec's cost ordering.
//     Reflected in inverted form: Pool=4, Arena=3, so Pool ⊐ Arena
//     (Pool is HIGHER, makes a stronger claim).  Defensible because
//     Pool grabs from preallocated freelist (no allocation event),
//     while Arena's bump can occasionally need a new chunk from
//     heap.  Pool is structurally bounded; Arena has cold-path
//     amortization risk.
//
//   Axiom coverage:
//     TypeSafe — AllocClassTag is a strong scoped enum;
//                cross-tag mixing requires `std::to_underlying`.
//     ThreadSafe — composes with HotPath / Wait / MemOrder for
//                  full hot-path admission fencing per CLAUDE.md
//                  §VIII (memory rules: no malloc on hot path).
//   Runtime cost:
//     leq / join / meet — single integer compare and a select; the
//     six-element domain compiles to a 1-byte field with a single
//     branch.  When wrapped at a fixed type-level tag via
//     `AllocClassLattice::At<AllocClassTag::Stack>`, the grade
//     EBO-collapses to zero bytes.
//
// ── At<T> singleton sub-lattice ─────────────────────────────────────
//
// Mirrors the five sister chain lattices: a per-AllocClassTag
// singleton sub-lattice with empty element_type.
// `Graded<Absolute, AllocClassLattice::At<AllocClassTag::Stack>, T>`
// pays zero runtime overhead for the grade itself.
//
// See FOUND-G38 (this file) for the lattice; FOUND-G39
// (safety/AllocClass.h) for the type-pinned wrapper;
// 28_04_2026_effects.md §4.3.6 for the production-call-site
// rationale; CLAUDE.md §VIII for the memory-plan / no-malloc-on-
// hot-path discipline this lattice fences.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── AllocClassTag — chain over the allocator-strategy cost spectrum ─
//
// Ordinal convention: HugePage=0 (bottom) ... Stack=5 (top), per
// project convention (bottom=0=weakest hot-path friendliness).
// INVERTS the spec's ordinal hint; semantic contract preserved.
//
// Note the type is named `AllocClassTag` to avoid collision with
// the `AllocClass` wrapper.  The wrapper's `At<>` parameter is
// `AllocClassTag::Stack` etc.
enum class AllocClassTag : std::uint8_t {
    HugePage = 0,    // bottom: mmap + huge-page hint (~20-100μs setup)
    Mmap     = 1,    // mmap(2) syscall (~10-50μs)
    Heap     = 2,    // jemalloc/malloc/new (~50-200ns)
    Arena    = 3,    // bump-pointer arena (~2-3ns; cold-path risk)
    Pool     = 4,    // preallocated freelist (~2-5ns; bounded)
    Stack    = 5,    // top: instruction-level (~0-1ns; no allocator)
};

// Cardinality + diagnostic name via reflection.
inline constexpr std::size_t alloc_class_tag_count =
    std::meta::enumerators_of(^^AllocClassTag).size();

[[nodiscard]] consteval std::string_view alloc_class_tag_name(AllocClassTag t) noexcept {
    switch (t) {
        case AllocClassTag::HugePage: return "HugePage";
        case AllocClassTag::Mmap:     return "Mmap";
        case AllocClassTag::Heap:     return "Heap";
        case AllocClassTag::Arena:    return "Arena";
        case AllocClassTag::Pool:     return "Pool";
        case AllocClassTag::Stack:    return "Stack";
        default:                      return std::string_view{"<unknown AllocClassTag>"};
    }
}

// ── Full AllocClassLattice (chain order) ────────────────────────────
struct AllocClassLattice : ChainLatticeOps<AllocClassTag> {
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return AllocClassTag::HugePage;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return AllocClassTag::Stack;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "AllocClassLattice";
    }

    template <AllocClassTag T>
    struct At {
        struct element_type {
            using alloc_class_tag_value_type = AllocClassTag;
            [[nodiscard]] constexpr operator alloc_class_tag_value_type() const noexcept {
                return T;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr AllocClassTag tag = T;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case AllocClassTag::HugePage: return "AllocClassLattice::At<HugePage>";
                case AllocClassTag::Mmap:     return "AllocClassLattice::At<Mmap>";
                case AllocClassTag::Heap:     return "AllocClassLattice::At<Heap>";
                case AllocClassTag::Arena:    return "AllocClassLattice::At<Arena>";
                case AllocClassTag::Pool:     return "AllocClassLattice::At<Pool>";
                case AllocClassTag::Stack:    return "AllocClassLattice::At<Stack>";
                default:                      return "AllocClassLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace alloc_class_tag {
    using HugePageAlloc = AllocClassLattice::At<AllocClassTag::HugePage>;
    using MmapAlloc     = AllocClassLattice::At<AllocClassTag::Mmap>;
    using HeapAlloc     = AllocClassLattice::At<AllocClassTag::Heap>;
    using ArenaAlloc    = AllocClassLattice::At<AllocClassTag::Arena>;
    using PoolAlloc     = AllocClassLattice::At<AllocClassTag::Pool>;
    using StackAlloc    = AllocClassLattice::At<AllocClassTag::Stack>;
}  // namespace alloc_class_tag

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::alloc_class_lattice_self_test {

static_assert(alloc_class_tag_count == 6,
    "AllocClassTag catalog diverged from {HugePage, Mmap, Heap, "
    "Arena, Pool, Stack}; confirm intent and update the dispatcher's "
    "hot-path admission gates.");

[[nodiscard]] consteval bool every_alloc_class_tag_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^AllocClassTag));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (alloc_class_tag_name([:en:]) ==
            std::string_view{"<unknown AllocClassTag>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_alloc_class_tag_has_name(),
    "alloc_class_tag_name() switch missing arm for at least one tag.");

// Concept conformance.
static_assert(Lattice<AllocClassLattice>);
static_assert(BoundedLattice<AllocClassLattice>);
static_assert(Lattice<alloc_class_tag::HugePageAlloc>);
static_assert(Lattice<alloc_class_tag::MmapAlloc>);
static_assert(Lattice<alloc_class_tag::HeapAlloc>);
static_assert(Lattice<alloc_class_tag::ArenaAlloc>);
static_assert(Lattice<alloc_class_tag::PoolAlloc>);
static_assert(Lattice<alloc_class_tag::StackAlloc>);
static_assert(BoundedLattice<alloc_class_tag::StackAlloc>);

static_assert(!UnboundedLattice<AllocClassLattice>);
static_assert(!Semiring<AllocClassLattice>);

static_assert(std::is_empty_v<alloc_class_tag::StackAlloc::element_type>);
static_assert(std::is_empty_v<alloc_class_tag::HeapAlloc::element_type>);
static_assert(std::is_empty_v<alloc_class_tag::HugePageAlloc::element_type>);

// EXHAUSTIVE lattice-axiom + distributivity coverage over
// (AllocClassTag)³ = 216 triples each.
static_assert(verify_chain_lattice_exhaustive<AllocClassLattice>(),
    "AllocClassLattice's chain-order lattice axioms must hold at "
    "every (AllocClassTag)³ triple.");
static_assert(verify_chain_lattice_distributive_exhaustive<AllocClassLattice>(),
    "AllocClassLattice's chain order must satisfy distributivity at "
    "every (AllocClassTag)³ triple.");

// Direct order witnesses — chain is increasing, with Stack at top
// (cheapest) and HugePage at bottom (most expensive setup).
static_assert( AllocClassLattice::leq(AllocClassTag::HugePage, AllocClassTag::Mmap));
static_assert( AllocClassLattice::leq(AllocClassTag::Mmap,     AllocClassTag::Heap));
static_assert( AllocClassLattice::leq(AllocClassTag::Heap,     AllocClassTag::Arena));
static_assert( AllocClassLattice::leq(AllocClassTag::Arena,    AllocClassTag::Pool));
static_assert( AllocClassLattice::leq(AllocClassTag::Pool,     AllocClassTag::Stack));
static_assert( AllocClassLattice::leq(AllocClassTag::HugePage, AllocClassTag::Stack)); // transitive
static_assert(!AllocClassLattice::leq(AllocClassTag::Stack,    AllocClassTag::HugePage));
static_assert(!AllocClassLattice::leq(AllocClassTag::Stack,    AllocClassTag::Pool));
static_assert(!AllocClassLattice::leq(AllocClassTag::Pool,     AllocClassTag::Arena));
static_assert(!AllocClassLattice::leq(AllocClassTag::Heap,     AllocClassTag::HugePage));

// Pin bottom / top.
static_assert(AllocClassLattice::bottom() == AllocClassTag::HugePage);
static_assert(AllocClassLattice::top()    == AllocClassTag::Stack);

// Join strengthens (max); meet weakens (min).
static_assert(AllocClassLattice::join(AllocClassTag::HugePage, AllocClassTag::Stack)
              == AllocClassTag::Stack);
static_assert(AllocClassLattice::join(AllocClassTag::Heap, AllocClassTag::Arena)
              == AllocClassTag::Arena);
static_assert(AllocClassLattice::meet(AllocClassTag::HugePage, AllocClassTag::Stack)
              == AllocClassTag::HugePage);
static_assert(AllocClassLattice::meet(AllocClassTag::Pool, AllocClassTag::Stack)
              == AllocClassTag::Pool);

// Diagnostic names.
static_assert(AllocClassLattice::name() == "AllocClassLattice");
static_assert(alloc_class_tag::HugePageAlloc::name() == "AllocClassLattice::At<HugePage>");
static_assert(alloc_class_tag::MmapAlloc::name()     == "AllocClassLattice::At<Mmap>");
static_assert(alloc_class_tag::HeapAlloc::name()     == "AllocClassLattice::At<Heap>");
static_assert(alloc_class_tag::ArenaAlloc::name()    == "AllocClassLattice::At<Arena>");
static_assert(alloc_class_tag::PoolAlloc::name()     == "AllocClassLattice::At<Pool>");
static_assert(alloc_class_tag::StackAlloc::name()    == "AllocClassLattice::At<Stack>");

// Reflection-driven At<T>::name() coverage.
[[nodiscard]] consteval bool every_at_alloc_class_tag_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^AllocClassTag));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (AllocClassLattice::At<([:en:])>::name() ==
            std::string_view{"AllocClassLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_alloc_class_tag_has_name(),
    "AllocClassLattice::At<T>::name() switch missing an arm for at "
    "least one tag.");

// Convenience aliases resolve correctly.
static_assert(alloc_class_tag::HugePageAlloc::tag == AllocClassTag::HugePage);
static_assert(alloc_class_tag::MmapAlloc::tag     == AllocClassTag::Mmap);
static_assert(alloc_class_tag::HeapAlloc::tag     == AllocClassTag::Heap);
static_assert(alloc_class_tag::ArenaAlloc::tag    == AllocClassTag::Arena);
static_assert(alloc_class_tag::PoolAlloc::tag     == AllocClassTag::Pool);
static_assert(alloc_class_tag::StackAlloc::tag    == AllocClassTag::Stack);

// ── Layout invariants on Graded<...,At<T>,T_> ───────────────────────
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

template <typename T_>
using StackGraded = Graded<ModalityKind::Absolute, alloc_class_tag::StackAlloc, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(StackGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(StackGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(StackGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(StackGraded, double);

template <typename T_>
using ArenaGraded = Graded<ModalityKind::Absolute, alloc_class_tag::ArenaAlloc, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ArenaGraded, EightByteValue);

template <typename T_>
using HeapGraded = Graded<ModalityKind::Absolute, alloc_class_tag::HeapAlloc, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HeapGraded, EightByteValue);

// ── Runtime smoke test ─────────────────────────────────────────────
inline void runtime_smoke_test() {
    AllocClassTag a = AllocClassTag::HugePage;
    AllocClassTag b = AllocClassTag::Stack;
    [[maybe_unused]] bool          l1   = AllocClassLattice::leq(a, b);
    [[maybe_unused]] AllocClassTag j1   = AllocClassLattice::join(a, b);
    [[maybe_unused]] AllocClassTag m1   = AllocClassLattice::meet(a, b);
    [[maybe_unused]] AllocClassTag bot  = AllocClassLattice::bottom();
    [[maybe_unused]] AllocClassTag topv = AllocClassLattice::top();

    // Mid-tier ops — chain through Heap/Arena/Pool boundary.
    AllocClassTag heap  = AllocClassTag::Heap;
    AllocClassTag arena = AllocClassTag::Arena;
    [[maybe_unused]] AllocClassTag j2 = AllocClassLattice::join(heap, arena);   // Arena
    [[maybe_unused]] AllocClassTag m2 = AllocClassLattice::meet(heap, arena);   // Heap

    // Graded<Absolute, StackAlloc, T> at runtime.
    OneByteValue v{42};
    StackGraded<OneByteValue> initial{v, alloc_class_tag::StackAlloc::bottom()};
    auto widened   = initial.weaken(alloc_class_tag::StackAlloc::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(alloc_class_tag::StackAlloc::top());

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    alloc_class_tag::StackAlloc::element_type e{};
    [[maybe_unused]] AllocClassTag rec = e;
}

}  // namespace detail::alloc_class_lattice_self_test

}  // namespace crucible::algebra::lattices
