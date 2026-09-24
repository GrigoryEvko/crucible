// The header's own assertions only run when a translation unit includes it,
// and they only cover the constant-evaluated path. Everything below feeds
// values the compiler cannot fold, so the same projection is exercised as a
// real bit-cast at runtime.

#include <crucible/fixy/fp/_Canonicalize.h>
#include <crucible/NumericalRecipe.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace fp = crucible::fixy::fp;

namespace {

void section_a_nan_canonicalization() {
    // The payloads are read through volatile, otherwise the whole computation
    // folds away at compile time and nothing runtime-specific is exercised.
    volatile std::uint64_t nan_payload_a = 0x7FF1234567890ABCULL;
    volatile std::uint64_t nan_payload_b = 0xFFFEDCBA98765432ULL;  // negative-sign NaN
    const double nan_a = std::bit_cast<double>(nan_payload_a);
    const double nan_b = std::bit_cast<double>(nan_payload_b);

    if (fp::canonicalize(nan_a) != fp::kCanonicalQNaN64) {
        std::fprintf(stderr, "(a) FAIL: positive-sign NaN payload did not canonicalize\n");
        std::abort();
    }
    if (fp::canonicalize(nan_b) != fp::kCanonicalQNaN64) {
        std::fprintf(stderr, "(a) FAIL: negative-sign NaN payload did not canonicalize\n");
        std::abort();
    }

    volatile std::uint32_t nan_payload_f = 0xFFC12345U;
    const float nan_f = std::bit_cast<float>(nan_payload_f);
    if (fp::canonicalize(nan_f) != fp::kCanonicalQNaN32) {
        std::fprintf(stderr, "(a) FAIL: float NaN did not canonicalize\n");
        std::abort();
    }

    // Two different payloads collapsing to one canonical value is the property
    // merkle hashing depends on.
    if (fp::canonicalize(nan_a) != fp::canonicalize(nan_b)) {
        std::fprintf(stderr, "(a) FAIL: distinct NaN payloads did not converge\n");
        std::abort();
    }
}

void section_b_signed_zero_canonicalization() {
    volatile double pos_zero = 0.0;
    volatile double neg_zero = -0.0;
    // The two zeros do have different raw bits, which is what makes the
    // canonicalization below non-trivial.
    if (std::bit_cast<std::uint64_t>(static_cast<double>(neg_zero)) == 0) {
        std::fprintf(stderr, "(b) FAIL: -0.0 raw bits unexpectedly zero\n");
        std::abort();
    }
    if (fp::canonicalize(static_cast<double>(pos_zero)) != 0) {
        std::fprintf(stderr, "(b) FAIL: +0.0 canonicalize result non-zero\n");
        std::abort();
    }
    if (fp::canonicalize(static_cast<double>(neg_zero)) != 0) {
        std::fprintf(stderr, "(b) FAIL: -0.0 canonicalize result non-zero\n");
        std::abort();
    }
    volatile float pos_zero_f = 0.0f;
    volatile float neg_zero_f = -0.0f;
    if (fp::canonicalize(static_cast<float>(pos_zero_f)) != 0) {
        std::fprintf(stderr, "(b) FAIL: +0.0f canonicalize result non-zero\n");
        std::abort();
    }
    if (fp::canonicalize(static_cast<float>(neg_zero_f)) != 0) {
        std::fprintf(stderr, "(b) FAIL: -0.0f canonicalize result non-zero\n");
        std::abort();
    }
}

void section_c_pass_through() {
    volatile double values[] = {1.0,
                                -1.0,
                                3.14,
                                -2.718,
                                1e-300,
                                1e300,
                                std::numeric_limits<double>::infinity(),
                                -std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::min(),  // smallest normal
                                std::numeric_limits<double>::denorm_min()};  // smallest subnormal
    for (const volatile double& v : values) {
        const double x = v;
        const auto expected = std::bit_cast<std::uint64_t>(x);
        if (fp::canonicalize(x) != expected) {
            std::fprintf(stderr, "(c) FAIL: finite value 0x%016lx did not pass through\n", expected);
            std::abort();
        }
    }

    volatile float values_f[] = {1.0f,
                                 -1.0f,
                                 3.14f,
                                 -2.718f,
                                 1e-30f,
                                 1e30f,
                                 std::numeric_limits<float>::infinity(),
                                 -std::numeric_limits<float>::infinity()};
    for (const volatile float& v : values_f) {
        const float x = v;
        const auto expected = std::bit_cast<std::uint32_t>(x);
        if (fp::canonicalize(x) != expected) {
            std::fprintf(stderr, "(c) FAIL: finite float 0x%08x did not pass through\n", expected);
            std::abort();
        }
    }
}

// The recipe is a template argument, so this overload is usable at compile
// time. It is equally usable at runtime, which is what these calls check.

constexpr fp::CanonicalizeRecipeSpec kSpecBitexactStrict{
    crucible::RoundingMode::RN,
    crucible::ReductionDeterminism::BITEXACT_STRICT,
};

constexpr fp::CanonicalizeRecipeSpec kSpecBitexactTc{
    crucible::RoundingMode::RN,
    crucible::ReductionDeterminism::BITEXACT_TC,
};

void section_d_recipe_gated() {
    volatile double finite = 42.5;
    volatile double signed_zero = -0.0;
    volatile double nan_val = std::bit_cast<double>(std::uint64_t{0x7FF8000000000001ULL});

    const auto h_strict = fp::canonicalize_for<kSpecBitexactStrict>(static_cast<double>(finite));
    const auto h_tc = fp::canonicalize_for<kSpecBitexactTc>(static_cast<double>(finite));
    const auto expected = std::bit_cast<std::uint64_t>(static_cast<double>(finite));
    if (h_strict != expected || h_tc != expected) {
        std::fprintf(stderr, "(d) FAIL: recipe-gated overload diverged on finite\n");
        std::abort();
    }

    if (fp::canonicalize_for<kSpecBitexactStrict>(static_cast<double>(signed_zero)) != 0) {
        std::fprintf(stderr, "(d) FAIL: recipe-gated -0.0 not canonicalized\n");
        std::abort();
    }
    if (fp::canonicalize_for<kSpecBitexactStrict>(static_cast<double>(nan_val)) != fp::kCanonicalQNaN64) {
        std::fprintf(stderr, "(d) FAIL: recipe-gated NaN not canonicalized\n");
        std::abort();
    }

    volatile float finite_f = -7.25f;
    const auto h_f = fp::canonicalize_for<kSpecBitexactStrict>(static_cast<float>(finite_f));
    if (h_f != std::bit_cast<std::uint32_t>(static_cast<float>(finite_f))) {
        std::fprintf(stderr, "(d) FAIL: recipe-gated float diverged\n");
        std::abort();
    }

    // The same call spelled by projecting a whole recipe rather than naming
    // the spec directly.
    constexpr crucible::NumericalRecipe kFullRecipe{
        .reduction_algo = crucible::ReductionAlgo::PAIRWISE,
        .rounding = crucible::RoundingMode::RN,
        .determinism = crucible::ReductionDeterminism::BITEXACT_TC,
        .hash = crucible::RecipeHash{},
    };
    const auto h_proj = fp::canonicalize_for<fp::CanonicalizeRecipeSpec{kFullRecipe}>(static_cast<double>(finite));
    if (h_proj != expected) {
        std::fprintf(stderr, "(d) FAIL: NumericalRecipe projection diverged\n");
        std::abort();
    }
}

// The claim the whole projection exists for: distinct bit patterns for the
// same logical value must reach a hash fold as one contribution. The fold
// below stands in for the real one.

void section_e_merkle_convergence() {
    volatile std::uint64_t nan_a_bits = 0x7FF0000000000001ULL;  // sNaN-like
    volatile std::uint64_t nan_b_bits = 0x7FFFFFFFFFFFFFFFULL;  // qNaN max payload
    volatile std::uint64_t nan_c_bits = 0xFFF8000000000000ULL;  // negative qNaN
    const double nan_a = std::bit_cast<double>(nan_a_bits);
    const double nan_b = std::bit_cast<double>(nan_b_bits);
    const double nan_c = std::bit_cast<double>(nan_c_bits);

    auto toy_fold = [](std::uint64_t bits) constexpr {
        // A single FNV-1a step, not the real fold. It only has to be
        // injective enough that a difference in the input would show.
        std::uint64_t h = 0xcbf29ce484222325ULL;
        h ^= bits;
        h *= 0x100000001b3ULL;
        return h;
    };

    const auto h_a = toy_fold(fp::canonicalize(nan_a));
    const auto h_b = toy_fold(fp::canonicalize(nan_b));
    const auto h_c = toy_fold(fp::canonicalize(nan_c));
    if (h_a != h_b || h_b != h_c) {
        std::fprintf(stderr,
                     "(e) FAIL: distinct NaN bit patterns produced "
                     "different merkle contributions (a=0x%016lx b=0x%016lx c=0x%016lx)\n",
                     h_a, h_b, h_c);
        std::abort();
    }

    // Signed zero is the second way the same logical value reaches the fold
    // with two encodings.
    volatile double pz = 0.0;
    volatile double nz = -0.0;
    const auto h_pz = toy_fold(fp::canonicalize(static_cast<double>(pz)));
    const auto h_nz = toy_fold(fp::canonicalize(static_cast<double>(nz)));
    if (h_pz != h_nz) {
        std::fprintf(stderr,
                     "(e) FAIL: ±0 produced different merkle "
                     "contributions (pz=0x%016lx nz=0x%016lx)\n",
                     h_pz, h_nz);
        std::abort();
    }
}

}  // namespace

int main() {
    section_a_nan_canonicalization();
    section_b_signed_zero_canonicalization();
    section_c_pass_through();
    section_d_recipe_gated();
    section_e_merkle_convergence();
    return 0;
}
