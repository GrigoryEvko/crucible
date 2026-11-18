#pragma once

// A recipe pins every algorithmic choice a backend is allowed to make. One
// kernel compiled against one recipe therefore agrees with itself across
// chips, to within the tolerance the recipe's determinism tier declares.

#include <crucible/Types.h>
#include <fixy/Bits.h>
#include <fixy/Core.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/core/FpMode.h>
#include <foundation/reflect/Mix.h>

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
//                    reduction, and at most one unit in the last place.  The
//                    bound holds only for exact fragments: integer MMA, FP64
//                    or FP32 MMA, or inputs split so that each product is
//                    exact.  FP16 and BF16 fragments give different bits on
//                    different vendors and generations, so a recipe of such
//                    fragments is UNORDERED
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
    // The IEEE 754 rounding direction, in the FP mode vocabulary of the core.
    ::foundation::core::RoundingMode rounding = ::foundation::core::RoundingMode::RN;
    ScalePolicy scale_policy = ScalePolicy::NONE;
    SoftmaxRecurrence softmax = SoftmaxRecurrence::ONLINE_LSE;
    ReductionDeterminism determinism = ReductionDeterminism::ORDERED;
    ::fixy::Bits<RecipeFlags> flags{};
    // Filled in when the recipe is interned, not at construction.
    RecipeHash hash;
};

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
// ScalarType::Undefined is -1. Casting it straight to uint64_t sign-extends
// and overwrites every other field in the packed word. Reinterpreting the one
// byte first keeps the value at 0xFF.
template <typename E>
[[nodiscard, gnu::const]] constexpr uint8_t enum_byte(E e) noexcept {
    static_assert(sizeof(E) == 1, "enum_byte expects a one-byte scoped enum; a wider enum field needs a wider "
                                  "hash fold");
    return ::fixy::bit_cast<uint8_t>(e);
}

// The finalizer is a bijection, and it maps the zero word to zero.  The recipe
// whose eight bytes are zero then gets the hash 0, which is the hash of a
// recipe that no pool interned.  One other word gets UINT64_MAX, the sentinel.
// A finalizer result of 0 or UINT64_MAX takes the finalizer result of its XOR
// with this salt.  The accumulator byte of the salt and of its complement names
// no ScalarType.  No recipe then packs to either word, and no two recipes share
// a hash.  The check file of this header does a check of these facts.
inline constexpr uint64_t kReservedSalt = 0x5245434950454944ULL;  // "RECIPEID"
}  // namespace detail_recipe

[[nodiscard, gnu::pure]] constexpr RecipeHash compute_recipe_hash(const NumericalRecipe& r) noexcept {
    using detail_recipe::enum_byte;
    const uint64_t packed = (uint64_t(enum_byte(r.accum_dtype)) << 0) | (uint64_t(enum_byte(r.out_dtype)) << 8)
                          | (uint64_t(enum_byte(r.reduction_algo)) << 16) | (uint64_t(enum_byte(r.rounding)) << 24)
                          | (uint64_t(enum_byte(r.scale_policy)) << 32) | (uint64_t(enum_byte(r.softmax)) << 40)
                          | (uint64_t(enum_byte(r.determinism)) << 48) | (uint64_t(r.flags.raw()) << 56);
    const uint64_t mixed = ::foundation::reflect::fmix64(packed);
    const bool is_reserved = mixed == 0 || mixed == RecipeHash::sentinel().raw();
    const RecipeHash result{is_reserved ? ::foundation::reflect::fmix64(mixed ^ detail_recipe::kReservedSalt) : mixed};
    CRUCIBLE_POST(result, ::foundation::decide::is_non_zero(result) && ::foundation::decide::not_sentinel_hash(result));
    return result;
}

[[nodiscard, gnu::pure]] constexpr NumericalRecipe hashed(NumericalRecipe r) noexcept {
    r.hash = compute_recipe_hash(r);
    return r;
}

// The numerics policy of a run: the part of the numerics that the ops of a
// region do not show.  The content hash of a region already folds the dtype
// and the device of each tensor, so the policy holds only the choices that a
// kernel of Crucible makes past them: the determinism tier, the accumulator
// of each class of narrow float, and the flags.  One region can hold ops of
// different dtypes, such as bf16 activations and f32 master weights, so the
// policy is a value of the run and not a recipe of one op.  The recipe of
// one op is the policy, the dtype and the device together, and the compile of
// a kernel derives it.
struct NumericsPolicy {
    ReductionDeterminism determinism = ReductionDeterminism::ORDERED;
    // The accumulator of Half and BFloat16.
    ScalarType accum_16bit = ScalarType::Float;
    // The accumulator of the fp8 formats.
    ScalarType accum_8bit = ScalarType::Float;
    ::fixy::Bits<RecipeFlags> flags{};

    [[nodiscard]] friend constexpr bool operator==(NumericsPolicy const&, NumericsPolicy const&) noexcept = default;
};

// The state of the front end that changes the numerics of its eager ops, in
// one word.  The front end reads it at the attach.  The word folds into the
// hash of each region beside the policy, so a region records under the eager
// numerics that its ops ran with.
struct EagerNumerics {
    // The precision of a float32 matmul of PyTorch: 0 for highest, 1 for high
    // and 2 for medium.
    static constexpr uint32_t MATMUL_PRECISION_MASK = 0x3;
    // A cuBLAS matmul can use TF32.
    static constexpr uint32_t MATMUL_TF32 = 1u << 2;
    // A cuDNN convolution can use TF32.
    static constexpr uint32_t CUDNN_TF32 = 1u << 3;
    // The host flushes denormal floats to zero.
    static constexpr uint32_t FLUSH_DENORMAL = 1u << 4;
    // Each bit that a field above names.
    static constexpr uint32_t NAMED_BITS = MATMUL_PRECISION_MASK | MATMUL_TF32 | CUDNN_TF32 | FLUSH_DENORMAL;

    uint32_t bits = 0;

    [[nodiscard]] friend constexpr bool operator==(EagerNumerics, EagerNumerics) noexcept = default;
};

// True when each bit of the word is a bit of a field of EagerNumerics, and the
// precision is one of the three that PyTorch names.  A word from outside the
// process, such as the word of a C caller, passes this test first.
[[nodiscard]] constexpr bool is_named_eager_word(EagerNumerics word) noexcept {
    return (word.bits & ~EagerNumerics::NAMED_BITS) == 0
        && (word.bits & EagerNumerics::MATMUL_PRECISION_MASK) != EagerNumerics::MATMUL_PRECISION_MASK;
}

namespace detail_recipe {
// The salt of the hash of a numerics policy, so the word of a policy and the
// word of a recipe of the same bytes give two hashes.
inline constexpr uint64_t kNumericsSalt = 0x4E554D4552494353ULL;  // "NUMERICS"
}  // namespace detail_recipe

// The hash of the policy and the eager word.  The four bytes of the policy and
// the 32 bits of the word pack into one word, and the finalizer of a recipe
// mixes it.  The result is never 0 and never the sentinel, so a region with a
// policy never has the hash of a region with none.  The packed word holds no
// address, so the hash is the same in each process.
[[nodiscard, gnu::pure]] constexpr NumericsHash compute_numerics_hash(const NumericsPolicy& policy,
                                                                      EagerNumerics eager) noexcept {
    using detail_recipe::enum_byte;
    const uint64_t packed = (uint64_t(enum_byte(policy.determinism)) << 0)
                          | (uint64_t(enum_byte(policy.accum_16bit)) << 8)
                          | (uint64_t(enum_byte(policy.accum_8bit)) << 16) | (uint64_t(policy.flags.raw()) << 24)
                          | (uint64_t(eager.bits) << 32);
    const uint64_t mixed = ::foundation::reflect::fmix64(packed ^ detail_recipe::kNumericsSalt);
    const bool is_reserved = mixed == 0 || mixed == NumericsHash::sentinel().raw();
    const NumericsHash result{is_reserved ? ::foundation::reflect::fmix64(mixed ^ detail_recipe::kReservedSalt)
                                          : mixed};
    CRUCIBLE_POST(result, ::foundation::decide::is_non_zero(result) && !result.is_sentinel());
    return result;
}

// Only BITEXACT_STRICT promises the same bytes on each vendor.  BITEXACT_TC
// holds only for exact fragments, and a recipe does not record the fragment
// kind.  Thus a BITEXACT_TC recipe is not bit-exact here.
[[nodiscard, gnu::const]] constexpr bool is_bitexact(ReductionDeterminism d) noexcept {
    return d == ReductionDeterminism::BITEXACT_STRICT;
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
