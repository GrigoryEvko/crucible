// The smoke test below runs first.  Every test after it uses function
// and argument tuples the smoke test never touches, so the two sets of
// slots are independent and running them in sequence cannot corrupt
// each other.
//
// Several tests here do depend on each other's slot state, and each
// says so where it does.

#include <crucible/cipher/ComputationCache.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include "test_assert.h"

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

}  // namespace

// The header only declares the body, and a consumer defines it.  The cache
// never reads a body, so an empty body serves: each object has an address
// of its own.
namespace crucible::cipher {
struct CompiledBody {};
}  // namespace crucible::cipher

// None of the functions below is ever called.  Their addresses serve
// as template arguments, and each function paired with an argument
// pack names one atomic slot.  They stand outside the unnamed
// namespace, because a key refuses a function with internal linkage.
namespace test_computation_cache_functions {

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

// The probe functions of the smoke test.  Each signature differs, so
// each probe keys a slot of its own.
inline void p_unary(int) noexcept {}
inline void p_binary(int, double) noexcept {}
inline void p_void() noexcept {}
inline void p_throwing(int) {}
inline void p_noexcept(int) noexcept {}

}  // namespace test_computation_cache_functions

namespace {

using namespace test_computation_cache_functions;

using Slot = ::fixy::Option<::fixy::Ref<cipher::CompiledBody>>;
using Bodies = ::foundation::core::FixedArray<cipher::CompiledBody, 4>;

// The bodies that the tests insert.  Each object lives as long as the
// process, as the slots do.
cipher::CompiledBody smoke_body_a{};
cipher::CompiledBody smoke_body_b{};
cipher::CompiledBody smoke_body_c{};
cipher::CompiledBody smoke_body_d{};
cipher::CompiledBody round_trip_body{};
cipher::CompiledBody overwrite_body{};
cipher::CompiledBody isolated_body{};
cipher::CompiledBody empty_row_body{};
cipher::CompiledBody blind_pair_body{};
cipher::CompiledBody bg_io_body{};
Bodies disjoint_bodies{};
Bodies contended_bodies{};
Bodies disjoint_row_bodies{};
Bodies contended_row_bodies{};

// True when the slot holds body.
[[nodiscard]] bool holds(Slot slot, cipher::CompiledBody const& body) noexcept {
    return std::move(slot).match(
        [&body](::fixy::Ref<cipher::CompiledBody> found) noexcept {
            return ::fixy::same_object(found, ::fixy::to(body));
        },
        []() noexcept { return false; });
}

// The body at position of bodies.
[[nodiscard]] cipher::CompiledBody& body_at(Bodies& bodies, std::size_t position) noexcept {
    return *bodies.view().get(position).expect("each position of the test is below four");
}

// This runs once per process and refuses to run twice. The slots it
// reads are program-lifetime globals, so the checks that a lookup
// misses before its insert hold on the first call only. A second call
// would find those slots already full, so it reports failure rather
// than let a miss quietly become a hit.
bool computation_cache_smoke_test() noexcept {
    static std::atomic<int> call_counter{0};
    if (call_counter.fetch_add(1, std::memory_order_relaxed) > 0) {
        return false;
    }

    bool ok = true;

    ok = ok && cipher::lookup_computation_cache<&p_unary, int>().is_none();
    ok = ok && cipher::lookup_computation_cache<&p_unary, float>().is_none();
    ok = ok && cipher::lookup_computation_cache<&p_binary, int, double>().is_none();

    cipher::insert_computation_cache<&p_unary, int>(::fixy::to(smoke_body_a));
    ok = ok && holds(cipher::lookup_computation_cache<&p_unary, int>(), smoke_body_a);

    cipher::insert_computation_cache<&p_unary, int>(::fixy::to(smoke_body_b));
    ok = ok && holds(cipher::lookup_computation_cache<&p_unary, int>(), smoke_body_a);

    ok = ok && cipher::lookup_computation_cache<&p_unary, float>().is_none();
    ok = ok && cipher::lookup_computation_cache<&p_binary, int, double>().is_none();

    // An empty argument pack must still reach a slot of its own, and
    // must still instantiate as a template rather than collapse onto
    // a plain function.
    ok = ok && cipher::lookup_computation_cache<&p_void>().is_none();
    cipher::insert_computation_cache<&p_void>(::fixy::to(smoke_body_c));
    ok = ok && holds(cipher::lookup_computation_cache<&p_void>(), smoke_body_c);
    ok = ok && holds(cipher::lookup_computation_cache<&p_unary, int>(), smoke_body_a);

    // Only the throwing slot is filled. The noexcept one must still
    // miss, because the two function-pointer types are distinct and
    // must not be folded onto one symbol.
    ok = ok && cipher::lookup_computation_cache<&p_throwing, int>().is_none();
    ok = ok && cipher::lookup_computation_cache<&p_noexcept, int>().is_none();
    cipher::insert_computation_cache<&p_throwing, int>(::fixy::to(smoke_body_d));
    ok = ok && holds(cipher::lookup_computation_cache<&p_throwing, int>(), smoke_body_d);
    ok = ok && cipher::lookup_computation_cache<&p_noexcept, int>().is_none();

    return ok;
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_computation_cache:\n");

    // The smoke test runs once per process and reports false
    // thereafter.  All three calls live in one body on purpose: as
    // separate sub-tests a reordering would make the second call the
    // first invocation and quietly swap the expected results.
    run_test("smoke_test_then_guard_rejects_reinvocation", [] {
        EXPECT_TRUE(computation_cache_smoke_test() == true);
        EXPECT_TRUE(computation_cache_smoke_test() == false);
        EXPECT_TRUE(computation_cache_smoke_test() == false);
    });

    run_test("lookup_before_insert_misses", [] {
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_a, int>().is_none());
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_b, int, double>().is_none());
        // An empty argument pack is a valid slot.
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_d>().is_none());
    });

    run_test("insert_then_lookup_roundtrips", [] {
        cipher::insert_computation_cache<&test_fn_a, int>(::fixy::to(round_trip_body));
        EXPECT_TRUE(holds(cipher::lookup_computation_cache<&test_fn_a, int>(), round_trip_body));
    });

    run_test("second_insert_is_idempotent", [] {
        // The round-trip test above filled this slot, and a second
        // insert must not displace what it put there.
        EXPECT_TRUE(holds(cipher::lookup_computation_cache<&test_fn_a, int>(), round_trip_body));

        cipher::insert_computation_cache<&test_fn_a, int>(::fixy::to(overwrite_body));

        EXPECT_TRUE(holds(cipher::lookup_computation_cache<&test_fn_a, int>(), round_trip_body));
        EXPECT_TRUE(!holds(cipher::lookup_computation_cache<&test_fn_a, int>(), overwrite_body));
    });

    run_test("distinct_instantiations_are_isolated", [] {
        // Changing either the function or the argument pack names a
        // different slot, so each of these still misses.
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_a, float>().is_none());

        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_b, int, double>().is_none());

        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_c, int>().is_none());

        cipher::insert_computation_cache<&test_fn_b, int, double>(::fixy::to(isolated_body));
        EXPECT_TRUE(holds(cipher::lookup_computation_cache<&test_fn_b, int, double>(), isolated_body));

        EXPECT_TRUE(holds(cipher::lookup_computation_cache<&test_fn_a, int>(), round_trip_body));
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
        std::atomic<int> ready{0};
        std::atomic<bool> go{false};

        auto worker = [&](::fixy::Ref<cipher::CompiledBody> body, auto inserter) {
            ready.fetch_add(1, std::memory_order_release);
            while (!go.load(std::memory_order_acquire)) { /* spin */
            }
            inserter(body);
        };

        // The instantiation travels in the inserter callable, so each
        // worker reaches a different slot.
        std::array<std::jthread, 4> workers = {
            std::jthread{[&] {
                worker(::fixy::to(body_at(disjoint_bodies, 0)), [](::fixy::Ref<cipher::CompiledBody> body) {
                    cipher::insert_computation_cache<&ct_fn_t0, int>(body);
                });
            }},
            std::jthread{[&] {
                worker(::fixy::to(body_at(disjoint_bodies, 1)), [](::fixy::Ref<cipher::CompiledBody> body) {
                    cipher::insert_computation_cache<&ct_fn_t1, int>(body);
                });
            }},
            std::jthread{[&] {
                worker(::fixy::to(body_at(disjoint_bodies, 2)), [](::fixy::Ref<cipher::CompiledBody> body) {
                    cipher::insert_computation_cache<&ct_fn_t2, int>(body);
                });
            }},
            std::jthread{[&] {
                worker(::fixy::to(body_at(disjoint_bodies, 3)), [](::fixy::Ref<cipher::CompiledBody> body) {
                    cipher::insert_computation_cache<&ct_fn_t3, int>(body);
                });
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

        EXPECT_TRUE(holds(cipher::lookup_computation_cache<&ct_fn_t0, int>(), body_at(disjoint_bodies, 0)));
        EXPECT_TRUE(holds(cipher::lookup_computation_cache<&ct_fn_t1, int>(), body_at(disjoint_bodies, 1)));
        EXPECT_TRUE(holds(cipher::lookup_computation_cache<&ct_fn_t2, int>(), body_at(disjoint_bodies, 2)));
        EXPECT_TRUE(holds(cipher::lookup_computation_cache<&ct_fn_t3, int>(), body_at(disjoint_bodies, 3)));
    });

    // Four threads, one slot, four different bodies.  One of them
    // wins and the other three find the slot taken and do nothing, so
    // the slot ends up holding one of the four and no hybrid of them.
    // The isolation test above left this slot empty.
    run_test("concurrent_inserts_same_slot_first_wins", [] {
        EXPECT_TRUE(cipher::lookup_computation_cache<&test_fn_c, int>().is_none());

        std::atomic<int> ready{0};
        std::atomic<bool> go{false};

        std::array<std::jthread, 4> workers;
        for (std::size_t i = 0; i < 4; ++i) {
            workers[i] = std::jthread{[&, i] {
                ready.fetch_add(1, std::memory_order_release);
                while (!go.load(std::memory_order_acquire)) { /* spin */
                }
                cipher::insert_computation_cache<&test_fn_c, int>(::fixy::to(body_at(contended_bodies, i)));
            }};
        }
        while (ready.load(std::memory_order_acquire) < 4) { /* spin */
        }
        go.store(true, std::memory_order_release);
        for (auto& t : workers) {
            t.join();
        }

        const Slot winner = cipher::lookup_computation_cache<&test_fn_c, int>();
        EXPECT_TRUE(winner.is_some());
        bool is_one_of = false;
        for (cipher::CompiledBody const& candidate : contended_bodies) {
            if (holds(winner, candidate)) {
                is_one_of = true;
                break;
            }
        }
        EXPECT_TRUE(is_one_of);
    });

    // One function under three different rows is three slots.  A
    // dispatcher that aliased compiled bodies across effect rows would
    // run a body compiled for one row in another.
    namespace eff = ::foundation::effects;
    using EmptyR = eff::Row<>;
    using BgR = eff::Row<eff::Effect::Bg>;
    using IOR = eff::Row<eff::Effect::IO>;
    using BgIOR = eff::Row<eff::Effect::Bg, eff::Effect::IO>;

    run_test("row_aware_slots_are_isolated_by_row", [] {
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&test_fn_a, EmptyR, int>().is_none());
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&test_fn_a, BgR, int>().is_none());
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&test_fn_a, IOR, int>().is_none());

        cipher::insert_computation_cache_in_row<&test_fn_a, EmptyR, int>(::fixy::to(empty_row_body));

        EXPECT_TRUE(holds(cipher::lookup_computation_cache_in_row<&test_fn_a, EmptyR, int>(), empty_row_body));
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&test_fn_a, BgR, int>().is_none());
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&test_fn_a, IOR, int>().is_none());
    });

    // An empty row is still a row: the row-aware slot for a function
    // and argument pack is not the row-blind slot for the same pair.
    // The row-blind side is already populated by an earlier test, so
    // only the row-aware side can still witness a miss here.
    run_test("row_blind_and_row_aware_slots_disjoint", [] {
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&test_fn_b, EmptyR, int, double>().is_none());

        cipher::insert_computation_cache_in_row<&test_fn_b, EmptyR, int, double>(::fixy::to(blind_pair_body));

        EXPECT_TRUE(holds(cipher::lookup_computation_cache_in_row<&test_fn_b, EmptyR, int, double>(), blind_pair_body));
    });

    // Two orderings of the same row are one row as far as the key is
    // concerned, because the hash sorts before folding.  They are two
    // distinct C++ types as far as the slots are concerned, because
    // each type instantiates its own atomic.  Both hold at once: slot
    // identity is type-driven and the key that travels is hash-driven.
    run_test("row_aware_slot_id_is_type_driven_not_hash_driven", [] {
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&test_fn_a, BgIOR, int>().is_none());
        // The two orderings agree on the key.
        EXPECT_TRUE(
            cipher::computation_cache_key_in_row<&test_fn_a, BgIOR, int>
            == cipher::computation_cache_key_in_row<&test_fn_a, eff::Row<eff::Effect::IO, eff::Effect::Bg>, int>);
        cipher::insert_computation_cache_in_row<&test_fn_a, BgIOR, int>(::fixy::to(bg_io_body));
        EXPECT_TRUE(holds(cipher::lookup_computation_cache_in_row<&test_fn_a, BgIOR, int>(), bg_io_body));
        // A distinct type is a distinct slot.
        EXPECT_TRUE(
            cipher::lookup_computation_cache_in_row<&test_fn_a, eff::Row<eff::Effect::IO, eff::Effect::Bg>, int>()
                .is_none());
    });

    // The disjoint-slot race again, on the row-aware slots.  Both
    // families compile to the same shape, so this adds no new
    // argument, but it does catch a refactor that weakens the memory
    // ordering on one family and not the other.
    run_test("concurrent_inserts_disjoint_slots_in_row", [] {
        std::atomic<int> ready{0};
        std::atomic<bool> go{false};

        auto worker = [&](::fixy::Ref<cipher::CompiledBody> body, auto inserter) {
            ready.fetch_add(1, std::memory_order_release);
            while (!go.load(std::memory_order_acquire)) { /* spin */
            }
            inserter(body);
        };

        std::array<std::jthread, 4> workers = {
            std::jthread{[&] {
                worker(::fixy::to(body_at(disjoint_row_bodies, 0)), [](::fixy::Ref<cipher::CompiledBody> body) {
                    cipher::insert_computation_cache_in_row<&cr_fn_t0, EmptyR, int>(body);
                });
            }},
            std::jthread{[&] {
                worker(::fixy::to(body_at(disjoint_row_bodies, 1)), [](::fixy::Ref<cipher::CompiledBody> body) {
                    cipher::insert_computation_cache_in_row<&cr_fn_t1, EmptyR, int>(body);
                });
            }},
            std::jthread{[&] {
                worker(::fixy::to(body_at(disjoint_row_bodies, 2)), [](::fixy::Ref<cipher::CompiledBody> body) {
                    cipher::insert_computation_cache_in_row<&cr_fn_t2, EmptyR, int>(body);
                });
            }},
            std::jthread{[&] {
                worker(::fixy::to(body_at(disjoint_row_bodies, 3)), [](::fixy::Ref<cipher::CompiledBody> body) {
                    cipher::insert_computation_cache_in_row<&cr_fn_t3, EmptyR, int>(body);
                });
            }},
        };

        while (ready.load(std::memory_order_acquire) < 4) { /* spin */
        }
        go.store(true, std::memory_order_release);
        for (auto& t : workers) {
            t.join();
        }

        EXPECT_TRUE(
            holds(cipher::lookup_computation_cache_in_row<&cr_fn_t0, EmptyR, int>(), body_at(disjoint_row_bodies, 0)));
        EXPECT_TRUE(
            holds(cipher::lookup_computation_cache_in_row<&cr_fn_t1, EmptyR, int>(), body_at(disjoint_row_bodies, 1)));
        EXPECT_TRUE(
            holds(cipher::lookup_computation_cache_in_row<&cr_fn_t2, EmptyR, int>(), body_at(disjoint_row_bodies, 2)));
        EXPECT_TRUE(
            holds(cipher::lookup_computation_cache_in_row<&cr_fn_t3, EmptyR, int>(), body_at(disjoint_row_bodies, 3)));
    });

    // The same-slot race again, on the row-aware slots, for the same
    // reason.
    run_test("concurrent_inserts_same_slot_first_wins_in_row", [] {
        EXPECT_TRUE(cipher::lookup_computation_cache_in_row<&cr_contention_fn, EmptyR, int>().is_none());

        std::atomic<int> ready{0};
        std::atomic<bool> go{false};

        std::array<std::jthread, 4> workers;
        for (std::size_t i = 0; i < 4; ++i) {
            workers[i] = std::jthread{[&, i] {
                ready.fetch_add(1, std::memory_order_release);
                while (!go.load(std::memory_order_acquire)) { /* spin */
                }
                cipher::insert_computation_cache_in_row<&cr_contention_fn, EmptyR, int>(
                    ::fixy::to(body_at(contended_row_bodies, i)));
            }};
        }
        while (ready.load(std::memory_order_acquire) < 4) { /* spin */
        }
        go.store(true, std::memory_order_release);
        for (auto& t : workers) {
            t.join();
        }

        const Slot winner = cipher::lookup_computation_cache_in_row<&cr_contention_fn, EmptyR, int>();
        EXPECT_TRUE(winner.is_some());
        bool is_one_of = false;
        for (cipher::CompiledBody const& candidate : contended_row_bodies) {
            if (holds(winner, candidate)) {
                is_one_of = true;
                break;
            }
        }
        EXPECT_TRUE(is_one_of);
    });

    crucible::test::pass("\ntotal: {} passed, {} failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
