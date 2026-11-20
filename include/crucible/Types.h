#pragma once

#include <fixy/Core.h>
#include <fixy/Refined.h>
#include <foundation/ByteSeal.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace crucible {

// The number of bytes one element of a type occupies.
//
// A bare byte count is interchangeable with a slot count, a log index or a
// running total, all of which also fit in eight bits and mean something else
// entirely. This does not convert to any of them. The domain is zero, for the
// undefined type, and the powers of two up to sixteen, so anything above
// sixteen fails at construction rather than propagating.
//
// The bound is a precondition of the constructor. The mint checks the same
// predicate, but a report of this precondition names the door that the
// caller used.
struct ElementBytes {
    using Bytes = ::fixy::Refined<::fixy::bounded_above<uint8_t{16}>, uint8_t>;

    Bytes value_ = ::fixy::mint_refined_constant<::fixy::bounded_above<uint8_t{16}>, uint8_t{0}>();

    constexpr ElementBytes() noexcept = default;
    explicit constexpr ElementBytes(uint8_t v) noexcept : value_{checked_(v)} {}

    [[nodiscard]] constexpr uint8_t raw() const noexcept { return value_.value(); }
    [[nodiscard]] constexpr bool is_zero() const noexcept { return value_.value() == 0; }

    auto operator<=>(const ElementBytes&) const = default;

    // Widens to a size, so a total is not truncated. The multiplication is
    // unchecked. A caller that cannot bound the count needs a checked one.
    [[nodiscard]] constexpr std::size_t times(std::size_t n) const noexcept { return std::size_t{value_.value()} * n; }

private:
    [[nodiscard]] static constexpr Bytes checked_(uint8_t v) noexcept {
        CRUCIBLE_PRE(v <= uint8_t{16});
        return ::fixy::mint_refined<::fixy::bounded_above<uint8_t{16}>>(v);
    }
};

// The ordinals mirror the foreign runtime's exactly, so a value crosses the
// boundary as a plain cast in either direction.
enum class ScalarType : int8_t {
    Byte = 0,
    Char = 1,
    Short = 2,
    Int = 3,
    Long = 4,
    Half = 5,
    Float = 6,
    Double = 7,
    ComplexHalf = 8,
    ComplexFloat = 9,
    ComplexDouble = 10,
    Bool = 11,
    BFloat16 = 15,
    Float8_e5m2 = 23,
    Float8_e4m3fn = 24,
    Float8_e5m2fnuz = 25,
    Float8_e4m3fnuz = 26,
    // An exponent of eight bits with no sign and no mantissa.  It holds the
    // shared scale of a block of narrow values.
    Float8_e8m0fnu = 44,
    // Two values of four bits in one byte.  One element of this type is the
    // byte, so the extents of a tensor count bytes and not values.
    Float4_e2m1fn_x2 = 45,
    Undefined = -1,
};

// Only the undefined type has a size of zero. The postcondition carries that
// to call sites where the type is known, so size arithmetic there needs no
// guard against dividing by zero. A caller that accepts the undefined type
// handles the zero itself.
CRUCIBLE_CONST constexpr ElementBytes element_size(ScalarType const t) noexcept {
    ElementBytes const result = [t]() noexcept -> ElementBytes {
        switch (t) {
            case ScalarType::Bool:
            case ScalarType::Byte:
            case ScalarType::Char:
            case ScalarType::Float8_e5m2:
            case ScalarType::Float8_e4m3fn:
            case ScalarType::Float8_e5m2fnuz:
            case ScalarType::Float8_e4m3fnuz:
            case ScalarType::Float8_e8m0fnu:
            case ScalarType::Float4_e2m1fn_x2:
                return ElementBytes{1};
            case ScalarType::Short:
            case ScalarType::Half:
            case ScalarType::BFloat16:
                return ElementBytes{2};
            case ScalarType::Int:
            case ScalarType::Float:
            case ScalarType::ComplexHalf:
                return ElementBytes{4};
            case ScalarType::Long:
            case ScalarType::Double:
            case ScalarType::ComplexFloat:
                return ElementBytes{8};
            case ScalarType::ComplexDouble:
                return ElementBytes{16};
            case ScalarType::Undefined:
                return ElementBytes{0};
            default:
                ::fixy::unreachable();
        }
    }();
    CRUCIBLE_POST(result, t == ScalarType::Undefined || !result.is_zero());
    return result;
}

// The ScalarType of each storage format of the base.  fp8_e4m3 is the E4M3
// of the OCP specification, which the foreign runtime names Float8_e4m3fn.
// The fnuz formats of the foreign runtime have no storage type.
template <::fixy::StorageFloat T>
inline constexpr ScalarType storage_scalar_type = std::same_as<T, ::fixy::f16>      ? ScalarType::Half
                                                : std::same_as<T, ::fixy::bf16>     ? ScalarType::BFloat16
                                                : std::same_as<T, ::fixy::fp8_e4m3> ? ScalarType::Float8_e4m3fn
                                                                                    : ScalarType::Float8_e5m2;

// The ordinals mirror the foreign runtime's exactly.
enum class DeviceType : int8_t {
    CPU = 0,
    CUDA = 1,
    MKLDNN = 2,
    HIP = 6,
    XLA = 9,
    MPS = 13,
    Meta = 14,
    PrivateUse1 = 20,
};

// The index of a device of the type, as Crucible keeps it.  The host has one
// device, and PyTorch names it with the index -1 or 0.  Each host index thus
// becomes -1.  A device of a different type keeps its index.
[[nodiscard]] constexpr int8_t normalize_device_index(DeviceType type, int8_t index) noexcept {
    return type == DeviceType::CPU ? int8_t{-1} : index;
}

// The device that holds the bytes of a tensor: the type of the device and
// its index.  The index is -1 for the host, and zero or more for a device of
// the type.  The constructor gives the index of normalize_device_index, and
// the two members are private, so no TensorDevice holds a second index of
// the host.  The equality and each hash of a device then agree on what one
// device is.  The annotation refuses decode, a bit copy of the base and a
// lifetime start over bytes, the roads that skip the constructor.
// TensorMeta, TensorSlot and MemoryPlan hold the same two values in two
// fields.
class[[= ::foundation::lifetime::no_start_over_bytes{}]] TensorDevice {
    DeviceType type_ = DeviceType::CPU;
    int8_t index_ = -1;

public:
    // The host.
    constexpr TensorDevice() noexcept = default;

    // The device of the type with the index, with each host index as -1.
    constexpr TensorDevice(DeviceType type, int8_t index) noexcept
        : type_{type}, index_{normalize_device_index(type, index)} {}

    [[nodiscard]] constexpr DeviceType type() const noexcept { return type_; }
    [[nodiscard]] constexpr int8_t index() const noexcept { return index_; }

    [[nodiscard]] friend constexpr bool operator==(TensorDevice const&, TensorDevice const&) noexcept = default;
};

// The memory of the host, which holds the bytes of a pool.
inline constexpr TensorDevice kHostDevice{};

// The address of the bytes of a tensor, and the device that holds them.
using TensorData = ::fixy::DataAddress<TensorDevice>;

// The ordinals mirror the foreign runtime's up to SparseCsr.  The foreign
// runtime puts Mkldnn at 3, which crucible does not name, so the ordinals
// differ from SparseCsc on, and the vessel maps a foreign layout by name.
enum class Layout : int8_t {
    Strided = 0,
    Sparse = 1,
    SparseCsr = 2,
    SparseCsc = 3,
    SparseBsr = 4,
    SparseBsc = 5,
};

// The phase of the training step that recorded an operation.  The op flags
// of a ring entry carry it in two bits (op_flag::PHASE_MASK in
// crucible/TraceRing.h).
enum class TrainingPhase : uint8_t {
    FORWARD = 0,
    BACKWARD = 1,
    OPTIMIZER = 2,
    OTHER = 3,
};

// Distinct index types that cannot be passed for one another. Arithmetic is
// deliberately absent: a caller unwraps, computes, and wraps the result back,
// which makes every place one index is derived from another one visible.
// The check file of this header finds each class that this macro and
// CRUCIBLE_STRONG_HASH write, and it holds each one to the size of its word.
// Each class takes the hash, the byte codec and the format of the Protocol
// family from its one word (fixy::derive_protocol).
//
// Word is an unsigned integer type.  Its largest value is the none value.

#define CRUCIBLE_STRONG_ID(Name, Word)                                                         \
    struct[[= ::fixy::derive_protocol{}]] Name {                                               \
    private:                                                                                   \
        /* Private, so the explicit constructor is the only way in. A public */                \
        /* field would let a plain assignment bypass it and rewrite an */                      \
        /* identifier in place. */                                                             \
        Word v;                                                                                \
        static constexpr Word none_word = ::fixy::max_of<Word>;                                \
                                                                                               \
    public:                                                                                    \
        constexpr Name() noexcept : v(none_word) {}                                            \
        constexpr explicit Name(Word val) noexcept : v(val) {}                                 \
        /* The named form for building one identifier kind out of another, */                  \
        /* which is where a silent mix-up happens. Naming it makes every */                    \
        /* such crossing findable by one search. */                                            \
        [[nodiscard]] static constexpr Name from_raw(Word val) noexcept { return Name{val}; }  \
        [[nodiscard]] static constexpr Name none() noexcept { return Name{none_word}; }        \
        [[nodiscard]] constexpr bool is_valid() const noexcept { return v != none_word; }      \
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return is_valid(); } \
        [[nodiscard]] constexpr Word raw() const noexcept { return v; }                        \
        constexpr auto operator<=>(const Name&) const noexcept = default;                      \
    }

CRUCIBLE_STRONG_ID(OpIndex, uint32_t);
CRUCIBLE_STRONG_ID(SlotId, uint32_t);
CRUCIBLE_STRONG_ID(NodeId, uint32_t);
// The count of the first record of a run in the metadata log, from the first
// record that the log held.  The count only grows, and 64 bits do not wrap in
// the life of a process.  The slot of the record is the count masked by the
// capacity of the log.
CRUCIBLE_STRONG_ID(MetaIndex, uint64_t);

#undef CRUCIBLE_STRONG_ID

// The nesting depth of an op: the count of the ops whose record was open on
// the producer thread when the op started.  An op that no recorded op runs has
// the depth zero, and an op that an op of depth d runs has the depth d + 1.
//
// A recording kernel gives its op to the Vigil after the op runs, so the ops
// of one nest come in post-order.  The parent of an op of depth d is the first
// op after it with the depth d - 1.
//
// The count stops at the largest value of its word.  Each level of a nest is a
// call of the dispatcher on the stack of one thread, so no nest gets near that
// value.  Zero is a value of its own, and an op that names no depth has it.
class OpDepth {
public:
    constexpr OpDepth() noexcept = default;
    constexpr explicit OpDepth(uint16_t depth) noexcept : depth_{depth} {}

    [[nodiscard]] constexpr uint16_t raw() const noexcept { return depth_; }

    // True for an op that no recorded op runs.
    [[nodiscard]] constexpr bool is_top() const noexcept { return depth_ == 0; }

    // The depth of an op that an op of this depth runs.
    [[nodiscard]] constexpr OpDepth nested() const noexcept {
        return OpDepth{::fixy::saturating_add(depth_, uint16_t{1})};
    }

    constexpr auto operator<=>(const OpDepth&) const noexcept = default;

private:
    uint16_t depth_ = 0;
};

// The tag of slot i of a packed set of tags (ScalarType2 of TraceRing.h): two
// bits for each slot, slot 0 in the low bits.
inline constexpr uint32_t kScalarTypeBits = 2;

// The inline scalar slots of an op, and a count of slots, which is at most
// kScalarSlots.
inline constexpr uint32_t kScalarSlots = 5;
using ScalarCount = ::fixy::Index<kScalarSlots + 1>;

// The roles of an index and the fields of a packed word of this header.
namespace side {

// The role of an index of the inline scalar slots of an op.  The bit set of
// the data slots of an op takes a key of this role, and the bit set of the
// data arguments of a schema takes a key of the role of an argument
// (vessel/torch/entry_slots.h).  An Index<5> converts to an Index<32>, and
// the two roles keep a slot out of the set of the arguments.
struct InlineScalarSlot final {};

// The two fields of the word of ScalarSlotKinds.
struct ScalarTagsField final {};
struct DataSlotsField final {};

}  // namespace side

// The index of one inline scalar slot of an op, in its role.
using ScalarSlot = ::fixy::Side<side::InlineScalarSlot, ::fixy::Index<kScalarSlots>>;

// The slot at a position, or no value past the inline slots.  One compare.
[[nodiscard]] constexpr ::fixy::Option<ScalarSlot> scalar_slot_at(std::size_t position) noexcept {
    for (const ::fixy::Index<kScalarSlots> index : ::fixy::Index<kScalarSlots>::check(position))
        return ::fixy::Option<ScalarSlot>::some(ScalarSlot{index});
    return ::fixy::Option<ScalarSlot>{::fixy::none};
}

// The role of a scalar slot.  The type of the argument in the schema of the
// op gives the role, and the value does not.
//
// A data slot holds a value that the computation reads at run time: an
// argument of the type Scalar, float or double, or an element of a list of
// them.  A wrapped number that the dispatch mode of Python gives as a scalar
// in a position of the type Tensor is data too, because the native vessel
// records its tensor as an input.  The guard and the content hash read the
// tag of a data slot but not its value, so a loop whose step size changes at
// each iteration keeps one region.  The replay binds the value of each data
// slot at each iteration, as it binds an external tensor.  A kernel takes the
// value as an argument at run time, so no lowering folds it into a constant,
// and the bound of its recipe holds for each value.
//
// A structural slot holds a value that picks the computation: an int, a bool,
// an enumerator, an element of a list of ints, an absent optional, or the
// overflow digest.  The guard and the content hash read its value.  A short
// list of exceptions gives the role structural to some arguments of the type
// Scalar or float (kStructuralScalars of vessel/torch/entry_slots.h).
enum class ScalarRole : uint8_t {
    STRUCTURAL = 0,
    DATA = 1,
};

// The data slots of an op: the set of the slots that hold a data scalar.  The
// slots past the count of the stored scalars are not in the set.  The set is a
// BitSet of one byte, keyed by the slot in its role.  A packed record, such as
// an entry of the ring, keeps the word of the set (bits()), and from_bits gives
// the set back.
class DataSlots {
public:
    constexpr DataSlots() noexcept = default;

    // The door from the word of a record, or no value when the word holds a
    // bit past the inline slots.  One compare.  The result is an
    // Option<DataSlots>.  The return type is deduced, so the Option is named
    // only where this class is complete.
    [[nodiscard]] static constexpr auto from_bits(uint8_t bits) noexcept {
        using Result = ::fixy::Option<DataSlots>;
        for (const Slots slots : ::fixy::mint_bit_set_from_bits<kScalarSlots, ScalarSlot>(bits))
            return Result::some(DataSlots{slots});
        return Result{::fixy::none};
    }

    // The data slots of the first count slots, with each later slot taken out.
    [[nodiscard]] constexpr DataSlots first(ScalarCount count) const noexcept {
        const auto kept = static_cast<uint8_t>(uint32_t{bits()} & ((uint32_t{1} << count.raw()) - 1));
        return DataSlots{::fixy::mint_bit_set_from_bits<kScalarSlots, ScalarSlot>(kept).expect(
            "a mask of the word of a set keeps only the bits of its slots")};
    }

    // The same slots with slot set to role.
    [[nodiscard]] constexpr DataSlots with(ScalarSlot slot, ScalarRole role) const noexcept {
        DataSlots changed{*this};
        if (role == ScalarRole::DATA) {
            changed.slots_.set(slot);
        } else {
            changed.slots_.reset(slot);
        }
        return changed;
    }

    [[nodiscard]] constexpr ScalarRole role(ScalarSlot slot) const noexcept {
        return slots_.test(slot) ? ScalarRole::DATA : ScalarRole::STRUCTURAL;
    }

    [[nodiscard]] constexpr bool has_data() const noexcept { return !slots_.is_empty(); }
    [[nodiscard]] constexpr uint8_t bits() const noexcept { return slots_.bits(); }

    [[nodiscard]] friend constexpr bool operator==(const DataSlots& left, const DataSlots& right) noexcept {
        return left.slots_ == right.slots_;
    }

private:
    using Slots = ::fixy::BitSet<kScalarSlots, ScalarSlot>;

    constexpr explicit DataSlots(const Slots& slots) noexcept : slots_{slots} {}

    Slots slots_{};
};

// The kinds of the inline scalar slots of an op in one PackedWord of 16 bits
// with two fields: the tag of each slot in the bits 0 to 9, two bits for each
// slot with slot 0 in the low bits, and the data slots in the bits 10 to 14.
// Bit 15 is zero.  The bits of a slot past the count of the stored scalars are
// zero.  An op of a region keeps this word, so a TraceEntry stays 96 bytes.
class ScalarSlotKinds {
    using Word = ::fixy::PackedWord<uint16_t, ::fixy::BitField<side::ScalarTagsField, kScalarTypeBits * kScalarSlots>,
                                    ::fixy::BitField<side::DataSlotsField, kScalarSlots>>;

public:
    // The first bit of the data slots in the word, after the tags of the
    // inline slots.  The guard word of the vessel places its bits by it.
    static constexpr uint32_t kDataShift = kScalarTypeBits * kScalarSlots;

    constexpr ScalarSlotKinds() noexcept = default;

    // The door from the word of a record, or no value when the word holds the
    // bit above the data slots.  One compare.  The result is an
    // Option<ScalarSlotKinds>.  The return type is deduced, so the Option is
    // named only where this class is complete.
    [[nodiscard]] static constexpr auto from_bits(uint16_t bits) noexcept {
        using Result = ::fixy::Option<ScalarSlotKinds>;
        for (const Word word : Word::from_bits(bits))
            return Result::some(ScalarSlotKinds{word});
        return Result{::fixy::none};
    }

    // The word of the packed tags and the data slots.  The bits of the tags
    // past the inline slots go.
    [[nodiscard]] static constexpr ScalarSlotKinds of(uint16_t tags, DataSlots data) noexcept {
        const auto kept_tags = static_cast<uint16_t>(tags & ((1u << kDataShift) - 1));
        const Word tagged = Word{}.with<side::ScalarTagsField>(kept_tags).expect("the mask keeps the bits of the tags");
        return ScalarSlotKinds{tagged.with<side::DataSlotsField>(data.bits())
                                   .expect("a set of the data slots holds only the bits of the inline slots")};
    }

    // The kinds of the first count slots, with each bit of a later slot
    // cleared.
    [[nodiscard]] constexpr ScalarSlotKinds first(ScalarCount count) const noexcept {
        const uint32_t tag_mask = (uint32_t{1} << (kScalarTypeBits * count.raw())) - 1;
        return of(static_cast<uint16_t>(tags() & tag_mask), data_slots().first(count));
    }

    // The tags, two bits for each slot, slot 0 in the low bits.
    [[nodiscard]] constexpr uint16_t tags() const noexcept { return word_.get<side::ScalarTagsField>(); }
    [[nodiscard]] constexpr DataSlots data_slots() const noexcept {
        return DataSlots::from_bits(word_.get<side::DataSlotsField>())
            .expect("the field of the data slots holds the bits of the inline slots only");
    }
    [[nodiscard]] constexpr uint16_t bits() const noexcept { return word_.bits(); }

    friend constexpr bool operator==(ScalarSlotKinds, ScalarSlotKinds) noexcept = default;

private:
    constexpr explicit ScalarSlotKinds(Word word) noexcept : word_{word} {}

    Word word_{};
};

struct AllocationCountTag {};
struct AllocatorEpochTag {};

// The number of one allocation in the count of the allocations of an
// allocator of the front end, such as the CPU allocator of the vessel.
using AllocationCount = ::fixy::Id<AllocationCountTag, uint32_t>;

// The number of one period of the CPU allocator of the front end.  The front
// end starts a new period each time that it sets a CPU allocator that gives no
// generation, such as the allocator of the vessel for the replay of a region.
using AllocatorEpoch = ::fixy::Id<AllocatorEpochTag, uint32_t>;

// The generation of the allocation of a storage.  The allocator can give the
// address of a dead storage to a new storage, and the recorder sees no free.
// So the key of a storage is its address and its generation (storage_key of
// crucible/TensorMeta.h).  One word holds one of three cases:
//
//   none     zero.  The front end gives no generation, as the C entries and
//            the record of a trace file do.  The key is the address alone.
//   known    the low 31 bits of the count of the allocation (AllocationCount),
//            which the allocator of the front end gave the storage.  The key
//            names one storage of the process.  Two storages of one region at
//            one address with one generation are 2^31 allocations apart, which
//            no region holds.
//   unknown  the top bit and the low 31 bits of the period (AllocatorEpoch) in
//            which the front end read a storage that its allocator did not
//            make.  The front end starts each period so that, in the ops that
//            it records in one period, one address with an unknown generation
//            names one storage (vessel/torch/storage_generation.h gives the
//            rule of the vessel).  The key of the address and the period then
//            never joins a dead storage to a new one, at the cost of a second
//            slot for a storage that lives across two periods.  An allocator
//            that the front end does not watch can still reuse an address
//            inside one period.
//
// The count and the period are local to one process, as an address is, so no
// file and no hash holds the word.
class StorageGeneration {
public:
    constexpr StorageGeneration() noexcept = default;

    [[nodiscard]] static constexpr StorageGeneration known(AllocationCount count) noexcept {
        const uint32_t low = count.raw() & COUNT_MASK;
        return StorageGeneration{low == 0 ? 1U : low};
    }

    [[nodiscard]] static constexpr StorageGeneration unknown(AllocatorEpoch epoch) noexcept {
        return StorageGeneration{UNKNOWN_BIT | (epoch.raw() & COUNT_MASK)};
    }

    [[nodiscard]] constexpr uint32_t raw() const noexcept { return generation_; }

    // True for a storage whose allocator gave it a generation.
    [[nodiscard]] constexpr bool is_known() const noexcept {
        return generation_ != 0 && (generation_ & UNKNOWN_BIT) == 0;
    }

    // True for a storage that the front end read in a period of its
    // allocator, with no generation of that allocator.
    [[nodiscard]] constexpr bool is_unknown() const noexcept { return (generation_ & UNKNOWN_BIT) != 0; }

    constexpr auto operator<=>(const StorageGeneration&) const noexcept = default;

private:
    static constexpr uint32_t UNKNOWN_BIT = uint32_t{1} << 31;
    static constexpr uint32_t COUNT_MASK = UNKNOWN_BIT - 1;

    constexpr explicit StorageGeneration(uint32_t generation) noexcept : generation_{generation} {}

    uint32_t generation_ = 0;
};

// Distinct hash types that cannot be passed for one another. Zero means unset.
// Arithmetic is absent for the same reason as on the index types above.

#define CRUCIBLE_STRONG_HASH(Name)                                                                \
    struct[[= ::fixy::derive_protocol{}]] Name {                                                  \
    private:                                                                                      \
        uint64_t v;                                                                               \
                                                                                                  \
    public:                                                                                       \
        constexpr Name() noexcept : v(0) {}                                                       \
        constexpr explicit Name(uint64_t val) noexcept : v(val) {}                                \
        /* The named form for building one hash kind out of another, so that */                   \
        /* every such crossing is findable by one search. */                                      \
        [[nodiscard]] static constexpr Name from_raw(uint64_t val) noexcept { return Name{val}; } \
        [[nodiscard]] constexpr uint64_t raw() const noexcept { return v; }                       \
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return v != 0; }        \
        /* An end-of-region marker. The hash functions spread their output   \
     * uniformly, so this one value is reserved rather than produced. */                    \
        [[nodiscard]] static constexpr Name sentinel() noexcept { return Name{UINT64_MAX}; }      \
        [[nodiscard]] constexpr bool is_sentinel() const noexcept { return v == UINT64_MAX; }     \
        constexpr auto operator<=>(const Name&) const noexcept = default;                         \
    }

CRUCIBLE_STRONG_HASH(SchemaHash);  // identity of an operation
// The guard word of an operation: its depth, the layout of each input and each
// scalar (compute_shape_hash of vessel/torch/entry_slots.h).
CRUCIBLE_STRONG_HASH(ShapeHash);
CRUCIBLE_STRONG_HASH(ScopeHash);  // path through the module hierarchy
CRUCIBLE_STRONG_HASH(CallsiteHash);  // identity of a source location
CRUCIBLE_STRONG_HASH(ContentHash);  // structural identity of one region
CRUCIBLE_STRONG_HASH(MerkleHash);  // identity of a subtree, descendants included
CRUCIBLE_STRONG_HASH(RecipeHash);  // identity of a numerical recipe
CRUCIBLE_STRONG_HASH(NumericsHash);  // identity of the numerics policy of a run
CRUCIBLE_STRONG_HASH(RowHash);  // identity of an effect row

#undef CRUCIBLE_STRONG_HASH

namespace hash_family {
struct FamilyA {};
struct FamilyB {};
}  // namespace hash_family

// Which family a hash belongs to. The primary template is left undefined so
// that a new hash type without a specialisation fails to compile rather than
// silently defaulting into one of the families. The concepts below let a
// consumer say which family it accepts without naming every member.
template <class HashT>
struct hash_family_of;

// One specialisation per hash above, in the same order, so a new hash has an
// obvious place to go.
template <>
struct hash_family_of<SchemaHash> {
    using family = hash_family::FamilyA;
};
template <>
struct hash_family_of<ShapeHash> {
    using family = hash_family::FamilyA;
};
template <>
struct hash_family_of<ScopeHash> {
    using family = hash_family::FamilyA;
};
template <>
struct hash_family_of<CallsiteHash> {
    using family = hash_family::FamilyA;
};
template <>
struct hash_family_of<ContentHash> {
    using family = hash_family::FamilyA;
};
template <>
struct hash_family_of<MerkleHash> {
    using family = hash_family::FamilyA;
};
template <>
struct hash_family_of<RecipeHash> {
    using family = hash_family::FamilyA;
};
template <>
struct hash_family_of<NumericsHash> {
    using family = hash_family::FamilyA;
};
template <>
struct hash_family_of<RowHash> {
    using family = hash_family::FamilyA;
};

template <class HashT>
using hash_family_of_t = typename hash_family_of<HashT>::family;

template <class HashT>
concept IsFamilyA = std::is_same_v<hash_family_of_t<HashT>, hash_family::FamilyA>;

template <class HashT>
concept IsFamilyB = std::is_same_v<hash_family_of_t<HashT>, hash_family::FamilyB>;

// The two families differ in what they promise, and the difference is what
// makes each one usable.
//
// A persistent hash is byte-stable across processes, machines and builds
// within one format version. Stored keys, shared artefacts and replay
// determinism all rest on that, so a single-bit change to how one is computed
// is a format break and needs a version bump. An expectation pinned for one is
// only trustworthy if it was taken from serialised bytes, since the bytes are
// what the promise covers, not the in-memory layout that produced them.
//
// A process-local hash is deliberately not stable. It mixes in address
// entropy, which a modern loader randomises per process, to get a cheap
// intern-table probe. The same structural input hashes differently in two
// processes. Such a value must never be persisted, pinned in a stored
// expectation, or folded into a persistent hash. Anything needing a stable
// identity for the same structure must be computed by walking that structure
// without consulting any address.

// Two axes, neither of which identifies a compiled kernel on its own. One says
// what the computation does. The other says what effects it is allowed to
// have. Two regions that agree on the first and differ on the second are the
// same computation under different regimes and must occupy different cache
// slots: one of them may be shareable between installations while the other,
// carrying an effect, is not, and letting them share a slot breaks that
// silently. Both are persistent hashes, so the whole key is stable across
// processes for as long as the format version holds.
//
// The name points at the wrong consumer. The in-process KernelCache never uses
// this type. That cache takes the two hashes as separate parameters
// (MerkleDag.h:895) and holds three separate atomics for each slot
// (MerkleDag.h:700). It keeps them apart on purpose, because the hot lookup
// reads one field at a time in probe order (MerkleDag.h:710-714). Do not pack
// this type into that slot. Three static_assert statements pin the slot at 24
// bytes (MerkleDag.h:706-708).
//
// No key of this type crosses a process boundary.  The federation wire and
// the session-event batches of the Cipher carry a FederationKey
// (crucible/cipher/FederationProtocol.h): a keyed hash of 32 bytes.  A peer
// that chooses its input can find two inputs of one 64-bit hash, so the two
// 64-bit halves of this key stay inside one process.
//
// For the cache the row half is the discriminator, and it must be unique.
// MerkleDag.h:1381 supplies a row-blind region content hash and a row, so one
// content hash occupies one slot for each discipline (MerkleDag.h:669-673).
// The probe finds a slot from the content alone (MerkleDag.h:902) and then
// filters on an exact row compare (MerkleDag.h:920-922). A row mismatch
// continues the probe and ends at a miss, which is the intent. A row collision
// returns the kernel of another discipline, which is a defect.
//
// The content half has a check after the match. The cache refuses a zero
// content hash (MerkleDag.h:897) and reserves UINT64_MAX (MerkleDag.h:932-934).
// Nothing checks the row half after a match. The row half rests on
// injectivity alone.
//
// test_row_hash_distinctness.cpp supplies that injectivity at compile time. It
// asserts pairwise distinctness across a 44-entry wrapper by stance matrix, it
// refuses a collision with a reserved value, and it pins the fold anchor. A
// collision in that matrix stops the build.
//
// A check after the match in the cache probe would add no guarantee, and it is
// not available at that site. The probe already compares the full 64-bit row.
// The cache receives the row as an opaque hash, and no second witness of the
// row identity reaches it. Such a witness needs a wider parameter list and a
// wider slot, and the 24-byte pins above refuse both.
//
// The cost, measured on cpu88 of the bench host over 12 runs of
// bench_kernel_cache, worst value of each run: the live lookup gives p50
// 1.39 ns, p99 1.39 ns, p99.9 1.74 ns and max 2.55 ns, at approximately 3.6
// cycles. The worst coefficient of variation is 2.2%, and the spread of p50
// across runs is 3.0%. A second acquire load and compare for each probe is a
// large part of 3.6 cycles.
//
// The row-sibling scenario of the same bench gives a coefficient of variation
// of 45%, which makes it not valid as a latency number. It still shows the
// shape: 128 rows for one content hash form one probe chain, because the slot
// index uses the content alone.
//
// is_zero() below needs both halves at zero. RowHash{0} is the bare-type
// baseline and a valid key (MerkleDag.h:672-673), and it is the row value with
// the most traffic in the shipped code (MerkleDag.h:1381).
struct KernelCacheKey {
    ContentHash content_hash{};
    RowHash row_hash{};

    // Orders on content first and breaks ties on the row.
    auto operator<=>(const KernelCacheKey&) const noexcept = default;

    // Both axes at their default. This normally means unset rather than a real
    // region, but only the sentinel below is guaranteed never to be produced.
    [[nodiscard]] constexpr bool is_zero() const noexcept { return !content_hash && !row_hash; }

    // The value no hash function produces, which is what an empty probe slot
    // is marked with.
    [[nodiscard]] static constexpr KernelCacheKey sentinel() noexcept {
        return KernelCacheKey{ContentHash::sentinel(), RowHash::sentinel()};
    }

    [[nodiscard]] constexpr bool is_sentinel() const noexcept {
        return content_hash.is_sentinel() && row_hash.is_sentinel();
    }
};

}  // namespace crucible
