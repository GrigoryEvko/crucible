// The eight-lane generator must produce, in every lane, exactly the
// bits the scalar generator produces for that lane's inputs.  The
// cases below are chosen by hand to sit at the edges where a lane
// could diverge.  Random coverage of the same claim is fuzzed
// separately.

#include <crucible/PhiloxSimd.h>
#include <fixy/Core.h>
#include <foundation/Simd.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <cstddef>
#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;
using namespace crucible::detail;

namespace {

using ::fixy::Philox4x32;
using Counter = Philox4x32::Counter;
using Key = Philox4x32::Key;
using Counters = ::fixy::FixedArray<Counter, 8>;
using Keys = ::fixy::FixedArray<Key, 8>;

// Subscripting a vector returns a value rather than a reference, so a
// lane cannot be assigned through it.  Building lane by lane goes
// through the generator constructor instead, and the lane index it
// passes is a compile-time constant.  The scalar inputs are per lane,
// and the batch wants one vector per word, so the blocks are transposed
// on the way in: lane i of the vector is the word Word of block i.
template <std::size_t Word, class Blocks>
[[nodiscard]] foundation::simd::u32x8 lanes_of_word(Blocks const& blocks) noexcept {
    return foundation::simd::u32x8([&](auto lane) noexcept -> uint32_t {
        return blocks.at(::fixy::index<decltype(lane)::value>()).at(::fixy::index<Word>());
    });
}

// The lane index is taken as a size for parity with the arrays used
// throughout the file, and narrowed at the subscript, which wants a
// signed index.
[[nodiscard]] uint32_t at(foundation::simd::u32x8 v, std::size_t lane) noexcept { return v[static_cast<int>(lane)]; }

[[nodiscard]] PhiloxBatch8 batch_of(Counters const& counters, Keys const& keys) noexcept {
    return philox_batch8(lanes_of_word<0>(counters), lanes_of_word<1>(counters), lanes_of_word<2>(counters),
                         lanes_of_word<3>(counters), lanes_of_word<0>(keys), lanes_of_word<1>(keys));
}

// Runs the scalar generator once per lane and compares the batch
// result against it, lane by lane.
void check_equivalence(Counters counters, Keys keys, ::fixy::TextView what) noexcept {
    auto batch = batch_of(counters, keys);

    for (::fixy::Index<8> const position : counters.indices()) {
        std::size_t const lane = position.raw();
        auto expected = Philox4x32::generate(counters.at(position), keys.at(position));
        uint32_t const want0 = expected.at(::fixy::index<0>());
        uint32_t const want1 = expected.at(::fixy::index<1>());
        uint32_t const want2 = expected.at(::fixy::index<2>());
        uint32_t const want3 = expected.at(::fixy::index<3>());
        uint32_t got0 = at(batch.r0, lane);
        uint32_t got1 = at(batch.r1, lane);
        uint32_t got2 = at(batch.r2, lane);
        uint32_t got3 = at(batch.r3, lane);

        if (got0 != want0 || got1 != want1 || got2 != want2 || got3 != want3) {
            ::fixy::fatal("[{}] lane {} MISMATCH:\n"
                          "  expected: {} {} {} {}\n"
                          "       got: {} {} {} {}",
                          what, lane, ::fixy::Hex<8>{want0}, ::fixy::Hex<8>{want1}, ::fixy::Hex<8>{want2},
                          ::fixy::Hex<8>{want3}, ::fixy::Hex<8>{got0}, ::fixy::Hex<8>{got1}, ::fixy::Hex<8>{got2},
                          ::fixy::Hex<8>{got3});
        }
    }
}

void test_all_zeros() {
    Counters counters{};  // value-initialized to zero
    Keys keys{};
    check_equivalence(counters, keys, "all-zeros");
    ::fixy::check::pass(kTest, "  test_all_zeros: PASSED\n");
}

void test_all_ones() {
    Counters counters{};
    Keys keys{};
    for (::fixy::Index<8> const i : counters.indices()) {
        counters.set(i, Counter{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu});
        keys.set(i, Key{0xFFFFFFFFu, 0xFFFFFFFFu});
    }
    check_equivalence(counters, keys, "all-ones");
    ::fixy::check::pass(kTest, "  test_all_ones: PASSED\n");
}

void test_streaming_counters() {
    // One key broadcast across lanes with the counter advancing per
    // lane is the shape a per-element draw over a row of a tensor
    // takes.
    Counters counters{};
    Keys keys{};
    constexpr Key shared_key{0xCAFEBABEu, 0xDEADBEEFu};
    for (::fixy::Index<8> const i : counters.indices()) {
        Counter const counter{static_cast<uint32_t>(i.raw()), 0, 0, 0};
        counters.set(i, counter);
        keys.set(i, shared_key);
    }
    check_equivalence(counters, keys, "streaming-counters");
    ::fixy::check::pass(kTest, "  test_streaming_counters: PASSED\n");
}

void test_heterogeneous_keys() {
    // A distinct key per lane is the shape a batch of unrelated draws
    // takes.  The keys differ in their high half as well as their
    // low, so a lane that mixed only the low half would show up here.
    Counters counters{};
    Keys keys{};
    for (::fixy::Index<8> const i : counters.indices()) {
        const uint32_t iu = static_cast<uint32_t>(i.raw());
        counters.set(i, Counter{0x12345678u, 0xAABBCCDDu, 0, 0});
        Key const key{0x10000000u + iu * 0x11111111u, 0xFEDCBA98u + iu};
        keys.set(i, key);
    }
    check_equivalence(counters, keys, "heterogeneous-keys");
    ::fixy::check::pass(kTest, "  test_heterogeneous_keys: PASSED\n");
}

void test_boundary_values() {
    // Values at the extremes of the word.  The widening multiply and
    // shift is where a lane would go wrong, and it goes wrong nearest
    // the wraparound.
    Counters counters{
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
    Keys keys{
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
    ::fixy::check::pass(kTest, "  test_boundary_values: PASSED\n");
}

void test_per_lane_independence() {
    // Changing one lane's input must leave every other lane's output
    // untouched.  Without that the batch is not eight independent
    // draws at all.
    //
    // Two batches are run whose inputs differ in the first lane
    // alone.  The other lanes carry distinct non-zero inputs, so a
    // leak between lanes would move them.
    Counters counters_a{};
    Counters counters_b{};
    Keys keys{};

    counters_a.set(::fixy::index<0>(), Counter{0, 0, 0, 0});
    // ← only difference
    counters_b.set(::fixy::index<0>(), Counter{1, 0, 0, 0});
    keys.set(::fixy::index<0>(), Key{0, 0});

    for (::fixy::Index<8> const i : counters_a.indices()) {
        if (i.raw() == 0) continue;
        const uint32_t iu = static_cast<uint32_t>(i.raw());
        Counter const counter{iu * 100u, iu * 200u, iu * 300u, iu * 400u};
        counters_a.set(i, counter);
        Counter const copied = counters_a.at(i);  // ← identical for lanes 1-7
        counters_b.set(i, copied);
        Key const key{iu * 11u, iu * 13u};
        keys.set(i, key);
    }

    auto a = batch_of(counters_a, keys);
    auto b = batch_of(counters_b, keys);

    // Equal outputs from different inputs are possible in principle
    // and vanishingly unlikely here.
    CRUCIBLE_TEST_ASSERT(kTest, at(a.r0, 0) != at(b.r0, 0) && "lane 0 must change when its input changes");

    for (std::size_t lane = 1; lane < 8; ++lane) {
        CRUCIBLE_TEST_ASSERT(kTest, at(a.r0, lane) == at(b.r0, lane) && "non-perturbed lane must not change");
        CRUCIBLE_TEST_ASSERT(kTest, at(a.r1, lane) == at(b.r1, lane));
        CRUCIBLE_TEST_ASSERT(kTest, at(a.r2, lane) == at(b.r2, lane));
        CRUCIBLE_TEST_ASSERT(kTest, at(a.r3, lane) == at(b.r3, lane));
    }

    ::fixy::check::pass(kTest, "  test_per_lane_independence: PASSED\n");
}

void test_known_vector_lane0() {
    // The four words asserted at the end are the generator's
    // published reference vector for an all-zero counter and key.
    // They are fixed outside this project, so they anchor the whole
    // implementation rather than only its internal consistency.
    Counters counters{};
    Keys keys{};

    // The remaining lanes carry noise, which must not reach the first.
    for (::fixy::Index<8> const i : counters.indices()) {
        if (i.raw() == 0) continue;
        const uint32_t iu = static_cast<uint32_t>(i.raw());
        Counter const counter{0xDEADBEEFu * iu, 0xCAFEBABEu * iu, 0xFEEDFACEu * iu, 0xBADC0DEDu * iu};
        counters.set(i, counter);
        Key const key{0xA5A5A5A5u * iu, 0x5A5A5A5Au * iu};
        keys.set(i, key);
    }

    auto batch = batch_of(counters, keys);

    CRUCIBLE_TEST_ASSERT(kTest, at(batch.r0, 0) == 0x6627E8D5u);
    CRUCIBLE_TEST_ASSERT(kTest, at(batch.r1, 0) == 0xE169C58Du);
    CRUCIBLE_TEST_ASSERT(kTest, at(batch.r2, 0) == 0xBC57AC4Cu);
    CRUCIBLE_TEST_ASSERT(kTest, at(batch.r3, 0) == 0x9B00DBD8u);

    ::fixy::check::pass(kTest, "  test_known_vector_lane0: PASSED\n");
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

    ::fixy::check::pass(kTest, "test_philox_simd: ALL PASSED\n");
    return 0;
}
