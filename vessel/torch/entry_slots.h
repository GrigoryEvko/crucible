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
// digest folds each scalar, the count of the scalars, the metadata of each
// input tensor past the metadata limit, and the count of those tensors.  An
// operation that overflows no limit keeps its scalars as they are and has no
// digest, so its entry is the entry that a path with no digest writes.
//
// The boxed fallback and the unboxed kernels use these types, so the two
// paths write the same entry for one operation.  The header holds no PyTorch
// type, so a test of each build can drive it.

#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <fixy/Checked.h>
#include <fixy/Core.h>
#include <fixy/FixedArray.h>
#include <foundation/reflect/Hash.h>

#include <cstdint>

namespace crucible::vessel {

// The metadata slots of one entry.  Both recording paths stop here, so an
// operation with more tensors records its first MAX_INLINE_METAS, identically
// on either path.
inline constexpr uint32_t MAX_INLINE_METAS = 32;

// The inline scalar slots of a trace entry.
inline constexpr uint16_t kInlineScalars =
    static_cast<uint16_t>(decltype(crucible::TraceRing::Entry::scalar_values)::capacity);

// The seed of each overflow fold: "overflow" in ASCII.
inline constexpr uint64_t kOverflowSeed = 0x6f766572666c6f77ULL;

template <bool MayOverflow>
struct ScalarArgs;

// The first kInlineScalars scalars of an operation, as the entry slots take
// them.  `stored` counts the values, and the slots past it hold zero.  The
// copy into an entry is then one memcpy of `stored` values.
struct StoredScalars {
    ::fixy::FixedArray<int64_t, kInlineScalars> values{};
    uint16_t stored = 0;

    void push(int64_t value) noexcept {
        if (stored < kInlineScalars) values[stored++] = value;
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

    void push(int64_t value) noexcept { inline_scalars.push(value); }
};

// The scalars of an operation that can carry more than the inline slots.
// The first kCapacity stay inline, `count` counts each scalar, and `digest`
// folds each scalar.  O(1) for each push.
template <>
struct ScalarArgs<true> {
    static constexpr uint16_t kCapacity = kInlineScalars;

    StoredScalars inline_scalars{};
    uint32_t count = 0;
    uint64_t digest = kOverflowSeed;

    void push(int64_t value) noexcept {
        inline_scalars.push(value);
        digest = ::foundation::reflect::combine_ids(digest, static_cast<uint64_t>(value));
        count = ::fixy::saturating_add(count, uint32_t{1});
    }
};

// The input tensors of an operation past the metadata limit.  Each one folds
// the fields that the content hash reads of an inline input: the rank, each
// extent, each stride, the element type and the device.  O(ndim) for each
// fold.
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
        const uint64_t placement = static_cast<uint64_t>(static_cast<uint8_t>(meta.dtype))
                                 | (static_cast<uint64_t>(static_cast<uint8_t>(meta.device_type)) << 8)
                                 | (static_cast<uint64_t>(static_cast<uint8_t>(meta.device_idx)) << 16);
        digest = ::foundation::reflect::combine_ids(digest, placement);
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

// Copies the stored scalars into the first slots of an entry.  The loop of a
// counted index over two arrays compiles to one memcpy.  O(kInlineScalars).
inline void copy_stored_scalars(crucible::TraceRing::Entry& entry, StoredScalars const& scalars) noexcept {
    entry.num_scalar_args = scalars.stored;
    for (uint16_t i = 0; i < scalars.stored; i++)
        entry.scalar_values[i] = scalars.values[i];
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
        entry.scalar_values[digest_slot] = static_cast<int64_t>(digest);
        entry.num_scalar_args = static_cast<uint16_t>(digest_slot + 1);
    }
}

}  // namespace crucible::vessel
