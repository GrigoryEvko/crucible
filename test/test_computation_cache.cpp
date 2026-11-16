// The smoke test below runs first.  Every test after it uses function
// and argument tuples the smoke test never touches, so the two sets of
// slots are independent and running them in sequence cannot corrupt
// each other.
//
// Several tests here do depend on each other's slot state, and each
// says so where it does.  main runs them in the order of this file.

#include <crucible/cipher/ComputationCache.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>

namespace {

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
using ::fixy::check::pass;

using Slot = ::fixy::Option<::fixy::Ref<cipher::CompiledBody>>;
using Bodies = ::foundation::core::FixedArray<cipher::CompiledBody, 4>;

// One function under these rows is one slot for each row.
namespace eff = ::foundation::effects;
using EmptyR = eff::Row<>;
using BgR = eff::Row<eff::Effect::Bg>;
using IOR = eff::Row<eff::Effect::IO>;
using BgIOR = eff::Row<eff::Effect::Bg, eff::Effect::IO>;

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

// The smoke test runs once per process and reports false thereafter.  All
// three calls live in one body on purpose: as separate sub-tests a
// reordering would make the second call the first invocation and quietly
// swap the expected results.
void test_smoke_test_then_guard_rejects_reinvocation(::fixy::TestRunnerCtx const& test) {
    CRUCIBLE_TEST_ASSERT(test, computation_cache_smoke_test() == true);
    CRUCIBLE_TEST_ASSERT(test, computation_cache_smoke_test() == false);
    CRUCIBLE_TEST_ASSERT(test, computation_cache_smoke_test() == false);
}

void test_lookup_before_insert_misses(::fixy::TestRunnerCtx const& test) {
    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache<&test_fn_a, int>().is_none());
    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache<&test_fn_b, int, double>().is_none());
    // An empty argument pack is a valid slot.
    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache<&test_fn_d>().is_none());
}

void test_insert_then_lookup_roundtrips(::fixy::TestRunnerCtx const& test) {
    cipher::insert_computation_cache<&test_fn_a, int>(::fixy::to(round_trip_body));
    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache<&test_fn_a, int>(), round_trip_body));
}

void test_second_insert_is_idempotent(::fixy::TestRunnerCtx const& test) {
    // The round-trip test above filled this slot, and a second
    // insert must not displace what it put there.
    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache<&test_fn_a, int>(), round_trip_body));

    cipher::insert_computation_cache<&test_fn_a, int>(::fixy::to(overwrite_body));

    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache<&test_fn_a, int>(), round_trip_body));
    CRUCIBLE_TEST_ASSERT(test, !holds(cipher::lookup_computation_cache<&test_fn_a, int>(), overwrite_body));
}

void test_distinct_instantiations_are_isolated(::fixy::TestRunnerCtx const& test) {
    // Changing either the function or the argument pack names a
    // different slot, so each of these still misses.
    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache<&test_fn_a, float>().is_none());

    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache<&test_fn_b, int, double>().is_none());

    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache<&test_fn_c, int>().is_none());

    cipher::insert_computation_cache<&test_fn_b, int, double>(::fixy::to(isolated_body));
    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache<&test_fn_b, int, double>(), isolated_body));

    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache<&test_fn_a, int>(), round_trip_body));
}

void test_cache_key_order_sensitive(::fixy::TestRunnerCtx const& test) {
    // The keys are compile-time values; comparing them at run time
    // gives the assertion a failure message.
    constexpr std::uint64_t k_int_double = cipher::computation_cache_key<&test_fn_b, int, double>;
    constexpr std::uint64_t k_double_int = cipher::computation_cache_key<&test_fn_b, double, int>;
    CRUCIBLE_TEST_ASSERT(test, k_int_double != k_double_int);
    CRUCIBLE_TEST_ASSERT(test, k_int_double != 0);
    CRUCIBLE_TEST_ASSERT(test, k_double_int != 0);
}

void test_cache_key_is_deterministic(::fixy::TestRunnerCtx const& test) {
    constexpr std::uint64_t k1 = cipher::computation_cache_key<&test_fn_a, int>;
    constexpr std::uint64_t k2 = cipher::computation_cache_key<&test_fn_a, int>;
    CRUCIBLE_TEST_ASSERT(test, k1 == k2);
}

void test_same_signature_different_name_distinct_keys(::fixy::TestRunnerCtx const& test) {
    constexpr std::uint64_t k_d = cipher::computation_cache_key<&test_fn_d>;
    constexpr std::uint64_t k_e_zeta_void = cipher::computation_cache_key<&hd_fn_alpha, int>;
    CRUCIBLE_TEST_ASSERT(test, k_d != 0);
    CRUCIBLE_TEST_ASSERT(test, k_d != k_e_zeta_void);
}

void test_hash_distribution_sanity(::fixy::TestRunnerCtx const& test) {
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
        CRUCIBLE_TEST_ASSERT(test, keys[i] != 0);
        for (std::size_t j = i + 1; j < keys.size(); ++j) {
            CRUCIBLE_TEST_ASSERT(test, keys[i] != keys[j]);
        }
    }
}

// Four threads, four slots, no overlap.  Distinct instantiations
// hold distinct atomics, so there is nothing for them to race on,
// and every slot must end up holding its own thread's body.
void test_concurrent_inserts_disjoint_slots(::fixy::TestRunnerCtx const& test) {
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

    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache<&ct_fn_t0, int>(), body_at(disjoint_bodies, 0)));
    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache<&ct_fn_t1, int>(), body_at(disjoint_bodies, 1)));
    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache<&ct_fn_t2, int>(), body_at(disjoint_bodies, 2)));
    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache<&ct_fn_t3, int>(), body_at(disjoint_bodies, 3)));
}

// Four threads, one slot, four different bodies.  One of them
// wins and the other three find the slot taken and do nothing, so
// the slot ends up holding one of the four and no hybrid of them.
// The isolation test above left this slot empty.
void test_concurrent_inserts_same_slot_first_wins(::fixy::TestRunnerCtx const& test) {
    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache<&test_fn_c, int>().is_none());

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
    CRUCIBLE_TEST_ASSERT(test, winner.is_some());
    bool is_one_of = false;
    for (cipher::CompiledBody const& candidate : contended_bodies) {
        if (holds(winner, candidate)) {
            is_one_of = true;
            break;
        }
    }
    CRUCIBLE_TEST_ASSERT(test, is_one_of);
}

// One function under three different rows is three slots.  A
// dispatcher that aliased compiled bodies across effect rows would
// run a body compiled for one row in another.
void test_row_aware_slots_are_isolated_by_row(::fixy::TestRunnerCtx const& test) {
    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache_in_row<&test_fn_a, EmptyR, int>().is_none());
    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache_in_row<&test_fn_a, BgR, int>().is_none());
    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache_in_row<&test_fn_a, IOR, int>().is_none());

    cipher::insert_computation_cache_in_row<&test_fn_a, EmptyR, int>(::fixy::to(empty_row_body));

    CRUCIBLE_TEST_ASSERT(test,
                         holds(cipher::lookup_computation_cache_in_row<&test_fn_a, EmptyR, int>(), empty_row_body));
    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache_in_row<&test_fn_a, BgR, int>().is_none());
    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache_in_row<&test_fn_a, IOR, int>().is_none());
}

// An empty row is still a row: the row-aware slot for a function
// and argument pack is not the row-blind slot for the same pair.
// The row-blind side is already populated by an earlier test, so
// only the row-aware side can still witness a miss here.
void test_row_blind_and_row_aware_slots_disjoint(::fixy::TestRunnerCtx const& test) {
    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache_in_row<&test_fn_b, EmptyR, int, double>().is_none());

    cipher::insert_computation_cache_in_row<&test_fn_b, EmptyR, int, double>(::fixy::to(blind_pair_body));

    CRUCIBLE_TEST_ASSERT(
        test, holds(cipher::lookup_computation_cache_in_row<&test_fn_b, EmptyR, int, double>(), blind_pair_body));
}

// Two orderings of the same row are one row as far as the key is
// concerned, because the hash sorts before folding.  They are two
// distinct C++ types as far as the slots are concerned, because
// each type instantiates its own atomic.  Both hold at once: slot
// identity is type-driven and the key that travels is hash-driven.
void test_row_aware_slot_id_is_type_driven_not_hash_driven(::fixy::TestRunnerCtx const& test) {
    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache_in_row<&test_fn_a, BgIOR, int>().is_none());
    // The two orderings agree on the key.
    CRUCIBLE_TEST_ASSERT(
        test, cipher::computation_cache_key_in_row<&test_fn_a, BgIOR, int>
                  == cipher::computation_cache_key_in_row<&test_fn_a, eff::Row<eff::Effect::IO, eff::Effect::Bg>, int>);
    cipher::insert_computation_cache_in_row<&test_fn_a, BgIOR, int>(::fixy::to(bg_io_body));
    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache_in_row<&test_fn_a, BgIOR, int>(), bg_io_body));
    // A distinct type is a distinct slot.
    CRUCIBLE_TEST_ASSERT(
        test, cipher::lookup_computation_cache_in_row<&test_fn_a, eff::Row<eff::Effect::IO, eff::Effect::Bg>, int>()
                  .is_none());
}

// The disjoint-slot race again, on the row-aware slots.  Both
// families compile to the same shape, so this adds no new
// argument, but it does catch a refactor that weakens the memory
// ordering on one family and not the other.
void test_concurrent_inserts_disjoint_slots_in_row(::fixy::TestRunnerCtx const& test) {
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

    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache_in_row<&cr_fn_t0, EmptyR, int>(),
                                     body_at(disjoint_row_bodies, 0)));
    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache_in_row<&cr_fn_t1, EmptyR, int>(),
                                     body_at(disjoint_row_bodies, 1)));
    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache_in_row<&cr_fn_t2, EmptyR, int>(),
                                     body_at(disjoint_row_bodies, 2)));
    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache_in_row<&cr_fn_t3, EmptyR, int>(),
                                     body_at(disjoint_row_bodies, 3)));
}

// The same-slot race again, on the row-aware slots, for the same
// reason.
void test_concurrent_inserts_same_slot_first_wins_in_row(::fixy::TestRunnerCtx const& test) {
    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache_in_row<&cr_contention_fn, EmptyR, int>().is_none());

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
    CRUCIBLE_TEST_ASSERT(test, winner.is_some());
    bool is_one_of = false;
    for (cipher::CompiledBody const& candidate : contended_row_bodies) {
        if (holds(winner, candidate)) {
            is_one_of = true;
            break;
        }
    }
    CRUCIBLE_TEST_ASSERT(test, is_one_of);
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    pass(test, "test_computation_cache:\n");
    test_smoke_test_then_guard_rejects_reinvocation(test);
    pass(test, "  smoke_test_then_guard_rejects_reinvocation: PASSED\n");
    test_lookup_before_insert_misses(test);
    pass(test, "  lookup_before_insert_misses: PASSED\n");
    test_insert_then_lookup_roundtrips(test);
    pass(test, "  insert_then_lookup_roundtrips: PASSED\n");
    test_second_insert_is_idempotent(test);
    pass(test, "  second_insert_is_idempotent: PASSED\n");
    test_distinct_instantiations_are_isolated(test);
    pass(test, "  distinct_instantiations_are_isolated: PASSED\n");
    test_cache_key_order_sensitive(test);
    pass(test, "  cache_key_order_sensitive: PASSED\n");
    test_cache_key_is_deterministic(test);
    pass(test, "  cache_key_is_deterministic: PASSED\n");
    test_same_signature_different_name_distinct_keys(test);
    pass(test, "  same_signature_different_name_distinct_keys: PASSED\n");
    test_hash_distribution_sanity(test);
    pass(test, "  hash_distribution_sanity: PASSED\n");
    test_concurrent_inserts_disjoint_slots(test);
    pass(test, "  concurrent_inserts_disjoint_slots: PASSED\n");
    test_concurrent_inserts_same_slot_first_wins(test);
    pass(test, "  concurrent_inserts_same_slot_first_wins: PASSED\n");
    test_row_aware_slots_are_isolated_by_row(test);
    pass(test, "  row_aware_slots_are_isolated_by_row: PASSED\n");
    test_row_blind_and_row_aware_slots_disjoint(test);
    pass(test, "  row_blind_and_row_aware_slots_disjoint: PASSED\n");
    test_row_aware_slot_id_is_type_driven_not_hash_driven(test);
    pass(test, "  row_aware_slot_id_is_type_driven_not_hash_driven: PASSED\n");
    test_concurrent_inserts_disjoint_slots_in_row(test);
    pass(test, "  concurrent_inserts_disjoint_slots_in_row: PASSED\n");
    test_concurrent_inserts_same_slot_first_wins_in_row(test);
    pass(test, "  concurrent_inserts_same_slot_first_wins_in_row: PASSED\n");
    pass(test, "test_computation_cache: ALL PASSED\n");
    return 0;
}
