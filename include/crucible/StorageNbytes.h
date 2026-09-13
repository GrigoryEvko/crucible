#pragma once

// The span in bytes a tensor's storage covers, which a negative stride makes
// a question about the distance between the lowest and the highest offset the
// tensor reaches, not about the product of its extents.
//
// A vector integer multiply has no per-lane overflow flag: it wraps. That
// matters because this function is a boundary against a corrupt or hostile
// descriptor, and a wrapped product understates the span, which would let a
// caller allocate too little.
//
// So the vector path is entered only after a scalar screen. Take the largest
// extent and the largest absolute stride over the live lanes. If their
// product fits, no per-lane product can overflow, because each factor is
// bounded by the one taken. If it does not fit, the scalar routine runs
// instead and reports the overflow at whichever step it occurs.
//
// The two routines must agree bit for bit on every input and every
// instruction set. Three things hold that: the screen removes the only
// unchecked arithmetic from the vector path, every vector operation is
// per-lane so no lane order can be observed, and the fold that follows is the
// same scalar code with the same overflow checks.

#include <crucible/Platform.h>
#include <crucible/TensorMeta.h>
#include <crucible/Types.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/safety/Simd.h>

#include <cstdint>

namespace crucible::detail {

// This is the reference. A change to the algorithm has to land here and in
// the vector routine in one step, or the two stop agreeing.

[[nodiscard, gnu::const]] CRUCIBLE_INLINE fixy::wrap::Saturated<uint64_t>
compute_storage_nbytes_scalar(ExternalTensorMeta meta) noexcept {
    using Sat = fixy::wrap::Saturated<uint64_t>;
    const TensorMeta& raw = meta.value();
    if (raw.ndim == 0) {
        return Sat{element_size(raw.dtype).raw()};
    }
    int64_t max_offset = 0;
    int64_t min_offset = 0;
    for (uint8_t d = 0; d < raw.ndim; ++d) {
        const int64_t size = raw_tensor_dim(raw.sizes[d]);
        const int64_t stride = raw_tensor_dim(raw.strides[d]);
        if (size == 0) return Sat{uint64_t{0}};
        int64_t dim_extent_bytes;
        // The size is positive here, so the subtraction cannot underflow.
        // The product still can overflow.
        if (__builtin_mul_overflow(size - 1, stride, &dim_extent_bytes)) [[unlikely]] {
            return Sat{UINT64_MAX, true};
        }
        if (dim_extent_bytes > 0) {
            if (__builtin_add_overflow(max_offset, dim_extent_bytes, &max_offset)) [[unlikely]] {
                return Sat{UINT64_MAX, true};
            }
        } else {
            if (__builtin_add_overflow(min_offset, dim_extent_bytes, &min_offset)) [[unlikely]] {
                return Sat{UINT64_MAX, true};
            }
        }
    }
    // The difference can overflow when the two offsets sit at opposite ends
    // of the int64 range.
    int64_t span_signed;
    if (__builtin_sub_overflow(max_offset, min_offset, &span_signed)) [[unlikely]] {
        return Sat{UINT64_MAX, true};
    }
    if (__builtin_add_overflow(span_signed, int64_t{1}, &span_signed)) [[unlikely]] {
        return Sat{UINT64_MAX, true};
    }
    // The span is non-negative, because the running maximum never falls
    // below zero and the running minimum never rises above it. That is what
    // makes the unsigned conversion below safe.
    uint64_t total_bytes;
    if (__builtin_mul_overflow(static_cast<uint64_t>(span_signed), static_cast<uint64_t>(element_size(raw.dtype).raw()),
                               &total_bytes)) [[unlikely]] {
        return Sat{UINT64_MAX, true};
    }
    return Sat{total_bytes};
}

[[nodiscard, gnu::const]]
CRUCIBLE_INLINE fixy::wrap::DetSafe<fixy::wrap::DetSafeTier_v::Pure, fixy::wrap::Saturated<uint64_t>>
compute_storage_nbytes_scalar_det(ExternalTensorMeta meta) noexcept {
    return fixy::wrap::DetSafe<fixy::wrap::DetSafeTier_v::Pure, fixy::wrap::Saturated<uint64_t>>{
        compute_storage_nbytes_scalar(meta)};
}

// The screen leans one way. Answering false for an input that would in fact
// have been safe costs a fall back to the scalar routine. Answering true for
// an input that is not safe is a defect.

[[nodiscard, gnu::pure]] CRUCIBLE_INLINE bool storage_nbytes_simd_safe_(ExternalTensorMeta meta) noexcept {
    using simd::i64x8;
    const TensorMeta& raw = meta.value();

    // The descriptor is aligned for its element type and no further, so the
    // load must not assume vector alignment.
    auto sizes = simd::load<i64x8>(raw.sizes.raw_data());
    auto strides = simd::load<i64x8>(raw.strides.raw_data());

    auto valid_mask = simd::prefix_mask<i64x8>(static_cast<int>(raw.ndim));

    // A size of zero would make this negative, but the caller returns before
    // reaching here in that case. Dead lanes go to zero so they cannot win
    // the reduction below.
    auto sizes_minus_one = simd::select(valid_mask, sizes - i64x8(1), i64x8(0));

    // Negating the most negative int64 does not produce a positive value, so
    // that one stride is mapped to the largest positive value instead, which
    // forces the screen to fail and the scalar routine to run.
    auto strides_neg = -strides;
    auto strides_abs_raw = simd::select(strides >= i64x8(0), strides, strides_neg);
    auto is_int64_min = (strides == i64x8(INT64_MIN));
    auto strides_abs = simd::select(is_int64_min, i64x8(INT64_MAX), strides_abs_raw);
    strides_abs = simd::select(valid_mask, strides_abs, i64x8(0));

    const int64_t max_smo = simd::reduce_max(sizes_minus_one);
    const int64_t max_str = simd::reduce_max(strides_abs);

    int64_t bound;
    return !__builtin_mul_overflow(max_smo, max_str, &bound);
}

[[nodiscard, gnu::pure]] CRUCIBLE_INLINE fixy::wrap::Saturated<uint64_t>
compute_storage_nbytes_simd(ExternalTensorMeta meta) noexcept {
    using Sat = fixy::wrap::Saturated<uint64_t>;
    const TensorMeta& raw = meta.value();
    if (raw.ndim == 0) {
        return Sat{element_size(raw.dtype).raw()};
    }

    using simd::i64x8;

    // The descriptor is aligned for its element type and no further.
    auto sizes = simd::load<i64x8>(raw.sizes.raw_data());
    auto strides = simd::load<i64x8>(raw.strides.raw_data());

    auto valid_mask = simd::prefix_mask<i64x8>(static_cast<int>(raw.ndim));

    auto zero_size_mask = (sizes == i64x8(0)) && valid_mask;
    if (any_of(zero_size_mask)) [[unlikely]] {
        return Sat{uint64_t{0}};
    }

    if (!storage_nbytes_simd_safe_(meta)) [[unlikely]] {
        return compute_storage_nbytes_scalar(meta);
    }

    // The screen above is what licenses this unchecked multiply. Dead lanes
    // go to zero so they add nothing to either running offset.
    auto sizes_minus_one = sizes - i64x8(1);
    auto extents = sizes_minus_one * strides;
    extents = simd::select(valid_mask, extents, i64x8(0));

    // The store below is the aligned form, hence the explicit alignment.
    alignas(64) fixy::wrap::FixedArray<int64_t, 8> extents_buf{};
    simd::store_aligned(extents, extents_buf.data());

    int64_t max_offset = 0;
    int64_t min_offset = 0;
    for (uint8_t d = 0; d < raw.ndim; ++d) {
        const int64_t e = extents_buf[d];
        if (e > 0) {
            if (__builtin_add_overflow(max_offset, e, &max_offset)) [[unlikely]] {
                return Sat{UINT64_MAX, true};
            }
        } else {
            if (__builtin_add_overflow(min_offset, e, &min_offset)) [[unlikely]] {
                return Sat{UINT64_MAX, true};
            }
        }
    }

    int64_t span_signed;
    if (__builtin_sub_overflow(max_offset, min_offset, &span_signed)) [[unlikely]] {
        return Sat{UINT64_MAX, true};
    }
    if (__builtin_add_overflow(span_signed, int64_t{1}, &span_signed)) [[unlikely]] {
        return Sat{UINT64_MAX, true};
    }
    uint64_t total_bytes;
    if (__builtin_mul_overflow(static_cast<uint64_t>(span_signed), static_cast<uint64_t>(element_size(raw.dtype).raw()),
                               &total_bytes)) [[unlikely]] {
        return Sat{UINT64_MAX, true};
    }
    return Sat{total_bytes};
}

[[nodiscard, gnu::pure]]
CRUCIBLE_INLINE fixy::wrap::DetSafe<fixy::wrap::DetSafeTier_v::Pure, fixy::wrap::Saturated<uint64_t>>
compute_storage_nbytes_simd_det(ExternalTensorMeta meta) noexcept {
    return fixy::wrap::DetSafe<fixy::wrap::DetSafeTier_v::Pure, fixy::wrap::Saturated<uint64_t>>{
        compute_storage_nbytes_simd(meta)};
}

}  // namespace crucible::detail
