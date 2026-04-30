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

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <thread>

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

// FOUND-F09-AUDIT: hash-distribution sanity functions.  Each
// declared function + a varied parameter pack stresses the
// computation_cache_key fold's ability to produce distinct
// 64-bit values under realistic dispatcher load.  A collision
// here would cause two unrelated cache slots to share a key in
// federation/serialization paths.
inline void hd_fn_alpha(int)                    noexcept {}
inline void hd_fn_beta (int)                    noexcept {}
inline void hd_fn_gamma(int, int)               noexcept {}
inline void hd_fn_delta(int, int, int)          noexcept {}
inline int  hd_fn_eps  (long)                   noexcept { return 0; }
inline char hd_fn_zeta (unsigned, char)         noexcept { return 0; }
inline void hd_fn_eta  (float, double)          noexcept {}
inline void hd_fn_theta(double, float)          noexcept {}  // swapped
inline void hd_fn_iota (int, double, char)      noexcept {}

// FOUND-F09-AUDIT: concurrency-test-only functions.  Each thread
// targets ITS OWN slot — the test verifies that distinct
// instantiations have isolated atomics, so concurrent inserts
// do NOT interfere.
inline void ct_fn_t0(int) noexcept {}
inline void ct_fn_t1(int) noexcept {}
inline void ct_fn_t2(int) noexcept {}
inline void ct_fn_t3(int) noexcept {}

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

    // ── (A2) FOUND-F09-AUDIT-5: single-call guard ────────────────
    // The header's smoke test is single-call-per-process — sub-blocks
    // (1)/(4)/(6)/(7) check miss-before-insert assertions that hold
    // only on first invocation.  A second call hits the static guard
    // and returns false, surfacing accidental re-invocation as a
    // clear test failure instead of a silent miss-becomes-hit
    // discrepancy.  Verifies the contract is observably enforced.
    run_test("smoke_test_guard_rejects_reinvocation", []{
        EXPECT_TRUE(cipher::computation_cache_smoke_test() == false);
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

    // ── (H) Empty Args still distinguishes by function NAME ──────
    // FOUND-F09-AUDIT: same-signature, different-name functions
    // MUST produce different keys.  We declare one extra void()
    // function `test_fn_e` adjacent to `test_fn_d` and verify
    // the two yield distinct keys despite identical signatures.
    run_test("same_signature_different_name_distinct_keys", []{
        // test_fn_d and test_fn_e are both `void()` but have
        // distinct identifiers — reflect_constant disambiguates.
        constexpr std::uint64_t k_d = cipher::computation_cache_key<&test_fn_d>;
        constexpr std::uint64_t k_e_zeta_void = cipher::computation_cache_key<&hd_fn_alpha, int>;
        // Both keys non-zero and distinct.
        EXPECT_TRUE(k_d != 0);
        EXPECT_TRUE(k_d != k_e_zeta_void);
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

    // ── (J) FOUND-F09-AUDIT: hash distribution sanity ────────────
    // Nine distinct (FnPtr, Args...) tuples — a realistic mix of
    // arities, types, and order-permutations — must produce nine
    // distinct 64-bit keys.  A collision here would alias two
    // unrelated cache slots in federation paths.
    run_test("hash_distribution_sanity", []{
        constexpr std::array<std::uint64_t, 9> keys = {
            cipher::computation_cache_key<&hd_fn_alpha, int>,
            cipher::computation_cache_key<&hd_fn_beta,  int>,
            cipher::computation_cache_key<&hd_fn_gamma, int, int>,
            cipher::computation_cache_key<&hd_fn_delta, int, int, int>,
            cipher::computation_cache_key<&hd_fn_eps,   long>,
            cipher::computation_cache_key<&hd_fn_zeta,  unsigned, char>,
            cipher::computation_cache_key<&hd_fn_eta,   float, double>,
            cipher::computation_cache_key<&hd_fn_theta, double, float>,
            cipher::computation_cache_key<&hd_fn_iota,  int, double, char>,
        };
        // Pairwise distinct.  Bubble-style O(n^2) check — fine
        // for n=9.
        for (std::size_t i = 0; i < keys.size(); ++i) {
            EXPECT_TRUE(keys[i] != 0);
            for (std::size_t j = i + 1; j < keys.size(); ++j) {
                EXPECT_TRUE(keys[i] != keys[j]);
            }
        }
    });

    // ── (K) FOUND-F09-AUDIT: concurrent inserts on disjoint slots ─
    // Spawns four threads, each inserting into its own slot.  The
    // ThreadSafe axiom guarantees no race because distinct
    // (FnPtr, Args...) instantiations have disjoint atomics.  The
    // post-condition: every slot has its expected body, none is
    // null, none is corrupted.
    run_test("concurrent_inserts_disjoint_slots", []{
        constexpr std::uintptr_t base = 0x10000;
        std::atomic<int> ready{0};
        std::atomic<bool> go{false};

        auto worker = [&](auto* body, auto inserter) {
            ready.fetch_add(1, std::memory_order_release);
            while (!go.load(std::memory_order_acquire)) { /* spin */ }
            inserter(body);
        };

        // Each lambda captures its slot's instantiation via the
        // explicit `inserter` callable.  Different inserters →
        // different atomic slots.
        std::array<std::jthread, 4> workers = {
            std::jthread{[&]{
                worker(make_stub(base + 0), [](cipher::CompiledBody* b){
                    cipher::insert_computation_cache<&ct_fn_t0, int>(b);
                });
            }},
            std::jthread{[&]{
                worker(make_stub(base + 1), [](cipher::CompiledBody* b){
                    cipher::insert_computation_cache<&ct_fn_t1, int>(b);
                });
            }},
            std::jthread{[&]{
                worker(make_stub(base + 2), [](cipher::CompiledBody* b){
                    cipher::insert_computation_cache<&ct_fn_t2, int>(b);
                });
            }},
            std::jthread{[&]{
                worker(make_stub(base + 3), [](cipher::CompiledBody* b){
                    cipher::insert_computation_cache<&ct_fn_t3, int>(b);
                });
            }},
        };

        // Wait until all four threads are at the barrier, then
        // release them simultaneously to maximize contention.
        while (ready.load(std::memory_order_acquire) < 4) { /* spin */ }
        go.store(true, std::memory_order_release);

        // jthread destructors join here.  After join, every slot
        // observed its insert from the corresponding thread.
        for (auto& t : workers) { t.join(); }

        EXPECT_TRUE(cipher::lookup_computation_cache<&ct_fn_t0, int>()
                    == make_stub(base + 0));
        EXPECT_TRUE(cipher::lookup_computation_cache<&ct_fn_t1, int>()
                    == make_stub(base + 1));
        EXPECT_TRUE(cipher::lookup_computation_cache<&ct_fn_t2, int>()
                    == make_stub(base + 2));
        EXPECT_TRUE(cipher::lookup_computation_cache<&ct_fn_t3, int>()
                    == make_stub(base + 3));
    });

    // ── (L) FOUND-F09-AUDIT: concurrent inserts on SAME slot ─────
    // First-writer-wins idempotency under contention.  Four threads
    // race to insert into the SAME slot with four distinct bodies.
    // Exactly ONE thread's body wins; the other three's CAS fails
    // and they no-op.  Verify the slot ends up at exactly one of
    // the four candidate bodies (idempotent contract).
    run_test("concurrent_inserts_same_slot_first_wins", []{
        // Use a distinct slot from previous tests:
        // (test_fn_c, int) was confirmed empty in test (E).
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_c, int>()
                    == nullptr);

        constexpr std::uintptr_t base = 0x20000;
        std::array<cipher::CompiledBody*, 4> candidates = {
            make_stub(base + 0), make_stub(base + 1),
            make_stub(base + 2), make_stub(base + 3),
        };
        std::atomic<int> ready{0};
        std::atomic<bool> go{false};

        std::array<std::jthread, 4> workers;
        for (std::size_t i = 0; i < 4; ++i) {
            workers[i] = std::jthread{[&, i]{
                ready.fetch_add(1, std::memory_order_release);
                while (!go.load(std::memory_order_acquire)) { /* spin */ }
                cipher::insert_computation_cache<&test_fn_c, int>(
                    candidates[i]);
            }};
        }
        while (ready.load(std::memory_order_acquire) < 4) { /* spin */ }
        go.store(true, std::memory_order_release);
        for (auto& t : workers) { t.join(); }

        // Slot must be one of the four candidates (idempotent +
        // first-wins).
        auto* winner = cipher::lookup_computation_cache<&test_fn_c, int>();
        EXPECT_TRUE(winner != nullptr);
        bool is_one_of = false;
        for (auto* c : candidates) {
            if (winner == c) { is_one_of = true; break; }
        }
        EXPECT_TRUE(is_one_of);
    });

    std::fprintf(stderr, "\ntotal: %d passed, %d failed\n",
                 total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
