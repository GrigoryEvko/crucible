// The compile-time checks of crucible/Serialize.h.

#include <crucible/Serialize.h>

namespace crucible {

// A loaded region holds a Ref with the brand of the DAG arenas, which is
// never null, and the tag adds no byte to it.
static_assert(
    std::is_same_v<std::remove_cvref_t<decltype(std::declval<LoadedRegionNode const&>().value())>, DagRef<RegionNode>>);
static_assert(sizeof(LoadedRegionNode) == sizeof(DagRef<RegionNode>));
static_assert(std::is_trivially_copy_constructible_v<LoadedRegionNode>);

namespace detail_ser {

// The image of a guard and of a slot holds each field, and the format of
// the store pins its size.  A new field changes the image, which needs a new
// CDAG_VERSION.
static_assert(::fixy::encoded_size_v<Guard> == 12 && sizeof(Guard) == 12,
              "the image of a Guard is its 12 bytes.  A new field changes the format of the store.");

static_assert(kTensorSlotWireBytes == 40 && sizeof(TensorSlot) == kTensorSlotWireBytes,
              "the image of a TensorSlot is its 40 bytes.  A new field changes the format of the store.");

// write_meta writes the image of a TensorMeta, and read_meta decodes it as an
// UncheckedTensorMeta, so the two images have one size.  The test of the
// store compares their bytes.
static_assert(::fixy::encoded_size_v<TensorMeta> == 156 && ::fixy::encoded_size_v<UncheckedTensorMeta> == 156,
              "the image of a descriptor is its 156 bytes.  A new field changes the format of the store.");
static_assert(::fixy::Decodable<UncheckedTensorMeta> && !::fixy::Decodable<TensorMeta>);

}  // namespace detail_ser

}  // namespace crucible
