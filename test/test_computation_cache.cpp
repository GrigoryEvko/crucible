// The cache header ships its own smoke test, which this file calls
// first.  Every test below it uses function and argument tuples the
// smoke test never touches, so the two sets of slots are independent
// and running them in sequence cannot corrupt each other.
//
// Several tests here do depend on each other's slot state, and each
// says so where it does.

#include <crucible/cipher/ComputationCache.h>

#include <array>
#include <atomic>
#include <bit>
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
#define EXPECT_TRUE(...)                                                                                    \
    do {                                                                                                    \
        if (!(__VA_ARGS__)) {                                                                               \
            std::fprintf(stderr, "    EXPECT_TRUE failed: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            throw TestFailure{};                                                                            \
        }                                                                                                   \
    } while (0)

namespace cipher = ::crucible::cipher;

// None of the functions below is ever called.  Their addresses serve
// as template arguments, and each function paired with an argument
// pack names one atomic slot.

inline void test_fn_a(int) noexcept {}
inline void test_fn_b(int, double) noexcept {}
inline int test_fn_c(int) noexcept { return 0; }
inline void test_fn_d() noexcept {}

// These nine vary in arity, in parameter type, and in parameter
// order.  A key collision between any two of them would give
// unrelated slots one identity wherever the key is what travels.
inline void hd_fn_alpha(int) noexcept {}
inline void hd_fn_beta(int) noexcept {}
inline void hd_fn_gamma(int, int) noexcept {}
inline void hd_fn_delta(int, int, int) noexcept {}
inline int hd_fn_eps(long) noexcept { return 0; }
inline char hd_fn_zeta(unsigned, char) noexcept { return 0; }
inline void hd_fn_eta(float, double) noexcept {}
inline void hd_fn_theta(double, float) noexcept {}  // swapped
inline void hd_fn_iota(int, double, char) noexcept {}

// One function per thread, so each thread writes its own slot.
inline void ct_fn_t0(int) noexcept {}
inline void ct_fn_t1(int) noexcept {}
inline void ct_fn_t2(int) noexcept {}
inline void ct_fn_t3(int) noexcept {}

// The same arrangement for the row-aware slots.  These are separate
// functions rather than a reuse of the four above, which keeps the
// row-blind and row-aware concurrency tests independent of each
// other's order.
inline void cr_fn_t0(int) noexcept {}
inline void cr_fn_t1(int) noexcept {}
inline void cr_fn_t2(int) noexcept {}
inline void cr_fn_t3(int) noexcept {}

// Separate from the row-blind contention fixture, so the row-aware
// first-writer test starts from a slot known to be empty.
inline int cr_contention_fn(int) noexcept { return 0; }

// The cache stores these pointers and never dereferences them, so a
// bit pattern that points nowhere is a valid stand-in for a body.

cipher::CompiledBody* make_stub(std::uintptr_t v) noexcept { return std::bit_cast<cipher::CompiledBody*>(v); }

}  // namespace

int main() {
    std::fprintf(stderr, "test_computation_cache:\n");

    // The smoke test runs once per process and reports false
    // thereafter.  All three calls live in one body on purpose: as
    // separate sub-tests a reordering would make the second call the
    // first invocation and quietly swap the expected results.
    run_test("header_smoke_test_then_guard_rejects_reinvocation", [] {
        EXPECT_TRUE(cipher::computation_cache_smoke_test() == true);
        EXPECT_TRUE(cipher::computation_cache_smoke_test() == false);
        EXPECT_TRUE(cipher::computation_cache_smoke_test() == false);
    });

    run_test("lookup_before_insert_returns_nullptr", [] {
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_a, int>() == nullptr);
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_b, int, double>() == nullptr);
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_d>()
                    == nullptr);  // an empty argument pack is a valid slot
    });

    run_test("insert_then_lookup_roundtrips", [] {
        auto* body = make_stub(0xCAFEBABE);
        cipher::insert_computation_cache<&test_fn_a, int>(body);
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_a, int>() == body);
    });

    run_test("second_insert_is_idempotent", [] {
        // The round-trip test above filled this slot, and a second
        // insert must not displace what it put there.
        auto* original = cipher::lookup_computation_cache<&test_fn_a, int>();
        EXPECT_TRUE(original != nullptr);

        auto* attempted_overwrite = make_stub(0xDEADBEEF);
        EXPECT_TRUE(original != attempted_overwrite);

        cipher::insert_computation_cache<&test_fn_a, int>(attempted_overwrite);

        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_a, int>() == original);
    });

    run_test("distinct_instantiations_are_isolated", [] {
        // Changing either the function or the argument pack names a
        // different slot, so each of these still misses.
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_a, float>() == nullptr);

        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_b, int, double>() == nullptr);

        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_c, int>() == nullptr);

        auto* body_b = make_stub(0xB0DECAFE);
        cipher::insert_computation_cache<&test_fn_b, int, double>(body_b);
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_b, int, double>() == body_b);

        auto* original_a = cipher::lookup_computation_cache<&test_fn_a, int>();
        EXPECT_TRUE(original_a != body_b);
        EXPECT_TRUE(original_a != nullptr);
    });

    run_test("cache_key_order_sensitive", [] {
        // The keys are compile-time values; comparing them at run time
        // gives the assertion a failure message.
        constexpr std::uint64_t k_int_double = cipher::computation_cache_key<&test_fn_b, int, double>;
        constexpr std::uint64_t k_double_int = cipher::computation_cache_key<&test_fn_b, double, int>;
        EXPECT_TRUE(k_int_double != k_double_int);
        EXPECT_TRUE(k_int_double != 0);
        EXPECT_TRUE(k_double_int != 0);
    });

    run_test("cache_key_is_deterministic", [] {
        constexpr std::uint64_t k1 = cipher::computation_cache_key<&test_fn_a, int>;
        constexpr std::uint64_t k2 = cipher::computation_cache_key<&test_fn_a, int>;
        EXPECT_TRUE(k1 == k2);
    });

    run_test("same_signature_different_name_distinct_keys", [] {
        constexpr std::uint64_t k_d = cipher::computation_cache_key<&test_fn_d>;
        constexpr std::uint64_t k_e_zeta_void = cipher::computation_cache_key<&hd_fn_alpha, int>;
        EXPECT_TRUE(k_d != 0);
        EXPECT_TRUE(k_d != k_e_zeta_void);
    });

    run_test("drain_is_phase5_stub_noop", [] {
        auto* original_a = cipher::lookup_computation_cache<&test_fn_a, int>();
        EXPECT_TRUE(original_a != nullptr);

        cipher::drain_computation_cache(std::chrono::seconds{0});

        // Draining leaves the slot exactly as it was.
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_a, int>() == original_a);
    });

    run_test("hash_distribution_sanity", [] {
        constexpr std::array<std::uint64_t, 9> keys = {
            cipher::computation_cache_key<&hd_fn_alpha, int>,
            cipher::computation_cache_key<&hd_fn_beta, int>,
            cipher::computation_cache_key<&hd_fn_gamma, int, int>,
            cipher::computation_cache_key<&hd_fn_delta, int, int, int>,
            cipher::computation_cache_key<&hd_fn_eps, long>,
            cipher::computation_cache_key<&hd_fn_zeta, unsigned, char>,
            cipher::computation_cache_key<&hd_fn_eta, float, double>,
            cipher::computation_cache_key<&hd_fn_theta, double, float>,
            cipher::computation_cache_key<&hd_fn_iota, int, double, char>,
        };
        // Comparing every pair is quadratic, which is affordable for
        // nine keys and exact, which sampling would not be.
        for (std::size_t i = 0; i < keys.size(); ++i) {
            EXPECT_TRUE(keys[i] != 0);
            for (std::size_t j = i + 1; j < keys.size(); ++j) {
                EXPECT_TRUE(keys[i] != keys[j]);
            }
        }
    });

    // Four threads, four slots, no overlap.  Distinct instantiations
    // hold distinct atomics, so there is nothing for them to race on,
    // and every slot must end up holding its own thread's body.
    run_test("concurrent_inserts_disjoint_slots", [] {
        constexpr std::uintptr_t base = 0x10000;
        std::atomic<int> ready{0};
        std::atomic<bool> go{false};

        auto worker = [&](auto* body, auto inserter) {
            ready.fetch_add(1, std::memory_order_release);
            while (!go.load(std::memory_order_acquire)) { /* spin */
            }
            inserter(body);
        };

        // The instantiation travels in the inserter callable, so each
        // worker reaches a different slot.
        std::array<std::jthread, 4> workers = {
            std::jthread{[&] {
                worker(make_stub(base + 0),
                       [](cipher::CompiledBody* b) { cipher::insert_computation_cache<&ct_fn_t0, int>(b); });
            }},
            std::jthread{[&] {
                worker(make_stub(base + 1),
                       [](cipher::CompiledBody* b) { cipher::insert_computation_cache<&ct_fn_t1, int>(b); });
            }},
            std::jthread{[&] {
                worker(make_stub(base + 2),
                       [](cipher::CompiledBody* b) { cipher::insert_computation_cache<&ct_fn_t2, int>(b); });
            }},
            std::jthread{[&] {
                worker(make_stub(base + 3),
                       [](cipher::CompiledBody* b) { cipher::insert_computation_cache<&ct_fn_t3, int>(b); });
            }},
        };

        // Hold every thread at the gate and release them together, so
        // the inserts actually overlap.
        while (ready.load(std::memory_order_acquire) < 4) { /* spin */
        }
        go.store(true, std::memory_order_release);

        for (auto& t : workers) {
            t.join();
        }

        EXPECT_TRUE(cipher::lookup_computation_cache<&ct_fn_t0, int>() == make_stub(base + 0));
        EXPECT_TRUE(cipher::lookup_computation_cache<&ct_fn_t1, int>() == make_stub(base + 1));
        EXPECT_TRUE(cipher::lookup_computation_cache<&ct_fn_t2, int>() == make_stub(base + 2));
        EXPECT_TRUE(cipher::lookup_computation_cache<&ct_fn_t3, int>() == make_stub(base + 3));
    });

    // Four threads, one slot, four different bodies.  One of them
    // wins and the other three find the slot taken and do nothing, so
    // the slot ends up holding one of the four and no hybrid of them.
    // The isolation test above left this slot empty.
    run_test("concurrent_inserts_same_slot_first_wins", [] {
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_c, int>() == nullptr);

        constexpr std::uintptr_t base = 0x20000;
        std::array<cipher::CompiledBody*, 4> candidates = {
            make_stub(base + 0),
            make_stub(base + 1),
            make_stub(base + 2),
            make_stub(base + 3),
        };
        std::atomic<int> ready{0};
        std::atomic<bool> go{false};

        std::array<std::jthread, 4> workers;
        for (std::size_t i = 0; i < 4; ++i) {
            workers[i] = std::jthread{[&, i] {
                ready.fetch_add(1, std::memory_order_release);
                while (!go.load(std::memory_order_acquire)) { /* spin */
                }
                cipher::insert_computation_cache<&test_fn_c, int>(candidates[i]);
            }};
        }
        while (ready.load(std::memory_order_acquire) < 4) { /* spin */
        }
        go.store(true, std::memory_order_release);
        for (auto& t : workers) {
            t.join();
        }

        auto* winner = cipher::lookup_computation_cache<&test_fn_c, int>();
        EXPECT_TRUE(winner != nullptr);
        bool is_one_of = false;
        for (auto* c : candidates) {
            if (winner == c) {
                is_one_of = true;
                break;
            }
        }
        EXPECT_TRUE(is_one_of);
    });

    // One function under three different rows is three slots.  A
    // dispatcher that aliased compiled bodies across effect rows would
    // run a body compiled for one row in another.
    namespace eff = ::crucible::effects;
    using EmptyR = eff::Row<>;
    using BgR = eff::Row<eff::Effect::Bg>;
    using IOR = eff::Row<eff::Effect::IO>;
    using BgIOR = eff::Row<eff::Effect::Bg, eff::Effect::IO>;

    run_test("row_aware_slots_are_isolated_by_row", [] {
        auto* body_empty = make_stub(0xE0);
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&test_fn_a, EmptyR, int>() == nullptr);
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&test_fn_a, BgR, int>() == nullptr);
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&test_fn_a, IOR, int>() == nullptr);

        cipher::insert_computation_cache_in_row<&test_fn_a, EmptyR, int>(body_empty);

        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&test_fn_a, EmptyR, int>() == body_empty);
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&test_fn_a, BgR, int>() == nullptr);
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&test_fn_a, IOR, int>() == nullptr);
    });

    // An empty row is still a row: the row-aware slot for a function
    // and argument pack is not the row-blind slot for the same pair.
    // The row-blind side is already populated by an earlier test, so
    // only the row-aware side can still witness a miss here.
    run_test("row_blind_and_row_aware_slots_disjoint", [] {
        auto* body_blind = make_stub(0xB1);
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&test_fn_b, EmptyR, int, double>() == nullptr);

        cipher::insert_computation_cache_in_row<&test_fn_b, EmptyR, int, double>(body_blind);

        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&test_fn_b, EmptyR, int, double>() == body_blind);
    });

    // Two orderings of the same row are one row as far as the key is
    // concerned, because the hash sorts before folding.  They are two
    // distinct C++ types as far as the slots are concerned, because
    // each type instantiates its own atomic.  Both hold at once: slot
    // identity is type-driven and the key that travels is hash-driven.
    run_test("row_aware_slot_id_is_type_driven_not_hash_driven", [] {
        auto* body_bgio = make_stub(0xC0);
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&test_fn_a, BgIOR, int>() == nullptr);
        // The two orderings agree on the key.
        EXPECT_TRUE(
            cipher::computation_cache_key_in_row<&test_fn_a, BgIOR, int>
            == cipher::computation_cache_key_in_row<&test_fn_a, eff::Row<eff::Effect::IO, eff::Effect::Bg>, int>);
        cipher::insert_computation_cache_in_row<&test_fn_a, BgIOR, int>(body_bgio);
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&test_fn_a, BgIOR, int>() == body_bgio);
        EXPECT_TRUE(
            cipher::lookup_computation_cache_in_row<&test_fn_a, eff::Row<eff::Effect::IO, eff::Effect::Bg>, int>()
            == nullptr);  // a distinct type is a distinct slot
    });

    // The disjoint-slot race again, on the row-aware slots.  Both
    // families compile to the same shape, so this adds no new
    // argument, but it does catch a refactor that weakens the memory
    // ordering on one family and not the other.
    run_test("concurrent_inserts_disjoint_slots_in_row", [] {
        constexpr std::uintptr_t base = 0x30000;
        std::atomic<int> ready{0};
        std::atomic<bool> go{false};

        auto worker = [&](auto* body, auto inserter) {
            ready.fetch_add(1, std::memory_order_release);
            while (!go.load(std::memory_order_acquire)) { /* spin */
            }
            inserter(body);
        };

        std::array<std::jthread, 4> workers = {
            std::jthread{[&] {
                worker(make_stub(base + 0), [](cipher::CompiledBody* b) {
                    cipher::insert_computation_cache_in_row<&cr_fn_t0, EmptyR, int>(b);
                });
            }},
            std::jthread{[&] {
                worker(make_stub(base + 1), [](cipher::CompiledBody* b) {
                    cipher::insert_computation_cache_in_row<&cr_fn_t1, EmptyR, int>(b);
                });
            }},
            std::jthread{[&] {
                worker(make_stub(base + 2), [](cipher::CompiledBody* b) {
                    cipher::insert_computation_cache_in_row<&cr_fn_t2, EmptyR, int>(b);
                });
            }},
            std::jthread{[&] {
                worker(make_stub(base + 3), [](cipher::CompiledBody* b) {
                    cipher::insert_computation_cache_in_row<&cr_fn_t3, EmptyR, int>(b);
                });
            }},
        };

        while (ready.load(std::memory_order_acquire) < 4) { /* spin */
        }
        go.store(true, std::memory_order_release);
        for (auto& t : workers) {
            t.join();
        }

        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&cr_fn_t0, EmptyR, int>() == make_stub(base + 0));
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&cr_fn_t1, EmptyR, int>() == make_stub(base + 1));
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&cr_fn_t2, EmptyR, int>() == make_stub(base + 2));
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&cr_fn_t3, EmptyR, int>() == make_stub(base + 3));
    });

    // The same-slot race again, on the row-aware slots, for the same
    // reason.
    run_test("concurrent_inserts_same_slot_first_wins_in_row", [] {
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&cr_contention_fn, EmptyR, int>() == nullptr);

        constexpr std::uintptr_t base = 0x40000;
        std::array<cipher::CompiledBody*, 4> candidates = {
            make_stub(base + 0),
            make_stub(base + 1),
            make_stub(base + 2),
            make_stub(base + 3),
        };
        std::atomic<int> ready{0};
        std::atomic<bool> go{false};

        std::array<std::jthread, 4> workers;
        for (std::size_t i = 0; i < 4; ++i) {
            workers[i] = std::jthread{[&, i] {
                ready.fetch_add(1, std::memory_order_release);
                while (!go.load(std::memory_order_acquire)) { /* spin */
                }
                cipher::insert_computation_cache_in_row<&cr_contention_fn, EmptyR, int>(candidates[i]);
            }};
        }
        while (ready.load(std::memory_order_acquire) < 4) { /* spin */
        }
        go.store(true, std::memory_order_release);
        for (auto& t : workers) {
            t.join();
        }

        auto* winner = cipher::lookup_computation_cache_in_row<&cr_contention_fn, EmptyR, int>();
        EXPECT_TRUE(winner != nullptr);
        bool is_one_of = false;
        for (auto* c : candidates) {
            if (winner == c) {
                is_one_of = true;
                break;
            }
        }
        EXPECT_TRUE(is_one_of);
    });

    std::fprintf(stderr, "\ntotal: %d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
