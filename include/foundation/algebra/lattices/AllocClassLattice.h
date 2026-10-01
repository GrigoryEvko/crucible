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

#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/EnumName.h>

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

// A cheaper allocation is the stronger claim.
struct AllocClassLattice : EnumChainLattice<AllocClassLattice, AllocClassTag, ClaimOrientation::stronger_is_higher> {
    template <AllocClassTag T>
    struct At : PinnedAt<AllocClassLattice, T> {
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

}  // namespace foundation::algebra::lattices
