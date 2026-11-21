// The smoke test below runs first.  Every test after it uses function
// and argument tuples the smoke test never touches, so the two sets of
// slots are independent and running them in sequence cannot corrupt
// each other.
//
// Several tests here do depend on each other's slot state, and each
// says so where it does.  main runs them in the order of this file.

#include <crucible/cipher/ComputationCache.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

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

// The number of the workers of each race, and the bodies that they insert.
constexpr std::uint32_t kWorkerCount = 4;
using Bodies = ::fixy::View<cipher::CompiledBody, kWorkerCount>;
using BodyIndex = ::fixy::Index<kWorkerCount>;

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

// The bodies of the races are runs of this arena, which lives as long as the
// process.
auto g_test = ::foundation::effects::testing::test();
::fixy::BumpArena g_runs{4096};

// A run of one body for each worker of a race.
[[nodiscard]] Bodies make_bodies() noexcept {
    return g_runs.array(g_test.alloc, kWorkerCount, cipher::CompiledBody{})
        .window<kWorkerCount>(0)
        .expect("the run holds one body for each worker");
}

Bodies const disjoint_bodies = make_bodies();
Bodies const contended_bodies = make_bodies();
Bodies const disjoint_row_bodies = make_bodies();
Bodies const contended_row_bodies = make_bodies();

// True when the slot holds body.
[[nodiscard]] bool holds(Slot slot, cipher::CompiledBody const& body) noexcept {
    return std::move(slot).match(
        [&body](::fixy::Ref<cipher::CompiledBody> found) noexcept {
            return ::fixy::same_object(found, ::fixy::to(body));
        },
        []() noexcept { return false; });
}

// The body at position of bodies.
[[nodiscard]] cipher::CompiledBody& body_at(Bodies bodies, BodyIndex position) noexcept { return bodies.at(position); }

// The Ref of a body on the erased brand.  A slot of the cache holds the
// bodies of each site, so an insert takes a Ref on the erased brand.
[[nodiscard]] ::fixy::Ref<cipher::CompiledBody> lend_erased(cipher::CompiledBody& body) noexcept {
    return ::fixy::erase_brand(::fixy::to(body));
}

// The gate of one race.  Each worker counts itself ready and spins until the
// opener opens the gate.  The opener waits until each worker is ready, so
// the inserts of the workers overlap.
struct RaceGate {
    ::fixy::Atomic<std::uint32_t> ready{0};
    ::fixy::Atomic<bool> go{false};
};

// The part of a worker before its insert.
void wait_at(RaceGate& gate) noexcept {
    static_cast<void>(gate.ready.fetch_add_acq_rel(1));
    while (!gate.go.load_acquire()) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// The body of the opener: it holds the gate until each worker is ready, and
// then it releases the workers together.
void open_when_ready(RaceGate& gate) noexcept {
    while (gate.ready.load_acquire() < kWorkerCount) {
        CRUCIBLE_SPIN_PAUSE;
    }
    gate.go.store_release(true);
}

// Runs the opener and the four workers of one race, each on a thread of its
// own, and joins each thread.  The bodies share the gate and the slots of the
// cache on purpose, so the race goes through the raw probe of the thread door.
template <typename... Workers>
    requires(sizeof...(Workers) == kWorkerCount)
void run_race(::fixy::TestRunnerCtx const& test, RaceGate& gate, Workers&... workers) noexcept {
    auto opener = [&gate] noexcept { open_when_ready(gate); };
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    ::fixy::thread::run_joined_raw_probe(ctx, test,
                                         "the workers of a race of the cache share its gate and its slots on purpose",
                                         ::fixy::thread::ThreadOptions{}, opener, workers...)
        .expect("the thread door starts the opener and each worker of the race");
}

// This runs once per process and refuses to run twice. The slots it
// reads are program-lifetime globals, so the checks that a lookup
// misses before its insert hold on the first call only. A second call
// would find those slots already full, so it reports failure rather
// than let a miss quietly become a hit.
bool computation_cache_smoke_test() noexcept {
    static ::fixy::Atomic<std::uint32_t> call_counter{0};
    if (call_counter.fetch_add_acq_rel(1) > 0) {
        return false;
    }

    bool ok = true;

    ok = ok && cipher::lookup_computation_cache<&p_unary, int>().is_none();
    ok = ok && cipher::lookup_computation_cache<&p_unary, float>().is_none();
    ok = ok && cipher::lookup_computation_cache<&p_binary, int, double>().is_none();

    cipher::insert_computation_cache<&p_unary, int>(lend_erased(smoke_body_a));
    ok = ok && holds(cipher::lookup_computation_cache<&p_unary, int>(), smoke_body_a);

    cipher::insert_computation_cache<&p_unary, int>(lend_erased(smoke_body_b));
    ok = ok && holds(cipher::lookup_computation_cache<&p_unary, int>(), smoke_body_a);

    ok = ok && cipher::lookup_computation_cache<&p_unary, float>().is_none();
    ok = ok && cipher::lookup_computation_cache<&p_binary, int, double>().is_none();

    // An empty argument pack must still reach a slot of its own, and
    // must still instantiate as a template rather than collapse onto
    // a plain function.
    ok = ok && cipher::lookup_computation_cache<&p_void>().is_none();
    cipher::insert_computation_cache<&p_void>(lend_erased(smoke_body_c));
    ok = ok && holds(cipher::lookup_computation_cache<&p_void>(), smoke_body_c);
    ok = ok && holds(cipher::lookup_computation_cache<&p_unary, int>(), smoke_body_a);

    // Only the throwing slot is filled. The noexcept one must still
    // miss, because the two function-pointer types are distinct and
    // must not be folded onto one symbol.
    ok = ok && cipher::lookup_computation_cache<&p_throwing, int>().is_none();
    ok = ok && cipher::lookup_computation_cache<&p_noexcept, int>().is_none();
    cipher::insert_computation_cache<&p_throwing, int>(lend_erased(smoke_body_d));
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
    cipher::insert_computation_cache<&test_fn_a, int>(lend_erased(round_trip_body));
    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache<&test_fn_a, int>(), round_trip_body));
}

void test_second_insert_is_idempotent(::fixy::TestRunnerCtx const& test) {
    // The round-trip test above filled this slot, and a second
    // insert must not displace what it put there.
    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache<&test_fn_a, int>(), round_trip_body));

    cipher::insert_computation_cache<&test_fn_a, int>(lend_erased(overwrite_body));

    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache<&test_fn_a, int>(), round_trip_body));
    CRUCIBLE_TEST_ASSERT(test, !holds(cipher::lookup_computation_cache<&test_fn_a, int>(), overwrite_body));
}

void test_distinct_instantiations_are_isolated(::fixy::TestRunnerCtx const& test) {
    // Changing either the function or the argument pack names a
    // different slot, so each of these still misses.
    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache<&test_fn_a, float>().is_none());

    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache<&test_fn_b, int, double>().is_none());

    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache<&test_fn_c, int>().is_none());

    cipher::insert_computation_cache<&test_fn_b, int, double>(lend_erased(isolated_body));
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
    constexpr ::fixy::FixedArray<std::uint64_t, 9> keys{
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
    for (::fixy::Index<9> const i : keys.indices()) {
        CRUCIBLE_TEST_ASSERT(test, keys.at(i) != 0);
        for (::fixy::Index<9> const j : keys.indices()) {
            if (j.raw() <= i.raw()) continue;
            CRUCIBLE_TEST_ASSERT(test, keys.at(i) != keys.at(j));
        }
    }
}

// Four threads, four slots, no overlap.  Distinct instantiations
// hold distinct atomics, so there is nothing for them to race on,
// and every slot must end up holding its own thread's body.
void test_concurrent_inserts_disjoint_slots(::fixy::TestRunnerCtx const& test) {
    RaceGate gate{};

    // The instantiation is in the body of each worker, so each worker
    // reaches a different slot.
    auto worker_0 = [&gate] noexcept {
        wait_at(gate);
        cipher::insert_computation_cache<&ct_fn_t0, int>(lend_erased(body_at(disjoint_bodies, ::fixy::index<0>())));
    };
    auto worker_1 = [&gate] noexcept {
        wait_at(gate);
        cipher::insert_computation_cache<&ct_fn_t1, int>(lend_erased(body_at(disjoint_bodies, ::fixy::index<1>())));
    };
    auto worker_2 = [&gate] noexcept {
        wait_at(gate);
        cipher::insert_computation_cache<&ct_fn_t2, int>(lend_erased(body_at(disjoint_bodies, ::fixy::index<2>())));
    };
    auto worker_3 = [&gate] noexcept {
        wait_at(gate);
        cipher::insert_computation_cache<&ct_fn_t3, int>(lend_erased(body_at(disjoint_bodies, ::fixy::index<3>())));
    };

    // Hold every thread at the gate and release them together, so
    // the inserts actually overlap.
    run_race(test, gate, worker_0, worker_1, worker_2, worker_3);

    CRUCIBLE_TEST_ASSERT(
        test, holds(cipher::lookup_computation_cache<&ct_fn_t0, int>(), body_at(disjoint_bodies, ::fixy::index<0>())));
    CRUCIBLE_TEST_ASSERT(
        test, holds(cipher::lookup_computation_cache<&ct_fn_t1, int>(), body_at(disjoint_bodies, ::fixy::index<1>())));
    CRUCIBLE_TEST_ASSERT(
        test, holds(cipher::lookup_computation_cache<&ct_fn_t2, int>(), body_at(disjoint_bodies, ::fixy::index<2>())));
    CRUCIBLE_TEST_ASSERT(
        test, holds(cipher::lookup_computation_cache<&ct_fn_t3, int>(), body_at(disjoint_bodies, ::fixy::index<3>())));
}

// Four threads, one slot, four different bodies.  One of them
// wins and the other three find the slot taken and do nothing, so
// the slot ends up holding one of the four and no hybrid of them.
// The isolation test above left this slot empty.
void test_concurrent_inserts_same_slot_first_wins(::fixy::TestRunnerCtx const& test) {
    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache<&test_fn_c, int>().is_none());

    RaceGate gate{};
    auto contender_of = [&gate](BodyIndex position) noexcept {
        return [&gate, position] noexcept {
            wait_at(gate);
            cipher::insert_computation_cache<&test_fn_c, int>(lend_erased(body_at(contended_bodies, position)));
        };
    };
    auto contender_0 = contender_of(::fixy::index<0>());
    auto contender_1 = contender_of(::fixy::index<1>());
    auto contender_2 = contender_of(::fixy::index<2>());
    auto contender_3 = contender_of(::fixy::index<3>());
    run_race(test, gate, contender_0, contender_1, contender_2, contender_3);

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

    cipher::insert_computation_cache_in_row<&test_fn_a, EmptyR, int>(lend_erased(empty_row_body));

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

    cipher::insert_computation_cache_in_row<&test_fn_b, EmptyR, int, double>(lend_erased(blind_pair_body));

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
    cipher::insert_computation_cache_in_row<&test_fn_a, BgIOR, int>(lend_erased(bg_io_body));
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
    RaceGate gate{};

    auto worker_0 = [&gate] noexcept {
        wait_at(gate);
        cipher::insert_computation_cache_in_row<&cr_fn_t0, EmptyR, int>(
            lend_erased(body_at(disjoint_row_bodies, ::fixy::index<0>())));
    };
    auto worker_1 = [&gate] noexcept {
        wait_at(gate);
        cipher::insert_computation_cache_in_row<&cr_fn_t1, EmptyR, int>(
            lend_erased(body_at(disjoint_row_bodies, ::fixy::index<1>())));
    };
    auto worker_2 = [&gate] noexcept {
        wait_at(gate);
        cipher::insert_computation_cache_in_row<&cr_fn_t2, EmptyR, int>(
            lend_erased(body_at(disjoint_row_bodies, ::fixy::index<2>())));
    };
    auto worker_3 = [&gate] noexcept {
        wait_at(gate);
        cipher::insert_computation_cache_in_row<&cr_fn_t3, EmptyR, int>(
            lend_erased(body_at(disjoint_row_bodies, ::fixy::index<3>())));
    };
    run_race(test, gate, worker_0, worker_1, worker_2, worker_3);

    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache_in_row<&cr_fn_t0, EmptyR, int>(),
                                     body_at(disjoint_row_bodies, ::fixy::index<0>())));
    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache_in_row<&cr_fn_t1, EmptyR, int>(),
                                     body_at(disjoint_row_bodies, ::fixy::index<1>())));
    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache_in_row<&cr_fn_t2, EmptyR, int>(),
                                     body_at(disjoint_row_bodies, ::fixy::index<2>())));
    CRUCIBLE_TEST_ASSERT(test, holds(cipher::lookup_computation_cache_in_row<&cr_fn_t3, EmptyR, int>(),
                                     body_at(disjoint_row_bodies, ::fixy::index<3>())));
}

// The same-slot race again, on the row-aware slots, for the same
// reason.
void test_concurrent_inserts_same_slot_first_wins_in_row(::fixy::TestRunnerCtx const& test) {
    CRUCIBLE_TEST_ASSERT(test, cipher::lookup_computation_cache_in_row<&cr_contention_fn, EmptyR, int>().is_none());

    RaceGate gate{};
    auto contender_of = [&gate](BodyIndex position) noexcept {
        return [&gate, position] noexcept {
            wait_at(gate);
            cipher::insert_computation_cache_in_row<&cr_contention_fn, EmptyR, int>(
                lend_erased(body_at(contended_row_bodies, position)));
        };
    };
    auto contender_0 = contender_of(::fixy::index<0>());
    auto contender_1 = contender_of(::fixy::index<1>());
    auto contender_2 = contender_of(::fixy::index<2>());
    auto contender_3 = contender_of(::fixy::index<3>());
    run_race(test, gate, contender_0, contender_1, contender_2, contender_3);

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
