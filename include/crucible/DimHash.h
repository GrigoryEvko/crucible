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
#include <crucible/Platform.h>
#include <crucible/TensorMeta.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/safety/Simd.h>

#include <cstdint>
#include <type_traits>

namespace crucible {

// The value is reproducible from the descriptor's bytes, but it is not on
// its own a key that may be persisted. The two wrappers make a consumer
// acknowledge both before mixing or exporting the bits.
using DimHash = ::crucible::fixy::wrap::Tagged<uint64_t, ::crucible::hash_family::FamilyB>;
using DimHashDet = ::crucible::fixy::wrap::DetSafe<::crucible::fixy::wrap::DetSafeTier_v::Pure, DimHash>;

static_assert(sizeof(DimHash) == sizeof(uint64_t), "Tagged<uint64_t, hash_family::FamilyB> must stay the width of "
                                                   "its payload so the dim hash stays register-sized");
static_assert(sizeof(DimHashDet) == sizeof(uint64_t), "DetSafe<Pure, Tagged<uint64_t, hash_family::FamilyB>> must "
                                                      "stay the width of its payload so the dim hash stays "
                                                      "register-sized");
static_assert(std::is_trivially_copyable_v<DimHash>);
static_assert(std::is_trivially_copyable_v<DimHashDet>);
static_assert(std::is_standard_layout_v<DimHash>);
static_assert(std::is_standard_layout_v<DimHashDet>);

[[nodiscard]] inline constexpr DimHash dim_hash(uint64_t hash) noexcept { return DimHash{hash}; }

[[nodiscard]] inline constexpr uint64_t raw_dim_hash(const DimHash& hash) noexcept { return hash.value(); }

[[nodiscard]] inline constexpr uint64_t raw_dim_hash(const DimHashDet& hash) noexcept {
    return raw_dim_hash(hash.peek());
}

}  // namespace crucible

namespace crucible::detail {

// The descriptor's dimension count must not exceed the lane count, which its
// own layout already guarantees.
[[nodiscard, gnu::pure]] CRUCIBLE_INLINE uint64_t dim_hash_simd(const TensorMeta& meta) noexcept {
    using simd::i64x8;
    using simd::u64x8;

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
    auto sizes = simd::load<i64x8>(meta.sizes.raw_data());
    auto strides = simd::load<i64x8>(meta.strides.raw_data());

    // The first half of the constant table is for extents and the second
    // half for strides. Element-aligned again: the table is a plain global.
    auto mix_lo = simd::load<u64x8>(detail::kDimMix);
    auto mix_hi = simd::load<u64x8>(detail::kDimMix + 8);

    // Reinterpreted as unsigned to match the scalar routine's arithmetic.
    u64x8 sizes_u(sizes);
    u64x8 strides_u(strides);

    u64x8 combined = (sizes_u * mix_lo) ^ (strides_u * mix_hi);

    // Only the live lanes are folded. Zero is xor's identity, so the mask
    // and a select over zero would be equivalent, but the masked reduction
    // avoids materializing the intermediate.
    auto valid_mask = simd::prefix_mask<u64x8>(static_cast<int>(meta.ndim));
    return simd::reduce_xor(combined, valid_mask);
}

[[nodiscard, gnu::pure]] CRUCIBLE_INLINE DimHashDet dim_hash_simd_det(const TensorMeta& meta) noexcept {
    return DimHashDet{dim_hash(dim_hash_simd(meta))};
}

// The reference. A change to the algorithm has to land here and in the
// vector routine in one step, or the two stop agreeing.
[[nodiscard, gnu::pure]] CRUCIBLE_INLINE uint64_t dim_hash_scalar(const TensorMeta& meta) noexcept {
    uint64_t result = 0;
    for (uint8_t d = 0; d < meta.ndim; ++d) {
        result ^= static_cast<uint64_t>(raw_tensor_dim(meta.sizes[d])) * detail::kDimMix[d];
        result ^= static_cast<uint64_t>(raw_tensor_dim(meta.strides[d])) * detail::kDimMix[d + 8];
    }
    return result;
}

[[nodiscard, gnu::pure]] CRUCIBLE_INLINE DimHashDet dim_hash_scalar_det(const TensorMeta& meta) noexcept {
    return DimHashDet{dim_hash(dim_hash_scalar(meta))};
}

}  // namespace crucible::detail
