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
using ScalarSlot = ::fixy::Index<kInlineScalars>;

// The seed of each overflow fold: "overflow" in ASCII.
inline constexpr uint64_t kOverflowSeed = 0x6f766572666c6f77ULL;

template <bool MayOverflow>
struct ScalarArgs;

// The first kInlineScalars scalars of an operation, as the entry slots take
// them.  `stored` counts the values, and the slots past it hold zero with the
// tag INT.  `tags` holds the tag of each stored slot, two bits for each slot,
// slot 0 in the low bits.  The copy into an entry is then one copy of the
// kInlineScalars slots and one write of the tags.
struct StoredScalars {
    ::fixy::FixedArray<int64_t, kInlineScalars> values{};
    uint16_t stored = 0;
    uint16_t tags = 0;

    void push(int64_t value, crucible::ScalarType2 tag) noexcept {
        if (stored < kInlineScalars) {
            values.at(ScalarSlot::check(stored).expect("the test above keeps the count below the slots")) = value;
            tags = static_cast<uint16_t>(tags | (static_cast<uint32_t>(tag) << (crucible::kScalarTypeBits * stored)));
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

    void push(int64_t value, crucible::ScalarType2 tag) noexcept { inline_scalars.push(value, tag); }

    // An argument that boxes into None.
    void push_absent() noexcept { push(0, crucible::ScalarType2::NONE); }
};

// The scalars of an operation that can carry more than the inline slots.
// The first kCapacity stay inline, `count` counts each scalar, and `digest`
// folds each scalar and its tag.  O(1) for each push.
template <>
struct ScalarArgs<true> {
    static constexpr uint16_t kCapacity = kInlineScalars;

    StoredScalars inline_scalars{};
    uint32_t count = 0;
    uint64_t digest = kOverflowSeed;

    void push(int64_t value, crucible::ScalarType2 tag) noexcept {
        inline_scalars.push(value, tag);
        digest = ::foundation::reflect::combine_ids(digest, static_cast<uint64_t>(value));
        digest = ::foundation::reflect::combine_ids(digest, static_cast<uint64_t>(tag));
        count = ::fixy::saturating_add(count, uint32_t{1});
    }

    // An argument that boxes into None.
    void push_absent() noexcept { push(0, crucible::ScalarType2::NONE); }
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
    entry.num_scalar_args = scalars.stored;
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
        entry.num_scalar_args = static_cast<uint16_t>(digest_slot + 1);
        // The digest slot holds an integer, so its tag is INT, and the bits
        // of a slot past it are zero.
        const uint32_t kept_bits = crucible::kScalarTypeBits * digest_slot;
        entry.set_packed_scalar_types(
            static_cast<uint16_t>(scalars.inline_scalars.tags & ((uint32_t{1} << kept_bits) - 1)));
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
//   - The depth of the op, the count of its inputs, the count of its scalars
//     and the tag of each stored scalar
//   - For each input, the word of the content hash (pack_tensor_word: the
//     extents, the strides, the element type, the device, the rank, the
//     layout, the negative and conjugate bits and the storage offset)
//   - Each stored scalar slot, with the overflow digest of an op that has
//     more scalars or more inputs than the slots.
//
// A changed alpha, stride, element type or storage offset thus gives a
// different word, and the replay diverges.  The word reads no data address, no
// version counter and no autograd field, because these change at each
// iteration or in each process.  A view at another offset of one storage reads
// other elements, so a loop whose view moves at each iteration gets no
// period, as a loop with a changing scalar gets none.
//
// A scalar that changes at each iteration, such as the step size of AdamW
// with no fused kernel or a schedule of the learning rate, changes the word
// of its op at each iteration.  Then the detector finds no period, and the
// Vigil records and builds no region.  A later design binds such a scalar
// as an input of the region, as an external tensor is bound.  That design
// splits the word into a part of the structure, which keys the detector, and
// a part of the values, which the replay binds at each iteration.
//
// The three recording paths call this function after they write the scalar
// slots of the entry, so their words agree.  O(inputs + kInlineScalars).
// =====================================================================

// The seed of the guard word: "guardwrd" in ASCII.
inline constexpr uint64_t kGuardSeed = 0x6775617264777264ULL;

[[nodiscard]] inline crucible::ShapeHash compute_shape_hash(::fixy::View<const crucible::TensorMeta> inputs,
                                                            crucible::TraceRing::Entry const& entry,
                                                            crucible::OpDepth depth) noexcept {
    // The slots past the count hold zero, and their tags are zero.  The mask
    // keeps each bit of a slot past the count out of the word all the same.
    const uint32_t stored = ::fixy::min(uint32_t{entry.num_scalar_args}, uint32_t{kInlineScalars});
    const uint32_t tag_mask = (uint32_t{1} << (crucible::kScalarTypeBits * stored)) - 1;
    const uint64_t header = uint64_t{entry.num_inputs} | (uint64_t{entry.num_scalar_args} << 16)
                          | (uint64_t{entry.packed_scalar_types() & tag_mask} << 32) | (uint64_t{depth.raw()} << 48);

    uint64_t word = ::foundation::reflect::combine_ids(kGuardSeed, header);
    for (crucible::TensorMeta const& input : inputs)
        word = ::foundation::reflect::combine_ids(
            word, crucible::pack_tensor_word(input, crucible::detail::dim_hash_scalar_det(input)));
    const ::fixy::View<int64_t const> scalars =
        entry.scalar_values.view().window(0, stored).expect("the count of the stored scalars fits the slots");
    for (const int64_t scalar : scalars)
        word = ::foundation::reflect::combine_ids(word, static_cast<uint64_t>(scalar));
    return crucible::ShapeHash{word};
}

}  // namespace crucible::vessel
