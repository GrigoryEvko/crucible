#include <crucible/Reflect.h>
#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>
#include <cassert>
#include <cstdio>

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

    std::printf("test_reflect: all tests passed\n");
    return 0;
}
