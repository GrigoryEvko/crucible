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

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/Enumerate.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

enum class AllocClassTag : std::uint8_t {
    HugePage = 0,  // mmap with a huge-page hint
    Mmap = 1,  // mmap(2)
    Heap = 2,  // malloc or operator new
    Arena = 3,  // bump pointer, freed in bulk at an epoch boundary
    Pool = 4,  // a slot from a preallocated freelist
    Stack = 5,  // no allocator call at all
};

inline constexpr std::size_t alloc_class_tag_count = ::foundation::reflect::enum_count<AllocClassTag>;

// The identifier of t, or "<unknown AllocClassTag>" for a value outside
// the enum.
[[nodiscard]] consteval std::string_view alloc_class_tag_name(AllocClassTag t) noexcept {
    return ::foundation::reflect::enum_name(t);
}

struct AllocClassLattice : ChainLatticeOps<AllocClassTag> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return AllocClassTag::HugePage; }
    [[nodiscard]] static constexpr element_type top() noexcept { return AllocClassTag::Stack; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "AllocClassLattice"; }

    template <AllocClassTag T>
    struct AtElement : PinnedElement<T> {
        using alloc_class_tag_value_type = AllocClassTag;
    };

    template <AllocClassTag T>
    struct At : PinnedAt<AllocClassLattice, T, AtElement<T>> {
        static constexpr AllocClassTag tag = T;
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

static_assert(verify_chain_lattice<AllocClassLattice>(),
              "AllocClassLattice: the chain order, the pinned grades or the "
              "reflected names diverged from the AllocClassTag enumerator list.");

static_assert(!UnboundedLattice<AllocClassLattice>);
static_assert(!Semiring<AllocClassLattice>);

static_assert(AllocClassLattice::bottom() == AllocClassTag::HugePage);
static_assert(AllocClassLattice::top() == AllocClassTag::Stack);

static_assert(AllocClassLattice::name() == "AllocClassLattice");
static_assert(alloc_class_tag::HugePageAlloc::name() == "AllocClassLattice::At<HugePage>");
static_assert(alloc_class_tag::StackAlloc::name() == "AllocClassLattice::At<Stack>");
static_assert(AllocClassLattice::At<static_cast<AllocClassTag>(255)>::name() == "AllocClassLattice::At<?>");

static_assert(alloc_class_tag_name(AllocClassTag::Arena) == "Arena");
static_assert(alloc_class_tag_name(static_cast<AllocClassTag>(255)) == "<unknown AllocClassTag>");

static_assert(alloc_class_tag::HugePageAlloc::tag == AllocClassTag::HugePage);
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

}  // namespace detail::alloc_class_lattice_self_test

}  // namespace foundation::algebra::lattices
