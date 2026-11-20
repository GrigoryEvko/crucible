// The compile-time checks of foundation/os/PageSource.h.

#include <foundation/os/PageSource.h>

#include <type_traits>

namespace foundation::os {

// A reservation owns its map: it moves, it does not copy, and no assignment
// gives it a second map.
static_assert(std::is_nothrow_move_constructible_v<Reservation>);
static_assert(!std::is_copy_constructible_v<Reservation>);
static_assert(!std::is_copy_assignable_v<Reservation>);
static_assert(!std::is_move_assignable_v<Reservation>);

// A range holds no address and owns nothing, so it copies.  Only the door
// makes one: no constructor of a range from its fields is public, and its
// copy is user-provided, so std::bit_cast and std::start_lifetime_as build
// none.
static_assert(std::is_nothrow_copy_constructible_v<ReservedRange>);
static_assert(std::is_nothrow_copy_constructible_v<CommittedRange>);
static_assert(!std::is_trivially_copyable_v<ReservedRange>);
static_assert(!std::is_trivially_copyable_v<CommittedRange>);
static_assert(!std::is_implicit_lifetime_v<ReservedRange>);
static_assert(!std::is_implicit_lifetime_v<CommittedRange>);
static_assert(!std::is_default_constructible_v<ReservedRange>);
static_assert(!std::is_default_constructible_v<CommittedRange>);
static_assert(!std::is_constructible_v<ReservedRange, std::uintptr_t, std::size_t, std::size_t>);
static_assert(!std::is_constructible_v<CommittedRange, std::uintptr_t, std::size_t, std::size_t>);

// A byte count and a cap are no numbers.
static_assert(!std::is_convertible_v<std::size_t, ByteCount>);
static_assert(!std::is_convertible_v<std::size_t, ReserveCap>);

// A node below kNumaNodeSetBits has a slot, and a set holds one bit for the
// node of each slot.
static_assert(numa_node_slot(NumaNodeId{0}).is_some());
static_assert(numa_node_slot(NumaNodeId{63}).is_some());
static_assert(numa_node_slot(NumaNodeId{64}).is_none());
static_assert(NumaNodeSet{}.is_empty());
static_assert(NumaNodeSet{}.with(core::index<3>()).mask_word() == 0x8);
static_assert(NumaNodeSet{}.with(core::index<1>()).with(core::index<2>()).contains(NumaNodeId{2}));
static_assert(!NumaNodeSet{}.with(core::index<1>()).contains(NumaNodeId{2}));
static_assert(!NumaNodeSet{}.contains(NumaNodeId{70}));

// A placement of an empty set is the unknown placement, and each kind keeps
// its node or its set.
static_assert(Placement::interleaved(NumaNodeSet{}) == Placement::unknown());
static_assert(Placement::unknown().kind() == PlacementKind::unknown);
static_assert(Placement::on_node(NumaNodeId{5}).kind() == PlacementKind::node);
static_assert(Placement::on_node(NumaNodeId{5}).node() == NumaNodeId{5});
static_assert(Placement::on_node(NumaNodeId{5}) != Placement::on_node(NumaNodeId{6}));
static_assert(Placement::interleaved(NumaNodeSet{}.with(core::index<1>())).kind() == PlacementKind::interleaved);
static_assert(Placement{} == Placement::unknown());

// The huge page of the door is a whole count of pages of each size that the
// two architectures use: 4 KiB, 16 KiB and 64 KiB.
static_assert(kPageSourceHugePageBytes % (std::size_t{64} << 10) == 0);

}  // namespace foundation::os
