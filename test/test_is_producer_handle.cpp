// ═══════════════════════════════════════════════════════════════════
// test_is_producer_handle — sentinel TU for safety/IsProducerHandle.h
//
// Same blind-spot rationale as test_is_owned_region (see
// feedback_header_only_static_assert_blind_spot memory): a header
// shipped with embedded static_asserts is unverified under the
// project warning flags unless a .cpp TU includes it.
//
// Coverage:
//   * Foundation header inclusion under full warning flags.
//   * runtime_smoke_test() execution.
//   * Synthetic positive (try_push only, value_type) and negative
//     witnesses (consumer-shaped, hybrid, missing typedef, void return).
//   * Cv-ref stripping parity.
//   * Concept form parity.
//   * Cross-check against REAL PermissionedSpscChannel::ProducerHandle
//     and ConsumerHandle — the real channel-shipped types must match
//     the structural-shape claim, OR the trait is wrong.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/IsProducerHandle.h>

#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/permissions/Permission.h>

#include <cstdio>
#include <cstdlib>
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

// ── Synthetic witnesses (mirroring the in-header self-test) ─────────

struct synth_producer {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

struct synth_consumer {
    [[nodiscard]] int try_pop() noexcept { return 0; }
};

struct synth_hybrid {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
    [[nodiscard]] int try_pop() noexcept { return 0; }
};

struct synth_void_push {
    void try_push(int const&) noexcept {}
};

struct synth_overloaded_push {
    [[nodiscard]] bool try_push(int const&)   noexcept { return true; }
    [[nodiscard]] bool try_push(float const&) noexcept { return true; }
};

// ── Real-world cross-check setup ────────────────────────────────────

struct cross_check_tag {};
using SpscChannel = concur::PermissionedSpscChannel<int, 16, cross_check_tag>;

void test_runtime_smoke() {
    EXPECT_TRUE(extract::is_producer_handle_smoke_test());
}

void test_synthetic_positive() {
    static_assert(extract::is_producer_handle_v<synth_producer>);
    static_assert(extract::IsProducerHandle<synth_producer>);
}

void test_synthetic_negative() {
    static_assert(!extract::is_producer_handle_v<int>);
    static_assert(!extract::is_producer_handle_v<int*>);
    static_assert(!extract::is_producer_handle_v<void>);
    static_assert(!extract::is_producer_handle_v<synth_consumer>);
    static_assert(!extract::is_producer_handle_v<synth_hybrid>);
    static_assert(!extract::is_producer_handle_v<synth_void_push>);
    static_assert(!extract::is_producer_handle_v<synth_overloaded_push>);
}

void test_cvref_stripping() {
    static_assert(extract::is_producer_handle_v<synth_producer>);
    static_assert(extract::is_producer_handle_v<synth_producer&>);
    static_assert(extract::is_producer_handle_v<synth_producer&&>);
    static_assert(extract::is_producer_handle_v<synth_producer const&>);
    static_assert(extract::is_producer_handle_v<synth_producer volatile>);
    static_assert(extract::is_producer_handle_v<synth_producer const volatile>);
}

void test_concept_form() {
    static_assert(extract::IsProducerHandle<synth_producer>);
    static_assert(extract::IsProducerHandle<synth_producer&&>);
    static_assert(!extract::IsProducerHandle<int>);
    static_assert(!extract::IsProducerHandle<synth_consumer>);
}

void test_value_type_extraction() {
    static_assert(std::is_same_v<
        extract::producer_handle_value_t<synth_producer>, int>);
    static_assert(std::is_same_v<
        extract::producer_handle_value_t<synth_producer&>, int>);
    static_assert(std::is_same_v<
        extract::producer_handle_value_t<synth_producer const&>, int>);
}

void test_pointer_to_handle_rejected() {
    // Pointers to handles are NOT handles — refuses pointer-decay.
    static_assert(!extract::is_producer_handle_v<synth_producer*>);
    static_assert(!extract::is_producer_handle_v<synth_producer* const>);
    static_assert(!extract::is_producer_handle_v<synth_producer const*>);
}

void test_real_spsc_producer_handle_matches() {
    // The shipping PermissionedSpscChannel<...>::ProducerHandle must
    // satisfy the structural shape — if not, the trait does not
    // recognize the canonical Permissioned* producer endpoint.
    using ProdH = SpscChannel::ProducerHandle;
    static_assert(extract::is_producer_handle_v<ProdH>);
    static_assert(extract::IsProducerHandle<ProdH>);
    static_assert(extract::IsProducerHandle<ProdH&&>);
}

void test_real_spsc_consumer_handle_rejected() {
    // Symmetric: the ConsumerHandle must NOT match the producer
    // recogniser — that would let the dispatcher silently route a
    // consumer to the producer-endpoint lowering.
    using ConH = SpscChannel::ConsumerHandle;
    static_assert(!extract::is_producer_handle_v<ConH>);
    static_assert(!extract::IsProducerHandle<ConH>);
}

void test_real_spsc_value_type() {
    using ProdH = SpscChannel::ProducerHandle;
    static_assert(std::is_same_v<
        extract::producer_handle_value_t<ProdH>, int>);
}

void test_runtime_round_trip() {
    // Construct the real channel + producer handle and exercise
    // try_push.  Confirms the runtime ABI matches the trait's claim
    // (the bool return on try_push is reachable, the value_type is
    // assignable).
    SpscChannel ch;
    auto whole = safety::mint_permission_root<typename SpscChannel::whole_tag>();
    auto split = safety::mint_permission_split<
        typename SpscChannel::producer_tag,
        typename SpscChannel::consumer_tag>(std::move(whole));
    auto prod = ch.producer(std::move(split.first));
    bool const r = prod.try_push(42);
    EXPECT_TRUE(r);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_producer_handle:\n");
    run_test("test_runtime_smoke",                    test_runtime_smoke);
    run_test("test_synthetic_positive",               test_synthetic_positive);
    run_test("test_synthetic_negative",               test_synthetic_negative);
    run_test("test_cvref_stripping",                  test_cvref_stripping);
    run_test("test_concept_form",                     test_concept_form);
    run_test("test_value_type_extraction",            test_value_type_extraction);
    run_test("test_pointer_to_handle_rejected",       test_pointer_to_handle_rejected);
    run_test("test_real_spsc_producer_handle_matches",
                                                      test_real_spsc_producer_handle_matches);
    run_test("test_real_spsc_consumer_handle_rejected",
                                                      test_real_spsc_consumer_handle_rejected);
    run_test("test_real_spsc_value_type",             test_real_spsc_value_type);
    run_test("test_runtime_round_trip",               test_runtime_round_trip);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
