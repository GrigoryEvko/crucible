#pragma once

#include <crucible/Types.h>
#include <fixy/Core.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/Simd.h>
#include <foundation/Transfer.h>
#include <foundation/contracts/Pre.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace crucible {

// The bound is structural, not a tuning knob: sizes and strides are inlined
// arrays of this length, so any larger dimension count reads out of bounds.
inline constexpr uint8_t kMaxTensorNDim = 8;

struct TensorMeta;

// The key of the address of the data of a tensor, which the front end gave
// at the record of the op.  It compares for equality and gives the key of a
// storage, and no pointer comes back from it.  TensorMeta is its owner, so
// only the members of TensorMeta re-mint the address (data_address and
// storage_address).
using TensorDataKey = ::fixy::ForeignAddressKey<TensorMeta>;

// The address of a storage that a test or a bench gives in place of the data
// pointer of a front end, as a number.  The record path compares the key of
// the address and keys a storage by it, and no code of a test reads through
// it.  Zero is no address, as the null pointer of a front end is no storage.
enum class SyntheticAddress : std::uint64_t {
};

// A synthetic address, and no other type, so a raw number cannot reach the
// mint.
template <class Address>
concept SyntheticAddressValue = std::is_same_v<Address, SyntheticAddress>;

// This value derives from an autograd object's identity, which is local to
// one process. It must never be used as a key that outlives the run.
using GradFnHash = ::fixy::Tagged<uint64_t, ::crucible::hash_family::FamilyB>;

// An extent is capped so that multiplying it by the widest element size
// cannot overflow int64_t, which is what the storage-span arithmetic does.
// The lanes stay plain int64_t so a consumer can load the whole block into a
// vector register without a copy.  Each write of a lane keeps the bound: it
// takes the refined type, or a block that decode_tensor_shape checked.
inline constexpr int64_t kTensorDimElementByteBudget = 16;
inline constexpr int64_t kMaxTensorDimExtent = ::fixy::max_of<int64_t> / kTensorDimElementByteBudget;

using TensorDim = ::fixy::Refined<::fixy::bounded_above<kMaxTensorDimExtent>, int64_t>;

class TensorShape;
struct UncheckedTensorMeta;

// The lanes of the sizes or of the strides of a tensor.  The Protocol family
// takes its equality, hash and image from the private block of lanes.
//
// Each lane holds a TensorDim.  Two writes put lanes in a block, and each
// keeps the bound, so the bound is an invariant of the block:
//   - set_lane_ writes one lane, and it takes a TensorDim
//   - set_lanes_ writes each lane from a block whose lanes decode_tensor_shape
//     checked against the bound.
// No byte door makes a block: no_start_over_bytes refuses decode of the
// family, decode and bit_cast of the Scalar family, and a lifetime start over
// bytes.
//
// The lanes of a descriptor agree with its rank: each lane past the rank
// holds zero.  So only TensorShape writes a lane, and only TensorShape and
// TensorMeta copy a block.  A copy of the block of another descriptor would
// put its lanes beside a rank that they do not match.  The copy constructor
// and the copy assignment are private, and each is trivial, so a TensorMeta
// stays trivially copyable.  The subscript reads a lane, and no subscript
// writes one.
struct[[= ::fixy::derive_protocol{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] TensorDimArray {
    // The index of one lane.
    using LaneIndex = ::fixy::Index<kMaxTensorNDim>;

    constexpr TensorDimArray() noexcept = default;

private:
    friend struct TensorMeta;
    friend class TensorShape;

    constexpr TensorDimArray(TensorDimArray const&) noexcept = default;
    constexpr TensorDimArray& operator=(TensorDimArray const&) noexcept = default;

    ::fixy::FixedArray<int64_t, kMaxTensorNDim> lanes_{};

    // The lane of a dimension.  A dimension index comes from a loop bounded
    // by ndim, which TensorRank bounds by the lane count, so a caller that
    // knows the bound folds the check away.
    [[nodiscard]] static constexpr LaneIndex lane_of_(std::size_t index) noexcept {
        return LaneIndex::check(index).expect("a dimension index is below the lane count of a TensorDimArray");
    }

    // The write of one lane.
    constexpr void set_lane_(std::size_t index, TensorDim dim) noexcept { lanes_.set(lane_of_(index), dim.value()); }

    // The write of each lane from a block that decode_tensor_shape checked:
    // each lane is at most kMaxTensorDimExtent.  Only the constructor of
    // TensorShape from checked fields calls it.  O(1), one copy of the block.
    constexpr void set_lanes_(::fixy::FixedArray<int64_t, kMaxTensorNDim> const& checked_lanes) noexcept {
        lanes_ = checked_lanes;
    }

public:
    // The lane of a block, which the subscript of the block names.  Only that
    // subscript makes one, so the lane holds a TensorDim.
    class ConstSlot {
    public:
        constexpr ConstSlot(ConstSlot const&) noexcept = default;
        void operator=(ConstSlot const&) = delete("a ConstSlot reads a lane of a block, and no write of a lane goes "
                                                  "through it.  TensorMeta::set_shape writes the lanes");

        [[nodiscard]] constexpr int64_t value() const noexcept { return *lane_; }

        // The lane holds a TensorDim: the one write of a lane takes a
        // TensorDim, and no byte door makes a block.  So the mint does not
        // check the bound again.
        [[nodiscard]] constexpr operator TensorDim() const noexcept {
            return ::fixy::mint_refined_trusted<::fixy::bounded_above<kMaxTensorDimExtent>>(*lane_);
        }

    private:
        friend struct TensorDimArray;

        constexpr explicit ConstSlot(::fixy::Ref<const int64_t> lane) noexcept : lane_{lane} {}

        ::fixy::Ref<const int64_t> lane_;
    };

    [[nodiscard]] constexpr ConstSlot operator[](std::size_t index) const noexcept {
        return ConstSlot{::fixy::erase_brand(::fixy::to(lanes_.at(lane_of_(index))))};
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
        return ::fixy::equal(left, right);
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
// The six named bits.  Each other bit is reserved.
inline constexpr uint8_t KNOWN_BITS = IS_LEAF | IS_CONTIGUOUS | HAS_GRAD_FN | IS_VIEW | IS_NEG | IS_CONJ;
}  // namespace meta_flags

// The flags of one tensor descriptor: the six bits of meta_flags, and bits 6
// and 7, which are reserved and hold zero.  TensorMeta::set_flags is the one
// writer, and it writes zero in each reserved bit, so each value of the type
// holds zero there.  A copy of the flags of another descriptor keeps that
// rule, so the copy is public.
//
// The conversion to uint8_t gives the byte to a reader that takes a byte.
// The named readers give each bit.
class[[= ::fixy::derive_protocol{}]] TensorFlags {
public:
    constexpr TensorFlags() noexcept = default;

    [[nodiscard]] constexpr bool is_leaf() const noexcept { return has_bit_(meta_flags::IS_LEAF); }
    [[nodiscard]] constexpr bool is_contiguous() const noexcept { return has_bit_(meta_flags::IS_CONTIGUOUS); }
    [[nodiscard]] constexpr bool has_grad_fn() const noexcept { return has_bit_(meta_flags::HAS_GRAD_FN); }
    [[nodiscard]] constexpr bool is_view() const noexcept { return has_bit_(meta_flags::IS_VIEW); }
    [[nodiscard]] constexpr bool is_neg() const noexcept { return has_bit_(meta_flags::IS_NEG); }
    [[nodiscard]] constexpr bool is_conj() const noexcept { return has_bit_(meta_flags::IS_CONJ); }

    // The byte of the flags.  Each reserved bit holds zero.
    [[nodiscard]] constexpr uint8_t bits() const noexcept { return bits_; }
    [[nodiscard]] constexpr operator uint8_t() const noexcept { return bits_; }

    [[nodiscard]] friend constexpr bool operator==(TensorFlags left, TensorFlags right) noexcept {
        return left.bits_ == right.bits_;
    }

private:
    friend struct TensorMeta;

    constexpr explicit TensorFlags(uint8_t bits) noexcept
        : bits_{static_cast<uint8_t>(bits & meta_flags::KNOWN_BITS)} {}

    [[nodiscard]] constexpr bool has_bit_(uint8_t bit) const noexcept { return (bits_ & bit) != 0; }

    uint8_t bits_ = 0;
};

// A rank, which is at most kMaxTensorNDim.  A rank from a byte of outside goes
// through the checked mint of this type.
using ValidNDim = ::fixy::Refined<::fixy::bounded_above<kMaxTensorNDim>, uint8_t>;

// The rank of a tensor descriptor: the count of its dimensions, at most
// kMaxTensorNDim.  A TensorShape holds a rank, and TensorMeta::set_shape
// writes the rank of the descriptor from it, so the rank agrees with the
// lanes.  No door makes a rank from a byte outside TensorShape.  A copy of the
// rank of another descriptor would put that rank beside lanes that it does not
// match, so the copy constructor and the copy assignment are private, and
// each is trivial.
//
// The conversion to uint8_t gives the rank to a reader that takes a byte.
class[[= ::fixy::derive_protocol{}]] TensorRank {
public:
    constexpr TensorRank() noexcept = default;

    // The count of the dimensions.
    [[nodiscard]] constexpr uint8_t count() const noexcept { return count_; }
    [[nodiscard]] constexpr operator uint8_t() const noexcept { return count_; }

    [[nodiscard]] friend constexpr bool operator==(TensorRank const& left, TensorRank const& right) noexcept {
        return left.count_ == right.count_;
    }

private:
    friend struct TensorMeta;
    friend class TensorShape;

    constexpr TensorRank(TensorRank const&) noexcept = default;
    constexpr TensorRank& operator=(TensorRank const&) noexcept = default;
    constexpr explicit TensorRank(ValidNDim count) noexcept : count_{count.value()} {}

    uint8_t count_ = 0;
};

// The sizes of the dimensions of a shape: one lane for each dimension.
struct TensorSizes {
    ::fixy::View<TensorDim const> lanes{};
};

// The strides of the dimensions of a shape: one lane for each dimension.
struct TensorStrides {
    ::fixy::View<TensorDim const> lanes{};
};

// The lanes of mint_tensor_shape: the sizes, and then the strides.  Each has
// its own type, so a call cannot give the strides in the place of the sizes.
template <class Sizes, class Strides>
concept TensorShapeLanes = std::is_same_v<Sizes, TensorSizes> && std::is_same_v<Strides, TensorStrides>;

// The shape of one tensor: its rank, and the size and the stride of each
// dimension.  Each lane past the rank holds zero.  Two doors make a shape:
// mint_tensor_shape from the lanes below the rank, and decode_tensor_shape
// from the fields of a writer outside the process.  TensorMeta::set_shape is
// the one door that writes a shape into a descriptor.  So each descriptor
// holds the two claims of its shape: the rank is at most kMaxTensorNDim, and
// each lane past the rank is zero.
class TensorShape {
public:
    [[nodiscard]] constexpr uint8_t ndim() const noexcept { return rank_.count(); }

    [[nodiscard]] constexpr TensorDimArray const& sizes() const& noexcept { return sizes_; }
    void sizes() const&& = delete("the lanes of a temporary shape end at the end of the full expression");

    [[nodiscard]] constexpr TensorDimArray const& strides() const& noexcept { return strides_; }
    void strides() const&& = delete("the lanes of a temporary shape end at the end of the full expression");

private:
    // The mint gives an Option<TensorShape>.  Its return type is deduced, so
    // the Option is named only where this class is complete.
    template <class Sizes, class Strides>
        requires TensorShapeLanes<Sizes, Strides>
    friend constexpr auto mint_tensor_shape(Sizes sizes, Strides strides) noexcept;
    friend auto decode_tensor_shape(UncheckedTensorMeta const& fields) noexcept;
    friend struct TensorMeta;

    // The shape of fields that decode_tensor_shape checked: the rank is at
    // most kMaxTensorNDim, each lane below the rank is at most
    // kMaxTensorDimExtent, and each lane past the rank is zero.  The
    // definition follows UncheckedTensorMeta.  O(1), one copy of each block.
    explicit TensorShape(UncheckedTensorMeta const& checked_fields) noexcept;

    // The shape of the lanes, which the mint checked: the two counts are equal
    // and at most kMaxTensorNDim.  O(rank).
    constexpr TensorShape(TensorSizes sizes, TensorStrides strides) noexcept
        : rank_{::fixy::mint_refined<::fixy::bounded_above<kMaxTensorNDim>>(static_cast<uint8_t>(sizes.lanes.size()))} {
        std::size_t index = 0;
        for (const TensorDim dim : sizes.lanes)
            sizes_.set_lane_(index++, dim);
        index = 0;
        for (const TensorDim dim : strides.lanes)
            strides_.set_lane_(index++, dim);
    }

    TensorRank rank_{};
    TensorDimArray sizes_{};
    TensorDimArray strides_{};
};

// The shape of the sizes and the strides, or no shape when the two counts
// differ or pass kMaxTensorNDim.  O(rank).
template <class Sizes, class Strides>
    requires TensorShapeLanes<Sizes, Strides>
[[nodiscard]] constexpr auto mint_tensor_shape(Sizes sizes, Strides strides) noexcept {
    using Answer = ::fixy::Option<TensorShape>;
    const std::size_t rank = sizes.lanes.size();
    if (rank != strides.lanes.size() || rank > kMaxTensorNDim) return Answer{::fixy::none};
    return Answer::some(TensorShape{sizes, strides});
}

// The lanes of the largest rank, each zero, for a writer that fills the lanes
// below a rank.  The list names each lane, and a FixedArray refuses a list of
// fewer elements than its count, so a change of kMaxTensorNDim fails here at
// compile time.
[[nodiscard]] constexpr ::fixy::FixedArray<TensorDim, kMaxTensorNDim> zero_tensor_dims() noexcept {
    constexpr TensorDim zero = ::fixy::mint_refined<::fixy::bounded_above<kMaxTensorDimExtent>>(int64_t{0});
    return ::fixy::FixedArray<TensorDim, kMaxTensorNDim>{zero, zero, zero, zero, zero, zero, zero, zero};
}

// The shape of rank 1 of a dense vector: the size extent and the stride 1.
[[nodiscard]] constexpr TensorShape vector_tensor_shape(TensorDim extent) noexcept {
    const ::fixy::FixedArray<TensorDim, 1> sizes{extent};
    const ::fixy::FixedArray<TensorDim, 1> strides{tensor_dim(1)};
    return mint_tensor_shape(TensorSizes{sizes.view()}, TensorStrides{strides.view()})
        .expect("one size and one stride make a shape of rank 1");
}

// The shape of the sizes with the strides of a dense tensor in row-major
// order, as PyTorch gives them: the last stride is 1, and each other stride
// is the product of the sizes after it, where a size of 0 counts as 1.  No
// shape when the count of the sizes passes kMaxTensorNDim, or when a stride
// passes kMaxTensorDimExtent.  O(rank).
[[nodiscard]] constexpr ::fixy::Option<TensorShape> contiguous_tensor_shape(TensorSizes sizes) noexcept {
    const std::size_t rank = sizes.lanes.size();
    if (rank > kMaxTensorNDim) return ::fixy::none;
    ::fixy::FixedArray<int64_t, kMaxTensorNDim> extents{};
    std::size_t index = 0;
    for (const TensorDim dim : sizes.lanes) {
        extents.set(TensorDimArray::LaneIndex::check(index).expect("the rank is at most kMaxTensorNDim"),
                    ::fixy::max(dim.value(), int64_t{1}));
        ++index;
    }
    ::fixy::FixedArray<TensorDim, kMaxTensorNDim> strides = zero_tensor_dims();
    int64_t stride = 1;
    for (std::size_t position = rank; position > 0; --position) {
        if (stride > kMaxTensorDimExtent) return ::fixy::none;
        const TensorDimArray::LaneIndex lane =
            TensorDimArray::LaneIndex::check(position - 1).expect("the rank is at most kMaxTensorNDim");
        strides.set(lane, tensor_dim(stride));
        // The first dimension has no stride before it, so its product is not
        // read, and it can pass the bound.
        if (position == 1) break;
        ::fixy::Option<int64_t> next = ::fixy::checked_mul(stride, extents.at(lane));
        if (next.is_none()) return ::fixy::none;
        stride = std::move(next).expect("the product has a value");
    }
    const ::fixy::View<TensorDim const> stride_lanes =
        strides.view().window(0, rank).expect("the rank is at most kMaxTensorNDim");
    return mint_tensor_shape(sizes, TensorStrides{stride_lanes});
}

// The descriptor of one tensor.  The Protocol family derives its equality from
// its parts, and its hash, image and format from the parts that are not local
// to the process: the data pointer, the storage generation and the hash of
// the grad_fn are local parts.  A descriptor holds claims that no field type
// holds: the rank is at most kMaxTensorNDim, and each lane past the rank is
// zero.  So no byte door makes one: no_start_over_bytes refuses decode of the
// family, decode of the Scalar family and a lifetime start over bytes, and
// decode_tensor_meta is the one door from bytes of outside.
//
// The descriptor moves between threads, and two threads read it at once
// (foundation/Transfer.h).  Its data_ptr is the key of an address, which the
// code compares and keys, and which only the members of the descriptor
// re-mint, so the descriptor states the two properties.
struct[[= ::fixy::derive_protocol{}]][[= ::foundation::lifetime::no_start_over_bytes{}]]
      [[= ::foundation::transfer::sendable{}]][[= ::foundation::transfer::shareable{}]] TensorMeta {
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
    // A session payload walk reads the descriptor as data: data_ptr is the
    // key above, which reaches no token, no loan and no session endpoint
    // (fixy/session/Classification.h).
    using session_payload_is_data = void;

    // set_shape writes sizes, strides and ndim together.  Each of the three
    // is public, so the descriptor stays standard-layout, and each type
    // refuses a write from outside.
    TensorDimArray sizes{};
    TensorDimArray strides{};
    // The key of the address of the data, or no value for a tensor with no
    // data pointer.
    [[= ::fixy::local_part{}]] ::fixy::Option<TensorDataKey> data_ptr{};
    TensorRank ndim{};
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
    //   bits 6 and 7: reserved, and zero (TensorFlags)
    TensorFlags flags{};

    uint8_t output_nr = 0;

    int64_t storage_offset = 0;
    uint32_t version = 0;  // bumped on in-place mutation
    // The generation of the allocation of the storage: none, known, or
    // unknown in a period of the allocator (StorageGeneration).  It is local
    // to the process, as data_ptr is, so the image of Serialize.h and the
    // record of a trace file do not hold it, and no hash folds it.
    [[= ::fixy::local_part{}]] StorageGeneration storage_generation{};
    // The size of the storage in bytes, which a view does not span.  A
    // storage of 4 GiB or more is usual for a large model, so the field has
    // 64 bits.
    uint64_t storage_nbytes = 0;
    [[= ::fixy::local_part{}]] GradFnHash grad_fn_hash{};  // 0 means no grad_fn

    // The one writer of the flags.  It writes the bits of meta_flags that the
    // byte sets, and zero in each reserved bit.
    constexpr void set_flags(uint8_t bits) noexcept { flags = TensorFlags{bits}; }

    // The one writer of the rank and of the lanes.  Each lane past the rank
    // of the shape holds zero, so each lane past the rank of the descriptor
    // holds zero.
    constexpr void set_shape(TensorShape const& shape) noexcept {
        ndim = shape.rank_;
        sizes = shape.sizes_;
        strides = shape.strides_;
    }

    // Two descriptors are equal when each field holds the same value, a
    // local part too: equal of the Protocol family reads each field in
    // declaration order, so a new field takes part with no new term.
    [[nodiscard]] friend constexpr bool operator==(TensorMeta const& left, TensorMeta const& right) noexcept {
        return ::fixy::equal(left, right);
    }

    // The two doors that re-mint the address of the data, each through
    // address_below_.
    friend auto data_address(TensorMeta const& meta) noexcept -> ::fixy::Option<TensorData>;
    friend auto storage_address(TensorMeta const& meta) noexcept -> ::fixy::Option<TensorData>;

    // The door that makes the key of a synthetic address, through
    // synthetic_key_.
    template <SyntheticAddressValue Address>
    friend constexpr auto mint_synthetic_data_ptr(Address address) noexcept -> ::fixy::Option<TensorDataKey>;

private:
    // The key of a synthetic address, which mint_synthetic_data_ptr checks to
    // be not zero.  TensorMeta owns the key, so this member and address_below_
    // are the two places that read or write the word of a key.  O(1).
    [[nodiscard]] static constexpr TensorDataKey synthetic_key_(SyntheticAddress address) noexcept {
        return TensorDataKey::of_word_(std::to_underlying(address));
    }

    // The address bytes below the address of the data, on the device of the
    // tensor, or no address for a tensor with no data pointer and for a count
    // past the largest offset of a pointer.  The front end gave the address
    // at the record of the op, so the door cannot prove that the bytes live.
    // The count is the offset of the element at the zero index of a tensor of
    // PyTorch from the first byte of its storage, so the address below is in
    // the same storage.  TensorMeta owns the key of the address, so this
    // member is the one road from a key back to an address.  O(1).
    [[nodiscard]] static auto address_below_(TensorMeta const& meta, uint64_t bytes) noexcept
        -> ::fixy::Option<TensorData>;
};

// The door of the front end.  The front end gives the data pointer of a
// tensor as a C pointer, and the door gives the key of its address, or no
// value for the null pointer.  No pointer comes back from the key.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the front end gave the data pointer of the tensor at the record of the op")
[[nodiscard]] inline ::fixy::Option<TensorDataKey> external_data_ptr(void* ptr) noexcept {
    return ::fixy::mint_foreign_run_address(ptr).match(
        [](::fixy::RunAddress address) noexcept {
            return ::fixy::Option<TensorDataKey>::some(::fixy::mint_foreign_address_key<TensorMeta>(address));
        },
        [] noexcept { return ::fixy::Option<TensorDataKey>{::fixy::none}; });
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The key of a synthetic address, for a test or a bench.  The address is not
// zero, so the key has a value, and a constant address of zero is a compile
// error.  A synthetic address is an integer from its start, so the key holds
// its bits with no pointer, and no code reads through it.  O(1).
template <SyntheticAddressValue Address>
[[nodiscard]] constexpr auto mint_synthetic_data_ptr(Address address) noexcept -> ::fixy::Option<TensorDataKey> {
    CRUCIBLE_PRE(address != SyntheticAddress{});
    return ::fixy::Option<TensorDataKey>::some(TensorMeta::synthetic_key_(address));
}

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

// The key of one storage: the key of the address of its first byte, and the
// generation of its allocation.  The allocator can give the address of a dead
// storage to a new storage after a free.  A storage with a known generation
// then has a new key, so its key names it for the life of the process.  A
// storage with an unknown generation has the key of its address in one period
// of the allocator, so a new storage at the address of a dead one in another
// period has another key.  A storage with no generation keeps the key of its
// address alone, and that key names the storage only while the storage lives.
// The key has no order.
struct StorageKey {
    ::fixy::AddressKey address = ::fixy::mint_address_key(static_cast<void const*>(nullptr));
    StorageGeneration generation{};
    uint32_t pad = 0;  // zero

    [[nodiscard]] friend constexpr bool operator==(StorageKey const& left, StorageKey const& right) noexcept {
        return left.address == right.address && left.generation == right.generation;
    }
};

// The key of the storage of the tensor.  Two views of one storage give one
// key at each offset.
[[nodiscard]] inline StorageKey storage_key(const TensorMeta& meta) noexcept {
    const ::fixy::AddressKey address =
        meta.data_ptr.match([](TensorDataKey key) noexcept { return key.address_key(); },
                            [] noexcept { return ::fixy::mint_address_key(static_cast<void const*>(nullptr)); });
    return StorageKey{
        .address = address.lower_by(storage_offset_bytes(meta)), .generation = meta.storage_generation, .pad = 0};
}

inline auto TensorMeta::address_below_(TensorMeta const& meta, uint64_t bytes) noexcept -> ::fixy::Option<TensorData> {
    if (bytes > uint64_t{PTRDIFF_MAX}) return ::fixy::none;
    return meta.data_ptr.match(
        [&meta, bytes](TensorDataKey key) noexcept {
            const ::fixy::Option<::fixy::RunAddress> address = TensorDataKey::remint_(key, bytes);
            return address.match(
                [&meta](::fixy::RunAddress found) noexcept { return ::fixy::some(TensorData{found, device_of(meta)}); },
                [] noexcept { return ::fixy::Option<TensorData>{::fixy::none}; });
        },
        [] noexcept { return ::fixy::Option<TensorData>{::fixy::none}; });
}

// The address of the bytes of the tensor on its device, or no address for a
// tensor with no data pointer.  The front end gave the address, so the door
// cannot prove that the bytes live.
[[nodiscard]] inline auto data_address(const TensorMeta& meta) noexcept -> ::fixy::Option<TensorData> {
    return TensorMeta::address_below_(meta, 0);
}

// The address of the first byte of the storage of the tensor on its device,
// or no address for a tensor with no data pointer.  A slot of a memory plan
// holds one storage, so the address of a slot is the address of its storage,
// and a view at an offset starts storage_offset elements later.
[[nodiscard]] inline auto storage_address(const TensorMeta& meta) noexcept -> ::fixy::Option<TensorData> {
    return TensorMeta::address_below_(meta, storage_offset_bytes(meta));
}

[[nodiscard]] inline constexpr GradFnHash grad_fn_hash(uint64_t hash) noexcept {
    return ::fixy::mint_tagged<::crucible::hash_family::FamilyB>(hash);
}

[[nodiscard]] inline constexpr uint64_t raw_grad_fn_hash(GradFnHash hash) noexcept { return hash.value(); }

[[nodiscard]] inline constexpr uint64_t raw_grad_fn_hash(const TensorMeta& meta) noexcept {
    return raw_grad_fn_hash(meta.grad_fn_hash);
}

using ExternalTensorMeta = ::fixy::Tagged<const TensorMeta&, ::fixy::tags::source::External>;

[[nodiscard]] inline constexpr ExternalTensorMeta external_tensor_meta(const TensorMeta& meta
                                                                       CRUCIBLE_LIFETIMEBOUND) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::External, const TensorMeta&>(meta);
}

// The tag of a temporary descriptor would name a descriptor that ends at the
// end of the full expression.
void external_tensor_meta(const TensorMeta&& meta) = delete(
    "the tag names its descriptor by reference, and a temporary descriptor ends at the end of the full expression");

// The fields of one tensor descriptor as a writer outside the process gave
// them: a C caller of the vessel, a metadata record of a trace file, or the
// image of a descriptor in a region of the store.  No field holds a checked
// claim, so this type is not a TensorMeta, and decode_tensor_meta is the one
// door from it to a TensorMeta.  The data pointer is not a field, because
// only the caller of the door knows if the address has a meaning in this
// process.
//
// The image of the Protocol family of this type has the bytes of the image of
// a TensorMeta, part for part: the pad of the C layout and the hash of the
// grad_fn are local parts, as the local parts of a TensorMeta are.  So the
// reader of the store decodes the image of a TensorMeta as this type.
struct[[= ::fixy::derive_protocol{}]] UncheckedTensorMeta {
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
    // The padding before the storage size.  The door does not read it, and
    // the image does not hold it.
    [[= ::fixy::local_part{}]] uint32_t pad0 = 0;
    uint64_t storage_nbytes = 0;
    // The identity of an autograd object of the process of the writer.  The
    // image does not hold it, so a descriptor of the store has no grad_fn.
    [[= ::fixy::local_part{}]] uint64_t grad_fn_hash = 0;
};

inline TensorShape::TensorShape(UncheckedTensorMeta const& checked_fields) noexcept
    : rank_{::fixy::mint_refined<::fixy::bounded_above<kMaxTensorNDim>>(checked_fields.ndim)} {
    sizes_.set_lanes_(checked_fields.sizes);
    strides_.set_lanes_(checked_fields.strides);
}

// The shape of the fields of a writer outside the process, or no shape when
// the rank passes kMaxTensorNDim, a size or a stride below the rank passes
// kMaxTensorDimExtent, or a lane past the rank is not zero.  The door tests
// the rank first, and then each lane of the two blocks in one vector
// compare: one load of each block, one mask of the lanes below the rank, and
// one test of the lanes that break their claim.  So the check of outside
// data costs a few cycles at its boundary.  O(1).
[[nodiscard]] inline auto decode_tensor_shape(UncheckedTensorMeta const& fields) noexcept {
    using Answer = ::fixy::Option<TensorShape>;
    using Lanes = ::foundation::simd::vec<int64_t, kMaxTensorNDim>;
    const uint8_t ndim = fields.ndim;
    if (ndim > kMaxTensorNDim) return Answer{::fixy::none};
    const Lanes sizes = ::foundation::simd::load<Lanes>(fields.sizes.view());
    const Lanes strides = ::foundation::simd::load<Lanes>(fields.strides.view());
    const auto below_rank = ::foundation::simd::prefix_mask<Lanes>(static_cast<int>(ndim));
    const Lanes bound{kMaxTensorDimExtent};
    const auto past_bound = (sizes > bound) | (strides > bound);
    const auto not_zero = (sizes | strides) != Lanes{int64_t{0}};
    const bool has_broken_lane = ::foundation::simd::any_of((past_bound & below_rank) | (not_zero & ~below_rank));
    if (has_broken_lane) return Answer{::fixy::none};
    return Answer::some(TensorShape{fields});
}

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
// The storage generation of the TensorMeta stays zero, because a writer
// outside the process gives no generation.  Linear in kMaxTensorNDim.
[[nodiscard]] inline ::fixy::Option<TensorMeta> decode_tensor_meta(UncheckedTensorMeta const& fields,
                                                                   ::fixy::Option<TensorDataKey> data_ptr) noexcept {
    ::fixy::Option<TensorShape> shape = decode_tensor_shape(fields);
    if (shape.is_none()) return ::fixy::none;
    TensorMeta meta{};
    meta.set_shape(std::move(shape).expect("the test above found the shape"));

    // ScalarType and DeviceType are sparse, and a switch over the element
    // type ends the process for a value of no enumerator.  A device and a
    // layout fold into the content hash of a node, so a value of no
    // enumerator would change the identity of the node with no other sign.
    // enum_from reads the enumerators by reflection, so a new enumerator
    // needs no edit here.
    ::fixy::Option<ScalarType> dtype = ::fixy::enum_from<ScalarType>(fields.dtype);
    ::fixy::Option<DeviceType> device_type = ::fixy::enum_from<DeviceType>(fields.device_type);
    ::fixy::Option<Layout> layout = ::fixy::enum_from<Layout>(fields.layout);
    if (dtype.is_none() || device_type.is_none() || layout.is_none()) return ::fixy::none;
    meta.dtype = std::move(dtype).value_or(ScalarType::Undefined);
    meta.device_type = std::move(device_type).value_or(DeviceType::CPU);
    meta.layout = std::move(layout).value_or(Layout::Strided);

    const bool has_reserved_flag = (fields.flags & ~meta_flags::KNOWN_BITS) != 0;
    if (fields.device_idx < -1 || fields.requires_grad > 1 || has_reserved_flag) return ::fixy::none;
    // The record keeps one index for the host, so each stored device is the
    // device that device_of gives.
    meta.device_idx = normalize_device_index(meta.device_type, fields.device_idx);
    meta.requires_grad = fields.requires_grad == 1;
    meta.set_flags(fields.flags);

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
    // Two indexes of the tensor name one element, as two indexes of a
    // broadcast do, so the tensor has no unique view, which writes.
    Overlap,
};

// The strided view of the tensor of meta over storage, the run of its slot
// in the memory plan, or the reason that the tensor has no view.  A slot
// holds one storage of PyTorch, and its run starts at the first byte of that
// storage.  So the element at the zero index is element storage_offset of
// storage, and a view of a slice at an offset reads the elements of its base.
// The build of the slots sizes each slot to hold the offset and the extent of
// each tensor in it.  The view has the dynamic rank of the tensor, and it
// reads the elements.  O(ndim).
template <class T>
    requires TensorElement<T>
[[nodiscard]] inline ::fixy::Result<::fixy::StridedView<T const>, TensorViewRefusal>
strided_view_of(const TensorMeta& meta, ::fixy::View<T const> storage) noexcept {
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
        sizes.set(lane, static_cast<::fixy::Option<std::size_t>&&>(size).expect("the size has a value"));
        strides.set(lane, static_cast<::fixy::Option<std::size_t>&&>(stride).expect("the stride has a value"));
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

// The unique strided view of the tensor of meta over storage, the run of its
// slot in a frame, or the reason that the tensor has no such view.  The view
// takes storage, so it is the one writer of the elements of the tensor.  The
// view makes the checks of the view above, and a tensor in which two indexes
// name one element gives Overlap.  O(ndim^2).
template <class T, class Brand, std::size_t Extent>
    requires TensorElement<T>
[[nodiscard]] inline ::fixy::Result<::fixy::StridedViewMut<T, ::fixy::dynamic_rank, Brand>, TensorViewRefusal>
strided_view_of(const TensorMeta& meta, ::fixy::ViewMut<T, Brand, Extent> storage) noexcept {
    const ::fixy::View<T const> elements = storage.view();
    CRUCIBLE_TRY(view, strided_view_of(meta, elements));
    // The view above holds each element inside the storage, so the mint
    // refuses only a tensor in which two indexes name one element.
    return ::fixy::mint_strided_view_mut(
               std::move(storage), view.sizes(), view.strides(),
               ::fixy::narrow<std::size_t>(meta.storage_offset).expect("the view above refuses a negative offset"))
        .ok_or(TensorViewRefusal::Overlap);
}

}  // namespace crucible
