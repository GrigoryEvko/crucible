// The compile-time checks of crucible/Serialize.h.

#include <crucible/Serialize.h>

namespace crucible {

// A loaded region holds a Ref, which is never null, and the tag adds no byte
// to it.
static_assert(std::is_same_v<std::remove_cvref_t<decltype(std::declval<LoadedRegionNode const&>().value())>,
                             ::fixy::Ref<RegionNode>>);
static_assert(sizeof(LoadedRegionNode) == sizeof(::fixy::Ref<RegionNode>));
static_assert(std::is_trivially_copy_constructible_v<LoadedRegionNode>);

namespace detail_ser {

static_assert(sizeof(Guard) == 12,
              "write_guard and read_guard move every field of Guard.  A new field needs a line in each.");

static_assert(sizeof(TensorSlot) == kTensorSlotWireBytes,
              "write_slot and read_slot move every field of TensorSlot.  A new field needs a line in each.");

}  // namespace detail_ser

}  // namespace crucible
