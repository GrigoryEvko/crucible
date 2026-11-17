#pragma once

// The scalar slots of a trace entry, and the digest of what does not fit.
//
// A trace entry holds five scalars inline (TraceRing::Entry::scalar_values),
// and each recording path keeps the metadata of MAX_INLINE_METAS tensors.
// The content hash of a region folds the scalars and the input metadata that
// the entry carries, so a scalar or an input tensor past those limits would
// stay out of the hash.  A fused optimizer step carries one step size for
// each parameter, and two steps that differ only past the fifth scalar would
// then hash equal.
//
// An operation that overflows a limit keeps the scalars before its digest
// slot as they are, and the digest slot holds the overflow digest.  The
// digest folds each scalar with its tag, the count of the scalars, the
// metadata of each input tensor past the metadata limit, and the count of
// those tensors.  The digest slot has the tag INT.  An operation that
// overflows no limit keeps its scalars as they are and has no digest, so its
// entry is the entry that a path with no digest writes.
//
// Each slot has the tag of its boxed value (ScalarType2 of TraceRing.h).  An
// argument that boxes into None takes one slot with push_absent, also an
// absent optional tensor and a None element of a list of optional tensors.
//
// The guard word of an entry (compute_shape_hash) folds the input metadata
// and the scalar slots after they are written.
//
// The boxed fallback, the unboxed kernels and the C entries use these types,
// so the three paths write the same entry for one operation.  The header
// holds no PyTorch type, so a test of each build can drive it.

#include <crucible/MerkleDag.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <crucible/Types.h>
#include <fixy/Core.h>
#include <foundation/reflect/Hash.h>

#include <cstdint>

namespace crucible::vessel {

// The metadata slots of one entry.  Both recording paths stop here, so an
// operation with more tensors records its first MAX_INLINE_METAS, identically
// on either path.
inline constexpr uint32_t MAX_INLINE_METAS = 32;

// The inline scalar slots of a trace entry.
inline constexpr uint16_t kInlineScalars =
    static_cast<uint16_t>(decltype(crucible::TraceRing::Entry::scalar_values)::size());

// The index of an inline scalar slot.
using ScalarSlot = crucible::ScalarSlot;

// The seed of each overflow fold: "overflow" in ASCII.
inline constexpr uint64_t kOverflowSeed = 0x6f766572666c6f77ULL;

// =====================================================================
// FNV-1a
//
// One definition for both recording paths, the Python ctypes path in
// vessel_api.cpp and the list of exceptions below.  The background thread
// joins a trace entry to the schema table on this hash, so two spellings of
// the fold would give two schema hashes for one operator.  The guard word of
// an entry is a different fold (compute_shape_hash).
// =====================================================================

inline constexpr uint64_t kFnvOffset = 0xcbf29ce484222325ULL;
inline constexpr uint64_t kFnvPrime = 0x100000001b3ULL;

// One step of the fold: one byte goes into the accumulator.
[[nodiscard]] constexpr uint64_t fnv1a_step(uint64_t accumulator, uint8_t byte) noexcept {
    return (accumulator ^ byte) * kFnvPrime;
}

// The fold over the characters of a text.  Complexity: O(n) in the
// characters.
[[nodiscard]] constexpr uint64_t fnv1a_text(::fixy::TextView text, uint64_t accumulator = kFnvOffset) noexcept {
    for (const char character : text)
        accumulator = fnv1a_step(accumulator, static_cast<uint8_t>(character));
    return accumulator;
}

// =====================================================================
// The role of a scalar argument
//
// The type of an argument in the ATen schema gives the role of each slot that
// it fills (ScalarRole of crucible/Types.h).  An argument of the type Scalar,
// float or double, and each element of a list of them, is data.  An argument
// of each other type is structural.  An optional takes the role of its
// payload, and an absent optional fills one structural slot.  The rule reads
// the schema of PyTorch, which is its stable contract, and no value of the
// argument.
//
// A data value never changes the shape of an output or the op sequence: an
// op whose decomposition depends on the value runs other nested ops, and the
// guard of the first different op diverges.  A kernel that takes a data value
// gives each value its meaning at run time, also a value with an arm of its
// own, such as beta = 0 of addmm, which reads no element of its input.
//
// The list below gives the role structural to the arguments of the type
// Scalar or float whose value changes the shape of an output or picks the arm
// of the kernel.  Each row names the schema, the position of the argument in
// the schema, and the reason.
// =====================================================================

// The position of an argument in the schema of an operator.  The unboxed
// kernels and the boxed fallback count the arguments in the same order.  An
// operator of the table of aten_op_table.h has at most 32 arguments, and an
// argument at a later position is structural on the two paths.
inline constexpr uint32_t kMaxSchemaArguments = 32;
using ArgumentPosition = ::fixy::Index<kMaxSchemaArguments>;

// One argument of the type Scalar or float that is structural.
struct StructuralScalar {
    ::fixy::TextView schema;
    uint32_t position = 0;
    ::fixy::TextView reason;
};

inline constexpr ::fixy::FixedArray<StructuralScalar, 16> kStructuralScalars{
    // The kernel of pow picks an arm by the exponent: a square, a square
    // root, a reciprocal or a general power.  So the value guards the arm.
    StructuralScalar{"aten::pow.Tensor_Scalar", 1, "the exponent picks the arm of the kernel"},
    StructuralScalar{"aten::pow.Tensor_Scalar_out", 1, "the exponent picks the arm of the kernel"},
    StructuralScalar{"aten::pow_.Scalar", 1, "the exponent picks the arm of the kernel"},
    // The values of arange give the count of the elements of its output.
    StructuralScalar{"aten::arange", 0, "the end gives the length of the output"},
    StructuralScalar{"aten::arange.start", 0, "the start gives the length of the output"},
    StructuralScalar{"aten::arange.start", 1, "the end gives the length of the output"},
    StructuralScalar{"aten::arange.start_step", 0, "the start gives the length of the output"},
    StructuralScalar{"aten::arange.start_step", 1, "the end gives the length of the output"},
    StructuralScalar{"aten::arange.start_step", 2, "the step gives the length of the output"},
    StructuralScalar{"aten::arange.out", 0, "the end gives the length of the output"},
    StructuralScalar{"aten::arange.start_out", 0, "the start gives the length of the output"},
    StructuralScalar{"aten::arange.start_out", 1, "the end gives the length of the output"},
    StructuralScalar{"aten::arange.start_out", 2, "the step gives the length of the output"},
    // The order of a vector norm picks the arm of the reduction: a count, a
    // sum of the magnitudes, a root of the sum of the squares, a maximum or a
    // general power.  clip_grad_norm_ calls the two of them.
    StructuralScalar{"aten::linalg_vector_norm", 1, "the order picks the arm of the reduction"},
    StructuralScalar{"aten::_foreach_norm.Scalar", 1, "the order picks the arm of the reduction"},
    StructuralScalar{"aten::norm.Scalar", 1, "the order picks the arm of the reduction"},
};

// True when the argument at position of the operator of schema_hash is in
// kStructuralScalars.  O(rows): the unboxed kernels ask at compile time, and
// the boxed fallback asks once for each schema, in its cold miss.
[[nodiscard]] constexpr bool is_structural_scalar(crucible::SchemaHash schema_hash,
                                                  ArgumentPosition position) noexcept {
    for (const StructuralScalar& row : kStructuralScalars) {
        if (fnv1a_text(row.schema) == schema_hash.raw() && row.position == position.raw()) return true;
    }
    return false;
}

// The arguments of a schema whose slots are data: the set of their positions.
// An argument past kMaxSchemaArguments is structural.  The set is a BitSet of
// one word of 32 bits, keyed by the position.  A cache slot of the boxed
// fallback keeps the word of the set (bits()), and from_bits gives the set
// back.
class DataArguments {
public:
    constexpr DataArguments() noexcept = default;

    // The door from the word of a cache slot.  A word of 32 bits holds no bit
    // past the positions, so each word gives a set.
    [[nodiscard]] static constexpr DataArguments from_bits(uint32_t bits) noexcept {
        return DataArguments{::fixy::mint_bit_set_from_bits<kMaxSchemaArguments, ArgumentPosition>(bits).expect(
            "a word of 32 bits holds only the bits of the positions")};
    }

    // The same arguments with the argument at position data.
    [[nodiscard]] constexpr DataArguments with_data(ArgumentPosition position) const noexcept {
        DataArguments changed{*this};
        changed.positions_.set(position);
        return changed;
    }

    // The role of the argument at position, and structural past the last
    // position.
    [[nodiscard]] constexpr crucible::ScalarRole role(size_t position) const noexcept {
        for (const ArgumentPosition argument : ArgumentPosition::check(position))
            return positions_.test(argument) ? crucible::ScalarRole::DATA : crucible::ScalarRole::STRUCTURAL;
        return crucible::ScalarRole::STRUCTURAL;
    }

    [[nodiscard]] constexpr uint32_t bits() const noexcept { return positions_.bits(); }

private:
    using Positions = ::fixy::BitSet<kMaxSchemaArguments, ArgumentPosition>;

    constexpr explicit DataArguments(const Positions& positions) noexcept : positions_{positions} {}

    Positions positions_{};
};

template <bool MayOverflow>
struct ScalarArgs;

// The first kInlineScalars scalars of an operation, as the entry slots take
// them.  `stored` counts the values, and the slots past it hold zero with the
// tag INT.  `tags` holds the tag of each stored slot, two bits for each slot,
// slot 0 in the low bits, and `data` holds the data slots.  The copy into an
// entry is then one copy of the kInlineScalars slots and one write of the
// tags and of the data slots.
struct StoredScalars {
    ::fixy::FixedArray<int64_t, kInlineScalars> values{};
    uint16_t stored = 0;
    uint16_t tags = 0;
    crucible::DataSlots data{};

    void push(int64_t value, crucible::ScalarType2 tag, crucible::ScalarRole role) noexcept {
        for (const ScalarSlot slot : ScalarSlot::check(stored)) {
            values.at(slot) = value;
            tags = static_cast<uint16_t>(tags | (static_cast<uint32_t>(tag) << (crucible::kScalarTypeBits * stored)));
            data = data.with(slot, role);
            ++stored;
        }
    }
};

// The scalars of an operation whose scalar count has a compile-time bound at
// or below the inline slots.
//
// The width is taken from the entry field it is copied into. The unboxed path
// certifies its entries at compile time from this constant, so a count that
// can exceed the field has to fail the build rather than the entry.
template <>
struct ScalarArgs<false> {
    static constexpr uint16_t kCapacity = kInlineScalars;

    StoredScalars inline_scalars{};

    void push(int64_t value, crucible::ScalarType2 tag, crucible::ScalarRole role) noexcept {
        inline_scalars.push(value, tag, role);
    }

    // An argument that boxes into None.  Its absence is structural.
    void push_absent() noexcept { push(0, crucible::ScalarType2::NONE, crucible::ScalarRole::STRUCTURAL); }
};

// The scalars of an operation that can carry more than the inline slots.
// The first kCapacity stay inline, `count` counts each scalar, and `digest`
// folds each scalar and its tag.  The digest folds no value of a data scalar
// in the first kCapacity - 1 slots, because those slots stay inline in each
// entry and the replay binds them.  An entry that overflows puts its digest
// in the last slot or before it, so a data scalar at or past the last slot
// can go into the digest.  Its value folds, and in such an entry it is
// structural (write_scalar_slots).  O(1) for each push.
template <>
struct ScalarArgs<true> {
    static constexpr uint16_t kCapacity = kInlineScalars;

    StoredScalars inline_scalars{};
    uint32_t count = 0;
    uint64_t digest = kOverflowSeed;

    void push(int64_t value, crucible::ScalarType2 tag, crucible::ScalarRole role) noexcept {
        const bool can_go_into_digest = count >= uint32_t{kCapacity} - 1;
        const bool is_bound_inline = role == crucible::ScalarRole::DATA && !can_go_into_digest;
        inline_scalars.push(value, tag, role);
        digest = ::foundation::reflect::combine_ids(digest, is_bound_inline ? 0 : static_cast<uint64_t>(value));
        digest = ::foundation::reflect::combine_ids(digest, static_cast<uint64_t>(tag));
        count = ::fixy::saturating_add(count, uint32_t{1});
    }

    // An argument that boxes into None.  Its absence is structural.
    void push_absent() noexcept { push(0, crucible::ScalarType2::NONE, crucible::ScalarRole::STRUCTURAL); }
};

// The input tensors of an operation past the metadata limit.  Each one folds
// the fields that the content hash reads of an inline input: the rank, each
// extent, each stride, the element type, the device, the layout, the negative
// and conjugate bits and the storage offset.  O(ndim) for each fold.
struct InputOverflow {
    uint32_t count = 0;
    uint64_t digest = kOverflowSeed;

    void fold(crucible::TensorMeta const& meta) noexcept {
        digest = ::foundation::reflect::combine_ids(digest, meta.ndim);
        for (uint8_t d = 0; d < meta.ndim; d++) {
            digest = ::foundation::reflect::combine_ids(digest,
                                                        static_cast<uint64_t>(crucible::raw_tensor_dim(meta.sizes[d])));
            digest = ::foundation::reflect::combine_ids(
                digest, static_cast<uint64_t>(crucible::raw_tensor_dim(meta.strides[d])));
        }
        // The device comes through device_of, as in the content hash.  The
        // layout, the negative and conjugate bits and the storage offset fold
        // as in the word of a tensor (pack_tensor_word), so an input past the
        // limit keeps each fact that changes the elements that the op reads.
        constexpr auto kValueBits = static_cast<uint8_t>(crucible::meta_flags::IS_NEG | crucible::meta_flags::IS_CONJ);
        const crucible::TensorDevice device = crucible::device_of(meta);
        const uint64_t placement = static_cast<uint64_t>(static_cast<uint8_t>(meta.dtype))
                                 | (static_cast<uint64_t>(static_cast<uint8_t>(device.type())) << 8)
                                 | (static_cast<uint64_t>(::fixy::bit_cast<uint8_t>(device.index())) << 16)
                                 | (static_cast<uint64_t>(::fixy::bit_cast<uint8_t>(meta.layout)) << 32)
                                 | (static_cast<uint64_t>(meta.flags & kValueBits) << 40);
        const uint64_t offset_word = static_cast<uint64_t>(meta.storage_offset) * crucible::detail::kStorageOffsetMix;
        digest = ::foundation::reflect::combine_ids(digest, placement ^ offset_word);
        count = ::fixy::saturating_add(count, uint32_t{1});
    }
};

// The input overflow of an operation that cannot overflow: no state.
struct NoInputOverflow {};

template <bool MayOverflow>
struct input_overflow_of {
    using type = InputOverflow;
};

template <>
struct input_overflow_of<false> {
    using type = NoInputOverflow;
};

// The meta of a result that is one scalar, such as the value of
// _local_scalar_dense.  The value lives on the host and has no storage, so
// the meta has rank zero and no data address.  The background pipeline gives
// an output with no data address no slot, and the operation keeps the output
// that drives the control flow of the caller.
[[nodiscard]] inline crucible::TensorMeta scalar_result_meta(crucible::ScalarType dtype) noexcept {
    crucible::TensorMeta meta{};
    meta.dtype = dtype;
    meta.device_type = crucible::DeviceType::CPU;
    return meta;
}

// Copies the stored scalars and their tags into the slots of an entry.  The
// slots past the count hold zero with the tag INT in scalars, so the entry
// holds them there too.  The two Views have one fixed extent, so the copy of
// the kInlineScalars slots has no check and no count.  The tag of slot 4 is in
// op_flags, and the other bits of op_flags stay.  O(kInlineScalars).
inline void copy_stored_scalars(crucible::TraceRing::Entry& entry, StoredScalars const& scalars) noexcept {
    entry.num_scalar_args = static_cast<uint8_t>(scalars.stored);
    entry.set_data_slots(scalars.data);
    ::fixy::copy(entry.scalar_values.view(), scalars.values.view());
    entry.set_packed_scalar_types(scalars.tags);
}

// Writes the scalar slots of an entry for an operation that cannot
// overflow.  O(kInlineScalars).
inline void write_scalar_slots(crucible::TraceRing::Entry& entry, ScalarArgs<false> const& scalars) noexcept {
    copy_stored_scalars(entry, scalars.inline_scalars);
}

// Writes the scalar slots of an entry for an operation that can overflow.
// With no overflow, the slots hold the scalars as they are.  With one, the
// first scalars stay, and the slot after them holds the overflow digest.
// O(kInlineScalars).
inline void write_scalar_slots(crucible::TraceRing::Entry& entry, ScalarArgs<true> const& scalars,
                               InputOverflow const& inputs) noexcept {
    copy_stored_scalars(entry, scalars.inline_scalars);
    const uint16_t stored = scalars.inline_scalars.stored;
    const bool overflows = scalars.count > kInlineScalars || inputs.count != 0;
    if (overflows) [[unlikely]] {
        const uint16_t digest_slot = ::fixy::min(stored, static_cast<uint16_t>(kInlineScalars - 1));
        uint64_t digest = ::foundation::reflect::combine_ids(scalars.digest, scalars.count);
        digest = ::foundation::reflect::combine_ids(digest, inputs.digest);
        digest = ::foundation::reflect::combine_ids(digest, inputs.count);
        entry.scalar_values.at(ScalarSlot::check(digest_slot).expect("the digest slot is the last slot or before it")) =
            static_cast<int64_t>(digest);
        entry.num_scalar_args = static_cast<uint8_t>(digest_slot + 1);
        // The digest slot holds an integer, so its tag is INT, and it is
        // structural.  The bits of a slot past it are zero.
        const uint32_t kept_bits = crucible::kScalarTypeBits * digest_slot;
        entry.set_packed_scalar_types(
            static_cast<uint16_t>(scalars.inline_scalars.tags & ((uint32_t{1} << kept_bits) - 1)));
        entry.set_data_slots(scalars.inline_scalars.data.first(
            crucible::ScalarCount::check(digest_slot).expect("the digest slot is a count of the slots")));
    }
}

// =====================================================================
// The guard word of an operation
//
// The replay compares two words of each live op with the op of the region:
// the schema hash and this word (ReplayEngine::advance).  The alignment, the
// search for an alternate region and the iteration detector compare the same
// pair.  So the word holds each field that the content hash of a region folds
// for the op and that the op shows before it runs, apart from the wiring:
//
//   - The depth of the op, the count of its inputs, the count of its scalars,
//     the tag of each stored scalar and the data slots
//   - For each input, the word of the content hash (pack_tensor_word: the
//     extents, the strides, the element type, the device, the rank, the
//     layout, the negative and conjugate bits and the storage offset)
//   - The value of each structural slot, with the overflow digest of an op
//     that has more scalars or more inputs than the slots.  A data slot
//     folds zero, and its role bit tells it from a structural zero.
//
// A changed stride, element type, storage offset, structural scalar or tag
// thus gives a different word, and the replay diverges.  The word reads no
// data address, no version counter and no autograd field, because these
// change at each iteration or in each process.  A view at another offset of
// one storage reads other elements, so a loop whose view moves at each
// iteration gets no period.
//
// A data scalar is an input of the region, as an external tensor is (the
// role of crucible/Types.h).  A loop whose step size or learning rate changes
// at each iteration thus keeps one word for each op, the detector finds its
// period, and the replay binds the new value at each iteration.  An op with
// no data slot gives the word of a fold that reads no role.
//
// The three recording paths call this function after they write the scalar
// slots of the entry, so their words agree.  O(inputs + kInlineScalars).
// =====================================================================

// The seed of the guard word: "guardwrd" in ASCII.
inline constexpr uint64_t kGuardSeed = 0x6775617264777264ULL;

// The bit of the header of the guard word where the data slots start, after
// the tags of the five slots.
inline constexpr uint32_t kGuardDataShift = 32 + crucible::ScalarSlotKinds::kDataShift;

[[nodiscard]] inline crucible::ShapeHash compute_shape_hash(::fixy::View<const crucible::TensorMeta> inputs,
                                                            crucible::TraceRing::Entry const& entry,
                                                            crucible::OpDepth depth) noexcept {
    // The slots past the count hold zero, and their tags and data bits are
    // zero.  The masks keep each bit of a slot past the count out of the word
    // all the same.
    const uint32_t stored = ::fixy::min(uint32_t{entry.num_scalar_args}, uint32_t{kInlineScalars});
    const uint32_t tag_mask = (uint32_t{1} << (crucible::kScalarTypeBits * stored)) - 1;
    const uint32_t data_bits = uint32_t{entry.data_slot_bits} & ((uint32_t{1} << stored) - 1);
    const uint64_t header = uint64_t{entry.num_inputs} | (uint64_t{entry.num_scalar_args} << 16)
                          | (uint64_t{entry.packed_scalar_types() & tag_mask} << 32)
                          | (uint64_t{data_bits} << kGuardDataShift) | (uint64_t{depth.raw()} << 48);

    uint64_t word = ::foundation::reflect::combine_ids(kGuardSeed, header);
    for (crucible::TensorMeta const& input : inputs)
        word = ::foundation::reflect::combine_ids(
            word, crucible::pack_tensor_word(input, crucible::detail::dim_hash_scalar_det(input)));
    // A data slot folds zero, as in the content hash (ContentHashFold of
    // crucible/MerkleDag.h).  The mask of a slot is each bit for a
    // structural slot and no bit for a data slot, so the fold has no branch.
    const ::fixy::View<int64_t const> scalars =
        entry.scalar_values.view().window(0, stored).expect("the count of the stored scalars fits the slots");
    uint32_t data_left = data_bits;
    for (const int64_t scalar : scalars) {
        const uint64_t structural_mask = uint64_t{data_left & 1u} - 1;
        word = ::foundation::reflect::combine_ids(word, static_cast<uint64_t>(scalar) & structural_mask);
        data_left >>= 1;
    }
    return crucible::ShapeHash{word};
}

}  // namespace crucible::vessel
