#pragma once

// A per-tensor hash of the extents and strides, folded with xor.
//
// The vector routine and the scalar one must agree bit for bit on every
// instruction set. What licenses that is xor being associative and
// commutative over integers: the two routines combine the same terms in
// different orders and still land on the same value. The lane count is fixed
// rather than derived from the target, so lane d is dimension d everywhere,
// and each term is an exact integer product.

#include <crucible/Expr.h>
#include <crucible/TensorMeta.h>
#include <fixy/Bands.h>
#include <fixy/Tagged.h>
#include <foundation/Platform.h>
#include <foundation/Simd.h>

#include <cstdint>
#include <type_traits>

namespace crucible {

// The value is reproducible from the descriptor's bytes, but it is not on
// its own a key that may be persisted. The two wrappers make a consumer
// acknowledge both before mixing or exporting the bits.
using DimHash = ::fixy::Tagged<uint64_t, ::crucible::hash_family::FamilyB>;
using DimHashDet = ::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, DimHash>;

[[nodiscard]] inline constexpr DimHash dim_hash(uint64_t hash) noexcept {
    return ::fixy::mint_tagged<::crucible::hash_family::FamilyB>(hash);
}

[[nodiscard]] inline constexpr uint64_t raw_dim_hash(const DimHash& hash) noexcept { return hash.value(); }

[[nodiscard]] inline constexpr uint64_t raw_dim_hash(const DimHashDet& hash) noexcept {
    return raw_dim_hash(hash.peek());
}

}  // namespace crucible

namespace crucible::detail {

// A distinct constant per lane, so an extent and a stride at the same index
// cannot cancel when the fold xors them together. One table is for the
// extents and one for the strides. Lane d of the extents holds the seed times
// d + 1, and lane d of the strides holds the seed times d + 9. The seed is
// odd, so a multiplication by it is a bijection of the 64-bit words, and the
// sixteen constants are distinct.
//
// Folding with xor rather than mixing each term keeps the sixteen products
// independent of one another, leaving a single mix at the end instead of a
// chain of them.
inline constexpr uint64_t kDimMixSeed = 0x9E3779B97F4A7C15ULL;
inline constexpr ::fixy::FixedArray<uint64_t, kMaxTensorNDim> kDimMixExtents{
    kDimMixSeed * 1, kDimMixSeed * 2, kDimMixSeed * 3, kDimMixSeed * 4,
    kDimMixSeed * 5, kDimMixSeed * 6, kDimMixSeed * 7, kDimMixSeed * 8,
};
inline constexpr ::fixy::FixedArray<uint64_t, kMaxTensorNDim> kDimMixStrides{
    kDimMixSeed * 9,  kDimMixSeed * 10, kDimMixSeed * 11, kDimMixSeed * 12,
    kDimMixSeed * 13, kDimMixSeed * 14, kDimMixSeed * 15, kDimMixSeed * 16,
};

// The constant of the storage offset of a tensor (pack_tensor_word of
// crucible/MerkleDag.h): the seed times 17, distinct from the sixteen
// constants of the lanes.  An offset of zero adds nothing to a fold.
inline constexpr uint64_t kStorageOffsetMix = kDimMixSeed * 17;

// The descriptor's dimension count must not exceed the lane count, which its
// own layout already guarantees.
[[nodiscard, gnu::pure]] CRUCIBLE_INLINE uint64_t dim_hash_simd(const TensorMeta& meta) noexcept {
    using ::foundation::simd::i64x8;
    using ::foundation::simd::u64x8;

    // The full width is loaded even for a descriptor of lower rank. The read
    // stays in bounds because each block is exactly the lane count wide, and
    // the values past the dimension count never reach the result: the
    // reduction below is masked to that count. So this routine does not
    // depend on the tail lanes being zero, and a reader looking for what does
    // will find it named in TensorMeta.h.
    //
    // The load is the element-aligned form. The descriptor is aligned for
    // its element type and no further, so the vector-aligned form would be
    // unsound even though each array is a full vector wide.
    auto sizes = ::foundation::simd::load<i64x8>(meta.sizes.lanes());
    auto strides = ::foundation::simd::load<i64x8>(meta.strides.lanes());

    // The first half of the constant table is for extents and the second
    // half for strides. Element-aligned again: the table is a plain global.
    auto mix_lo = ::foundation::simd::load<u64x8>(kDimMixExtents.view());
    auto mix_hi = ::foundation::simd::load<u64x8>(kDimMixStrides.view());

    // Reinterpreted as unsigned to match the scalar routine's arithmetic.
    u64x8 sizes_u(sizes);
    u64x8 strides_u(strides);

    u64x8 combined = (sizes_u * mix_lo) ^ (strides_u * mix_hi);

    // Only the live lanes are folded. Zero is xor's identity, so the mask
    // and a select over zero would be equivalent, but the masked reduction
    // avoids materializing the intermediate.
    auto valid_mask = ::foundation::simd::prefix_mask<u64x8>(static_cast<int>(meta.ndim));
    return ::foundation::simd::reduce_xor(combined, valid_mask);
}

[[nodiscard, gnu::pure]] CRUCIBLE_INLINE DimHashDet dim_hash_simd_det(const TensorMeta& meta) noexcept {
    return ::fixy::mint_band<DimHashDet>(dim_hash(dim_hash_simd(meta)));
}

// The reference. A change to the algorithm has to land here and in the
// vector routine in one step, or the two stop agreeing.
[[nodiscard, gnu::pure]] CRUCIBLE_INLINE uint64_t dim_hash_scalar(const TensorMeta& meta) noexcept {
    uint64_t result = 0;
    for (::fixy::Index<kMaxTensorNDim> d : kDimMixExtents.indices()) {
        if (d.raw() >= meta.ndim) break;
        result ^= static_cast<uint64_t>(raw_tensor_dim(meta.sizes[d.raw()])) * kDimMixExtents.at(d);
        result ^= static_cast<uint64_t>(raw_tensor_dim(meta.strides[d.raw()])) * kDimMixStrides.at(d);
    }
    return result;
}

[[nodiscard, gnu::pure]] CRUCIBLE_INLINE DimHashDet dim_hash_scalar_det(const TensorMeta& meta) noexcept {
    return ::fixy::mint_band<DimHashDet>(dim_hash(dim_hash_scalar(meta)));
}

}  // namespace crucible::detail
