// The compile-time checks of foundation/algebra/lattices/AllocClassLattice.h.

#include <foundation/algebra/lattices/AllocClassLattice.h>

namespace foundation::algebra::lattices {

namespace detail::alloc_class_lattice_self_test {

static_assert(::foundation::reflect::enum_count<AllocClassTag> == 6,
              "AllocClassTag catalog diverged from {HugePage, Mmap, Heap, Arena, Pool, Stack}.  Confirm intent and "
              "update the hot-path admission gates.");

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

static_assert(alloc_class_tag::HugePageAlloc::tag == AllocClassTag::HugePage);
static_assert(alloc_class_tag::StackAlloc::tag == AllocClassTag::Stack);

}  // namespace detail::alloc_class_lattice_self_test

}  // namespace foundation::algebra::lattices
