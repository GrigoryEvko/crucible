// The compile-time checks of crucible/TensorMeta.h.

#include <crucible/TensorMeta.h>
#include <fixy/Protocol.h>

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

static_assert(sizeof(TensorFlags) == 1);
static_assert(std::is_trivially_copyable_v<TensorFlags>);
static_assert(std::is_standard_layout_v<TensorFlags>);

// The writer of the flags writes zero in each reserved bit, and no door makes
// flags from a byte, so each value holds zero there.
static_assert(!std::is_constructible_v<TensorFlags, uint8_t>);
static_assert(!std::is_assignable_v<TensorFlags&, uint8_t>);
template <class Change>
[[nodiscard]] consteval uint8_t flags_after(Change change) {
    TensorMeta changed{};
    change(changed);
    return changed.flags.bits();
}
static_assert(flags_after([](TensorMeta& meta) { meta.set_flags(0xFF); }) == meta_flags::KNOWN_BITS);
static_assert(flags_after([](TensorMeta& meta) { meta.set_flags(0xC0); }) == 0);
static_assert(flags_after([](TensorMeta& meta) { meta.set_flags(meta_flags::IS_VIEW); }) == meta_flags::IS_VIEW);
static_assert([] {
    TensorMeta meta{};
    meta.set_flags(meta_flags::IS_LEAF | meta_flags::IS_CONJ);
    const TensorFlags flags = meta.flags;
    return flags.is_leaf() && flags.is_conj() && !flags.is_contiguous() && !flags.has_grad_fn() && !flags.is_view()
        && !flags.is_neg();
}());

// The rank and the lanes take no write and no copy from outside TensorMeta and
// TensorShape, so set_shape writes them together.  Each private copy is
// trivial, so the descriptor stays trivially copyable.
static_assert(sizeof(TensorRank) == 1);
static_assert(std::is_trivially_copyable_v<TensorRank> && std::is_standard_layout_v<TensorRank>);
static_assert(!std::is_copy_constructible_v<TensorRank> && !std::is_copy_assignable_v<TensorRank>);
static_assert(!std::is_constructible_v<TensorRank, ValidNDim> && !std::is_constructible_v<TensorRank, uint8_t>);
static_assert(!std::is_copy_constructible_v<TensorDimArray> && !std::is_copy_assignable_v<TensorDimArray>);
static_assert(!std::is_constructible_v<TensorShape, TensorSizes, TensorStrides>);
static_assert(std::is_nothrow_move_constructible_v<TensorShape> && std::is_trivially_copyable_v<TensorShape>);

// The shape of the sizes and the strides, which the test gives as a valid
// shape.
template <std::size_t Rank>
[[nodiscard]] consteval TensorShape shape_of(::fixy::FixedArray<TensorDim, Rank> const& sizes,
                                             ::fixy::FixedArray<TensorDim, Rank> const& strides) {
    return mint_tensor_shape(TensorSizes{sizes.view()}, TensorStrides{strides.view()}).expect("a valid shape");
}

// The mint refuses two counts that differ and a count past kMaxTensorNDim.
static_assert([] {
    const ::fixy::FixedArray<TensorDim, 2> two{tensor_dim(4), tensor_dim(3)};
    const ::fixy::FixedArray<TensorDim, 1> one{tensor_dim(1)};
    const TensorDim zero = tensor_dim(0);
    const ::fixy::FixedArray<TensorDim, kMaxTensorNDim + 1> nine{zero, zero, zero, zero, zero, zero, zero, zero, zero};
    return mint_tensor_shape(TensorSizes{two.view()}, TensorStrides{one.view()}).is_none()
        && mint_tensor_shape(TensorSizes{nine.view()}, TensorStrides{nine.view()}).is_none()
        && mint_tensor_shape(TensorSizes{two.view()}, TensorStrides{two.view()}).is_some();
}());

// A shape writes each lane past its rank as zero, also over a descriptor of a
// larger rank, and the rank and the lanes agree with the shape.
static_assert([] {
    const TensorDim one = tensor_dim(1);
    const ::fixy::FixedArray<TensorDim, 3> wide{one, one, one};
    const ::fixy::FixedArray<TensorDim, 1> narrow{tensor_dim(5)};
    TensorMeta meta{};
    meta.set_shape(shape_of(wide, wide));
    meta.set_shape(shape_of(narrow, narrow));
    TensorMeta fresh{};
    fresh.set_shape(shape_of(narrow, narrow));
    return meta == fresh && meta.ndim == 1 && meta.sizes[0].value() == 5 && meta.sizes[1].value() == 0
        && meta.strides[2].value() == 0;
}());

// The contiguous shape: the last stride is 1, each other stride is the
// product of the sizes after it, and a size of 0 counts as 1.  A stride that
// passes kMaxTensorDimExtent gives no shape, and the product of the first
// size is not read.
template <std::size_t Rank>
[[nodiscard]] consteval bool has_contiguous_strides(::fixy::FixedArray<TensorDim, Rank> const& sizes,
                                                    ::fixy::FixedArray<int64_t, Rank> const& strides) {
    ::fixy::Option<TensorShape> shape = contiguous_tensor_shape(TensorSizes{sizes.view()});
    if (shape.is_none()) return false;
    const TensorShape found = std::move(shape).value_or(shape_of(sizes, sizes));
    for (const ::fixy::Index<Rank> lane : strides.indices()) {
        if (found.strides()[lane.raw()].value() != strides.at(lane)) return false;
    }
    return found.ndim() == Rank;
}
static_assert(has_contiguous_strides(::fixy::FixedArray<TensorDim, 2>{tensor_dim(4), tensor_dim(3)},
                                     ::fixy::FixedArray<int64_t, 2>{3, 1}));
static_assert(has_contiguous_strides(::fixy::FixedArray<TensorDim, 3>{tensor_dim(2), tensor_dim(0), tensor_dim(3)},
                                     ::fixy::FixedArray<int64_t, 3>{3, 3, 1}));
static_assert(has_contiguous_strides(::fixy::FixedArray<TensorDim, 2>{tensor_dim(int64_t{1} << 40),
                                                                      tensor_dim(int64_t{1} << 40)},
                                     ::fixy::FixedArray<int64_t, 2>{int64_t{1} << 40, 1}));
static_assert(contiguous_tensor_shape(TensorSizes{}).is_some());
static_assert([] {
    const ::fixy::FixedArray<TensorDim, 3> wide{tensor_dim(2), tensor_dim(int64_t{1} << 40),
                                                tensor_dim(int64_t{1} << 40)};
    return contiguous_tensor_shape(TensorSizes{wide.view()}).is_none();
}());

static_assert(sizeof(TensorMeta) == 176, "TensorMeta layout check");
static_assert(std::is_standard_layout_v<TensorMeta>);
static_assert(std::is_trivially_copyable_v<TensorMeta>);
static_assert(std::is_aggregate_v<TensorMeta>);
static_assert(offsetof(TensorMeta, ndim) == 136);
static_assert(offsetof(TensorMeta, flags) == 142);
static_assert(offsetof(TensorMeta, storage_generation) == 156);
static_assert(sizeof(StorageGeneration) == sizeof(uint32_t));
static_assert(sizeof(StorageKey) == 16, "the key of a storage is the address word and the generation, and no more");
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

// The shape of the largest rank, with zero in each lane but one size.
[[nodiscard]] consteval TensorShape shape_with_last_size(TensorDim size) {
    const TensorDim zero = tensor_dim(0);
    const ::fixy::FixedArray<TensorDim, kMaxTensorNDim> sizes{zero, zero, zero, zero, zero, zero, zero, size};
    return shape_of(sizes, zero_tensor_dims());
}

static_assert(TensorMeta{} == TensorMeta{});
static_assert(is_unequal_after([](TensorMeta& meta) {
    meta.set_shape(
        shape_of(::fixy::FixedArray<TensorDim, 1>{tensor_dim(4)}, ::fixy::FixedArray<TensorDim, 1>{tensor_dim(0)}));
}));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.set_shape(shape_with_last_size(tensor_dim(4))); }));
static_assert(is_unequal_after([](TensorMeta& meta) {
    const TensorDim zero = tensor_dim(0);
    meta.set_shape(shape_of(::fixy::FixedArray<TensorDim, 4>{zero, zero, zero, zero},
                            ::fixy::FixedArray<TensorDim, 4>{zero, zero, zero, tensor_dim(1)}));
}));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.data_ptr = external_data_ptr(static_cast<void*>(&meta)); }));
static_assert(is_unequal_after([](TensorMeta& meta) {
    const TensorDim zero = tensor_dim(0);
    meta.set_shape(
        shape_of(::fixy::FixedArray<TensorDim, 2>{zero, zero}, ::fixy::FixedArray<TensorDim, 2>{zero, zero}));
}));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.dtype = ScalarType::Int; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.device_type = DeviceType::CUDA; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.device_idx = 0; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.layout = Layout::Sparse; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.requires_grad = true; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.set_flags(1); }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.output_nr = 1; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.storage_offset = 1; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.version = 1; }));
static_assert(is_unequal_after([](TensorMeta& meta) {
    meta.storage_generation = StorageGeneration::known(AllocationCount{1});
}));
static_assert(is_unequal_after([](TensorMeta& meta) {
    meta.storage_generation = StorageGeneration::unknown(AllocatorEpoch{1});
}));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.storage_nbytes = 1; }));
static_assert(is_unequal_after([](TensorMeta& meta) { meta.grad_fn_hash = grad_fn_hash(1); }));

// The Protocol family gives a descriptor a hash, an image and a format, and no
// decode: a byte door would skip the claims of decode_tensor_meta.
static_assert(::fixy::Encodable<TensorMeta> && !::fixy::Decodable<TensorMeta>);

// A block of lanes has a hash and an image too, and no byte door makes one: a
// lane from bytes would skip the bound of a TensorDim.
static_assert(::fixy::Encodable<TensorDimArray> && !::fixy::Decodable<TensorDimArray>);
static_assert(!::fixy::BitValid<TensorDimArray>);
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<TensorDimArray>);

// Only the subscript of a block makes a slot.
static_assert(!std::is_constructible_v<TensorDimArray::ConstSlot, ::fixy::Ref<int64_t const>>);
static_assert(::fixy::encoded_size_v<TensorMeta> == 2 * 8 * sizeof(int64_t) + 8 + 8 + 4 + 8);

// The hash reads no local part: two descriptors that differ only in the data
// pointer, the storage generation or the hash of the grad_fn give one hash,
// and a field that the image holds moves the hash.
template <class Change>
[[nodiscard]] consteval bool keeps_hash_after(Change change) {
    TensorMeta changed{};
    change(changed);
    return ::fixy::hash(changed) == ::fixy::hash(TensorMeta{});
}

static_assert(keeps_hash_after([](TensorMeta& meta) { meta.data_ptr = external_data_ptr(static_cast<void*>(&meta)); }));
static_assert(keeps_hash_after([](TensorMeta& meta) {
    meta.storage_generation = StorageGeneration::known(AllocationCount{1});
}));
static_assert(keeps_hash_after([](TensorMeta& meta) { meta.grad_fn_hash = grad_fn_hash(1); }));
static_assert(!keeps_hash_after([](TensorMeta& meta) { meta.set_shape(shape_with_last_size(tensor_dim(1))); }));
static_assert(!keeps_hash_after([](TensorMeta& meta) { meta.storage_nbytes = 1; }));

// Two threads read one descriptor at the same time, and its fields are plain,
// so it states shareable{} and two threads cannot write it.
static_assert(::foundation::transfer::Shareable<TensorMeta> && ::foundation::transfer::Sendable<TensorMeta>);
static_assert(!::foundation::transfer::Synchronized<TensorMeta>);

}  // namespace crucible
