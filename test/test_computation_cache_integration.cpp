// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0
//
// The unit tests for this cache take one property at a time.  The
// scenarios here sequence several of those properties the way a
// dispatcher does, so each one fails on a composition that the unit
// tests would still call correct.  Each scenario says which failure it
// is there to catch.

#include <crucible/cipher/ComputationCache.h>
#include <foundation/effects/Row.h>

#include "test_assert.h"

#include <array>
#include <cstdint>
#include <cstdio>

// The header only declares the body, and a consumer defines it.  The cache
// never reads a body, so an empty body serves: each object has an address
// of its own.
namespace crucible::cipher {
struct CompiledBody {};
}  // namespace crucible::cipher

namespace {

namespace cipher = ::crucible::cipher;
namespace eff = ::foundation::effects;

using EmptyR = eff::Row<>;
using BgR = eff::Row<eff::Effect::Bg>;
using IOR = eff::Row<eff::Effect::IO>;
using BgIOR = eff::Row<eff::Effect::Bg, eff::Effect::IO>;

// Comparing every pair is quadratic, which for sixteen keys is a
// hundred and twenty comparisons, and folds into one assertion rather
// than a hundred and twenty.
template <std::size_t N>
constexpr bool all_pairwise_distinct(const std::array<std::uint64_t, N>& keys) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            if (keys[i] == keys[j]) return false;
        }
    }
    return true;
}

}  // namespace

// These fixtures share no name with any other cache fixture.  The two
// sets of tests run in separate processes and could not collide, but
// distinct names keep a search for one set from turning up the other.
// They stand outside the unnamed namespace, because a key refuses a
// function with internal linkage.
namespace test_computation_cache_integration_functions {
inline void ic_unary(int) noexcept {}
inline void ic_binary(int, double) noexcept {}
inline void ic_void() noexcept {}
inline void ic_pop1(int) noexcept {}
inline void ic_pop2(int) noexcept {}
inline void ic_pop3(int) noexcept {}
inline void ic_pop4(int) noexcept {}
inline void ic_pop5(int) noexcept {}
inline void ic_pop6(int) noexcept {}
inline void ic_pop7(int) noexcept {}
inline void ic_pop8(int) noexcept {}
inline void ic_lifetime(int) noexcept {}
inline void ic_hotloop(int) noexcept {}
}  // namespace test_computation_cache_integration_functions

namespace {

using namespace test_computation_cache_integration_functions;

using Slot = ::fixy::Option<::fixy::Ref<cipher::CompiledBody>>;
using Bodies = ::foundation::core::FixedArray<cipher::CompiledBody, 8>;

// The bodies that the scenarios insert.  Each object lives as long as the
// process, as the slots do.
cipher::CompiledBody fast_path_body{};
cipher::CompiledBody aware_body{};
cipher::CompiledBody blind_body{};
cipher::CompiledBody empty_row_body{};
cipher::CompiledBody bg_row_body{};
cipher::CompiledBody io_row_body{};
Bodies blind_population{};
Bodies aware_population{};
cipher::CompiledBody lifetime_body{};
cipher::CompiledBody hot_body{};

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
    return *bodies.view().get(position).expect("each position of the test is below eight");
}

// Variadic, so that a comma inside a template argument list is not
// taken for a macro argument separator.  The single-argument assertion
// macro cannot express these calls.
#define ASSERT_TRUE(...)                                                                                    \
    do {                                                                                                    \
        if (!(__VA_ARGS__)) {                                                                               \
            std::fprintf(stderr, "    ASSERT_TRUE failed: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            std::abort();                                                                                   \
        }                                                                                                   \
    } while (0)

}  // namespace

int main() {
    std::fprintf(stderr, "test_computation_cache_integration:\n");

    // A dispatcher looks up, branches to the cached body on a hit, and
    // compiles and inserts on a miss.  If a lookup after an insert ever
    // missed, every call would compile again and the compiled bodies
    // would never hold a stable identity.
    {
        std::fprintf(stderr, "  scenario 1 (compile_then_cache_fast_path): ");

        ASSERT_TRUE(cipher::lookup_computation_cache<&ic_unary, int>().is_none());

        cipher::insert_computation_cache<&ic_unary, int>(::fixy::to(fast_path_body));

        // Looking up repeatedly, because a slot that reset itself would
        // pass a single probe.
        for (int i = 0; i < 100; ++i) {
            ASSERT_TRUE(holds(cipher::lookup_computation_cache<&ic_unary, int>(), fast_path_body));
        }

        std::fprintf(stderr, "PASSED\n");
    }

    // A refactor that routed the row-blind and the row-aware lookups to
    // one slot, to save space, would serve a body compiled without any
    // row to a caller that asked for one.  That is the whole of what
    // the row-aware side is for.
    {
        std::fprintf(stderr, "  scenario 2 (row_blind_aware_disjoint): ");

        cipher::insert_computation_cache_in_row<&ic_binary, EmptyR, int, double>(::fixy::to(aware_body));

        ASSERT_TRUE(cipher::lookup_computation_cache<&ic_binary, int, double>().is_none());
        ASSERT_TRUE(holds(cipher::lookup_computation_cache_in_row<&ic_binary, EmptyR, int, double>(), aware_body));

        // With both slots occupied by different bodies, each lookup
        // still finds its own.
        cipher::insert_computation_cache<&ic_binary, int, double>(::fixy::to(blind_body));

        ASSERT_TRUE(holds(cipher::lookup_computation_cache<&ic_binary, int, double>(), blind_body));
        ASSERT_TRUE(holds(cipher::lookup_computation_cache_in_row<&ic_binary, EmptyR, int, double>(), aware_body));
        ASSERT_TRUE(!::fixy::same_object(::fixy::to(blind_body), ::fixy::to(aware_body)));

        std::fprintf(stderr, "PASSED\n");
    }

    // One function under two different rows is one logical operation
    // in two contexts, and the two can compile differently.  Serving
    // either body to the other's caller is the confusion this guards
    // against.
    {
        std::fprintf(stderr, "  scenario 3 (cross_row_body_isolation): ");

        cipher::insert_computation_cache_in_row<&ic_void, EmptyR>(::fixy::to(empty_row_body));
        cipher::insert_computation_cache_in_row<&ic_void, BgR>(::fixy::to(bg_row_body));
        cipher::insert_computation_cache_in_row<&ic_void, IOR>(::fixy::to(io_row_body));

        ASSERT_TRUE(holds(cipher::lookup_computation_cache_in_row<&ic_void, EmptyR>(), empty_row_body));
        ASSERT_TRUE(holds(cipher::lookup_computation_cache_in_row<&ic_void, BgR>(), bg_row_body));
        ASSERT_TRUE(holds(cipher::lookup_computation_cache_in_row<&ic_void, IOR>(), io_row_body));

        // The keys differ too, which is what the slots turn into once
        // they leave this process.
        static_assert(cipher::computation_cache_key_in_row<&ic_void, EmptyR>
                      != cipher::computation_cache_key_in_row<&ic_void, BgR>);
        static_assert(cipher::computation_cache_key_in_row<&ic_void, BgR>
                      != cipher::computation_cache_key_in_row<&ic_void, IOR>);
        static_assert(cipher::computation_cache_key_in_row<&ic_void, EmptyR>
                      != cipher::computation_cache_key_in_row<&ic_void, IOR>);

        std::fprintf(stderr, "PASSED\n");
    }

    // Sixteen live slots at once, across both lookup paths.  The whole
    // design rests on the linker giving each instantiation of an inline
    // template its own object, and a handful of slots would not show a
    // failure of that.
    {
        std::fprintf(stderr, "  scenario 4 (population_disjointness): ");

        cipher::insert_computation_cache<&ic_pop1, int>(::fixy::to(body_at(blind_population, 0)));
        cipher::insert_computation_cache<&ic_pop2, int>(::fixy::to(body_at(blind_population, 1)));
        cipher::insert_computation_cache<&ic_pop3, int>(::fixy::to(body_at(blind_population, 2)));
        cipher::insert_computation_cache<&ic_pop4, int>(::fixy::to(body_at(blind_population, 3)));
        cipher::insert_computation_cache<&ic_pop5, int>(::fixy::to(body_at(blind_population, 4)));
        cipher::insert_computation_cache<&ic_pop6, int>(::fixy::to(body_at(blind_population, 5)));
        cipher::insert_computation_cache<&ic_pop7, int>(::fixy::to(body_at(blind_population, 6)));
        cipher::insert_computation_cache<&ic_pop8, int>(::fixy::to(body_at(blind_population, 7)));

        // The same functions and arguments again, on the row-aware
        // side.
        cipher::insert_computation_cache_in_row<&ic_pop1, EmptyR, int>(::fixy::to(body_at(aware_population, 0)));
        cipher::insert_computation_cache_in_row<&ic_pop2, EmptyR, int>(::fixy::to(body_at(aware_population, 1)));
        cipher::insert_computation_cache_in_row<&ic_pop3, EmptyR, int>(::fixy::to(body_at(aware_population, 2)));
        cipher::insert_computation_cache_in_row<&ic_pop4, EmptyR, int>(::fixy::to(body_at(aware_population, 3)));
        cipher::insert_computation_cache_in_row<&ic_pop5, EmptyR, int>(::fixy::to(body_at(aware_population, 4)));
        cipher::insert_computation_cache_in_row<&ic_pop6, EmptyR, int>(::fixy::to(body_at(aware_population, 5)));
        cipher::insert_computation_cache_in_row<&ic_pop7, EmptyR, int>(::fixy::to(body_at(aware_population, 6)));
        cipher::insert_computation_cache_in_row<&ic_pop8, EmptyR, int>(::fixy::to(body_at(aware_population, 7)));

        ASSERT_TRUE(holds(cipher::lookup_computation_cache<&ic_pop1, int>(), body_at(blind_population, 0)));
        ASSERT_TRUE(holds(cipher::lookup_computation_cache<&ic_pop2, int>(), body_at(blind_population, 1)));
        ASSERT_TRUE(holds(cipher::lookup_computation_cache<&ic_pop3, int>(), body_at(blind_population, 2)));
        ASSERT_TRUE(holds(cipher::lookup_computation_cache<&ic_pop4, int>(), body_at(blind_population, 3)));
        ASSERT_TRUE(holds(cipher::lookup_computation_cache<&ic_pop5, int>(), body_at(blind_population, 4)));
        ASSERT_TRUE(holds(cipher::lookup_computation_cache<&ic_pop6, int>(), body_at(blind_population, 5)));
        ASSERT_TRUE(holds(cipher::lookup_computation_cache<&ic_pop7, int>(), body_at(blind_population, 6)));
        ASSERT_TRUE(holds(cipher::lookup_computation_cache<&ic_pop8, int>(), body_at(blind_population, 7)));

        ASSERT_TRUE(
            holds(cipher::lookup_computation_cache_in_row<&ic_pop1, EmptyR, int>(), body_at(aware_population, 0)));
        ASSERT_TRUE(
            holds(cipher::lookup_computation_cache_in_row<&ic_pop2, EmptyR, int>(), body_at(aware_population, 1)));
        ASSERT_TRUE(
            holds(cipher::lookup_computation_cache_in_row<&ic_pop3, EmptyR, int>(), body_at(aware_population, 2)));
        ASSERT_TRUE(
            holds(cipher::lookup_computation_cache_in_row<&ic_pop4, EmptyR, int>(), body_at(aware_population, 3)));
        ASSERT_TRUE(
            holds(cipher::lookup_computation_cache_in_row<&ic_pop5, EmptyR, int>(), body_at(aware_population, 4)));
        ASSERT_TRUE(
            holds(cipher::lookup_computation_cache_in_row<&ic_pop6, EmptyR, int>(), body_at(aware_population, 5)));
        ASSERT_TRUE(
            holds(cipher::lookup_computation_cache_in_row<&ic_pop7, EmptyR, int>(), body_at(aware_population, 6)));
        ASSERT_TRUE(
            holds(cipher::lookup_computation_cache_in_row<&ic_pop8, EmptyR, int>(), body_at(aware_population, 7)));

        // Distinct bodies are not enough.  Two slots that held
        // distinct pointers but shared a key would owe that only to
        // each having its own static, and would alias one another the
        // moment the keys rather than the pointers travelled.
        constexpr std::array<std::uint64_t, 16> all_keys = {
            cipher::computation_cache_key<&ic_pop1, int>,
            cipher::computation_cache_key<&ic_pop2, int>,
            cipher::computation_cache_key<&ic_pop3, int>,
            cipher::computation_cache_key<&ic_pop4, int>,
            cipher::computation_cache_key<&ic_pop5, int>,
            cipher::computation_cache_key<&ic_pop6, int>,
            cipher::computation_cache_key<&ic_pop7, int>,
            cipher::computation_cache_key<&ic_pop8, int>,
            cipher::computation_cache_key_in_row<&ic_pop1, EmptyR, int>,
            cipher::computation_cache_key_in_row<&ic_pop2, EmptyR, int>,
            cipher::computation_cache_key_in_row<&ic_pop3, EmptyR, int>,
            cipher::computation_cache_key_in_row<&ic_pop4, EmptyR, int>,
            cipher::computation_cache_key_in_row<&ic_pop5, EmptyR, int>,
            cipher::computation_cache_key_in_row<&ic_pop6, EmptyR, int>,
            cipher::computation_cache_key_in_row<&ic_pop7, EmptyR, int>,
            cipher::computation_cache_key_in_row<&ic_pop8, EmptyR, int>,
        };
        static_assert(all_pairwise_distinct(all_keys), "All 16 (FnPtr, Args, [Row]) federation keys must be "
                                                       "pairwise distinct.  Distinct body pointers are not enough, "
                                                       "because what travels between processes is the key.");
        volatile bool runtime_distinct = all_pairwise_distinct(all_keys);
        ASSERT_TRUE(runtime_distinct);

        std::fprintf(stderr, "PASSED\n");
    }

    // The slots have static storage duration and so last as long as
    // the process.  A refactor to thread-local storage, or to a
    // function-local static that re-initialises on each call, would
    // break the promise that an insert in one scope is visible in the
    // next.  The two blocks below share no variable but the body, which
    // the second block only compares against.
    {
        std::fprintf(stderr, "  scenario 5a (lifetime_persist_insert): ");
        cipher::insert_computation_cache<&ic_lifetime, int>(::fixy::to(lifetime_body));
        ASSERT_TRUE(holds(cipher::lookup_computation_cache<&ic_lifetime, int>(), lifetime_body));
        std::fprintf(stderr, "PASSED\n");
    }
    {
        std::fprintf(stderr, "  scenario 5b (lifetime_persist_lookup): ");
        const Slot recovered = cipher::lookup_computation_cache<&ic_lifetime, int>();
        ASSERT_TRUE(recovered.is_some());
        ASSERT_TRUE(holds(recovered, lifetime_body));
        std::fprintf(stderr, "PASSED\n");
    }

    // The key is a compile-time computation and also a cross-process
    // identifier.  A call site that saw a different value than the
    // assertion did would miss silently, so each key is read once at
    // compile time and once at run time and the two are compared.
    {
        std::fprintf(stderr, "  scenario 6 (federation_key_consteval_runtime_eq): ");

        constexpr std::uint64_t key_blind_consteval = cipher::computation_cache_key<&ic_unary, int>;
        constexpr std::uint64_t key_aware_consteval = cipher::computation_cache_key_in_row<&ic_unary, EmptyR, int>;

        // Volatile, or the optimizer folds the run-time read back into
        // the compile-time one and the comparison proves nothing.
        volatile std::uint64_t key_blind_runtime = cipher::computation_cache_key<&ic_unary, int>;
        volatile std::uint64_t key_aware_runtime = cipher::computation_cache_key_in_row<&ic_unary, EmptyR, int>;

        ASSERT_TRUE(key_blind_runtime == key_blind_consteval);
        ASSERT_TRUE(key_aware_runtime == key_aware_consteval);
        static_assert(cipher::computation_cache_key<&ic_unary, int>
                      != cipher::computation_cache_key_in_row<&ic_unary, EmptyR, int>);
        // A zero key would match an uninitialised slot.
        ASSERT_TRUE(key_blind_consteval != 0);
        ASSERT_TRUE(key_aware_consteval != 0);

        // An empty row, a row of one effect and a row of two each take
        // a different edge through the fold: no atoms, one atom, and a
        // sort that actually has something to sort.  Each needs its own
        // comparison.
        constexpr std::uint64_t key_bg_consteval = cipher::computation_cache_key_in_row<&ic_unary, BgR, int>;
        constexpr std::uint64_t key_bgio_consteval = cipher::computation_cache_key_in_row<&ic_unary, BgIOR, int>;

        volatile std::uint64_t key_bg_runtime = cipher::computation_cache_key_in_row<&ic_unary, BgR, int>;
        volatile std::uint64_t key_bgio_runtime = cipher::computation_cache_key_in_row<&ic_unary, BgIOR, int>;

        ASSERT_TRUE(key_bg_runtime == key_bg_consteval);
        ASSERT_TRUE(key_bgio_runtime == key_bgio_consteval);

        // Four shapes for one function and argument list: no row at
        // all, and rows of zero, one and two effects.
        constexpr std::array<std::uint64_t, 4> row_shape_keys = {
            key_blind_consteval,
            key_aware_consteval,
            key_bg_consteval,
            key_bgio_consteval,
        };
        static_assert(all_pairwise_distinct(row_shape_keys),
                      "Row-blind, row-aware-Empty, row-aware-Bg and row-aware-BgIO "
                      "must produce four distinct federation keys.  Each row shape "
                      "takes a different edge through the row-hash fold.");

        ASSERT_TRUE(key_bg_consteval != 0);
        ASSERT_TRUE(key_bgio_consteval != 0);

        std::fprintf(stderr, "PASSED\n");
    }

    // Once the cache is warm every lookup in the dispatcher's loop
    // must hit.  A single miss among a thousand would mean the slot was
    // reset or evicted, or that the lookup keeps state of its own.
    {
        std::fprintf(stderr, "  scenario 7 (hot_loop_hit_rate): ");

        cipher::insert_computation_cache<&ic_hotloop, int>(::fixy::to(hot_body));

        for (int i = 0; i < 1024; ++i) {
            ASSERT_TRUE(holds(cipher::lookup_computation_cache<&ic_hotloop, int>(), hot_body));
        }

        std::fprintf(stderr, "PASSED\n");
    }

    crucible::test::pass("\n8 scenarios passed.\n");
    return 0;
}
