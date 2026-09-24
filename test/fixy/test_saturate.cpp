// The wrapped saturating helpers, exercised under a sequence of calls.
//
// These are the groups of test/test_saturate.cpp that exercise the nine
// helpers returning a fixy wrapper — the det, from and into families —
// which live in fixy/Saturate.h.  The six groups over the three plain
// helpers are in test/foundation/test_saturate.cpp, beside the header
// those live in.  The bodies are the old test's with its assert spelled
// EXPECT, so one run reports every failure it has.  Two spellings
// changed with the band: the old member `relax<Tier>()` is the free
// `relax<Tier>(band)` of fixy/Bands.h, and the old member
// `satisfies<Tier>` is the free `satisfies_v<Band, Tier>`.

#include <fixy/Bands.h>
#include <fixy/Saturate.h>
#include <fixy/Saturated.h>

#include <cstdint>
#include <cstdio>
#include <limits>
#include <type_traits>

using fixy::sat::add_sat_det;
using fixy::sat::add_sat_from;
using fixy::sat::add_sat_into;
using fixy::sat::mul_sat_det;
using fixy::sat::mul_sat_from;
using fixy::sat::mul_sat_into;
using fixy::sat::sub_sat_det;
using fixy::sat::sub_sat_from;
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
static_assert(add_sat_from(counter, 2u).value() == 42u);
static_assert(!add_sat_from(counter, 2u).was_clamped());
static_assert(sub_sat_from(counter, 50u).value() == 0u);
static_assert(sub_sat_from(counter, 50u).was_clamped());
static_assert(mul_sat_from(counter, 2u).value() == 80u);

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
    uint32_t add_counter = MAX<uint32_t> - 1u;
    auto add = add_sat_into(add_counter, 10u);
    EXPECT(add_counter == MAX<uint32_t>);
    EXPECT(add.value() == MAX<uint32_t>);
    EXPECT(add.was_clamped());

    uint32_t sub_counter = 3u;
    auto sub = sub_sat_into(sub_counter, 10u);
    EXPECT(sub_counter == 0u);
    EXPECT(sub.value() == 0u);
    EXPECT(sub.was_clamped());

    uint16_t mul_counter = 1000u;
    auto mul = mul_sat_into<uint16_t>(mul_counter, uint16_t{70});
    EXPECT(mul_counter == MAX<uint16_t>);
    EXPECT(mul.value() == MAX<uint16_t>);
    EXPECT(mul.was_clamped());

    uint32_t read_only = 9u;
    auto projected = mul_sat_from(read_only, 9u);
    EXPECT(read_only == 9u);
    EXPECT(projected.value() == 81u);
    EXPECT(!projected.was_clamped());
}

}  // namespace

int main() {
    test_det_wrappers();
    test_memory_counter_wrappers();
    if (g_failures != 0) {
        std::fprintf(stderr, "test_saturate: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_saturate: both wrapped groups passed\n");
    return 0;
}
