// ═══════════════════════════════════════════════════════════════════
// test_is_consumer_handle — sentinel TU for safety/IsConsumerHandle.h
//
// Mirror of test_is_producer_handle (FOUND-D05).  Forces the header
// through the project's full warning matrix and cross-checks the
// trait against REAL PermissionedSpscChannel::ConsumerHandle and
// ProducerHandle (the latter must be rejected).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/IsConsumerHandle.h>

#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/permissions/Permission.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>

namespace {

struct TestFailure {};
int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

#define EXPECT_TRUE(cond)                                                  \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr,                                           \
                "    EXPECT_TRUE failed: %s (%s:%d)\n",                    \
                #cond, __FILE__, __LINE__);                                \
            throw TestFailure{};                                           \
        }                                                                  \
    } while (0)

namespace extract = ::crucible::safety::extract;
namespace concur  = ::crucible::concurrent;
namespace safety  = ::crucible::safety;

// ── Synthetic witnesses ─────────────────────────────────────────────

struct synth_consumer {
    [[nodiscard]] std::optional<int> try_pop() noexcept { return {}; }
};

struct synth_producer {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

struct synth_hybrid {
    [[nodiscard]] std::optional<int> try_pop() noexcept       { return {}; }
    [[nodiscard]] bool try_push(int const&) noexcept          { return true; }
};

struct synth_bool_pop {
    [[nodiscard]] bool try_pop() noexcept { return false; }
};

struct synth_overloaded_pop {
    [[nodiscard]] std::optional<int> try_pop() noexcept    { return {}; }
    [[nodiscard]] std::optional<int> try_pop(int) noexcept { return {}; }
};

// ── Real-world cross-check setup ────────────────────────────────────

struct cross_check_tag {};
using SpscChannel = concur::PermissionedSpscChannel<int, 16, cross_check_tag>;

void test_runtime_smoke() {
    EXPECT_TRUE(extract::is_consumer_handle_smoke_test());
}

void test_synthetic_positive() {
    static_assert(extract::is_consumer_handle_v<synth_consumer>);
    static_assert(extract::IsConsumerHandle<synth_consumer>);
}

void test_synthetic_negative() {
    static_assert(!extract::is_consumer_handle_v<int>);
    static_assert(!extract::is_consumer_handle_v<int*>);
    static_assert(!extract::is_consumer_handle_v<void>);
    static_assert(!extract::is_consumer_handle_v<synth_producer>);
    static_assert(!extract::is_consumer_handle_v<synth_hybrid>);
    static_assert(!extract::is_consumer_handle_v<synth_bool_pop>);
    static_assert(!extract::is_consumer_handle_v<synth_overloaded_pop>);
}

void test_cvref_stripping() {
    static_assert(extract::is_consumer_handle_v<synth_consumer&>);
    static_assert(extract::is_consumer_handle_v<synth_consumer&&>);
    static_assert(extract::is_consumer_handle_v<synth_consumer const&>);
    static_assert(extract::is_consumer_handle_v<synth_consumer volatile>);
}

void test_concept_form() {
    static_assert(extract::IsConsumerHandle<synth_consumer>);
    static_assert(extract::IsConsumerHandle<synth_consumer&&>);
    static_assert(!extract::IsConsumerHandle<int>);
    static_assert(!extract::IsConsumerHandle<synth_producer>);
}

void test_value_type_extraction() {
    static_assert(std::is_same_v<
        extract::consumer_handle_value_t<synth_consumer>, int>);
    static_assert(std::is_same_v<
        extract::consumer_handle_value_t<synth_consumer&>, int>);
    static_assert(std::is_same_v<
        extract::consumer_handle_value_t<synth_consumer const&>, int>);
}

void test_pointer_to_handle_rejected() {
    static_assert(!extract::is_consumer_handle_v<synth_consumer*>);
    static_assert(!extract::is_consumer_handle_v<synth_consumer const*>);
}

void test_real_spsc_consumer_handle_matches() {
    using ConH = SpscChannel::ConsumerHandle;
    static_assert(extract::is_consumer_handle_v<ConH>);
    static_assert(extract::IsConsumerHandle<ConH>);
    static_assert(extract::IsConsumerHandle<ConH&&>);
}

void test_real_spsc_producer_handle_rejected() {
    using ProdH = SpscChannel::ProducerHandle;
    static_assert(!extract::is_consumer_handle_v<ProdH>);
    static_assert(!extract::IsConsumerHandle<ProdH>);
}

void test_real_spsc_value_type() {
    using ConH = SpscChannel::ConsumerHandle;
    static_assert(std::is_same_v<
        extract::consumer_handle_value_t<ConH>, int>);
}

void test_runtime_round_trip() {
    SpscChannel ch;
    auto whole = safety::permission_root_mint<typename SpscChannel::whole_tag>();
    auto split = safety::permission_split<
        typename SpscChannel::producer_tag,
        typename SpscChannel::consumer_tag>(std::move(whole));
    auto prod = ch.producer(std::move(split.first));
    auto cons = ch.consumer(std::move(split.second));
    EXPECT_TRUE(prod.try_push(7));
    auto v = cons.try_pop();
    EXPECT_TRUE(v.has_value() && *v == 7);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_consumer_handle:\n");
    run_test("test_runtime_smoke",                     test_runtime_smoke);
    run_test("test_synthetic_positive",                test_synthetic_positive);
    run_test("test_synthetic_negative",                test_synthetic_negative);
    run_test("test_cvref_stripping",                   test_cvref_stripping);
    run_test("test_concept_form",                      test_concept_form);
    run_test("test_value_type_extraction",             test_value_type_extraction);
    run_test("test_pointer_to_handle_rejected",        test_pointer_to_handle_rejected);
    run_test("test_real_spsc_consumer_handle_matches", test_real_spsc_consumer_handle_matches);
    run_test("test_real_spsc_producer_handle_rejected",
                                                       test_real_spsc_producer_handle_rejected);
    run_test("test_real_spsc_value_type",              test_real_spsc_value_type);
    run_test("test_runtime_round_trip",                test_runtime_round_trip);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
