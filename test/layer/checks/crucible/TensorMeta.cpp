// The compile-time checks of crucible/TensorMeta.h.

#include <crucible/TensorMeta.h>

namespace crucible {

static_assert(sizeof(ExternalDataPtr) == sizeof(void*),
              "Tagged<void*, source::External> must EBO-collapse so TensorMeta "
              "stays layout-stable");
static_assert(std::is_trivially_copyable_v<ExternalDataPtr>);
static_assert(std::is_standard_layout_v<ExternalDataPtr>);

static_assert(sizeof(GradFnHash) == sizeof(uint64_t), "Tagged<uint64_t, hash_family::FamilyB> must EBO-collapse so "
                                                      "TensorMeta stays layout-stable");
static_assert(std::is_trivially_copyable_v<GradFnHash>);
static_assert(std::is_standard_layout_v<GradFnHash>);

static_assert(sizeof(TensorDim) == sizeof(int64_t), "Refined<bounded_above<kMaxTensorDimExtent>, int64_t> must "
                                                    "EBO-collapse so TensorMeta stays layout-stable");
static_assert(std::is_standard_layout_v<TensorDim>);

static_assert(sizeof(TensorDimArray) == sizeof(int64_t) * kMaxTensorNDim,
              "TensorDimArray must remain the same 64-byte lane block as int64_t[8]");
static_assert(std::is_trivially_copyable_v<TensorDimArray>);
static_assert(std::is_standard_layout_v<TensorDimArray>);

static_assert(sizeof(TensorMeta) == 168, "TensorMeta layout check");

}  // namespace crucible
