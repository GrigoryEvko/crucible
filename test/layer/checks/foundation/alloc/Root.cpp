// The compile-time checks of foundation/alloc/Root.h.

#include <foundation/alloc/Root.h>

#include <type_traits>

namespace foundation::alloc {

// The root has no destructor that runs at the end of the process, so no unit
// registers one and no end of the process unmaps the host window (THE ROOT).
static_assert(std::is_trivially_destructible_v<AllocRoot>);
static_assert(!std::is_copy_constructible_v<AllocRoot> && !std::is_copy_assignable_v<AllocRoot>);

// A constant expression builds the root, so a constinit variable of it runs
// no code at the start of the process.
namespace root_check {

inline constexpr bool kIsConstant = [] consteval {
    AllocRoot const root{};
    return root.window(kHostWindow).is_empty();
}();

}  // namespace root_check

static_assert(root_check::kIsConstant);

// THE HEAP TABLE: a slot of 20 KiB is five pages, and it holds the HeapState
// and the bin stacks of its heap.  The slots of 65,534 heaps take 1.25 GiB of
// address space, and a step of 2 MiB holds 102 whole slots.
static_assert(kHeapSlotBytes == 5 * 4096);
static_assert(sizeof(HeapState) + kBinStackBytes <= kHeapSlotBytes);
static_assert(kHeapTableSlotsBytes == (std::size_t{5} << 28));
static_assert(kHeapTableStepBytes / kHeapSlotBytes == 102);

}  // namespace foundation::alloc
