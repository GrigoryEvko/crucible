// ═══════════════════════════════════════════════════════════════════
// prop_storage_nbytes_simd_equivalence — randomized SIMD-2 oracle
//
// Property:
//   For every TensorMeta drawn at random, the SIMD path
//   compute_storage_nbytes_simd produces the IDENTICAL uint64_t
//   to compute_storage_nbytes_scalar.
//
// What this catches that the unit test doesn't:
//
//   * Adversarial inputs the unit test forgot.  100k iterations
//     across the input space catches edge cases (e.g.,
//     specifically-tuned negative strides that make max-min
//     subtraction overflow, ndim transitions across the 4-lane
//     and 8-lane boundaries, sizes that make pre-screen exactly
//     reach INT64_MAX, etc.).
//   * Compiler reorderings that perturb the SIMD multiply or
//     pre-screen reduce_max.
//   * Future GCC/libstdc++ updates that change std::simd's
//     reduce_max behavior or select semantics — would surface as
//     CI red the moment new code lands.
//
// ─── How it works ───────────────────────────────────────────────────
//
// Each iteration draws a random TensorMeta with parameters that
// span the algorithmic space:
//   * ndim in [0, 8]
//   * sizes in [0, 2^40] (forces some zero-size short-circuits;
//     forces some pre-screen failures and fallback)
//   * strides in [-2^40, 2^40] (negative + extreme magnitudes)
//   * dtype across all enum variants
//
// Compares scalar vs SIMD bit-for-bit.  Failure prints the
// triggering input + both return values for trivial reproduction.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/StorageNbytes.h>
#include <crucible/TensorMeta.h>
#include <crucible/Types.h>

#include <cstdint>
#include <cstdio>

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::detail;
    using namespace crucible::fuzz::prop;

    const Config cfg = parse_args(argc, argv);

    return run("StorageNbytes SIMD bit-equivalence", cfg,
        [](Rng& rng) {
            TensorMeta meta{};
            // ndim in [0, 8] — covers scalar (ndim=0), single-dim,
            // and full 8-dim cases.
            meta.ndim = static_cast<uint8_t>(rng.next_below(9));

            // dtype: pick one of the common scalar types.
            // Match the ordinals that exist in Types.h.
            const uint32_t dtype_idx = rng.next_below(8);
            switch (dtype_idx) {
                case 0: meta.dtype = ScalarType::Byte;   break;
                case 1: meta.dtype = ScalarType::Char;   break;
                case 2: meta.dtype = ScalarType::Short;  break;
                case 3: meta.dtype = ScalarType::Int;    break;
                case 4: meta.dtype = ScalarType::Long;   break;
                case 5: meta.dtype = ScalarType::Half;   break;
                case 6: meta.dtype = ScalarType::Float;  break;
                default: meta.dtype = ScalarType::Double;
            }

            // For each valid dim, pick a size in [0, 2^40] and a
            // stride in [-(2^40), 2^40].  This range:
            //   - includes 0 sizes (zero-size short-circuit path)
            //   - includes huge sizes / strides (forces pre-screen
            //     failure, scalar fallback path)
            //   - includes negative strides (max/min split)
            for (uint8_t d = 0; d < meta.ndim; ++d) {
                const uint64_t s = rng.next64() & ((uint64_t{1} << 40) - 1);
                meta.sizes[d] = static_cast<int64_t>(s);

                const uint64_t st = rng.next64() & ((uint64_t{1} << 41) - 1);
                meta.strides[d] = static_cast<int64_t>(st) -
                                  (int64_t{1} << 40);
            }
            return meta;
        },
        [](const TensorMeta& meta) {
            const uint64_t scalar = compute_storage_nbytes_scalar(meta);
            const uint64_t simd_v = compute_storage_nbytes_simd(meta);

            if (scalar != simd_v) {
                std::fprintf(stderr,
                    "\nSIMD/scalar divergence:\n"
                    "  scalar=%llu simd=%llu\n"
                    "  ndim=%u dtype=%d\n"
                    "  sizes=[",
                    static_cast<unsigned long long>(scalar),
                    static_cast<unsigned long long>(simd_v),
                    unsigned(meta.ndim),
                    int(meta.dtype));
                for (uint8_t d = 0; d < meta.ndim; ++d) {
                    std::fprintf(stderr, "%lld%s",
                        static_cast<long long>(meta.sizes[d]),
                        d + 1 < meta.ndim ? "," : "");
                }
                std::fprintf(stderr, "] strides=[");
                for (uint8_t d = 0; d < meta.ndim; ++d) {
                    std::fprintf(stderr, "%lld%s",
                        static_cast<long long>(meta.strides[d]),
                        d + 1 < meta.ndim ? "," : "");
                }
                std::fprintf(stderr, "]\n");
                return false;
            }
            return true;
        });
}
