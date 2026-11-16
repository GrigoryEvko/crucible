#pragma once

#include <crucible/Types.h>
#include <fixy/Core.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/contracts/Pre.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace crucible {

// The bound is structural, not a tuning knob: sizes and strides are inlined
// arrays of this length, so any larger dimension count reads out of bounds.
inline constexpr uint8_t kMaxTensorNDim = 8;

// A storage address arrives from a frontend, a trace or a file. It is hashed
// and compared as an opaque cookie and never dereferenced, until some later
// validator retags it.
using ExternalDataPtr = ::fixy::Tagged<void*, ::fixy::tags::source::External>;

// This value derives from an autograd object's identity, which is local to
// one process. It must never be used as a key that outlives the run.
using GradFnHash = ::fixy::Tagged<uint64_t, ::crucible::hash_family::FamilyB>;

// An extent is capped so that multiplying it by the widest element size
// cannot overflow int64_t, which is what the storage-span arithmetic does.
// The lanes stay plain int64_t so a consumer can load the whole block into a
// vector register without a copy. Only the write path goes through the
// refined type.
inline constexpr int64_t kTensorDimElementByteBudget = 16;
inline constexpr int64_t kMaxTensorDimExtent = ::fixy::max_of<int64_t> / kTensorDimElementByteBudget;

using TensorDim = ::fixy::Refined<::fixy::bounded_above<kMaxTensorDimExtent>, int64_t>;

struct TensorDimArray {
    // The index of one lane.
    using LaneIndex = ::fixy::Index<kMaxTensorNDim>;

private:
    ::fixy::FixedArray<int64_t, kMaxTensorNDim> lanes_{};

    // The lane of a dimension.  A dimension index comes from a loop bounded
    // by ndim, which the trust ladder bounds by the lane count, so a caller
    // that knows the bound folds the check away.
    [[nodiscard]] static constexpr LaneIndex lane_of_(std::size_t index) noexcept {
        return LaneIndex::check(index).expect("a dimension index is below the lane count of a TensorDimArray");
    }

public:
    struct Slot {
        ::fixy::Ref<int64_t> lane;

        constexpr void operator=(TensorDim dim) const noexcept { *lane = dim.value(); }

        [[nodiscard]] constexpr int64_t value() const noexcept { return *lane; }

        [[nodiscard]] constexpr operator TensorDim() const noexcept {
            return ::fixy::mint_refined_trusted<::fixy::bounded_above<kMaxTensorDimExtent>>(*lane);
        }
    };

    struct ConstSlot {
        ::fixy::Ref<const int64_t> lane;

        [[nodiscard]] constexpr int64_t value() const noexcept { return *lane; }

        [[nodiscard]] constexpr operator TensorDim() const noexcept {
            return ::fixy::mint_refined_trusted<::fixy::bounded_above<kMaxTensorDimExtent>>(*lane);
        }
    };

    [[nodiscard]] constexpr Slot operator[](std::size_t index) noexcept {
        return Slot{::fixy::to(lanes_.at(lane_of_(index)))};
    }

    [[nodiscard]] constexpr ConstSlot operator[](std::size_t index) const noexcept {
        return ConstSlot{::fixy::to(lanes_.at(lane_of_(index)))};
    }

    // Each lane, the lanes past ndim too, for a reader that takes the whole
    // block, such as a vector load.
    [[nodiscard]] constexpr ::fixy::View<int64_t const, kMaxTensorNDim> lanes() const& noexcept {
        return lanes_.view();
    }
    void lanes() const&& = delete("a view of the lanes of a temporary block dangles at the end of the full expression");

    // Two blocks are equal when each lane holds the same value, the lanes
    // past ndim too.
    [[nodiscard]] friend constexpr bool operator==(TensorDimArray const& left, TensorDimArray const& right) noexcept {
        return left.lanes_ == right.lanes_;
    }
};

// A size or a stride comes from PyTorch, a trace or a file. The bound is a
// precondition of this door. The mint checks the same predicate, but a
// report of this precondition names the door that the caller used.
[[nodiscard]] inline constexpr TensorDim tensor_dim(int64_t value) noexcept {
    CRUCIBLE_PRE(value <= kMaxTensorDimExtent);
    return ::fixy::mint_refined<::fixy::bounded_above<kMaxTensorDimExtent>>(value);
}

[[nodiscard]] inline constexpr int64_t raw_tensor_dim(TensorDim dim) noexcept { return dim.value(); }

[[nodiscard]] inline constexpr int64_t raw_tensor_dim(TensorDimArray::Slot dim) noexcept { return dim.value(); }

[[nodiscard]] inline constexpr int64_t raw_tensor_dim(TensorDimArray::ConstSlot dim) noexcept { return dim.value(); }

// The bits of TensorMeta::flags.  Bits 6 and 7 are reserved, and each
// descriptor writes them as zero.
namespace meta_flags {
inline constexpr uint8_t IS_LEAF = 1 << 0;
inline constexpr uint8_t IS_CONTIGUOUS = 1 << 1;
inline constexpr uint8_t HAS_GRAD_FN = 1 << 2;
inline constexpr uint8_t IS_VIEW = 1 << 3;
inline constexpr uint8_t IS_NEG = 1 << 4;
inline constexpr uint8_t IS_CONJ = 1 << 5;
}  // namespace meta_flags

struct TensorMeta {
    // The lanes past ndim must start at zero, and the reason is the wire
    // image rather than any hash. write_meta in Serialize.h writes both
    // blocks at their full width, so a tail lane holding anything but zero
    // makes two otherwise equal descriptors serialize to different bytes,
    // which is the one thing that file exists to prevent.
    //
    // Every hash masks instead, so none of them depends on the tail:
    // dim_hash_simd loads the full width and reduces under a prefix mask of
    // ndim, dim_hash_scalar loops to ndim, and the recording kernel's shape
    // hash takes ndim lanes. Do not cite a hash as the reason here.
    //
    // Each block also carries its own lane initializer, so a descriptor is
    // zeroed by default-construction whether or not its holder writes braces.
    // Removing the braces at a holder saves nothing.
    TensorDimArray sizes{};
    TensorDimArray strides{};
    ExternalDataPtr data_ptr{};
    uint8_t ndim = 0;
    ScalarType dtype = ScalarType::Undefined;
    DeviceType device_type = DeviceType::CPU;
    int8_t device_idx = -1;  // -1 is the host; 0 and up index a device
    Layout layout = Layout::Strided;
    bool requires_grad = false;

    // Bit layout:
    //   bit 0: is_leaf, a parameter or user-created tensor with no grad_fn
    //   bit 1: is_contiguous
    //   bit 2: has_grad_fn
    //   bit 3: is_view, shares storage with another tensor
    //   bit 4: is_neg, negation bit-view
    //   bit 5: is_conj, conjugate bit-view
    //   bits 6 and 7: reserved
    uint8_t flags = 0;

    uint8_t output_nr = 0;

    int64_t storage_offset = 0;
    uint32_t version = 0;  // bumped on in-place mutation
    uint32_t pad0 = 0;  // zero, so the storage size starts at a multiple of eight
    // The size of the storage in bytes, which a view does not span.  A
    // storage of 4 GiB or more is usual for a large model, so the field has
    // 64 bits.
    uint64_t storage_nbytes = 0;
    GradFnHash grad_fn_hash{};  // 0 means no grad_fn

    // Two descriptors are equal when each field holds the same value.  The
    // padding is the named member pad0, so the result is the result of a
    // compare of the bytes.  A Tagged field has no compare of its own, so
    // the compare reads its value, and the compare cannot be defaulted.  A
    // new field needs a term here.
    [[nodiscard]] friend constexpr bool operator==(TensorMeta const& left, TensorMeta const& right) noexcept {
        return left.sizes == right.sizes && left.strides == right.strides
            && left.data_ptr.value() == right.data_ptr.value() && left.ndim == right.ndim && left.dtype == right.dtype
            && left.device_type == right.device_type && left.device_idx == right.device_idx
            && left.layout == right.layout && left.requires_grad == right.requires_grad && left.flags == right.flags
            && left.output_nr == right.output_nr && left.storage_offset == right.storage_offset
            && left.version == right.version && left.pad0 == right.pad0 && left.storage_nbytes == right.storage_nbytes
            && left.grad_fn_hash.value() == right.grad_fn_hash.value();
    }
};

// The door of the front end.  The front end gives the data pointer of a
// tensor as a C pointer, and the door tags it as external.  value() of the
// tagged pointer gives the C pointer back.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the front end gave the data pointer of the tensor at the record of the op")
[[nodiscard]] inline constexpr ExternalDataPtr external_data_ptr(void* ptr) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::External>(ptr);
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The device that holds the bytes of the tensor.  The constructor of
// TensorDevice gives the index of normalize_device_index (crucible/Types.h).
// Each equality and each hash of a device reads it through this door, so
// the two agree on what one device is.
[[nodiscard]] inline constexpr TensorDevice device_of(const TensorMeta& meta) noexcept {
    return TensorDevice{meta.device_type, meta.device_idx};
}

// The count of bytes from the first byte of the storage of the tensor to its
// element at the zero index: storage_offset elements of its dtype.  The data
// pointer of a strided tensor of PyTorch is the first byte of its storage
// plus this count.  A negative offset, which no tensor of PyTorch holds,
// counts as zero, and the product saturates.
[[nodiscard]] inline constexpr uint64_t storage_offset_bytes(const TensorMeta& meta) noexcept {
    const uint64_t elements = meta.storage_offset < 0 ? uint64_t{0} : static_cast<uint64_t>(meta.storage_offset);
    return ::fixy::saturating_mul(elements, uint64_t{element_size(meta.dtype).raw()});
}

// The key of the storage of the tensor: the key of the first byte of its
// storage.  Two views of one storage give one key at each offset.  An address
// of a storage can come back from the allocator for a new storage after a
// free, so the key names a storage only while that storage lives.
[[nodiscard]] inline ::fixy::AddressKey storage_key(const TensorMeta& meta) noexcept {
    return ::fixy::mint_address_key(meta.data_ptr.value()).lower_by(storage_offset_bytes(meta));
}

namespace detail {

// The address bytes below the data pointer of the tensor, on its device, or
// no address for a tensor with no data pointer and for a count past the
// largest offset of a pointer.  The front end gave the address, so the door
// cannot prove that the bytes live.  The count is the offset of the element
// at the zero index of a tensor of PyTorch from the first byte of its
// storage, so the address below is in the same storage.
[[nodiscard]] inline ::fixy::Option<TensorData> foreign_address_below(const TensorMeta& meta, uint64_t bytes) noexcept {
    if (meta.data_ptr.value() == nullptr || bytes > uint64_t{PTRDIFF_MAX}) return ::fixy::none;
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the front end gave the data pointer of the tensor at the record of the op")
    const ::fixy::Option<::fixy::RunAddress> address = ::fixy::mint_foreign_run_address(
        static_cast<char*>(meta.data_ptr.value()) - static_cast<std::ptrdiff_t>(bytes));
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return address.match(
        [&meta](::fixy::RunAddress found) noexcept { return ::fixy::some(TensorData{found, device_of(meta)}); },
        [] noexcept { return ::fixy::Option<TensorData>{::fixy::none}; });
}

}  // namespace detail

// The address of the bytes of the tensor on its device, or no address for a
// tensor with no data pointer.  The front end gave the address, so the door
// cannot prove that the bytes live.
[[nodiscard]] inline ::fixy::Option<TensorData> data_address(const TensorMeta& meta) noexcept {
    return detail::foreign_address_below(meta, 0);
}

// The address of the first byte of the storage of the tensor on its device,
// or no address for a tensor with no data pointer.  A slot of a memory plan
// holds one storage, so the address of a slot is the address of its storage,
// and a view at an offset starts storage_offset elements later.
[[nodiscard]] inline ::fixy::Option<TensorData> storage_address(const TensorMeta& meta) noexcept {
    return detail::foreign_address_below(meta, storage_offset_bytes(meta));
}

[[nodiscard]] inline constexpr GradFnHash grad_fn_hash(uint64_t hash) noexcept {
    return ::fixy::mint_tagged<::crucible::hash_family::FamilyB>(hash);
}

[[nodiscard]] inline constexpr uint64_t raw_grad_fn_hash(GradFnHash hash) noexcept { return hash.value(); }

[[nodiscard]] inline constexpr uint64_t raw_grad_fn_hash(const TensorMeta& meta) noexcept {
    return raw_grad_fn_hash(meta.grad_fn_hash);
}

using ExternalTensorMeta = ::fixy::Tagged<const TensorMeta&, ::fixy::tags::source::External>;

[[nodiscard]] inline constexpr ExternalTensorMeta external_tensor_meta(const TensorMeta& meta) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::External, const TensorMeta&>(meta);
}

// A deserialized byte can hold any value in [0, 255]. Every write of ndim
// from outside the process goes through this so the field keeps the bound the
// inline arrays depend on. The factory hands back a bare uint8_t, which is
// what keeps the struct layout unchanged.
using ValidNDim = ::fixy::Refined<::fixy::bounded_above<kMaxTensorNDim>, uint8_t>;

[[nodiscard, gnu::const]] inline constexpr uint8_t make_ndim(ValidNDim raw) noexcept { return raw.value(); }

// ScalarType is sparse: many int8_t values are not enumerators. A consumer
// that switches over every enumerator and marks the remainder unreachable
// ends the process for an unchecked byte from a file, so a deserialized
// dtype passes through this predicate first. It fails closed.
//
// Each predicate here is a named class. A refinement folds the name of its
// predicate into its row hash, and a closure type has no stable name.  Each
// predicate asks enum_from, which reads the enumerators of the enumeration
// by reflection, so a new enumerator needs no edit here.
struct IsValidScalarType {
    constexpr bool operator()(auto raw) const noexcept {
        return ::fixy::enum_from<ScalarType>(static_cast<std::int8_t>(raw)).is_some();
    }
};

inline constexpr IsValidScalarType valid_scalar_type{};

using ValidScalarType = ::fixy::Refined<valid_scalar_type, std::int8_t>;

[[nodiscard, gnu::const]] inline constexpr ScalarType make_scalar_type(ValidScalarType raw) noexcept {
    return static_cast<ScalarType>(raw.value());
}

// DeviceType is sparse as well, but no consumer treats a gap value as
// unreachable, so this gate is not about undefined behaviour. The field is
// folded into a content hash that serves as a node identity, and an invalid
// byte corrupts that identity without any other symptom.
struct IsValidDeviceType {
    constexpr bool operator()(auto raw) const noexcept {
        return ::fixy::enum_from<DeviceType>(static_cast<std::int8_t>(raw)).is_some();
    }
};

inline constexpr IsValidDeviceType valid_device_type{};

using ValidDeviceType = ::fixy::Refined<valid_device_type, std::int8_t>;

[[nodiscard, gnu::const]] inline constexpr DeviceType make_device_type(ValidDeviceType raw) noexcept {
    return static_cast<DeviceType>(raw.value());
}

// Layout is dense, so an upper bound would appear to be enough. It is not:
// the byte is signed, and a bound of five accepts every negative value.
// Like the device type, an invalid layout corrupts the content hash silently.
struct IsValidLayout {
    constexpr bool operator()(auto raw) const noexcept {
        return ::fixy::enum_from<Layout>(static_cast<std::int8_t>(raw)).is_some();
    }
};

inline constexpr IsValidLayout valid_layout{};

using ValidLayout = ::fixy::Refined<valid_layout, std::int8_t>;

[[nodiscard, gnu::const]] inline constexpr Layout make_layout(ValidLayout raw) noexcept {
    return static_cast<Layout>(raw.value());
}

// The fields of one tensor descriptor as a writer outside the process gave
// them: a C caller of the vessel, or a metadata record of a trace file.  No
// field holds a checked claim, so this type is not a TensorMeta, and
// decode_tensor_meta is the one door from it to a TensorMeta.  The data
// pointer is not a field, because only the caller of the door knows if the
// address has a meaning in this process.
struct UncheckedTensorMeta {
    ::fixy::FixedArray<int64_t, kMaxTensorNDim> sizes{};
    ::fixy::FixedArray<int64_t, kMaxTensorNDim> strides{};
    uint8_t ndim = 0;
    int8_t dtype = 0;
    int8_t device_type = 0;
    int8_t device_idx = 0;
    int8_t layout = 0;
    uint8_t requires_grad = 0;
    uint8_t flags = 0;
    uint8_t output_nr = 0;
    int64_t storage_offset = 0;
    uint32_t version = 0;
    // The padding before the storage size.  The door does not read it.
    uint32_t pad0 = 0;
    uint64_t storage_nbytes = 0;
    uint64_t grad_fn_hash = 0;
};

// The TensorMeta of the fields, with data_ptr as its data pointer, or no value
// when a field breaks a claim of the type.  The claims are:
//
//   - the rank is at most kMaxTensorNDim
//   - each size and each stride below the rank is at most kMaxTensorDimExtent
//   - each lane past the rank holds zero, so equal descriptors write equal bytes
//   - the element type, the device and the layout each name an enumerator
//   - the device index is -1 for the host, or zero or more.  The door
//     stores -1 for each index of the host (normalize_device_index)
//   - the gradient byte is 0 or 1, because a bool with another byte is
//     undefined behaviour to read
//   - no reserved bit of the flags is set.
//
// The padding of the TensorMeta stays zero.  Linear in kMaxTensorNDim.
[[nodiscard]] inline ::fixy::Option<TensorMeta> decode_tensor_meta(UncheckedTensorMeta const& fields,
                                                                   ExternalDataPtr data_ptr) noexcept {
    const uint8_t ndim = fields.ndim;
    if (ndim > kMaxTensorNDim) return ::fixy::none;
    TensorMeta meta{};
    meta.ndim = make_ndim(::fixy::mint_refined<::fixy::bounded_above<kMaxTensorNDim>>(ndim));
    for (const TensorDimArray::LaneIndex lane : decltype(fields.sizes)::indices()) {
        const int64_t size = fields.sizes.at(lane);
        const int64_t stride = fields.strides.at(lane);
        const bool is_lane_held =
            lane.raw() < ndim ? size <= kMaxTensorDimExtent && stride <= kMaxTensorDimExtent : size == 0 && stride == 0;
        if (!is_lane_held) return ::fixy::none;
        meta.sizes[lane.raw()] = tensor_dim(size);
        meta.strides[lane.raw()] = tensor_dim(stride);
    }

    if (!valid_scalar_type(fields.dtype) || !valid_device_type(fields.device_type) || !valid_layout(fields.layout))
        return ::fixy::none;
    meta.dtype = make_scalar_type(::fixy::mint_refined<valid_scalar_type>(fields.dtype));
    meta.device_type = make_device_type(::fixy::mint_refined<valid_device_type>(fields.device_type));
    meta.layout = make_layout(::fixy::mint_refined<valid_layout>(fields.layout));

    constexpr uint8_t known_flags = meta_flags::IS_LEAF | meta_flags::IS_CONTIGUOUS | meta_flags::HAS_GRAD_FN
                                  | meta_flags::IS_VIEW | meta_flags::IS_NEG | meta_flags::IS_CONJ;
    if (fields.device_idx < -1 || fields.requires_grad > 1 || (fields.flags & ~known_flags) != 0) return ::fixy::none;
    // The record keeps one index for the host, so each stored device is the
    // device that device_of gives.
    meta.device_idx = normalize_device_index(meta.device_type, fields.device_idx);
    meta.requires_grad = fields.requires_grad == 1;
    meta.flags = fields.flags;

    meta.data_ptr = data_ptr;
    meta.output_nr = fields.output_nr;
    meta.storage_offset = fields.storage_offset;
    meta.version = fields.version;
    meta.storage_nbytes = fields.storage_nbytes;
    meta.grad_fn_hash = grad_fn_hash(fields.grad_fn_hash);
    return ::fixy::Option<TensorMeta>::some(meta);
}

// The element type of a dtype, for each dtype that has a C++ type of the same
// bits: the dtype of T, or Undefined.  A storage format of the base has the
// dtype of storage_scalar_type (crucible/Types.h).  A bool is no element of a
// view of a plan, because a byte of the plan can hold a value other than 0
// and 1.  A dtype with no C++ type of its own, such as ComplexFloat, has no
// element type here.
template <class T>
inline constexpr ScalarType scalar_type_of = ScalarType::Undefined;
template <::fixy::StorageFloat T>
inline constexpr ScalarType scalar_type_of<T> = storage_scalar_type<T>;
template <>
inline constexpr ScalarType scalar_type_of<uint8_t> = ScalarType::Byte;
template <>
inline constexpr ScalarType scalar_type_of<int8_t> = ScalarType::Char;
template <>
inline constexpr ScalarType scalar_type_of<int16_t> = ScalarType::Short;
template <>
inline constexpr ScalarType scalar_type_of<int32_t> = ScalarType::Int;
template <>
inline constexpr ScalarType scalar_type_of<int64_t> = ScalarType::Long;
template <>
inline constexpr ScalarType scalar_type_of<float> = ScalarType::Float;
template <>
inline constexpr ScalarType scalar_type_of<double> = ScalarType::Double;

// An element type of a strided view of a tensor: a type with a dtype.
template <class T>
concept TensorElement = scalar_type_of<T> != ScalarType::Undefined;

// The reason that strided_view_of gives no view.
enum class TensorViewRefusal : uint8_t {
    // The layout of the tensor is not Strided.
    NotStrided,
    // The dtype of the tensor is not the dtype of the element type.
    ElementType,
    // A size below the rank is negative.
    NegativeSize,
    // A stride below the rank is negative.  A strided view of the base holds
    // no negative stride.
    NegativeStride,
    // An element of the tensor lies outside the storage, the offset of the
    // storage is negative, or a count of the sizes or of the span wraps.
    OutsideStorage,
};

// The strided view of the tensor of meta over storage, the run of its slot
// in the memory plan, or the reason that the tensor has no view.  A slot
// holds one storage of PyTorch, and its run starts at the first byte of that
// storage.  So the element at the zero index is element storage_offset of
// storage, and a view of a slice at an offset reads the elements of its base.
// The build of the slots sizes each slot to hold the offset and the extent of
// each tensor in it.  The view has the dynamic rank of the tensor.  O(ndim).
template <class T>
    requires TensorElement<T>
[[nodiscard]] inline ::fixy::Result<::fixy::StridedView<T>, TensorViewRefusal>
strided_view_of(const TensorMeta& meta, ::fixy::View<T> storage) noexcept {
    if (meta.layout != Layout::Strided) return ::fixy::err(TensorViewRefusal::NotStrided);
    if (meta.dtype != scalar_type_of<T>) return ::fixy::err(TensorViewRefusal::ElementType);
    ::fixy::FixedArray<std::size_t, kMaxTensorNDim> sizes{};
    ::fixy::FixedArray<std::size_t, kMaxTensorNDim> strides{};
    for (const TensorDimArray::LaneIndex lane : sizes.indices()) {
        if (lane.raw() >= meta.ndim) break;
        ::fixy::Option<std::size_t> size = ::fixy::narrow<std::size_t>(raw_tensor_dim(meta.sizes[lane.raw()]));
        ::fixy::Option<std::size_t> stride = ::fixy::narrow<std::size_t>(raw_tensor_dim(meta.strides[lane.raw()]));
        if (size.is_none()) return ::fixy::err(TensorViewRefusal::NegativeSize);
        if (stride.is_none()) return ::fixy::err(TensorViewRefusal::NegativeStride);
        sizes.at(lane) = static_cast<::fixy::Option<std::size_t>&&>(size).expect("the size has a value");
        strides.at(lane) = static_cast<::fixy::Option<std::size_t>&&>(stride).expect("the stride has a value");
    }
    const ::fixy::View<const std::size_t> size_lanes =
        sizes.view().window(0, meta.ndim).expect("the rank is at most kMaxTensorNDim");
    const ::fixy::View<const std::size_t> stride_lanes =
        strides.view().window(0, meta.ndim).expect("the rank is at most kMaxTensorNDim");
    ::fixy::Option<std::size_t> offset = ::fixy::narrow<std::size_t>(meta.storage_offset);
    if (offset.is_none()) return ::fixy::err(TensorViewRefusal::OutsideStorage);
    return ::fixy::mint_strided_view(
               storage, size_lanes, stride_lanes,
               static_cast<::fixy::Option<std::size_t>&&>(offset).expect("the offset has a value"))
        .ok_or(TensorViewRefusal::OutsideStorage);
}

}  // namespace crucible
