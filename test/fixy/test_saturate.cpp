// The wrapped saturating helpers, exercised under a sequence of calls.
//
// These groups exercise the six helpers that return a fixy wrapper —
// the det and into families — which live in fixy/Saturate.h, and the
// read of a counter with no write-back, which is the checked form of
// fixy/Saturated.h.  The plain saturating forms are the Scalar family, and
// test/foundation/test_core_scalar_arith.cpp holds their tests.
// Each check is an EXPECT, so one run reports every failure it has.  The
// band checks use the free `relax<Tier>(band)` of fixy/Bands.h and the
// free `satisfies_v<Band, Tier>`.

#include <fixy/Bands.h>
#include <fixy/Saturate.h>
#include <fixy/Saturated.h>

#include <cstdint>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using fixy::add_sat_checked;
using fixy::mul_sat_checked;
using fixy::sub_sat_checked;
using fixy::sat::add_sat_det;
using fixy::sat::add_sat_into;
using fixy::sat::mul_sat_det;
using fixy::sat::mul_sat_into;
using fixy::sat::sub_sat_det;
using fixy::sat::sub_sat_into;

namespace {

int g_failures = 0;

#define EXPECT(cond)                                                               \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            ++g_failures;                                                          \
        }                                                                          \
    } while (0)

template <typename T>
constexpr T MIN = std::numeric_limits<T>::min();
template <typename T>
constexpr T MAX = std::numeric_limits<T>::max();

// Every operation must be usable in a constant expression.
static_assert(add_sat_det<uint8_t>(200, 100).peek().value() == MAX<uint8_t>);
static_assert(add_sat_det<uint8_t>(200, 100).peek().was_clamped());
static_assert(!sub_sat_det<uint32_t>(5, 3).peek().was_clamped());
static_assert(mul_sat_det<int32_t>(MIN<int32_t>, -1).peek().value() == MAX<int32_t>);
static_assert(fixy::satisfies_v<decltype(mul_sat_det<int32_t>(MIN<int32_t>, -1)), fixy::DetSafeTier_v::Pure>);
constexpr uint32_t counter = 40;
static_assert(add_sat_checked(counter, 2u).value() == 42u);
static_assert(!add_sat_checked(counter, 2u).was_clamped());
static_assert(sub_sat_checked(counter, 50u).value() == 0u);
static_assert(sub_sat_checked(counter, 50u).was_clamped());
static_assert(mul_sat_checked(counter, 2u).value() == 80u);

void test_det_wrappers() {
    auto add = add_sat_det<uint32_t>(MAX<uint32_t>, 1u);
    EXPECT(add.peek().value() == MAX<uint32_t>);
    EXPECT(add.peek().was_clamped());

    auto sub = sub_sat_det<uint32_t>(10u, 3u);
    EXPECT(sub.peek().value() == 7u);
    EXPECT(!sub.peek().was_clamped());

    auto mul = mul_sat_det<int32_t>(MIN<int32_t>, -1);
    EXPECT(mul.peek().value() == MAX<int32_t>);
    EXPECT(mul.peek().was_clamped());

    auto relaxed = fixy::relax<fixy::DetSafeTier_v::PhiloxRng>(mul);
    EXPECT(relaxed.peek().value() == MAX<int32_t>);
}

void test_memory_counter_wrappers() {
    auto add_counter = fixy::Saturated<uint32_t>::exact(MAX<uint32_t> - 1u);
    add_sat_into(add_counter, 10u);
    EXPECT(add_counter.value() == MAX<uint32_t>);
    EXPECT(add_counter.was_clamped());

    auto sub_counter = fixy::Saturated<uint32_t>::exact(3u);
    sub_sat_into(sub_counter, 10u);
    EXPECT(sub_counter.value() == 0u);
    EXPECT(sub_counter.was_clamped());

    auto mul_counter = fixy::Saturated<uint16_t>::exact(1000u);
    mul_sat_into<uint16_t>(mul_counter, uint16_t{70});
    EXPECT(mul_counter.value() == MAX<uint16_t>);
    EXPECT(mul_counter.was_clamped());

    auto clean_counter = fixy::Saturated<uint32_t>::exact(40u);
    add_sat_into(clean_counter, 2u);
    EXPECT(clean_counter.value() == 42u);
    EXPECT(!clean_counter.was_clamped());

    uint32_t read_only = 9u;
    auto projected = mul_sat_checked(read_only, 9u);
    EXPECT(read_only == 9u);
    EXPECT(projected.value() == 81u);
    EXPECT(!projected.was_clamped());
}

// The flag of a counter stays set after a clamp.  A step that fits, after
// a step that clamped, gives a value that a clamp made, so the counter
// still reads as clamped.  The old form wrote only the value back into a
// plain counter, and the next step then gave a false flag.
void test_counter_keeps_its_clamp() {
    auto stepped = fixy::Saturated<uint32_t>::exact(MAX<uint32_t> - 50u);
    add_sat_into(stepped, 100u);
    EXPECT(stepped.value() == MAX<uint32_t>);
    EXPECT(stepped.was_clamped());
    sub_sat_into(stepped, 5u);
    EXPECT(stepped.value() == MAX<uint32_t> - 5u);
    EXPECT(stepped.was_clamped());
    mul_sat_into(stepped, 1u);
    EXPECT(stepped.was_clamped());
    stepped = fixy::Saturated<uint32_t>::exact(7u);
    EXPECT(!stepped.was_clamped());
}

}  // namespace

int main() {
    test_det_wrappers();
    test_memory_counter_wrappers();
    test_counter_keeps_its_clamp();
    if (g_failures != 0) {
        std::fprintf(stderr, "test_saturate: %d failure(s)\n", g_failures);
        return 1;
    }
    ::fixy::check::pass(kTest, "test_saturate: each wrapped group passed\n");
    return 0;
}
