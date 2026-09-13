#pragma once

// Eight independent Philox streams advanced together, one per lane.
//
// The inputs are in lane-major order: lane i of every input vector belongs to
// the i-th stream, and lane i of every output vector is that stream's result.
// That is the layout a caller already has when it draws numbers for eight
// consecutive elements of a tensor.
//
// Every operation here is lane-wise and there is no reduction, so each lane
// is bit-identical to what the scalar routine produces for the same inputs on
// any instruction set. The truncating product is unsigned wraparound, which
// is defined. The high half comes from widening to sixty-four bits and
// shifting, which is what the scalar routine's high-multiply does.

#include <crucible/Philox.h>
#include <crucible/Platform.h>
#include <crucible/safety/Simd.h>

#include <cstdint>

namespace crucible::detail {

struct PhiloxBatch8 {
    simd::u32x8 r0;
    simd::u32x8 r1;
    simd::u32x8 r2;
    simd::u32x8 r3;
};

[[nodiscard, gnu::const]] CRUCIBLE_INLINE PhiloxBatch8 philox_batch8(simd::u32x8 ctr0, simd::u32x8 ctr1,
                                                                     simd::u32x8 ctr2, simd::u32x8 ctr3,
                                                                     simd::u32x8 key0, simd::u32x8 key1) noexcept {
    using simd::u32x8;
    using simd::u64x8;

    const u32x8 m0(Philox::M0);
    const u32x8 m1(Philox::M1);
    const u32x8 w0(Philox::W0);
    const u32x8 w1(Philox::W1);

    for (int round = 0; round < 10; ++round) {
        const u32x8 lo0 = ctr0 * m0;
        const u32x8 lo1 = ctr2 * m1;

        // The widening conversion zero-extends each lane, so the product
        // is the full unsigned one and its upper half is what the shift
        // recovers.
        const u64x8 prod0 = u64x8(ctr0) * u64x8(m0);
        const u64x8 prod1 = u64x8(ctr2) * u64x8(m1);
        const u32x8 hi0 = static_cast<u32x8>(prod0 >> 32);
        const u32x8 hi1 = static_cast<u32x8>(prod1 >> 32);

        // All four new words are named before any is stored back, because
        // three of them read words the store would otherwise overwrite.
        const u32x8 nctr0 = hi1 ^ ctr1 ^ key0;
        const u32x8 nctr1 = lo1;
        const u32x8 nctr2 = hi0 ^ ctr3 ^ key1;
        const u32x8 nctr3 = lo0;
        ctr0 = nctr0;
        ctr1 = nctr1;
        ctr2 = nctr2;
        ctr3 = nctr3;

        key0 = key0 + w0;
        key1 = key1 + w1;
    }

    return {ctr0, ctr1, ctr2, ctr3};
}

}  // namespace crucible::detail
