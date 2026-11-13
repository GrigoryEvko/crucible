// The eight-lane generator must produce, in every lane, exactly the
// bits the scalar generator produces for that lane's inputs.  The
// cases below are chosen by hand to sit at the edges where a lane
// could diverge.  Random coverage of the same claim is fuzzed
// separately.

#include <crucible/PhiloxSimd.h>
#include <fixy/Core.h>
#include <foundation/Simd.h>

#include <array>
#include "test_assert.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>

using namespace crucible;
using namespace crucible::detail;

namespace {

using ::fixy::Philox4x32;
using Counter = Philox4x32::Counter;
using Key = Philox4x32::Key;

// Subscripting a vector returns a value rather than a reference, so a
// lane cannot be assigned through it.  Building lane by lane goes
// through the generator constructor instead, and the lane index it
// passes is a compile-time constant.  The scalar inputs are per lane,
// and the batch wants one vector per word, so the blocks are transposed
// on the way in: lane i of the vector is the word Word of block i.
template <std::size_t Word, class Blocks>
[[nodiscard]] foundation::simd::u32x8 lanes_of_word(Blocks const& blocks) noexcept {
    return foundation::simd::u32x8(
        [&](auto lane) noexcept -> uint32_t { return blocks[decltype(lane)::value].at(::fixy::index<Word>()); });
}

// The lane index is taken as a size for parity with the arrays used
// throughout the file, and narrowed at the subscript, which wants a
// signed index.
[[nodiscard]] uint32_t at(foundation::simd::u32x8 v, std::size_t lane) noexcept { return v[static_cast<int>(lane)]; }

[[nodiscard]] PhiloxBatch8 batch_of(std::array<Counter, 8> const& counters, std::array<Key, 8> const& keys) noexcept {
    return philox_batch8(lanes_of_word<0>(counters), lanes_of_word<1>(counters), lanes_of_word<2>(counters),
                         lanes_of_word<3>(counters), lanes_of_word<0>(keys), lanes_of_word<1>(keys));
}

// Runs the scalar generator once per lane and compares the batch
// result against it, lane by lane.
void check_equivalence(std::array<Counter, 8> counters, std::array<Key, 8> keys, const char* what) noexcept {
    auto batch = batch_of(counters, keys);

    for (std::size_t lane = 0; lane < 8; ++lane) {
        auto expected = Philox4x32::generate(counters[lane], keys[lane]);
        uint32_t const want0 = expected.at(::fixy::index<0>());
        uint32_t const want1 = expected.at(::fixy::index<1>());
        uint32_t const want2 = expected.at(::fixy::index<2>());
        uint32_t const want3 = expected.at(::fixy::index<3>());
        uint32_t got0 = at(batch.r0, lane);
        uint32_t got1 = at(batch.r1, lane);
        uint32_t got2 = at(batch.r2, lane);
        uint32_t got3 = at(batch.r3, lane);

        if (got0 != want0 || got1 != want1 || got2 != want2 || got3 != want3) {
            std::fprintf(stderr,
                         "[%s] lane %zu MISMATCH:\n"
                         "  expected: %08x %08x %08x %08x\n"
                         "       got: %08x %08x %08x %08x\n",
                         what, lane, want0, want1, want2, want3, got0, got1, got2, got3);
            std::abort();
        }
    }
}

void test_all_zeros() {
    std::array<Counter, 8> counters{};  // value-initialized to zero
    std::array<Key, 8> keys{};
    check_equivalence(counters, keys, "all-zeros");
    crucible::test::pass("  test_all_zeros: PASSED\n");
}

void test_all_ones() {
    std::array<Counter, 8> counters;
    std::array<Key, 8> keys;
    for (std::size_t i = 0; i < 8; ++i) {
        counters[i] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        keys[i] = {0xFFFFFFFFu, 0xFFFFFFFFu};
    }
    check_equivalence(counters, keys, "all-ones");
    crucible::test::pass("  test_all_ones: PASSED\n");
}

void test_streaming_counters() {
    // One key broadcast across lanes with the counter advancing per
    // lane is the shape a per-element draw over a row of a tensor
    // takes.
    std::array<Counter, 8> counters;
    std::array<Key, 8> keys;
    constexpr Key shared_key = {0xCAFEBABEu, 0xDEADBEEFu};
    for (std::size_t i = 0; i < 8; ++i) {
        counters[i] = {static_cast<uint32_t>(i), 0, 0, 0};
        keys[i] = shared_key;
    }
    check_equivalence(counters, keys, "streaming-counters");
    crucible::test::pass("  test_streaming_counters: PASSED\n");
}

void test_heterogeneous_keys() {
    // A distinct key per lane is the shape a batch of unrelated draws
    // takes.  The keys differ in their high half as well as their
    // low, so a lane that mixed only the low half would show up here.
    std::array<Counter, 8> counters;
    std::array<Key, 8> keys;
    for (std::size_t i = 0; i < 8; ++i) {
        const uint32_t iu = static_cast<uint32_t>(i);
        counters[i] = {0x12345678u, 0xAABBCCDDu, 0, 0};
        keys[i] = {0x10000000u + iu * 0x11111111u, 0xFEDCBA98u + iu};
    }
    check_equivalence(counters, keys, "heterogeneous-keys");
    crucible::test::pass("  test_heterogeneous_keys: PASSED\n");
}

void test_boundary_values() {
    // Values at the extremes of the word.  The widening multiply and
    // shift is where a lane would go wrong, and it goes wrong nearest
    // the wraparound.
    std::array<Counter, 8> counters{
        Counter{0u, 0u, 0u, 0u},
        Counter{0xFFFFFFFFu, 0u, 0u, 0u},
        Counter{0x80000000u, 0x80000000u, 0u, 0u},
        Counter{0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu},
        Counter{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0u},
        Counter{1u, 0xFFFFFFFEu, 1u, 0xFFFFFFFEu},
        Counter{Philox4x32::multiplier0, Philox4x32::multiplier1, Philox4x32::weyl_step0,
                Philox4x32::weyl_step1},  // the round constants as input
        Counter{0xDEADBEEFu, 0xCAFEBABEu, 0xFEEDFACEu, 0xBADC0DEDu},
    };
    std::array<Key, 8> keys{
        Key{0u, 0u},
        Key{0xFFFFFFFFu, 0u},
        Key{0u, 0xFFFFFFFFu},
        Key{0xFFFFFFFFu, 0xFFFFFFFFu},
        Key{0x80000000u, 0x80000000u},
        Key{0x7FFFFFFFu, 0x7FFFFFFFu},
        Key{Philox4x32::multiplier0, Philox4x32::multiplier1},
        Key{0xA5A5A5A5u, 0x5A5A5A5Au},
    };
    check_equivalence(counters, keys, "boundary-values");
    crucible::test::pass("  test_boundary_values: PASSED\n");
}

void test_per_lane_independence() {
    // Changing one lane's input must leave every other lane's output
    // untouched.  Without that the batch is not eight independent
    // draws at all.
    //
    // Two batches are run whose inputs differ in the first lane
    // alone.  The other lanes carry distinct non-zero inputs, so a
    // leak between lanes would move them.
    std::array<Counter, 8> counters_a;
    std::array<Counter, 8> counters_b;
    std::array<Key, 8> keys{};

    counters_a[0] = {0, 0, 0, 0};
    counters_b[0] = {1, 0, 0, 0};  // ← only difference
    keys[0] = {0, 0};

    for (std::size_t i = 1; i < 8; ++i) {
        const uint32_t iu = static_cast<uint32_t>(i);
        counters_a[i] = {iu * 100u, iu * 200u, iu * 300u, iu * 400u};
        counters_b[i] = counters_a[i];  // ← identical for lanes 1-7
        keys[i] = {iu * 11u, iu * 13u};
    }

    auto a = batch_of(counters_a, keys);
    auto b = batch_of(counters_b, keys);

    // Equal outputs from different inputs are possible in principle
    // and vanishingly unlikely here.
    assert(at(a.r0, 0) != at(b.r0, 0) && "lane 0 must change when its input changes");

    for (std::size_t lane = 1; lane < 8; ++lane) {
        assert(at(a.r0, lane) == at(b.r0, lane) && "non-perturbed lane must not change");
        assert(at(a.r1, lane) == at(b.r1, lane));
        assert(at(a.r2, lane) == at(b.r2, lane));
        assert(at(a.r3, lane) == at(b.r3, lane));
    }

    crucible::test::pass("  test_per_lane_independence: PASSED\n");
}

void test_known_vector_lane0() {
    // The four words asserted at the end are the generator's
    // published reference vector for an all-zero counter and key.
    // They are fixed outside this project, so they anchor the whole
    // implementation rather than only its internal consistency.
    std::array<Counter, 8> counters{};
    std::array<Key, 8> keys{};

    // The remaining lanes carry noise, which must not reach the first.
    for (std::size_t i = 1; i < 8; ++i) {
        const uint32_t iu = static_cast<uint32_t>(i);
        counters[i] = {0xDEADBEEFu * iu, 0xCAFEBABEu * iu, 0xFEEDFACEu * iu, 0xBADC0DEDu * iu};
        keys[i] = {0xA5A5A5A5u * iu, 0x5A5A5A5Au * iu};
    }

    auto batch = batch_of(counters, keys);

    assert(at(batch.r0, 0) == 0x6627E8D5u);
    assert(at(batch.r1, 0) == 0xE169C58Du);
    assert(at(batch.r2, 0) == 0xBC57AC4Cu);
    assert(at(batch.r3, 0) == 0x9B00DBD8u);

    crucible::test::pass("  test_known_vector_lane0: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_philox_simd:\n");

    test_all_zeros();
    test_all_ones();
    test_streaming_counters();
    test_heterogeneous_keys();
    test_boundary_values();
    test_per_lane_independence();
    test_known_vector_lane0();

    crucible::test::pass("test_philox_simd: ALL PASSED\n");
    return 0;
}
