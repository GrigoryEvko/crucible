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

static_assert(sizeof(TensorMeta) == 176, "TensorMeta layout check");
static_assert(offsetof(TensorMeta, pad0) == 156);
static_assert(offsetof(TensorMeta, storage_nbytes) == 160);
static_assert(offsetof(TensorMeta, grad_fn_hash) == 168);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(TensorMeta);

// The compare of two descriptors reads each field.  A descriptor that holds a
// different value in one field is not equal to the default descriptor, and it
// is equal to itself.
template <class Change>
[[nodiscard]] consteval bool is_unequal_after(Change change) {
    TensorMeta changed{};
    change(changed);
    return !(changed == TensorMeta{}) && changed == changed;
}

static_assert(TensorMeta{} == TensorMeta{});
static_assert(is_unequal_after([](TensorMeta& meta) { meta.sizes[0] = tensor_dim(4); }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.sizes[kMaxTensorNDim - 1] = tensor_dim(4); }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.strides[3] = tensor_dim(1); }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.data_ptr = external_data_ptr(static_cast<void*>(&meta)); }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.ndim = 2; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.dtype = ScalarType::Int; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.device_type = DeviceType::CUDA; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.device_idx = 0; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.layout = Layout::Sparse; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.requires_grad = true; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.flags = 1; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.output_nr = 1; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.storage_offset = 1; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.version = 1; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.pad0 = 1; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.storage_nbytes = 1; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.grad_fn_hash = grad_fn_hash(1); }));

}  // namespace crucible
