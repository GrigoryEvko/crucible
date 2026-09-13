#pragma once

// A recipe pins every algorithmic choice a backend is allowed to make. One
// kernel compiled against one recipe therefore agrees with itself across
// chips, to within the tolerance the recipe's determinism tier declares.

#include <crucible/Platform.h>
#include <crucible/Types.h>
#include <crucible/fixy/Wrap.h>

#include <bit>
#include <cstdint>
#include <utility>

namespace crucible {

// The shape of the accumulator's walk across the reduction axis.
enum class ReductionAlgo : uint8_t {
    PAIRWISE,  // pairwise tree
    LINEAR,  // sequential left fold
    KAHAN,  // compensated summation
    BLOCK_STABLE,  // block tree, for formats carrying an explicit per-block scale
};

// The IEEE 754 rounding modes.
enum class RoundingMode : uint8_t {
    RN,  // to nearest, ties to even
    RZ,  // toward zero
    RM,  // toward minus infinity
    RP,  // toward plus infinity
};

// The policy belongs in the recipe because it decides where in the
// accumulator pipeline the scale is applied, and that changes the result.
enum class ScalePolicy : uint8_t {
    NONE,
    PER_TENSOR_POST,  // one scalar scale on the final output
    PER_TENSOR_PRE,  // one scalar scale on the inputs, before the multiply
    PER_BLOCK_MX,  // one scale per 32-element block
    PER_BLOCK_NVFP4,  // one scale per 16-element block
    PER_CHANNEL,  // one scale per output channel
};

// Each variant rounds its intermediates differently, so the bit patterns
// differ at reduced precision. The recipe names the one a backend must
// realize rather than leaving the choice to the backend.
enum class SoftmaxRecurrence : uint8_t {
    NAIVE,  // two passes: subtract the max, exponentiate, normalize
    ONLINE_LSE,  // single-pass log-sum-exp
    FLASH2,
    FLASH3,  // warp-specialized
};

// The ladder is ordered. Each tier is at least as deterministic as the one
// before it and costs more.
//
//   UNORDERED        no guarantee at all
//   ORDERED          reduction topology pinned to a canonical index order,
//                    tile shapes free, and a few units in the last place
//   BITEXACT_TC      short tensor-core fragments with a pinned outer scalar
//                    reduction, and at most one unit in the last place
//   BITEXACT_STRICT  a scalar fused-multiply-add chain throughout, with no
//                    tensor cores, and byte-identical across architectures
enum class ReductionDeterminism : uint8_t {
    UNORDERED,
    ORDERED,
    BITEXACT_TC,
    BITEXACT_STRICT,
};

// The recipe hash folds the whole flags byte, so the four unused bits must
// stay zero in existing recipes or their hashes move. A new flag takes the
// next free bit and leaves the assigned ones alone.
//
//   FLUSH_TO_ZERO          clamp denormals to zero in the accumulator, which
//                          some tensor-core units cannot represent
//   SPLIT_K_ATOMIC_OK      a reduction split across blocks may use atomic
//                          adds, which no bit-exact tier permits
//   ALLOW_DENORMAL         accept denormal inputs instead of requiring the
//                          caller to clamp them
//   ATTN_MASK_ADD_IN_FP32  add the attention mask at full precision even when
//                          the accumulator is narrower, which is what keeps
//                          the masked region from underflowing
enum class RecipeFlags : uint8_t {
    FLUSH_TO_ZERO = 1 << 0,
    SPLIT_K_ATOMIC_OK = 1 << 1,
    ALLOW_DENORMAL = 1 << 2,
    ATTN_MASK_ADD_IN_FP32 = 1 << 3,
};

struct alignas(16) NumericalRecipe {
    ScalarType accum_dtype = ScalarType::Float;
    ScalarType out_dtype = ScalarType::Undefined;
    ReductionAlgo reduction_algo = ReductionAlgo::PAIRWISE;
    RoundingMode rounding = RoundingMode::RN;
    ScalePolicy scale_policy = ScalePolicy::NONE;
    SoftmaxRecurrence softmax = SoftmaxRecurrence::ONLINE_LSE;
    ReductionDeterminism determinism = ReductionDeterminism::ORDERED;
    fixy::wrap::Bits<RecipeFlags> flags{};
    // Filled in when the recipe is interned, not at construction.
    RecipeHash hash;
};
static_assert(sizeof(NumericalRecipe) == 16, "NumericalRecipe must stay 16 bytes: both the intern table and the "
                                             "kernel nodes that point at it depend on the layout");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(NumericalRecipe);

// The hash covers the eight semantic bytes and excludes the hash field
// itself. Excluding it is what makes the operation idempotent: a recipe read
// back from disk with its hash already set rehashes to the same value.
//
// The packed word carries no pointer and no address, so the result is the
// same in every process and on every machine.
//
// A reflective per-member fold is the obvious alternative and loses twice.
// The eight bytes pack exactly into one word, so a single finalizer already
// gives full avalanche over the whole identity, and a fold over reflected
// members would include the hash field and break idempotence.

namespace detail_recipe {
// This finalizer is duplicated rather than shared, which keeps this header a
// leaf. Both copies must produce identical bits for identical inputs.
[[nodiscard, gnu::const]] constexpr uint64_t fmix64(uint64_t k) noexcept {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

// ScalarType::Undefined is -1. Casting it straight to uint64_t sign-extends
// and overwrites every other field in the packed word. Reinterpreting the one
// byte first keeps the value at 0xFF.
template <typename E>
[[nodiscard, gnu::const]] constexpr uint8_t enum_byte(E e) noexcept {
    static_assert(sizeof(E) == 1, "enum_byte expects a one-byte scoped enum; a wider enum field needs a wider "
                                  "hash fold");
    return std::bit_cast<uint8_t>(e);
}
}  // namespace detail_recipe

[[nodiscard, gnu::pure]] constexpr RecipeHash compute_recipe_hash(const NumericalRecipe& r) noexcept {
    using detail_recipe::enum_byte;
    const uint64_t packed = (uint64_t(enum_byte(r.accum_dtype)) << 0) | (uint64_t(enum_byte(r.out_dtype)) << 8)
                          | (uint64_t(enum_byte(r.reduction_algo)) << 16) | (uint64_t(enum_byte(r.rounding)) << 24)
                          | (uint64_t(enum_byte(r.scale_policy)) << 32) | (uint64_t(enum_byte(r.softmax)) << 40)
                          | (uint64_t(enum_byte(r.determinism)) << 48) | (uint64_t(r.flags.raw()) << 56);
    return RecipeHash{detail_recipe::fmix64(packed)};
}

[[nodiscard, gnu::pure]] constexpr NumericalRecipe hashed(NumericalRecipe r) noexcept {
    r.hash = compute_recipe_hash(r);
    return r;
}

[[nodiscard, gnu::const]] constexpr bool is_bitexact(ReductionDeterminism d) noexcept {
    return d == ReductionDeterminism::BITEXACT_TC || d == ReductionDeterminism::BITEXACT_STRICT;
}

[[nodiscard, gnu::const]] constexpr bool permits_tensor_cores(ReductionDeterminism d) noexcept {
    // Only the strictest tier bans them outright. BITEXACT_TC still permits
    // them, but constrains the fragment length and the outer reduction order.
    return d != ReductionDeterminism::BITEXACT_STRICT;
}

[[nodiscard, gnu::const]] constexpr bool allows_block_scaled_formats(ReductionDeterminism d) noexcept {
    // Vendors apply a block scale at different points, and the divergence
    // that causes is larger than a bit-exact tier admits. ORDERED is the
    // strongest tier a per-block scale policy can reach.
    return d == ReductionDeterminism::UNORDERED || d == ReductionDeterminism::ORDERED;
}

}  // namespace crucible
