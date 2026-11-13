#pragma once

#include <crucible/Types.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <foundation/Platform.h>

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
private:
    int64_t lanes_[kMaxTensorNDim]{};

public:
    struct Slot {
        int64_t* lane = nullptr;

        constexpr void operator=(TensorDim dim) const noexcept { *lane = dim.value(); }

        [[nodiscard]] constexpr int64_t value() const noexcept { return *lane; }

        [[nodiscard]] constexpr operator TensorDim() const noexcept {
            return ::fixy::mint_refined_trusted<::fixy::bounded_above<kMaxTensorDimExtent>>(*lane);
        }
    };

    struct ConstSlot {
        const int64_t* lane = nullptr;

        [[nodiscard]] constexpr int64_t value() const noexcept { return *lane; }

        [[nodiscard]] constexpr operator TensorDim() const noexcept {
            return ::fixy::mint_refined_trusted<::fixy::bounded_above<kMaxTensorDimExtent>>(*lane);
        }
    };

    [[nodiscard]] constexpr Slot operator[](std::size_t index) noexcept { return Slot{&lanes_[index]}; }

    [[nodiscard]] constexpr ConstSlot operator[](std::size_t index) const noexcept { return ConstSlot{&lanes_[index]}; }

    [[nodiscard]] constexpr int64_t* raw_data() noexcept { return lanes_; }

    [[nodiscard]] constexpr const int64_t* raw_data() const noexcept { return lanes_; }

    // Two blocks are equal when each lane holds the same value, the lanes
    // past ndim too.  The compare is a loop and not a defaulted compare.  In
    // a constant evaluation that compares two blocks, GCC 16 gives the
    // result of the first defaulted compare of a C array for the second one.
    [[nodiscard]] friend constexpr bool operator==(TensorDimArray const& left, TensorDimArray const& right) noexcept {
        for (std::size_t lane = 0; lane < kMaxTensorNDim; ++lane) {
            if (left.lanes_[lane] != right.lanes_[lane]) return false;
        }
        return true;
    }
};

// A size or a stride comes from PyTorch, a trace or a file. The bound is a
// contract assertion ahead of the mint, because the mint alone only assumes
// its predicate under NDEBUG, and a Release build must still stop here.
[[nodiscard]] inline constexpr TensorDim tensor_dim(int64_t value) noexcept {
    contract_assert(value <= kMaxTensorDimExtent);
    return ::fixy::mint_refined<::fixy::bounded_above<kMaxTensorDimExtent>>(value);
}

[[nodiscard]] inline constexpr int64_t raw_tensor_dim(TensorDim dim) noexcept { return dim.value(); }

[[nodiscard]] inline constexpr int64_t raw_tensor_dim(TensorDimArray::Slot dim) noexcept { return dim.value(); }

[[nodiscard]] inline constexpr int64_t raw_tensor_dim(TensorDimArray::ConstSlot dim) noexcept { return dim.value(); }

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

[[nodiscard]] inline constexpr ExternalDataPtr external_data_ptr(void* ptr) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::External>(ptr);
}

[[nodiscard]] inline constexpr void* raw_data_ptr(ExternalDataPtr ptr) noexcept { return ptr.value(); }

[[nodiscard]] inline constexpr void* raw_data_ptr(const TensorMeta& meta) noexcept {
    return raw_data_ptr(meta.data_ptr);
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

}  // namespace crucible
