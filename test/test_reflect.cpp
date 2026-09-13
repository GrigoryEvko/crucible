#include <crucible/Reflect.h>
#include <crucible/MerkleDag.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/Platform.h>
#include "test_assert.h"
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <unordered_set>

struct Point {
    int32_t x;
    int32_t y;
    float z;
};

// Covers the array-member case, which the fold has to walk element by
// element rather than hash whole.
struct Dims {
    int64_t values[4];
    uint8_t count;
};

int main() {
    Point p1{10, 20, 3.14f};
    Point p2{10, 20, 3.14f};
    Point p3{10, 21, 3.14f};

    uint64_t h1 = crucible::reflect_hash(p1);
    uint64_t h2 = crucible::reflect_hash(p2);
    uint64_t h3 = crucible::reflect_hash(p3);

    assert(h1 == h2);
    // Distinct inputs colliding is possible in principle.  The mixing
    // function makes it unlikely enough to assert against.
    assert(h1 != h3);
    assert(h1 != 0);

    Dims d1{{1, 2, 3, 4}, 4};
    Dims d2{{1, 2, 3, 4}, 4};
    Dims d3{{1, 2, 3, 5}, 4};

    assert(crucible::reflect_hash(d1) == crucible::reflect_hash(d2));
    assert(crucible::reflect_hash(d1) != crucible::reflect_hash(d3));

    // The remaining cases use production types rather than fixtures,
    // so the walk is exercised over real field layouts.
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

    crucible::TensorMeta m1{};
    m1.ndim = 2;
    m1.sizes[0] = ::crucible::tensor_dim(32);
    m1.sizes[1] = ::crucible::tensor_dim(64);
    m1.strides[0] = ::crucible::tensor_dim(64);
    m1.strides[1] = ::crucible::tensor_dim(1);
    m1.dtype = crucible::ScalarType::Float;
    m1.device_type = crucible::DeviceType::CUDA;
    m1.device_idx = 0;

    crucible::TensorMeta m2 = m1;
    crucible::TensorMeta m3 = m1;
    m3.sizes[1] = ::crucible::tensor_dim(128);

    assert(crucible::reflect_hash(m1) == crucible::reflect_hash(m2));
    assert(crucible::reflect_hash(m1) != crucible::reflect_hash(m3));

    std::fprintf(stderr, "reflect_print(Point): ");
    crucible::reflect_print(p1, stderr);
    std::fprintf(stderr, "\n");

    std::fprintf(stderr, "reflect_print(Guard): ");
    crucible::reflect_print(g1, stderr);
    std::fprintf(stderr, "\n");

    // The trait holds for a class whose every member can itself be
    // hashed.
    static_assert(crucible::has_reflected_hash<Point>);
    static_assert(crucible::has_reflected_hash<Dims>);
    static_assert(crucible::has_reflected_hash<crucible::TensorMeta>);
    static_assert(crucible::has_reflected_hash<crucible::Guard>);

    // A non-class type has no members to walk, so the trait rejects
    // it outright rather than hashing the object representation.
    static_assert(!crucible::has_reflected_hash<int>);
    static_assert(!crucible::has_reflected_hash<float>);
    static_assert(!crucible::has_reflected_hash<int*>);

    // The seed separates domains: the same input under two seeds must
    // not land on the same hash, or two unrelated uses of the fold
    // would share a key space.
    {
        constexpr uint64_t kSeedA = 0xDEADBEEFCAFEBABEULL;
        constexpr uint64_t kSeedB = 0x0123456789ABCDEFULL;

        const Point p_a{1, 2, 3.0f};
        const Point p_b{1, 2, 3.0f};
        const Point p_c{1, 3, 3.0f};

        const auto h_a = crucible::reflect_fmix_fold<kSeedA>(p_a);
        const auto h_b = crucible::reflect_fmix_fold<kSeedA>(p_b);
        const auto h_c = crucible::reflect_fmix_fold<kSeedA>(p_c);

        assert(h_a == h_b);
        assert(h_a != h_c);

        const auto h_a2 = crucible::reflect_fmix_fold<kSeedB>(p_a);
        assert(h_a != h_a2);

        assert(h_a != 0);
    }

    // Under the strict avalanche criterion, flipping any one input bit
    // flips about half the output bits.  The mixing function has that
    // property on its own, and hashing field by field has to preserve
    // it.
    //
    // Each of the 256 input bits is flipped in turn and the number of
    // output bits that change is counted.  With a sample this small
    // the result is not a statistical test.  It does catch a gross
    // failure, such as one field never reaching part of the output.
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
            uint64_t* fields[4] = {&perturbed.a, &perturbed.b, &perturbed.c, &perturbed.d};
            const auto bit_idx = static_cast<unsigned>(bit % 64);
            *fields[bit / 64] ^= (uint64_t{1} << bit_idx);
            const uint64_t perturbed_h = crucible::reflect_hash(perturbed);
            const uint64_t diff = base_h ^ perturbed_h;
            // The cast is explicit only to keep the implicit
            // promotion from tripping the sign-conversion warning.
            // The two types have the same width here.
            const int popcount = __builtin_popcountll(static_cast<unsigned long long>(diff));
            total_flips += popcount;
            if (popcount < min_flips) min_flips = popcount;
            if (popcount > max_flips) max_flips = popcount;
        }
        const double mean_flips = static_cast<double>(total_flips) / (64 * 4);
        // Half of 64 output bits is 32.  The window of six bits either
        // side absorbs the noise of a 256-sample mean.
        assert(mean_flips > 26.0 && mean_flips < 38.0);
        // A field that barely reached the output would show up as a
        // perturbation that changes almost nothing.
        assert(min_flips >= 16);
        assert(max_flips <= 50);
    }

    // A thousand distinct inputs must give a thousand distinct
    // hashes.  A chance collision at this scale is vanishingly
    // unlikely, so one here means the fold is structurally wrong
    // rather than unlucky.
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

    // The emitted code is fixed at compile time, so a pinned input
    // under a pinned seed gives the same bytes on every build and
    // every run.  The value below is that constant.  Changing the
    // mixing scheme changes it, which is the point.
    //
    // Replace it only after deciding the change is intended, and only
    // after checking that nothing persisted holds the old value.
    {
        constexpr uint64_t kSeed = 0x9E3779B97F4A7C15ULL;
        struct GoldenSpec {
            uint8_t a;
            uint16_t b;
            uint32_t c;
            uint64_t d;
        };
        constexpr GoldenSpec spec{0xAB, 0xCDEF, 0x12345678U, 0x9ABCDEF012345678ULL};
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

    // Both folds are pure, so a fixed input gives identical bytes on
    // every call.  A thousand calls is far more than the property
    // needs, but it would expose hidden state in a static local or a
    // shared generator.
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

    // An empty edge set signs as zero and a non-empty one never does.
    // These boundary cases are the part of the contract that survives
    // any change to the bit pattern.
    {
        assert(crucible::feedback_signature({}) == 0);
        const crucible::FeedbackEdge one[1] = {{0, 1}};
        assert(crucible::feedback_signature(std::span<const crucible::FeedbackEdge>{one, 1}) != 0);
        // The edge count is folded in, so repeating one edge signs
        // differently from having it once.
        const crucible::FeedbackEdge two_same[2] = {{0, 1}, {0, 1}};
        assert(crucible::feedback_signature(std::span<const crucible::FeedbackEdge>{one, 1})
               != crucible::feedback_signature(std::span<const crucible::FeedbackEdge>{two_same, 2}));
        // The fold is not commutative, so reordering the edges
        // changes the signature.
        const crucible::FeedbackEdge ab[2] = {{1, 2}, {3, 4}};
        const crucible::FeedbackEdge ba[2] = {{3, 4}, {1, 2}};
        assert(crucible::feedback_signature(std::span<const crucible::FeedbackEdge>{ab, 2})
               != crucible::feedback_signature(std::span<const crucible::FeedbackEdge>{ba, 2}));
    }

    // Each of the three termination fields must move the hash on its
    // own.  A fold that dropped a field, or that became commutative,
    // would still pass a test that varied them together.
    {
        crucible::LoopNode base{};
        base.term_kind = crucible::LoopTermKind::REPEAT;
        base.repeat_count = 100;
        base.epsilon = 0.001f;
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
