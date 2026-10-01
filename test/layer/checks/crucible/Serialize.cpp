// The compile-time checks of crucible/Serialize.h.

#include <crucible/Serialize.h>

namespace crucible {

static_assert(sizeof(LoadedRegionNode) == sizeof(RegionNode*));
static_assert(std::is_trivially_copy_constructible_v<LoadedRegionNode>);

namespace detail_ser {

static_assert(sizeof(Guard) == 12,
              "write_guard and read_guard move every field of Guard.  A new field needs a line in each.");

static_assert(sizeof(TensorSlot) == kTensorSlotWireBytes,
              "write_slot and read_slot move every field of TensorSlot.  A new field needs a line in each.");

}  // namespace detail_ser

}  // namespace crucible
