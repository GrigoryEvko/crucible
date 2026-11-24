// Two units of one program, built for two targets, keep their own body of
// each function of the base whose source reads the vector width of the
// target, of the three conversions of f16 whose source reads F16C, and of the
// out-of-line exact form of fp::fma, and their own object of each constant
// that the target sets.  The
// unit target_unit_baseline.cpp compiles for x86-64 and the unit
// target_unit_wide.cpp for x86-64-v4.  Each gives the addresses of its compile
// as constant data, so the checks of the addresses run no instruction of the
// wide unit.  If the two units shared one body, the link could keep the body
// of AVX-512 for the unit of the baseline target.  Only a processor of
// x86-64-v4 runs the bodies of the wide unit, and each body gives the bits of
// the body of the unit of the test (the lanes of foundation/core/Reduce.h,
// The dispatch of foundation/core/StorageFloat.h).
//
// The foundation library compiles for the level of the build, x86-64-v3 in
// the build of the preset default, so each of the two units has another level
// than the library.  Each unit links a call of keyed_hash, whose run-time arm
// is in the library, and the call gives the bits of the scalar arm of a
// constant evaluation.

#include "target_units.h"

#include <foundation/os/X86Level.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/core/KeyedHash.h>
#include <foundation/core/StorageFloat.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using ::crucible::test::baseline_unit_bodies;
using ::crucible::test::TargetUnitBodies;
using ::crucible::test::wide_unit_bodies;
using ::foundation::core::FixedArray;
using ::foundation::core::View;

// Tells if the host runs the bodies of the unit of x86-64-v4.
enum class WideUnit : std::uint8_t {
    skipped,
    runs
};

// The lengths of the messages whose keyed hash a constant evaluation gives:
// each side of one block and of one chunk, and two and three chunks.
constexpr FixedArray<std::size_t, 10> kConstantLengths{0, 1, 63, 64, 65, 1023, 1024, 1025, 2048, 2049};
constexpr std::size_t kConstantLongest = 2049;

// The lengths of the longer messages: 4, 8, 16, 31 and 64 chunks, one byte
// past 64 chunks, and 100 chunks.  They run the lanes of the arm.
constexpr FixedArray<std::size_t, 7> kLongLengths{4096, 8192, 16384, 31744, 65536, 65537, 102400};
constexpr std::size_t kLongest = 102400;

// The bytes of the messages: the byte i % 251 at i.
template <std::size_t Length>
struct ProbeInput final {
    FixedArray<std::byte, Length> bytes{};

    constexpr ProbeInput() noexcept {
        for (auto const index : decltype(bytes)::indices()) {
            std::byte const value = static_cast<std::byte>(index.raw() % 251);
            bytes.set(index, value);
        }
    }
};

// The keyed hash of each constant length, by the scalar arm of a constant
// evaluation.
[[nodiscard]] consteval FixedArray<FixedArray<std::byte, 32>, 10> constant_keyed_hashes() noexcept {
    ProbeInput<kConstantLongest> const input{};
    FixedArray<FixedArray<std::byte, 32>, 10> hashes{};
    for (auto const index : decltype(hashes)::indices()) {
        View<std::byte const> const message =
            input.bytes.view().window(0, kConstantLengths.at(index)).expect("the length is in the input");
        FixedArray<std::byte, 32> const hash =
            ::foundation::core::keyed_hash(::crucible::test::target_probe_key(), message).bytes();
        hashes.set(index, hash);
    }
    return hashes;
}

// The three bodies whose source reads the vector width, and the out-of-line
// exact form of fp::fma, are different functions in the two units.
void test_each_unit_keeps_its_bodies() {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the address of a body or an object tells which unit of the link holds it")
    bool const is_sum_by_tree_shared = baseline_unit_bodies.sum_by_tree_body == wide_unit_bodies.sum_by_tree_body;
    bool const is_sum_scan_shared = baseline_unit_bodies.sum_scan_body == wide_unit_bodies.sum_scan_body;
    bool const is_fixed_copy_shared = baseline_unit_bodies.fixed_copy_body == wide_unit_bodies.fixed_copy_body;
    bool const is_fma_exact_shared = baseline_unit_bodies.fma_exact_body == wide_unit_bodies.fma_exact_body;
    bool const is_widen_f16_shared = baseline_unit_bodies.widen_f16_body == wide_unit_bodies.widen_f16_body;
    bool const is_round_f16_shared = baseline_unit_bodies.round_f16_body == wide_unit_bodies.round_f16_body;
    bool const is_round_f16_saturating_shared =
        baseline_unit_bodies.round_f16_saturating_body == wide_unit_bodies.round_f16_saturating_body;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    CRUCIBLE_TEST_ASSERT(kTest, !is_sum_by_tree_shared);
    CRUCIBLE_TEST_ASSERT(kTest, !is_sum_scan_shared);
    CRUCIBLE_TEST_ASSERT(kTest, !is_fixed_copy_shared);
    CRUCIBLE_TEST_ASSERT(kTest, !is_fma_exact_shared);
    CRUCIBLE_TEST_ASSERT(kTest, !is_widen_f16_shared);
    CRUCIBLE_TEST_ASSERT(kTest, !is_round_f16_shared);
    CRUCIBLE_TEST_ASSERT(kTest, !is_round_f16_saturating_shared);
}

// Each unit reads the vector width of its own target: 16 bytes for x86-64,
// and 64 bytes for x86-64-v4.
void test_each_unit_keeps_its_constants() {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the address of a body or an object tells which unit of the link holds it")
    std::size_t const baseline_vector = *baseline_unit_bodies.vector_bytes;
    std::size_t const wide_vector = *wide_unit_bodies.vector_bytes;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    CRUCIBLE_TEST_ASSERT(kTest, baseline_vector == 16);
    CRUCIBLE_TEST_ASSERT(kTest, wide_vector == 64);
}

// The wide unit runs only on a processor of x86-64-v4.
[[nodiscard]] WideUnit wide_unit_of_host() noexcept {
    bool const is_wide_host =
        ::foundation::os::x86_level_of(::foundation::os::read_x86_feature_words()) == ::foundation::os::X86Level::v4;
    return is_wide_host ? WideUnit::runs : WideUnit::skipped;
}

// The sum of each unit has the same bits.
void test_the_sums_agree(WideUnit wide) {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the address of a body or an object tells which unit of the link holds it")
    std::uint32_t const baseline_bits = baseline_unit_bodies.sum_bits_body();
    std::uint32_t const wide_bits = wide == WideUnit::runs ? wide_unit_bodies.sum_bits_body() : baseline_bits;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    CRUCIBLE_TEST_ASSERT(kTest, wide_bits == baseline_bits);
}

// The count of the conversions of f16 by the bodies of the unit that give
// other bits than the calls of the unit of the test: the widen of each of the
// 2^16 words, and the two rounds of the binary32 value of each word, of the
// tie above it for a normal word, and of the word on each side of that tie.
// Complexity: linear in the 2^16 words.
[[nodiscard]] std::uint64_t f16_mismatches_of(TargetUnitBodies const& unit) noexcept {
    namespace fc = ::foundation::core;
    constexpr FixedArray<std::uint32_t, 4> offsets{0x0U, 0xFFFU, 0x1000U, 0x1001U};
    std::uint64_t mismatches = 0;
    for (std::uint32_t word = 0; word <= 0xFFFFU; ++word) {
        fc::f16 const value = fc::f16::from_bits(static_cast<std::uint16_t>(word));
        std::uint32_t const bits = fc::bit_cast<std::uint32_t>(fc::widen<float>(value));
        if (fc::bit_cast<std::uint32_t>(unit.widen_f16_body(value)) != bits) ++mismatches;
        for (std::uint32_t const offset : offsets) {
            float const input = fc::bit_cast<float>(bits + offset);
            if (unit.round_f16_body(input).raw() != fc::round_nonsaturating<fc::f16>(input).raw()) ++mismatches;
            if (unit.round_f16_saturating_body(input).raw() != fc::round_saturating<fc::f16>(input).raw()) {
                ++mismatches;
            }
        }
    }
    return mismatches;
}

// The conversions of f16 of each unit give the bits of the unit of the test.
void test_the_f16_conversions_agree(WideUnit wide) {
    std::uint64_t const baseline_mismatches = f16_mismatches_of(baseline_unit_bodies);
    std::uint64_t const wide_mismatches = wide == WideUnit::runs ? f16_mismatches_of(wide_unit_bodies) : 0;
    CRUCIBLE_TEST_ASSERT(kTest, baseline_mismatches == 0);
    CRUCIBLE_TEST_ASSERT(kTest, wide_mismatches == 0);
}

// The count of the units whose keyed hash of the message differs from the
// hash of the call of the unit of the test.
[[nodiscard]] std::uint64_t keyed_hash_mismatches_of(View<std::byte const> message,
                                                     ::foundation::core::Hash256 const& expected,
                                                     WideUnit wide) noexcept {
    std::uint64_t mismatches = 0;
    if (baseline_unit_bodies.keyed_hash_body(message) != expected) ++mismatches;
    if (wide == WideUnit::runs && wide_unit_bodies.keyed_hash_body(message) != expected) ++mismatches;
    return mismatches;
}

// The keyed hash of each unit gives the bits of the call of the unit of the
// test, and at each constant length also the bits of the scalar arm of a
// constant evaluation.  Complexity: linear in the bytes of the messages.
void test_the_keyed_hashes_agree(WideUnit wide) {
    static ProbeInput<kLongest> const input{};
    constexpr FixedArray<FixedArray<std::byte, 32>, 10> constant_hashes = constant_keyed_hashes();
    std::uint64_t constant_mismatches = 0;
    std::uint64_t unit_mismatches = 0;
    for (auto const index : decltype(kConstantLengths)::indices()) {
        View<std::byte const> const message =
            input.bytes.view().window(0, kConstantLengths.at(index)).expect("the length is in the input");
        ::foundation::core::Hash256 const native =
            ::foundation::core::keyed_hash(::crucible::test::target_probe_key(), message);
        if (!::fixy::equal(native.bytes(), constant_hashes.at(index))) ++constant_mismatches;
        unit_mismatches += keyed_hash_mismatches_of(message, native, wide);
    }
    for (std::size_t const length : kLongLengths) {
        View<std::byte const> const message = input.bytes.view().window(0, length).expect("the length is in the input");
        ::foundation::core::Hash256 const native =
            ::foundation::core::keyed_hash(::crucible::test::target_probe_key(), message);
        unit_mismatches += keyed_hash_mismatches_of(message, native, wide);
    }
    CRUCIBLE_TEST_ASSERT(kTest, constant_mismatches == 0);
    CRUCIBLE_TEST_ASSERT(kTest, unit_mismatches == 0);
}

}  // namespace

int main() {
    test_each_unit_keeps_its_bodies();
    test_each_unit_keeps_its_constants();
    WideUnit const wide = wide_unit_of_host();
    test_the_sums_agree(wide);
    test_the_f16_conversions_agree(wide);
    test_the_keyed_hashes_agree(wide);
    if (wide == WideUnit::runs) {
        ::fixy::check::pass(kTest, "test_target_units: two units of two targets keep their own bodies, link the "
                                   "keyed hash, and give the bits of the unit of the test\n");
    } else {
        ::fixy::check::pass(kTest,
                            "test_target_units: two units of two targets keep their own bodies and link the keyed "
                            "hash.  The host has no x86-64-v4, so the bodies of the wide unit did not run\n");
    }
    return 0;
}
