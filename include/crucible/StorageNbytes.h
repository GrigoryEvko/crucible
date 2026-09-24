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
#include <foundation/Simd.h>

#include <cstdint>

namespace crucible::detail {

// This is the reference. A change to the algorithm has to land here and in
// the vector routine in one step, or the two stop agreeing.
//
// It is also the ONLY copy. MerkleDag.h used to carry a third transcription
// of the same twelve steps under the public name compute_storage_nbytes, and
// that third copy was the one the runtime ran: every caller of
// compute_storage_nbytes_det in BackgroundThread.h reached it. So the
// differential fuzzer that holds this routine and the vector one to bit
// equality was comparing two functions with no production caller between
// them, and could not have reported a divergence in the copy that mattered.
// crucible::compute_storage_nbytes now forwards here, which is what puts
// production on the far side of that comparison.
//
// constexpr because the public entry point is, and a constexpr function that
// forwards to a non-constexpr one is only constexpr until someone evaluates
// it. Nothing here is outside the constant-evaluation subset: the overflow
// builtins are all constexpr-usable.

[[nodiscard, gnu::const]] CRUCIBLE_INLINE constexpr fixy::wrap::Saturated<uint64_t>
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
        // A hostile descriptor can hold a negative size. The subtraction is
        // modular, so it is defined for every size: INT64_MIN gives INT64_MAX.
        // The product can still overflow.
        const int64_t size_minus_one = static_cast<int64_t>(static_cast<uint64_t>(size) - uint64_t{1});
        int64_t dim_extent_bytes;
        if (__builtin_mul_overflow(size_minus_one, stride, &dim_extent_bytes)) [[unlikely]] {
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
    using ::foundation::simd::i64x8;
    const TensorMeta& raw = meta.value();

    // The descriptor is aligned for its element type and no further, so the
    // load must not assume vector alignment.
    auto sizes = ::foundation::simd::load<i64x8>(raw.sizes.raw_data());
    auto strides = ::foundation::simd::load<i64x8>(raw.strides.raw_data());

    auto valid_mask = ::foundation::simd::prefix_mask<i64x8>(static_cast<int>(raw.ndim));

    // Each lane is made safe before any arithmetic, because a dead lane and a
    // hostile live lane can hold any value. A dead lane becomes size one and
    // stride zero, so it cannot win either reduction below.
    const i64x8 one(1);
    auto live_sizes = ::foundation::simd::select(valid_mask, sizes, one);

    // The bound below holds for sizes of one or more only, so a live size
    // below one sends the input to the scalar routine, which takes every
    // size. The caller has already returned for a size of zero. The clamp
    // keeps the subtraction defined on such a lane.
    const bool every_size_positive = !any_of(live_sizes < one);
    auto sizes_minus_one = ::foundation::simd::max(live_sizes, one) - one;

    // Negating the most negative int64 is undefined, so that one stride is
    // clamped to -INT64_MAX first. Its absolute value is then INT64_MAX, which
    // forces the screen to fail and the scalar routine to run.
    auto live_strides = ::foundation::simd::select(valid_mask, strides, i64x8(0));
    auto strides_clamped = ::foundation::simd::max(live_strides, i64x8(-INT64_MAX));
    auto strides_abs = ::foundation::simd::select(strides_clamped >= i64x8(0), strides_clamped, -strides_clamped);

    const int64_t max_smo = ::foundation::simd::reduce_max(sizes_minus_one);
    const int64_t max_str = ::foundation::simd::reduce_max(strides_abs);

    int64_t bound;
    const bool bound_fits = !__builtin_mul_overflow(max_smo, max_str, &bound);
    return every_size_positive & bound_fits;
}

[[nodiscard, gnu::pure]] CRUCIBLE_INLINE fixy::wrap::Saturated<uint64_t>
compute_storage_nbytes_simd(ExternalTensorMeta meta) noexcept {
    using Sat = fixy::wrap::Saturated<uint64_t>;
    const TensorMeta& raw = meta.value();
    if (raw.ndim == 0) {
        return Sat{element_size(raw.dtype).raw()};
    }

    using ::foundation::simd::i64x8;

    // The descriptor is aligned for its element type and no further.
    auto sizes = ::foundation::simd::load<i64x8>(raw.sizes.raw_data());
    auto strides = ::foundation::simd::load<i64x8>(raw.strides.raw_data());

    auto valid_mask = ::foundation::simd::prefix_mask<i64x8>(static_cast<int>(raw.ndim));

    auto zero_size_mask = (sizes == i64x8(0)) && valid_mask;
    if (any_of(zero_size_mask)) [[unlikely]] {
        return Sat{uint64_t{0}};
    }

    if (!storage_nbytes_simd_safe_(meta)) [[unlikely]] {
        return compute_storage_nbytes_scalar(meta);
    }

    // The screen above is what licenses this unchecked multiply: each live
    // size is one or more, and each live product is bounded. A dead lane
    // becomes size one and stride zero before any arithmetic, so it
    // multiplies to zero and adds nothing to either running offset.
    const i64x8 one(1);
    auto sizes_minus_one = ::foundation::simd::select(valid_mask, sizes, one) - one;
    auto extents = sizes_minus_one * ::foundation::simd::select(valid_mask, strides, i64x8(0));

    // The store below is the aligned form, hence the explicit alignment.
    alignas(64) fixy::wrap::FixedArray<int64_t, 8> extents_buf{};
    ::foundation::simd::store_aligned(extents, extents_buf.data());

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
