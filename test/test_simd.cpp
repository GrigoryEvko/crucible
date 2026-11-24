// The facade under test is built on compiler vector extensions rather
// than on the standard library's vector header, because that header is
// gated behind an x86 feature macro and compiles to nothing on the
// other supported architecture.

#include <foundation/Simd.h>
#include <crucible/DimHash.h>
#include <crucible/MerkleDag.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace simd = foundation::simd;

static void test_type_aliases() {
    static_assert(simd::i64x8::size() == 8);
    static_assert(simd::u64x8::size() == 8);
    static_assert(simd::i32x16::size() == 16);
    static_assert(simd::u8x32::size() == 32);
    ::fixy::check::pass(kTest, "  test_type_aliases: PASSED\n");
}

static void test_iota() {
    auto indices = simd::iota_v<simd::i64x8>();
    for (int lane = 0; lane < static_cast<int>(simd::i64x8::size()); ++lane) {
        CRUCIBLE_TEST_ASSERT(kTest, indices[lane] == static_cast<int64_t>(lane));
    }
    ::fixy::check::pass(kTest, "  test_iota: PASSED\n");
}

static void test_prefix_mask() {
    // Both ends of the range are covered as well as a partial count,
    // because an off-by-one implementation passes the middle case.
    auto mask0 = simd::prefix_mask<simd::i64x8>(0);
    for (int lane = 0; lane < static_cast<int>(simd::i64x8::size()); ++lane) {
        CRUCIBLE_TEST_ASSERT(kTest, !mask0[lane]);
    }
    auto mask5 = simd::prefix_mask<simd::i64x8>(5);
    for (int lane = 0; lane < 5; ++lane)
        CRUCIBLE_TEST_ASSERT(kTest, mask5[lane]);
    for (int lane = 5; lane < 8; ++lane)
        CRUCIBLE_TEST_ASSERT(kTest, !mask5[lane]);
    auto mask8 = simd::prefix_mask<simd::i64x8>(8);
    for (int lane = 0; lane < 8; ++lane)
        CRUCIBLE_TEST_ASSERT(kTest, mask8[lane]);
    ::fixy::check::pass(kTest, "  test_prefix_mask: PASSED\n");
}

static void test_det_safe_simd_concept() {
    static_assert(simd::DetSafeSimd<simd::i64x8>);
    static_assert(simd::DetSafeSimd<simd::u64x8>);
    static_assert(simd::DetSafeSimd<simd::u32x8>);
    static_assert(simd::DetSafeSimd<simd::u8x32>);

    // Floating-point lanes are excluded on purpose.  A reduction folds
    // the lanes in an order the vector width decides, and rounding
    // makes that order visible in the result, so the same input would
    // produce different bits on different machines.
    using f32x8 = simd::vec<float, 8>;
    using f64x4 = simd::vec<double, 4>;
    static_assert(!simd::DetSafeSimd<f32x8>);
    static_assert(!simd::DetSafeSimd<f64x4>);

    static_assert(!simd::DetSafeSimd<int64_t>);
    ::fixy::check::pass(kTest, "  test_det_safe_simd_concept: PASSED\n");
}

static void test_load() {
    ::foundation::core::FixedArray<int64_t, 8> source{10, 20, 30, 40, 50, 60, 70, 80};
    auto v = simd::load<simd::i64x8>(source.view());
    for (auto const index : source.indices()) {
        CRUCIBLE_TEST_ASSERT(kTest, v[static_cast<int>(index.raw())] == source.at(index));
    }
    ::fixy::check::pass(kTest, "  test_load: PASSED\n");
}

// A load from the bytes of one register gives each lane its little-endian
// word, the lowest lane first.  The window starts at an odd offset, so the
// load reads an address with no alignment.
static void test_load_bytes() {
    ::foundation::core::FixedArray<std::byte, 33> bytes{};
    for (auto index : bytes.indices()) {
        bytes.set(index, static_cast<std::byte>(index.raw() + 1));
    }
    auto const v = simd::load<simd::u64x4>(bytes.view().window<32>(1).expect("32 bytes fit after the first"));
    CRUCIBLE_TEST_ASSERT(kTest, v[0] == 0x0908070605040302ULL);
    CRUCIBLE_TEST_ASSERT(kTest, v[1] == 0x11100F0E0D0C0B0AULL);
    CRUCIBLE_TEST_ASSERT(kTest, v[2] == 0x1918171615141312ULL);
    CRUCIBLE_TEST_ASSERT(kTest, v[3] == 0x21201F1E1D1C1B1AULL);
    ::fixy::check::pass(kTest, "  test_load_bytes: PASSED\n");
}

// A load from the lanes of one register gives each lane its element, the
// lowest lane first.  The window starts one element in, so the load reads
// an address that has only the alignment of the element.
static void test_load_lanes() {
    ::foundation::core::FixedArray<int64_t, 9> lanes{};
    for (auto index : lanes.indices()) {
        lanes.set(index, static_cast<int64_t>(index.raw()) * -3);
    }
    auto const v = simd::load<simd::i64x8>(lanes.view().window<8>(1).expect("8 lanes fit after the first"));
    for (int lane = 0; lane < 8; ++lane) {
        CRUCIBLE_TEST_ASSERT(kTest, v[lane] == static_cast<int64_t>(lane + 1) * -3);
    }
    ::fixy::check::pass(kTest, "  test_load_lanes: PASSED\n");
}

// A store into the bytes of one register writes each lane as its
// little-endian word, and touches no byte outside the window.  A bit cast
// keeps each bit of the register.
static void test_store_bytes_and_bit_cast() {
    ::foundation::core::FixedArray<std::byte, 34> bytes{};
    simd::u64x4 const v{simd::u64x4::raw_type{0x0807060504030201ULL, 0x100F0E0D0C0B0A09ULL, 0x1817161514131211ULL,
                                              0x201F1E1D1C1B1A19ULL}};
    bytes.with_mut([v](auto run) noexcept {
        simd::store(v, ::fixy::take_window<32>(std::move(run), 1).expect("32 bytes fit after the first"));
    });
    for (auto index : bytes.indices()) {
        auto const expected = index.raw() == 0 || index.raw() == 33 ? 0U : index.raw();
        CRUCIBLE_TEST_ASSERT(kTest, bytes.at(index) == static_cast<std::byte>(expected));
    }
    auto const halves = simd::bit_cast<simd::vec<std::uint32_t, 8>>(v);
    CRUCIBLE_TEST_ASSERT(kTest, halves[0] == 0x04030201U && halves[1] == 0x08070605U && halves[7] == 0x201F1E1DU);
    CRUCIBLE_TEST_ASSERT(kTest, simd::bit_cast<simd::u64x4>(halves)[2] == v[2]);
    ::fixy::check::pass(kTest, "  test_store_bytes_and_bit_cast: PASSED\n");
}

// A store into the lanes of one register writes each lane into its element,
// the lowest lane first, and touches no element outside the window.  The
// window starts one element in, so the store writes an address that has only
// the alignment of the element.  A load of the same window gives the vector
// back.
static void test_store_lanes() {
    ::foundation::core::FixedArray<int64_t, 10> lanes{};
    simd::i64x8 const v = simd::iota_v<simd::i64x8>() * simd::i64x8{int64_t{-3}} + simd::i64x8{int64_t{7}};
    lanes.with_mut([v](auto run) noexcept {
        simd::store(v, ::fixy::take_window<8>(std::move(run), 1).expect("8 lanes fit after the first"));
    });
    for (auto index : lanes.indices()) {
        auto const lane = static_cast<int64_t>(index.raw());
        auto const expected = index.raw() == 0 || index.raw() == 9 ? int64_t{0} : (lane - 1) * -3 + 7;
        CRUCIBLE_TEST_ASSERT(kTest, lanes.at(index) == expected);
    }
    auto const back = simd::load<simd::i64x8>(lanes.view().window<8>(1).expect("8 lanes fit after the first"));
    for (int lane = 0; lane < 8; ++lane) {
        CRUCIBLE_TEST_ASSERT(kTest, back[lane] == v[lane]);
    }

    ::foundation::core::FixedArray<std::uint32_t, 10> words{};
    simd::u32x8 const w = simd::iota_v<simd::u32x8>() + simd::u32x8{std::uint32_t{0xA0000000U}};
    words.with_mut([w](auto run) noexcept {
        simd::store(w, ::fixy::take_window<8>(std::move(run), 2).expect("8 lanes fit after the second"));
    });
    for (auto index : words.indices()) {
        auto const expected = index.raw() < 2 ? 0U : 0xA0000000U + static_cast<std::uint32_t>(index.raw() - 2);
        CRUCIBLE_TEST_ASSERT(kTest, words.at(index) == expected);
    }
    ::fixy::check::pass(kTest, "  test_store_lanes: PASSED\n");
}

// The sign bit and the equal bit of each lane, at one width.  One lane at a
// time differs from the others, so a lost or a moved lane fails.  Lanes 7
// and 15 hold the top byte of a 64-bit word.  The Debug build has no AVX2
// and no AVX-512, so there the 32 and the 64 lanes take the form with one
// multiply for each 8 lanes.
template <int Lanes>
static void check_collect_bits() {
    using Bytes = simd::vec<std::int8_t, Lanes>;
    std::uint64_t const every_lane = Lanes == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << Lanes) - 1;
    Bytes const tags{std::int8_t{0x42}};
    Bytes const empty{std::int8_t{-128}};
    CRUCIBLE_TEST_ASSERT(kTest, simd::collect_sign_bits(tags) == 0);
    CRUCIBLE_TEST_ASSERT(kTest, simd::collect_sign_bits(empty) == every_lane);
    CRUCIBLE_TEST_ASSERT(kTest, simd::collect_equal_bits(tags, tags) == every_lane);
    for (int lane = 0; lane < Lanes; ++lane) {
        Bytes const one_empty([lane](auto index) noexcept {
            return decltype(index)::value == lane ? std::int8_t{-128} : std::int8_t{0x42};
        });
        std::uint64_t const bit = std::uint64_t{1} << lane;
        CRUCIBLE_TEST_ASSERT(kTest, simd::collect_sign_bits(one_empty) == bit);
        CRUCIBLE_TEST_ASSERT(kTest, simd::collect_sign_bits(one_empty == empty) == bit);
        CRUCIBLE_TEST_ASSERT(kTest, simd::collect_equal_bits(one_empty, empty) == bit);
        CRUCIBLE_TEST_ASSERT(kTest, simd::collect_equal_bits(one_empty, tags) == (every_lane & ~bit));
    }
}

static void test_collect_bits() {
    check_collect_bits<16>();
    check_collect_bits<32>();
    check_collect_bits<64>();
    ::fixy::check::pass(kTest, "  test_collect_bits: PASSED\n");
}

// The register runs of the run of a frame and of a View of one count: each
// whole run once, in order, and the bytes after the last whole run as the
// rest.  The form of two Views reads the same runs.
static void test_for_each_register_run() {
    using Bytes = simd::vec<std::uint8_t, 16>;
    ::foundation::core::FixedArray<std::byte, 37> target{};
    ::foundation::core::FixedArray<std::byte, 37> source{};
    for (auto index : source.indices()) {
        source.set(index, static_cast<std::byte>(index.raw() + 1));
    }
    int runs = 0;
    target.with_mut([&runs, &source](auto run) noexcept {
        auto [target_rest, source_rest] = simd::for_each_register_run<Bytes>(
            std::move(run), ::fixy::View<std::byte const>{source.view()}, [&runs](auto out, auto in) noexcept {
                simd::store(simd::load<Bytes>(in), std::move(out));
                ++runs;
            });
        CRUCIBLE_TEST_ASSERT(kTest, runs == 2);
        CRUCIBLE_TEST_ASSERT(kTest, target_rest.size() == 5 && source_rest.size() == 5);
    });
    for (auto index : target.indices()) {
        auto const expected = index.raw() < 32 ? index.raw() + 1 : 0U;
        CRUCIBLE_TEST_ASSERT(kTest, target.at(index) == static_cast<std::byte>(expected));
    }
    int equal_runs = 0;
    auto const [first_rest, second_rest] = simd::for_each_register_run<Bytes>(
        ::fixy::View<std::byte const>{target.view()}, ::fixy::View<std::byte const>{source.view()},
        [&equal_runs](auto first, auto second) noexcept {
            if (simd::collect_equal_bits(simd::load<Bytes>(first), simd::load<Bytes>(second)) == 0xFFFFU) {
                ++equal_runs;
            }
        });
    CRUCIBLE_TEST_ASSERT(kTest, equal_runs == 2 && first_rest.size() == 5 && second_rest.size() == 5);
    ::fixy::check::pass(kTest, "  test_for_each_register_run: PASSED\n");
}

static void test_reduce_xor_sum() {
    ::foundation::core::FixedArray<int64_t, 8> values{0, 1, 2, 3, 4, 5, 6, 7};
    auto input = simd::load<simd::i64x8>(values.view());

    int64_t xor_expected = 0;
    int64_t sum_expected = 0;
    for (auto v : values) {
        xor_expected ^= v;
        sum_expected += v;
    }

    int64_t xor_got = simd::reduce_xor(input);
    int64_t sum_got = simd::reduce_add(input);
    CRUCIBLE_TEST_ASSERT(kTest, xor_got == xor_expected);
    CRUCIBLE_TEST_ASSERT(kTest, sum_got == sum_expected);
    ::fixy::check::pass(kTest, "  test_reduce_xor_sum: PASSED (xor={} sum={})\n", static_cast<long long>(xor_got),
                        static_cast<long long>(sum_got));
}

static void test_reduce_or_and() {
    ::foundation::core::FixedArray<uint64_t, 8> values{0x01ULL, 0x02ULL, 0x04ULL, 0x08ULL,
                                                       0x10ULL, 0x20ULL, 0x40ULL, 0x80ULL};
    auto input = simd::load<simd::u64x8>(values.view());
    CRUCIBLE_TEST_ASSERT(kTest, simd::reduce_or(input) == 0xFFULL);
    CRUCIBLE_TEST_ASSERT(kTest, simd::reduce_and(input) == 0ULL);
    ::fixy::check::pass(kTest, "  test_reduce_or_and: PASSED\n");
}

static void test_reduce_max_min() {
    ::foundation::core::FixedArray<int64_t, 8> values{-5, 100, 33, -200, 7, 42, 999, 0};
    auto input = simd::load<simd::i64x8>(values.view());
    CRUCIBLE_TEST_ASSERT(kTest, simd::reduce_max(input) == 999);
    CRUCIBLE_TEST_ASSERT(kTest, simd::reduce_min(input) == -200);
    ::fixy::check::pass(kTest, "  test_reduce_max_min: PASSED\n");
}

static void test_select() {
    ::foundation::core::FixedArray<int64_t, 8> on_true{100, 200, 300, 400, 500, 600, 700, 800};
    ::foundation::core::FixedArray<int64_t, 8> on_false{1, 2, 3, 4, 5, 6, 7, 8};

    auto t_v = simd::load<simd::i64x8>(on_true.view());
    auto f_v = simd::load<simd::i64x8>(on_false.view());

    auto mask = simd::prefix_mask<simd::i64x8>(4);
    auto out = simd::select(mask, t_v, f_v);
    CRUCIBLE_TEST_ASSERT(kTest, out[0] == 100);
    CRUCIBLE_TEST_ASSERT(kTest, out[1] == 200);
    CRUCIBLE_TEST_ASSERT(kTest, out[2] == 300);
    CRUCIBLE_TEST_ASSERT(kTest, out[3] == 400);
    CRUCIBLE_TEST_ASSERT(kTest, out[4] == 5);
    CRUCIBLE_TEST_ASSERT(kTest, out[5] == 6);
    CRUCIBLE_TEST_ASSERT(kTest, out[6] == 7);
    CRUCIBLE_TEST_ASSERT(kTest, out[7] == 8);

    // The two operands are re-read afterwards: a select that wrote
    // into one of them would still produce the right result above.
    for (auto const index : on_true.indices()) {
        int const lane = static_cast<int>(index.raw());
        CRUCIBLE_TEST_ASSERT(kTest, t_v[lane] == on_true.at(index));
        CRUCIBLE_TEST_ASSERT(kTest, f_v[lane] == on_false.at(index));
    }
    ::fixy::check::pass(kTest, "  test_select: PASSED\n");
}

// This is the shape the dimension hash uses: one masked reduction over
// a full-width vector, with the identity covering the lanes past the
// dimension count, instead of a select and a second pass.
static void test_masked_reduce_for_dim_hash_pattern() {
    ::foundation::core::FixedArray<int64_t, 8> sizes{2, 3, 4, 5, 6, 7, 8, 9};
    ::foundation::core::FixedArray<int64_t, 8> mix_lo{7, 11, 13, 17, 19, 23, 29, 31};
    constexpr int ndim = 5;

    auto sizes_v = simd::load<simd::i64x8>(sizes.view());
    auto mix_v = simd::load<simd::i64x8>(mix_lo.view());
    simd::i64x8 product = sizes_v * mix_v;

    int64_t simd_result = simd::reduce_xor(product, simd::prefix_mask<simd::i64x8>(ndim));

    int64_t scalar_result = 0;
    for (auto const index : sizes.indices()) {
        if (index.raw() < static_cast<std::size_t>(ndim)) scalar_result ^= sizes.at(index) * mix_lo.at(index);
    }

    CRUCIBLE_TEST_ASSERT(kTest, simd_result == scalar_result);
    ::fixy::check::pass(kTest,
                        "  test_masked_reduce_for_dim_hash_pattern: PASSED "
                        "(simd={} scalar={})\n",
                        static_cast<long long>(simd_result), static_cast<long long>(scalar_result));
}

static void test_microarch_detection() {
    // The compile-time flags describe the build target, and a debug
    // build names no target at all, so all of them may legitimately be
    // false.  They are printed and not asserted.
    //
    // The runtime probes describe the machine this is running on, and
    // there at least one extension has to be present: the project's
    // own baseline already requires more than the oldest of them.
#if defined(__x86_64__) || defined(__i386__)
    bool any_runtime =
        simd::runtime_supports_sse42() || simd::runtime_supports_avx2() || simd::runtime_supports_avx512();
    CRUCIBLE_TEST_ASSERT(kTest, any_runtime && "no SIMD ISA detected at runtime on x86-64");
#endif
    ::fixy::check::pass(kTest,
                        "  test_microarch_detection: PASSED "
                        "(compile: sse42={} avx2={} avx512={} neon={}; "
                        "runtime: sse42={} avx2={} avx512={})\n",
                        simd::kSse42Available, simd::kAvx2Available, simd::kAvx512Available, simd::kNeonAvailable,
                        simd::runtime_supports_sse42(), simd::runtime_supports_avx2(), simd::runtime_supports_avx512());
}

// The shapes below are chosen by hand, one per structural case the
// hash has to handle.  Broad random coverage is a fuzzer's job.

static crucible::TensorMeta make_meta(std::initializer_list<int64_t> sizes, std::initializer_list<int64_t> strides,
                                      crucible::ScalarType dtype = crucible::ScalarType::Float) {
    crucible::TensorMeta meta{};
    meta.ndim = static_cast<uint8_t>(sizes.size());
    CRUCIBLE_TEST_ASSERT(kTest, strides.size() == sizes.size());
    uint8_t d = 0;
    for (int64_t const size : sizes) {
        meta.sizes[d] = ::crucible::tensor_dim(size);
        ++d;
    }
    d = 0;
    for (int64_t const stride : strides) {
        meta.strides[d] = ::crucible::tensor_dim(stride);
        ++d;
    }
    meta.dtype = dtype;
    return meta;
}

// A TensorMeta one natural alignment past a boundary of 64 bytes: the lead
// fills the bytes before it.
struct alignas(64) MisalignedMeta {
    ::foundation::core::FixedArray<uint8_t, alignof(crucible::TensorMeta)> lead{};
    crucible::TensorMeta meta{};
};

static void test_dim_hash_equivalence_handcoded() {
    using namespace crucible::detail;

    // A tensor with no dimensions has nothing to fold, and both paths
    // must agree that the answer is zero rather than an identity that
    // leaked out of the reduction.
    auto m_empty = make_meta({}, {});
    CRUCIBLE_TEST_ASSERT(kTest, dim_hash_scalar(m_empty) == 0);
    CRUCIBLE_TEST_ASSERT(kTest, dim_hash_simd(m_empty) == 0);
    static_assert(std::is_same_v<decltype(dim_hash_scalar_det(m_empty)), crucible::DimHashDet>);
    static_assert(std::is_same_v<decltype(dim_hash_simd_det(m_empty)), crucible::DimHashDet>);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::raw_dim_hash(dim_hash_scalar_det(m_empty)) == 0);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::raw_dim_hash(dim_hash_simd_det(m_empty)) == 0);

    // The two paths agree on one shape, as raw hashes and as typed hashes.
    // The lambda keeps one copy of the inlined hash code in this test.
    const auto expect_agree = [](const crucible::TensorMeta& meta) {
        CRUCIBLE_TEST_ASSERT(kTest, dim_hash_simd(meta) == dim_hash_scalar(meta));
        CRUCIBLE_TEST_ASSERT(kTest, crucible::raw_dim_hash(dim_hash_simd_det(meta))
                                        == crucible::raw_dim_hash(dim_hash_scalar_det(meta)));
    };

    auto m_1d = make_meta({4096}, {1});
    expect_agree(m_1d);

    auto m_2d = make_meta({128, 256}, {256, 1});
    expect_agree(m_2d);

    auto m_nchw = make_meta({32, 64, 224, 224}, {64 * 224 * 224, 224 * 224, 224, 1});
    expect_agree(m_nchw);

    // Eight dimensions fill the vector exactly, so the masked path
    // degenerates to an unmasked one here.
    auto m_8d = make_meta({2, 3, 5, 7, 11, 13, 17, 19}, {1, 2, 3, 4, 5, 6, 7, 8});
    expect_agree(m_8d);

    // A reversed view carries a negative stride, which is where a
    // signed and an unsigned lane type would disagree.
    auto m_neg = make_meta({4, 8}, {-8, 1});
    expect_agree(m_neg);

    // Real callers hand over metadata that is only aligned as the type
    // requires, never to a vector boundary.  The holder below puts the
    // metadata at such an address deliberately, because a load that
    // assumed vector alignment would pass on every case above.
    MisalignedMeta holder{};
    holder.meta = make_meta({16, 32, 64}, {2048, 64, 1});
    CRUCIBLE_TEST_ASSERT(kTest,
                         ::fixy::address_of(::fixy::to(holder.meta)).is_aligned<alignof(crucible::TensorMeta)>());
    CRUCIBLE_TEST_ASSERT(
        kTest, !::fixy::address_of(::fixy::to(holder.meta.sizes.lanes().at(::fixy::index<0>()))).is_aligned<64>());
    expect_agree(holder.meta);

    // Agreement alone would be satisfied by a hash that returned a
    // constant, so two different shapes are required to differ.
    CRUCIBLE_TEST_ASSERT(kTest, dim_hash_scalar(m_1d) != dim_hash_scalar(m_2d));
    CRUCIBLE_TEST_ASSERT(kTest, dim_hash_simd(m_1d) != dim_hash_simd(m_2d));

    ::fixy::check::pass(kTest, "  test_dim_hash_equivalence_handcoded: PASSED\n");
}

int main() {
    test_type_aliases();
    test_iota();
    test_prefix_mask();
    test_det_safe_simd_concept();
    test_load();
    test_load_bytes();
    test_load_lanes();
    test_store_bytes_and_bit_cast();
    test_store_lanes();
    test_collect_bits();
    test_for_each_register_run();
    test_reduce_xor_sum();
    test_reduce_or_and();
    test_reduce_max_min();
    test_select();
    test_masked_reduce_for_dim_hash_pattern();
    test_microarch_detection();
    test_dim_hash_equivalence_handcoded();
    ::fixy::check::pass(kTest, "test_simd: ALL PASSED\n");
    return 0;
}
