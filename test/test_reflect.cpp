#include <crucible/Reflect.h>
#include <crucible/MerkleDag.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/Platform.h>
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <unordered_set>

// Simple test struct with diverse field types.
struct Point {
  int32_t x;
  int32_t y;
  float z;
};

// Struct with array member (like TensorMeta's sizes[8]).
struct Dims {
  int64_t values[4];
  uint8_t count;
};

int main() {
    // ── reflect_hash: basic struct ──────────────────────────────────
    Point p1{10, 20, 3.14f};
    Point p2{10, 20, 3.14f};
    Point p3{10, 21, 3.14f};

    uint64_t h1 = crucible::reflect_hash(p1);
    uint64_t h2 = crucible::reflect_hash(p2);
    uint64_t h3 = crucible::reflect_hash(p3);

    // Same values → same hash
    assert(h1 == h2);
    // Different values → different hash (probabilistic, but fmix64 is good)
    assert(h1 != h3);
    // Non-zero
    assert(h1 != 0);

    // ── reflect_hash: struct with array ─────────────────────────────
    Dims d1{{1, 2, 3, 4}, 4};
    Dims d2{{1, 2, 3, 4}, 4};
    Dims d3{{1, 2, 3, 5}, 4};

    assert(crucible::reflect_hash(d1) == crucible::reflect_hash(d2));
    assert(crucible::reflect_hash(d1) != crucible::reflect_hash(d3));

    // ── reflect_hash: Guard (real Crucible struct) ──────────────────
    crucible::Guard g1{};
    g1.kind = crucible::Guard::Kind::SHAPE_DIM;
    g1.op_index = crucible::OpIndex{42};
    g1.arg_index = 1;
    g1.dim_index = 3;

    crucible::Guard g2 = g1;
    crucible::Guard g3 = g1;
    g3.dim_index = 4;

    assert(crucible::reflect_hash(g1) == crucible::reflect_hash(g2));
    assert(crucible::reflect_hash(g1) != crucible::reflect_hash(g3));

    // ── reflect_hash: TensorMeta (144B, has arrays + pointer + enums) ──
    crucible::TensorMeta m1{};
    m1.ndim = 2;
    m1.sizes[0] = 32;
    m1.sizes[1] = 64;
    m1.strides[0] = 64;
    m1.strides[1] = 1;
    m1.dtype = crucible::ScalarType::Float;
    m1.device_type = crucible::DeviceType::CUDA;
    m1.device_idx = 0;

    crucible::TensorMeta m2 = m1;
    crucible::TensorMeta m3 = m1;
    m3.sizes[1] = 128;

    assert(crucible::reflect_hash(m1) == crucible::reflect_hash(m2));
    assert(crucible::reflect_hash(m1) != crucible::reflect_hash(m3));

    // ── reflect_print: smoke test ───────────────────────────────────
    std::fprintf(stderr, "reflect_print(Point): ");
    crucible::reflect_print(p1, stderr);
    std::fprintf(stderr, "\n");

    std::fprintf(stderr, "reflect_print(Guard): ");
    crucible::reflect_print(g1, stderr);
    std::fprintf(stderr, "\n");

    // ── has_reflected_hash<T> trait ─────────────────────────────────
    //
    // Class types whose every member is reflect_hash-supported should
    // satisfy the trait; non-class types should not.
    static_assert(crucible::has_reflected_hash<Point>);
    static_assert(crucible::has_reflected_hash<Dims>);
    static_assert(crucible::has_reflected_hash<crucible::TensorMeta>);
    static_assert(crucible::has_reflected_hash<crucible::Guard>);

    // Non-class types: trait is false (reflect_hash requires
    // is_class_v<T> per the requires-clause).
    static_assert(!crucible::has_reflected_hash<int>);
    static_assert(!crucible::has_reflected_hash<float>);
    static_assert(!crucible::has_reflected_hash<int*>);

    // ── reflect_fmix_fold<Seed, T> ──────────────────────────────────
    //
    // Same input → same hash (determinism).  Different input → high
    // probability of different hash (avalanche from per-field fmix64).
    // Different seed → different hash (domain separation).
    {
        constexpr uint64_t kSeedA = 0xDEADBEEFCAFEBABEULL;
        constexpr uint64_t kSeedB = 0x0123456789ABCDEFULL;

        const Point p_a{1, 2, 3.0f};
        const Point p_b{1, 2, 3.0f};
        const Point p_c{1, 3, 3.0f};

        const auto h_a = crucible::reflect_fmix_fold<kSeedA>(p_a);
        const auto h_b = crucible::reflect_fmix_fold<kSeedA>(p_b);
        const auto h_c = crucible::reflect_fmix_fold<kSeedA>(p_c);

        assert(h_a == h_b);  // same input → same hash
        assert(h_a != h_c);  // different input → different hash

        // Same input, different seed → different hash.
        const auto h_a2 = crucible::reflect_fmix_fold<kSeedB>(p_a);
        assert(h_a != h_a2);

        // Hash is non-zero with overwhelming probability.
        assert(h_a != 0);
    }

    // ── Avalanche: bit-flip in input → ~50% bits change in output ──
    //
    // Strict avalanche criterion (Webster & Tavares 1985): flipping
    // any single input bit should flip ~50% of output bits.  fmix64
    // is well-tested for this property; reflect_hash composing
    // fmix64-per-field should preserve it.
    //
    // We sample 64 single-bit perturbations on a Wide-ish Spec
    // struct; for each, count how many of the 64 hash bits flip.
    // The mean across perturbations should be near 32 (uniform);
    // standard deviation should be small (no field is "stuck").
    //
    // This isn't a strict statistical test (sample size 64 is small)
    // but catches gross failures like "field N never affects bits 32-47".
    {
        struct AvalancheSpec {
            uint64_t a, b, c, d;
        };
        const AvalancheSpec base{
            0xCAFEBABE12345678ULL,
            0xDEADBEEFFEEDFACEULL,
            0xABCDEF0123456789ULL,
            0x1122334455667788ULL,
        };
        const uint64_t base_h = crucible::reflect_hash(base);

        int total_flips = 0;
        int min_flips = 64;
        int max_flips = 0;
        for (int bit = 0; bit < 64 * 4; ++bit) {
            AvalancheSpec perturbed = base;
            // Flip the bit-th bit of the chosen u64 field.
            uint64_t* fields[4] = {&perturbed.a, &perturbed.b,
                                    &perturbed.c, &perturbed.d};
            const auto bit_idx = static_cast<unsigned>(bit % 64);
            *fields[bit / 64] ^= (uint64_t{1} << bit_idx);
            const uint64_t perturbed_h = crucible::reflect_hash(perturbed);
            const uint64_t diff = base_h ^ perturbed_h;
            // popcount: convert uint64_t → unsigned long long via
            // static_cast (same width on x86-64; the cast suppresses
            // the -Wsign-conversion noise from implicit promotion).
            const int popcount = __builtin_popcountll(
                static_cast<unsigned long long>(diff));
            total_flips += popcount;
            if (popcount < min_flips) min_flips = popcount;
            if (popcount > max_flips) max_flips = popcount;
        }
        const double mean_flips =
            static_cast<double>(total_flips) / (64 * 4);
        // Mean should be near 32 (half of 64 output bits).
        // Tolerance: ±6 bits absorbs sample-size noise (n=256).
        assert(mean_flips > 26.0 && mean_flips < 38.0);
        // No field should be stuck — min flips should be > 16
        // (significantly more than zero or "barely any").
        assert(min_flips >= 16);
        assert(max_flips <= 50);
    }

    // ── Collision resistance over a 1024-input grid ─────────────────
    //
    // 1024 distinct AvalancheSpec instances → 1024 distinct hashes.
    // fmix64's avalanche makes collisions exceptionally unlikely at
    // this scale (~2^-54 per pair); any collision in this grid
    // indicates a structural bug in the reflection fold.
    {
        struct AvalancheSpec {
            uint64_t a, b, c, d;
        };
        std::unordered_set<uint64_t> seen;
        constexpr int N = 1024;
        for (int i = 0; i < N; ++i) {
            const uint64_t iu = static_cast<uint64_t>(i);
            AvalancheSpec s{
                iu,
                iu * uint64_t{0x9E3779B97F4A7C15ULL},
                iu ^ uint64_t{0xDEADBEEFCAFEBABEULL},
                ~iu,
            };
            const auto [it, inserted] = seen.insert(crucible::reflect_hash(s));
            assert(inserted && "reflect_hash collision in 1024-input grid");
        }
        assert(seen.size() == N);
    }

    // ── reflect_fmix_fold cross-process stability goldens ──────────
    //
    // Reflection emits compile-time-fixed code; the output for a
    // pinned input + pinned seed is byte-stable across compiler
    // invocations and runs.  Pin one canonical golden so a future
    // change to fmix_fold's mixing scheme would surface immediately
    // in CI.
    //
    // Update procedure (only after AUDITED intentional change):
    //   1. Run this test, capture printed actual hash
    //   2. Replace the EXPECTED_* constant
    //   3. Bump CDAG_VERSION if any persisted consumer depends on
    //      reflect_fmix_fold output (none today)
    {
        constexpr uint64_t kSeed = 0x9E3779B97F4A7C15ULL;
        struct GoldenSpec {
            uint8_t  a;
            uint16_t b;
            uint32_t c;
            uint64_t d;
        };
        constexpr GoldenSpec spec{
            0xAB, 0xCDEF, 0x12345678U, 0x9ABCDEF012345678ULL
        };
        const uint64_t actual = crucible::reflect_fmix_fold<kSeed>(spec);

        constexpr uint64_t EXPECTED = 0xf03145ef4f0efa55ULL;
        if (actual != EXPECTED) {
            std::fprintf(stderr,
                "reflect_fmix_fold golden DRIFT: "
                "expected 0x%016" PRIx64 ", got 0x%016" PRIx64 "\n"
                "  → update EXPECTED constant in test_reflect.cpp\n"
                "  → audit any persisted consumer of reflect_fmix_fold\n",
                EXPECTED, actual);
            assert(false && "reflect_fmix_fold golden mismatch");
        }
    }

    // ── Cross-call determinism (1000 invocations) ───────────────────
    //
    // reflect_hash + reflect_fmix_fold are gnu::pure constexpr; the
    // output for a fixed input must be byte-identical across every
    // invocation.  1000 calls is excessive but verifies no hidden
    // state leaks via static locals or process-global RNG.
    {
        const Point p{42, -7, 3.14159f};
        const uint64_t h0 = crucible::reflect_hash(p);
        for (int i = 0; i < 1000; ++i) {
            assert(crucible::reflect_hash(p) == h0);
        }
        const uint64_t h0_fold = crucible::reflect_fmix_fold<0xABCDULL>(p);
        for (int i = 0; i < 1000; ++i) {
            assert(crucible::reflect_fmix_fold<0xABCDULL>(p) == h0_fold);
        }
    }

    // ── Empty span / zero-edge behaviour for refactored sites ──────
    //
    // feedback_signature documents: empty → 0; non-empty → nonzero.
    // The refactor preserved this contract.  Pin the boundary cases
    // here (the only behavioural goldens that survive bit-pattern
    // refactors).
    {
        // Empty span → 0.
        assert(crucible::feedback_signature({}) == 0);
        // Single-edge → non-zero.
        const crucible::FeedbackEdge one[1] = {{0, 1}};
        assert(crucible::feedback_signature(
                   std::span<const crucible::FeedbackEdge>{one, 1}) != 0);
        // Edge count is folded in: {A} ≠ {A, A}.
        const crucible::FeedbackEdge two_same[2] = {{0, 1}, {0, 1}};
        assert(crucible::feedback_signature(
                   std::span<const crucible::FeedbackEdge>{one, 1}) !=
               crucible::feedback_signature(
                   std::span<const crucible::FeedbackEdge>{two_same, 2}));
        // Different edge order → different signature (order matters
        // because hash is not commutative).
        const crucible::FeedbackEdge ab[2] = {{1, 2}, {3, 4}};
        const crucible::FeedbackEdge ba[2] = {{3, 4}, {1, 2}};
        assert(crucible::feedback_signature(
                   std::span<const crucible::FeedbackEdge>{ab, 2}) !=
               crucible::feedback_signature(
                   std::span<const crucible::FeedbackEdge>{ba, 2}));
    }

    // ── loopterm_hash field-sensitivity ─────────────────────────────
    //
    // Each of the three semantic fields (term_kind, repeat_count,
    // epsilon) must individually affect the hash.  The reflect_fmix_fold
    // pattern preserves this property by construction (fmix64 per
    // field).  Verify explicitly so a future regression to a
    // commutative or one-field-only fold is caught.
    {
        crucible::LoopNode base{};
        base.term_kind    = crucible::LoopTermKind::REPEAT;
        base.repeat_count = 100;
        base.epsilon      = 0.001f;
        const uint64_t h_base = crucible::loopterm_hash(base);

        crucible::LoopNode alt_kind = base;
        alt_kind.term_kind = crucible::LoopTermKind::UNTIL;
        assert(crucible::loopterm_hash(alt_kind) != h_base);

        crucible::LoopNode alt_count = base;
        alt_count.repeat_count = 101;
        assert(crucible::loopterm_hash(alt_count) != h_base);

        crucible::LoopNode alt_eps = base;
        alt_eps.epsilon = 0.002f;
        assert(crucible::loopterm_hash(alt_eps) != h_base);
    }

    std::printf("test_reflect: all tests passed\n");
    return 0;
}
