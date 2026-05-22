// FIXY-V-093 sentinel TU — fixy::fp::canonicalize merkle-hash-safe FP.
//
// This TU exists to:
//   1. Force the static_assert wall in Canonicalize.h (cells a-d) to
//      fire under the project's warning flags + contract semantics.
//      Header-only static_asserts only run when a real TU includes
//      the header — without this sentinel they go silent.
//   2. Witness the canonicalize/canonicalize_for surfaces at runtime
//      (consteval is one thing; a real bit-cast on a non-constant
//      double is another).
//   3. Witness the four divergence axes (NaN payload, NaN sign, ±0,
//      recipe-gated overload) the way a downstream caller would
//      consume them: feed in, hash the result, prove same-bits-same-
//      hash and different-bits-different-hash.
//
// Layered as 5 named sections, each one runtime-checking what the
// header asserts at compile time, plus one section that exercises
// the recipe-gated form with a real bit_cast<uint64_t> identity.

#include <crucible/fixy/fp/Canonicalize.h>
#include <crucible/NumericalRecipe.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace fp = crucible::fixy::fp;

namespace {

// ─── (a) NaN canonicalization runtime witness ──────────────────────
//
// Compile-time asserts in Canonicalize.h cover the consteval path;
// here we feed runtime-only NaN payloads (constructed from runtime
// volatile uint64_t to defeat constant folding) and verify the same
// canonical projection.

void section_a_nan_canonicalization() {
    // Construct a non-constant NaN bit pattern via a volatile read
    // (otherwise GCC consteval-collapses the whole computation).
    volatile std::uint64_t nan_payload_a = 0x7FF1234567890ABCULL;
    volatile std::uint64_t nan_payload_b = 0xFFFEDCBA98765432ULL;  // negative-sign NaN
    const double nan_a = std::bit_cast<double>(nan_payload_a);
    const double nan_b = std::bit_cast<double>(nan_payload_b);

    if (fp::canonicalize(nan_a) != fp::kCanonicalQNaN64) {
        std::fprintf(stderr, "V-093 (a) FAIL: positive-sign NaN payload did not canonicalize\n");
        std::abort();
    }
    if (fp::canonicalize(nan_b) != fp::kCanonicalQNaN64) {
        std::fprintf(stderr, "V-093 (a) FAIL: negative-sign NaN payload did not canonicalize\n");
        std::abort();
    }

    // Float side.
    volatile std::uint32_t nan_payload_f = 0xFFC12345U;
    const float nan_f = std::bit_cast<float>(nan_payload_f);
    if (fp::canonicalize(nan_f) != fp::kCanonicalQNaN32) {
        std::fprintf(stderr, "V-093 (a) FAIL: float NaN did not canonicalize\n");
        std::abort();
    }

    // Two different NaN payloads must collapse to the SAME canonical
    // value — this is THE property merkle hashing depends on.
    if (fp::canonicalize(nan_a) != fp::canonicalize(nan_b)) {
        std::fprintf(stderr, "V-093 (a) FAIL: distinct NaN payloads did not converge\n");
        std::abort();
    }
}

// ─── (b) ±0 canonicalization runtime witness ───────────────────────

void section_b_signed_zero_canonicalization() {
    volatile double pos_zero = 0.0;
    volatile double neg_zero = -0.0;
    // Sanity: raw bit patterns differ.
    if (std::bit_cast<std::uint64_t>(static_cast<double>(neg_zero)) == 0) {
        std::fprintf(stderr, "V-093 (b) FAIL: -0.0 raw bits unexpectedly zero\n");
        std::abort();
    }
    if (fp::canonicalize(static_cast<double>(pos_zero)) != 0) {
        std::fprintf(stderr, "V-093 (b) FAIL: +0.0 canonicalize result non-zero\n");
        std::abort();
    }
    if (fp::canonicalize(static_cast<double>(neg_zero)) != 0) {
        std::fprintf(stderr, "V-093 (b) FAIL: -0.0 canonicalize result non-zero\n");
        std::abort();
    }
    // Float side.
    volatile float pos_zero_f = 0.0f;
    volatile float neg_zero_f = -0.0f;
    if (fp::canonicalize(static_cast<float>(pos_zero_f)) != 0) {
        std::fprintf(stderr, "V-093 (b) FAIL: +0.0f canonicalize result non-zero\n");
        std::abort();
    }
    if (fp::canonicalize(static_cast<float>(neg_zero_f)) != 0) {
        std::fprintf(stderr, "V-093 (b) FAIL: -0.0f canonicalize result non-zero\n");
        std::abort();
    }
}

// ─── (c) Finite-value pass-through runtime witness ─────────────────

void section_c_pass_through() {
    volatile double values[] = {1.0, -1.0, 3.14, -2.718, 1e-300, 1e300,
                                std::numeric_limits<double>::infinity(),
                                -std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::min(),  // smallest normal
                                std::numeric_limits<double>::denorm_min()};  // smallest subnormal
    for (const volatile double& v : values) {
        const double x = v;
        const auto expected = std::bit_cast<std::uint64_t>(x);
        if (fp::canonicalize(x) != expected) {
            std::fprintf(stderr,
                         "V-093 (c) FAIL: finite value 0x%016lx did not pass through\n",
                         expected);
            std::abort();
        }
    }

    // Float side.
    volatile float values_f[] = {1.0f, -1.0f, 3.14f, -2.718f, 1e-30f, 1e30f,
                                  std::numeric_limits<float>::infinity(),
                                  -std::numeric_limits<float>::infinity()};
    for (const volatile float& v : values_f) {
        const float x = v;
        const auto expected = std::bit_cast<std::uint32_t>(x);
        if (fp::canonicalize(x) != expected) {
            std::fprintf(stderr,
                         "V-093 (c) FAIL: finite float 0x%08x did not pass through\n",
                         expected);
            std::abort();
        }
    }
}

// ─── (d) Recipe-gated overload runtime witness ─────────────────────
//
// The recipe-gated path is consteval-friendly (R is an NTTP); calling
// it at runtime is also valid because std::bit_cast + the std::isnan
// constexpr machinery all fold at runtime too.

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
    const auto h_tc     = fp::canonicalize_for<kSpecBitexactTc>(static_cast<double>(finite));
    const auto expected = std::bit_cast<std::uint64_t>(static_cast<double>(finite));
    if (h_strict != expected || h_tc != expected) {
        std::fprintf(stderr,
                     "V-093 (d) FAIL: recipe-gated overload diverged on finite\n");
        std::abort();
    }

    if (fp::canonicalize_for<kSpecBitexactStrict>(static_cast<double>(signed_zero)) != 0) {
        std::fprintf(stderr, "V-093 (d) FAIL: recipe-gated -0.0 not canonicalized\n");
        std::abort();
    }
    if (fp::canonicalize_for<kSpecBitexactStrict>(static_cast<double>(nan_val))
        != fp::kCanonicalQNaN64) {
        std::fprintf(stderr, "V-093 (d) FAIL: recipe-gated NaN not canonicalized\n");
        std::abort();
    }

    // Float overload symmetry.
    volatile float finite_f = -7.25f;
    const auto h_f = fp::canonicalize_for<kSpecBitexactStrict>(static_cast<float>(finite_f));
    if (h_f != std::bit_cast<std::uint32_t>(static_cast<float>(finite_f))) {
        std::fprintf(stderr, "V-093 (d) FAIL: recipe-gated float diverged\n");
        std::abort();
    }

    // Projection from a full NumericalRecipe (ergonomic call-site).
    constexpr crucible::NumericalRecipe kFullRecipe{
        .reduction_algo = crucible::ReductionAlgo::PAIRWISE,
        .rounding       = crucible::RoundingMode::RN,
        .determinism    = crucible::ReductionDeterminism::BITEXACT_TC,
        .hash           = crucible::RecipeHash{},
    };
    const auto h_proj = fp::canonicalize_for<fp::CanonicalizeRecipeSpec{kFullRecipe}>(
        static_cast<double>(finite));
    if (h_proj != expected) {
        std::fprintf(stderr,
                     "V-093 (d) FAIL: NumericalRecipe projection diverged\n");
        std::abort();
    }
}

// ─── (e) Convergence property — DIFFERENT NaN payloads ───────────
//                                    ⟹ SAME merkle contribution ───
//
// This is the load-bearing claim of V-093: feeding two distinct NaN
// bit patterns into a merkle hash via canonicalize produces the same
// hash output.  We assemble a toy "merkle fold" (one FNV-1a step) on
// top of two different NaN encodings of the same logical NaN and
// verify the fold result agrees.

void section_e_merkle_convergence() {
    volatile std::uint64_t nan_a_bits = 0x7FF0000000000001ULL;  // sNaN-like
    volatile std::uint64_t nan_b_bits = 0x7FFFFFFFFFFFFFFFULL;  // qNaN max payload
    volatile std::uint64_t nan_c_bits = 0xFFF8000000000000ULL;  // negative qNaN
    const double nan_a = std::bit_cast<double>(nan_a_bits);
    const double nan_b = std::bit_cast<double>(nan_b_bits);
    const double nan_c = std::bit_cast<double>(nan_c_bits);

    auto toy_fold = [](std::uint64_t bits) constexpr {
        // FNV-1a-like single step (NOT the real merkle wymix — just
        // an injective enough hash for this convergence test).
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
                     "V-093 (e) FAIL: distinct NaN bit patterns produced "
                     "different merkle contributions (a=0x%016lx b=0x%016lx c=0x%016lx)\n",
                     h_a, h_b, h_c);
        std::abort();
    }

    // Also: +0.0 and -0.0 must agree (this is the second class of
    // merkle drift).
    volatile double pz = 0.0;
    volatile double nz = -0.0;
    const auto h_pz = toy_fold(fp::canonicalize(static_cast<double>(pz)));
    const auto h_nz = toy_fold(fp::canonicalize(static_cast<double>(nz)));
    if (h_pz != h_nz) {
        std::fprintf(stderr,
                     "V-093 (e) FAIL: ±0 produced different merkle "
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
