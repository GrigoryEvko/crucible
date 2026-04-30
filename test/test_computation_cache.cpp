// ═══════════════════════════════════════════════════════════════════
// test_computation_cache — sentinel TU for cipher/ComputationCache.h
//
// FOUND-F09 — exercises the per-instantiation atomic-slot
// computation cache against the four runtime contracts:
//
//   (1) Lookup-before-insert returns nullptr (miss).
//   (2) Insert + lookup round-trips for the same instantiation.
//   (3) Idempotent insert: first writer wins; second insert no-ops.
//   (4) Distinct (FnPtr, Args...) instantiations have isolated slots.
//   (5) drain_computation_cache is a no-op stub today (Phase 5
//       wiring lands the global registry).
//
// The header itself ships a runtime smoke test
// (computation_cache_smoke_test()) that this TU calls; the
// remaining tests exercise OTHER (FnPtr, Args...) tuples to verify
// instantiation isolation in a separate set of slots.  This way,
// the smoke test's slots and the standalone tests' slots are
// independent — running them in sequence doesn't risk cross-test
// state corruption.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/cipher/ComputationCache.h>

#include <cstdio>
#include <cstdint>
#include <cstdlib>

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

// Variadic so unparenthesized commas inside template arg-lists
// (e.g. `lookup<&fn, int>()`) don't get treated as macro-arg
// separators by the preprocessor.
#define EXPECT_TRUE(...)                                                   \
    do {                                                                   \
        if (!(__VA_ARGS__)) {                                              \
            std::fprintf(stderr,                                           \
                "    EXPECT_TRUE failed: %s (%s:%d)\n",                    \
                #__VA_ARGS__, __FILE__, __LINE__);                         \
            throw TestFailure{};                                           \
        }                                                                  \
    } while (0)

namespace cipher = ::crucible::cipher;

// ── Distinct test functions ───────────────────────────────────────
// These functions are NEVER called.  Their addresses are used as
// NTTPs for the cache template instantiation.  Each function +
// argument-pack tuple maps to a separate atomic slot.

inline void test_fn_a(int)            noexcept {}
inline void test_fn_b(int, double)    noexcept {}
inline int  test_fn_c(int)            noexcept { return 0; }
inline void test_fn_d()               noexcept {}

// ── Stub CompiledBody pointers ────────────────────────────────────
// The cache stores pointers opaquely; the contents are never
// dereferenced by the cache itself, so we use bit-pattern stubs.

cipher::CompiledBody* make_stub(std::uintptr_t v) noexcept {
    return reinterpret_cast<cipher::CompiledBody*>(v);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_computation_cache:\n");

    // ── (A) Header's smoke test ──────────────────────────────────
    // The header ships its own self-contained smoke test that
    // exercises distinct (p_unary, p_binary) instantiations within
    // the namespace `crucible::cipher::detail::computation_cache_self_test`.
    // We delegate to it first to confirm the header is consistent
    // when included from a fresh TU.
    run_test("header_smoke_test", []{
        EXPECT_TRUE(cipher::computation_cache_smoke_test());
    });

    // ── (B) Lookup-before-insert: miss ───────────────────────────
    run_test("lookup_before_insert_returns_nullptr", []{
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_a, int>()
                    == nullptr);
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_b, int, double>()
                    == nullptr);
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_d>()
                    == nullptr);  // empty Args... is valid
    });

    // ── (C) Insert + lookup round-trips ──────────────────────────
    run_test("insert_then_lookup_roundtrips", []{
        auto* body = make_stub(0xCAFEBABE);
        cipher::insert_computation_cache<&test_fn_a, int>(body);
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_a, int>()
                    == body);
    });

    // ── (D) Idempotent insert: first writer wins ─────────────────
    run_test("second_insert_is_idempotent", []{
        // test_fn_a / int already populated by test (C) above with
        // 0xCAFEBABE.  A second insert here MUST NOT overwrite.
        auto* original = cipher::lookup_computation_cache<&test_fn_a, int>();
        EXPECT_TRUE(original != nullptr);

        auto* attempted_overwrite = make_stub(0xDEADBEEF);
        EXPECT_TRUE(original != attempted_overwrite);

        cipher::insert_computation_cache<&test_fn_a, int>(
            attempted_overwrite);

        // The first writer's body persists.
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_a, int>()
                    == original);
    });

    // ── (E) Distinct (FnPtr, Args...) → isolated slots ───────────
    run_test("distinct_instantiations_are_isolated", []{
        // test_fn_a / float still misses (different Args from int slot).
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_a, float>()
                    == nullptr);

        // test_fn_b still misses (different function).
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_b, int, double>()
                    == nullptr);

        // test_fn_c still misses.
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_c, int>()
                    == nullptr);

        // Insert into a different slot.
        auto* body_b = make_stub(0xB0DECAFE);
        cipher::insert_computation_cache<&test_fn_b, int, double>(body_b);
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_b, int, double>()
                    == body_b);

        // test_fn_a / int slot is still its original value (NOT body_b).
        auto* original_a = cipher::lookup_computation_cache<&test_fn_a, int>();
        EXPECT_TRUE(original_a != body_b);
        EXPECT_TRUE(original_a != nullptr);
    });

    // ── (F) Cache key is order-sensitive ─────────────────────────
    run_test("cache_key_order_sensitive", []{
        // Compile-time check folded into runtime sanity.
        constexpr std::uint64_t k_int_double =
            cipher::computation_cache_key<&test_fn_b, int, double>;
        constexpr std::uint64_t k_double_int =
            cipher::computation_cache_key<&test_fn_b, double, int>;
        EXPECT_TRUE(k_int_double != k_double_int);
        EXPECT_TRUE(k_int_double != 0);
        EXPECT_TRUE(k_double_int != 0);
    });

    // ── (G) Cache key is deterministic across re-evaluation ──────
    run_test("cache_key_is_deterministic", []{
        constexpr std::uint64_t k1 =
            cipher::computation_cache_key<&test_fn_a, int>;
        constexpr std::uint64_t k2 =
            cipher::computation_cache_key<&test_fn_a, int>;
        EXPECT_TRUE(k1 == k2);
    });

    // ── (H) Empty Args → key reduces to function ID ──────────────
    run_test("empty_args_reduces_to_function_id", []{
        constexpr std::uint64_t fn_only =
            cipher::computation_cache_key<&test_fn_d>;
        constexpr std::uint64_t fn_id =
            ::crucible::safety::diag::stable_function_id<&test_fn_d>;
        EXPECT_TRUE(fn_only == fn_id);
    });

    // ── (I) drain_computation_cache: no-op (Phase 5 stub) ────────
    run_test("drain_is_phase5_stub_noop", []{
        auto* original_a = cipher::lookup_computation_cache<&test_fn_a, int>();
        EXPECT_TRUE(original_a != nullptr);

        cipher::drain_computation_cache(std::chrono::seconds{0});

        // After drain, the slot is unchanged (no-op stub).
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_a, int>()
                    == original_a);
    });

    std::fprintf(stderr, "\ntotal: %d passed, %d failed\n",
                 total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
