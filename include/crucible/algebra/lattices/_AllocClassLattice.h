#pragma once

// Chain over the allocation strategy a function uses.  bottom is
// HugePage and top is Stack.  A cheaper allocation is the stronger claim
// and sits higher, so leq(weak, strong) reads "a consumer that tolerates
// the weaker strategy accepts a stronger provider".  join takes the
// cheaper of two providers and meet the more expensive.
//
// Pool sits above Arena.  A pool takes a slot from a preallocated
// freelist and never allocates, while an arena's bump pointer can run
// out and need a fresh chunk, so only the pool is bounded.
//
// The Tag suffix keeps the enum's name clear of the wrapper that carries
// it.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>
#include <crucible/algebra/lattices/_ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

enum class AllocClassTag : std::uint8_t {
    HugePage = 0,  // mmap with a huge-page hint
    Mmap = 1,  // mmap(2)
    Heap = 2,  // malloc or operator new
    Arena = 3,  // bump pointer, freed in bulk at an epoch boundary
    Pool = 4,  // a slot from a preallocated freelist
    Stack = 5,  // no allocator call at all
};

inline constexpr std::size_t alloc_class_tag_count = std::meta::enumerators_of(^^AllocClassTag).size();

[[nodiscard]] consteval std::string_view alloc_class_tag_name(AllocClassTag t) noexcept {
    switch (t) {
        case AllocClassTag::HugePage:
            return "HugePage";
        case AllocClassTag::Mmap:
            return "Mmap";
        case AllocClassTag::Heap:
            return "Heap";
        case AllocClassTag::Arena:
            return "Arena";
        case AllocClassTag::Pool:
            return "Pool";
        case AllocClassTag::Stack:
            return "Stack";
        default:
            return std::string_view{"<unknown AllocClassTag>"};
    }
}

struct AllocClassLattice : ChainLatticeOps<AllocClassTag> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return AllocClassTag::HugePage; }
    [[nodiscard]] static constexpr element_type top() noexcept { return AllocClassTag::Stack; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "AllocClassLattice"; }

    template <AllocClassTag T>
    struct At {
        struct element_type {
            using alloc_class_tag_value_type = AllocClassTag;
            [[nodiscard]] constexpr operator alloc_class_tag_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr AllocClassTag tag = T;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case AllocClassTag::HugePage:
                    return "AllocClassLattice::At<HugePage>";
                case AllocClassTag::Mmap:
                    return "AllocClassLattice::At<Mmap>";
                case AllocClassTag::Heap:
                    return "AllocClassLattice::At<Heap>";
                case AllocClassTag::Arena:
                    return "AllocClassLattice::At<Arena>";
                case AllocClassTag::Pool:
                    return "AllocClassLattice::At<Pool>";
                case AllocClassTag::Stack:
                    return "AllocClassLattice::At<Stack>";
                default:
                    return "AllocClassLattice::At<?>";
            }
        }
    };
};

namespace alloc_class_tag {
using HugePageAlloc = AllocClassLattice::At<AllocClassTag::HugePage>;
using MmapAlloc = AllocClassLattice::At<AllocClassTag::Mmap>;
using HeapAlloc = AllocClassLattice::At<AllocClassTag::Heap>;
using ArenaAlloc = AllocClassLattice::At<AllocClassTag::Arena>;
using PoolAlloc = AllocClassLattice::At<AllocClassTag::Pool>;
using StackAlloc = AllocClassLattice::At<AllocClassTag::Stack>;
}  // namespace alloc_class_tag

namespace detail::alloc_class_lattice_self_test {

static_assert(alloc_class_tag_count == 6, "AllocClassTag catalog diverged from {HugePage, Mmap, Heap, "
                                          "Arena, Pool, Stack}.  Confirm intent and update the hot-path "
                                          "admission gates.");

[[nodiscard]] consteval bool every_alloc_class_tag_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^AllocClassTag));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (alloc_class_tag_name([:en:]) == std::string_view{"<unknown AllocClassTag>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_alloc_class_tag_has_name(), "alloc_class_tag_name() switch missing an arm for at least one "
                                                "tag.");

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

static_assert(verify_chain_lattice_exhaustive<AllocClassLattice>(),
              "AllocClassLattice chain-order lattice axioms fail at some triple.");
static_assert(verify_chain_lattice_distributive_exhaustive<AllocClassLattice>(),
              "AllocClassLattice chain fails distributivity at some triple.");

static_assert(AllocClassLattice::leq(AllocClassTag::HugePage, AllocClassTag::Mmap));
static_assert(AllocClassLattice::leq(AllocClassTag::Mmap, AllocClassTag::Heap));
static_assert(AllocClassLattice::leq(AllocClassTag::Heap, AllocClassTag::Arena));
static_assert(AllocClassLattice::leq(AllocClassTag::Arena, AllocClassTag::Pool));
static_assert(AllocClassLattice::leq(AllocClassTag::Pool, AllocClassTag::Stack));
static_assert(AllocClassLattice::leq(AllocClassTag::HugePage, AllocClassTag::Stack));
static_assert(!AllocClassLattice::leq(AllocClassTag::Stack, AllocClassTag::HugePage));
static_assert(!AllocClassLattice::leq(AllocClassTag::Stack, AllocClassTag::Pool));
static_assert(!AllocClassLattice::leq(AllocClassTag::Pool, AllocClassTag::Arena));
static_assert(!AllocClassLattice::leq(AllocClassTag::Heap, AllocClassTag::HugePage));

static_assert(AllocClassLattice::bottom() == AllocClassTag::HugePage);
static_assert(AllocClassLattice::top() == AllocClassTag::Stack);

static_assert(AllocClassLattice::join(AllocClassTag::HugePage, AllocClassTag::Stack) == AllocClassTag::Stack);
static_assert(AllocClassLattice::join(AllocClassTag::Heap, AllocClassTag::Arena) == AllocClassTag::Arena);
static_assert(AllocClassLattice::meet(AllocClassTag::HugePage, AllocClassTag::Stack) == AllocClassTag::HugePage);
static_assert(AllocClassLattice::meet(AllocClassTag::Pool, AllocClassTag::Stack) == AllocClassTag::Pool);

static_assert(AllocClassLattice::name() == "AllocClassLattice");
static_assert(alloc_class_tag::HugePageAlloc::name() == "AllocClassLattice::At<HugePage>");
static_assert(alloc_class_tag::MmapAlloc::name() == "AllocClassLattice::At<Mmap>");
static_assert(alloc_class_tag::HeapAlloc::name() == "AllocClassLattice::At<Heap>");
static_assert(alloc_class_tag::ArenaAlloc::name() == "AllocClassLattice::At<Arena>");
static_assert(alloc_class_tag::PoolAlloc::name() == "AllocClassLattice::At<Pool>");
static_assert(alloc_class_tag::StackAlloc::name() == "AllocClassLattice::At<Stack>");

[[nodiscard]] consteval bool every_at_alloc_class_tag_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^AllocClassTag));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (AllocClassLattice::At<([:en:])>::name() == std::string_view{"AllocClassLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_alloc_class_tag_has_name(), "AllocClassLattice::At<T>::name() switch missing an arm for at "
                                                   "least one tag.");

static_assert(alloc_class_tag::HugePageAlloc::tag == AllocClassTag::HugePage);
static_assert(alloc_class_tag::MmapAlloc::tag == AllocClassTag::Mmap);
static_assert(alloc_class_tag::HeapAlloc::tag == AllocClassTag::Heap);
static_assert(alloc_class_tag::ArenaAlloc::tag == AllocClassTag::Arena);
static_assert(alloc_class_tag::PoolAlloc::tag == AllocClassTag::Pool);
static_assert(alloc_class_tag::StackAlloc::tag == AllocClassTag::Stack);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

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

// Static assertions alone can mask consteval, SFINAE and inline-body
// defects.  These calls pass non-constant arguments.
inline void runtime_smoke_test() {
    AllocClassTag a = AllocClassTag::HugePage;
    AllocClassTag b = AllocClassTag::Stack;
    [[maybe_unused]] bool l1 = AllocClassLattice::leq(a, b);
    [[maybe_unused]] AllocClassTag j1 = AllocClassLattice::join(a, b);
    [[maybe_unused]] AllocClassTag m1 = AllocClassLattice::meet(a, b);
    [[maybe_unused]] AllocClassTag bot = AllocClassLattice::bottom();
    [[maybe_unused]] AllocClassTag topv = AllocClassLattice::top();

    AllocClassTag heap = AllocClassTag::Heap;
    AllocClassTag arena = AllocClassTag::Arena;
    [[maybe_unused]] AllocClassTag j2 = AllocClassLattice::join(heap, arena);
    [[maybe_unused]] AllocClassTag m2 = AllocClassLattice::meet(heap, arena);

    OneByteValue v{42};
    StackGraded<OneByteValue> initial{v, alloc_class_tag::StackAlloc::bottom()};
    auto widened = initial.weaken(alloc_class_tag::StackAlloc::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(alloc_class_tag::StackAlloc::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    alloc_class_tag::StackAlloc::element_type e{};
    [[maybe_unused]] AllocClassTag rec = e;
}

}  // namespace detail::alloc_class_lattice_self_test

}  // namespace crucible::algebra::lattices
