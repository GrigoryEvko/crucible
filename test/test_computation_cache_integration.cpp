// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0
//
// The unit tests for this cache take one property at a time.  The
// scenarios here sequence several of those properties the way a
// dispatcher does, so each one fails on a composition that the unit
// tests would still call correct.  Each scenario says which failure it
// is there to catch.

#include <crucible/cipher/ComputationCache.h>
#include <crucible/effects/EffectRow.h>

#include "test_assert.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>

namespace {

namespace cipher = ::crucible::cipher;
namespace eff = ::crucible::effects;

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

// These fixtures share no name with any other cache fixture.  The two
// sets of tests run in separate processes and could not collide, but
// distinct names keep a search for one set from turning up the other.
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

// The cache never dereferences a body, so a bare bit pattern serves.
inline cipher::CompiledBody* mk_body(std::uintptr_t v) noexcept { return std::bit_cast<cipher::CompiledBody*>(v); }

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

        auto* cold = cipher::lookup_computation_cache<&ic_unary, int>();
        ASSERT_TRUE(cold == nullptr);

        auto* body = mk_body(0x100001);
        cipher::insert_computation_cache<&ic_unary, int>(body);

        // Looking up repeatedly, because a slot that reset itself would
        // pass a single probe.
        for (int i = 0; i < 100; ++i) {
            auto* probe = cipher::lookup_computation_cache<&ic_unary, int>();
            ASSERT_TRUE(probe == body);
        }

        std::fprintf(stderr, "PASSED\n");
    }

    // A refactor that routed the row-blind and the row-aware lookups to
    // one slot, to save space, would serve a body compiled without any
    // row to a caller that asked for one.  That is the whole of what
    // the row-aware side is for.
    {
        std::fprintf(stderr, "  scenario 2 (row_blind_aware_disjoint): ");

        auto* body_aware = mk_body(0x200001);
        cipher::insert_computation_cache_in_row<&ic_binary, EmptyR, int, double>(body_aware);

        auto* blind_probe = cipher::lookup_computation_cache<&ic_binary, int, double>();
        ASSERT_TRUE(blind_probe == nullptr);

        auto* aware_probe = cipher::lookup_computation_cache_in_row<&ic_binary, EmptyR, int, double>();
        ASSERT_TRUE(aware_probe == body_aware);

        // With both slots occupied by different bodies, each lookup
        // still finds its own.
        auto* body_blind = mk_body(0x200002);
        cipher::insert_computation_cache<&ic_binary, int, double>(body_blind);

        auto* blind_after = cipher::lookup_computation_cache<&ic_binary, int, double>();
        ASSERT_TRUE(blind_after == body_blind);
        auto* aware_after = cipher::lookup_computation_cache_in_row<&ic_binary, EmptyR, int, double>();
        ASSERT_TRUE(aware_after == body_aware);
        ASSERT_TRUE(body_blind != body_aware);

        std::fprintf(stderr, "PASSED\n");
    }

    // One function under two different rows is one logical operation
    // in two contexts, and the two can compile differently.  Serving
    // either body to the other's caller is the confusion this guards
    // against.
    {
        std::fprintf(stderr, "  scenario 3 (cross_row_body_isolation): ");

        auto* body_empty = mk_body(0x300001);
        auto* body_bg = mk_body(0x300002);
        auto* body_io = mk_body(0x300003);

        cipher::insert_computation_cache_in_row<&ic_void, EmptyR>(body_empty);
        cipher::insert_computation_cache_in_row<&ic_void, BgR>(body_bg);
        cipher::insert_computation_cache_in_row<&ic_void, IOR>(body_io);

        auto* empty_probe = cipher::lookup_computation_cache_in_row<&ic_void, EmptyR>();
        auto* bg_probe = cipher::lookup_computation_cache_in_row<&ic_void, BgR>();
        auto* io_probe = cipher::lookup_computation_cache_in_row<&ic_void, IOR>();
        ASSERT_TRUE(empty_probe == body_empty);
        ASSERT_TRUE(bg_probe == body_bg);
        ASSERT_TRUE(io_probe == body_io);

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

        constexpr std::uintptr_t base_blind = 0x400000;
        cipher::insert_computation_cache<&ic_pop1, int>(mk_body(base_blind + 1));
        cipher::insert_computation_cache<&ic_pop2, int>(mk_body(base_blind + 2));
        cipher::insert_computation_cache<&ic_pop3, int>(mk_body(base_blind + 3));
        cipher::insert_computation_cache<&ic_pop4, int>(mk_body(base_blind + 4));
        cipher::insert_computation_cache<&ic_pop5, int>(mk_body(base_blind + 5));
        cipher::insert_computation_cache<&ic_pop6, int>(mk_body(base_blind + 6));
        cipher::insert_computation_cache<&ic_pop7, int>(mk_body(base_blind + 7));
        cipher::insert_computation_cache<&ic_pop8, int>(mk_body(base_blind + 8));

        // The same functions and arguments again, on the row-aware
        // side.
        constexpr std::uintptr_t base_aware = 0x480000;
        cipher::insert_computation_cache_in_row<&ic_pop1, EmptyR, int>(mk_body(base_aware + 1));
        cipher::insert_computation_cache_in_row<&ic_pop2, EmptyR, int>(mk_body(base_aware + 2));
        cipher::insert_computation_cache_in_row<&ic_pop3, EmptyR, int>(mk_body(base_aware + 3));
        cipher::insert_computation_cache_in_row<&ic_pop4, EmptyR, int>(mk_body(base_aware + 4));
        cipher::insert_computation_cache_in_row<&ic_pop5, EmptyR, int>(mk_body(base_aware + 5));
        cipher::insert_computation_cache_in_row<&ic_pop6, EmptyR, int>(mk_body(base_aware + 6));
        cipher::insert_computation_cache_in_row<&ic_pop7, EmptyR, int>(mk_body(base_aware + 7));
        cipher::insert_computation_cache_in_row<&ic_pop8, EmptyR, int>(mk_body(base_aware + 8));

        ASSERT_TRUE(cipher::lookup_computation_cache<&ic_pop1, int>() == mk_body(base_blind + 1));
        ASSERT_TRUE(cipher::lookup_computation_cache<&ic_pop2, int>() == mk_body(base_blind + 2));
        ASSERT_TRUE(cipher::lookup_computation_cache<&ic_pop3, int>() == mk_body(base_blind + 3));
        ASSERT_TRUE(cipher::lookup_computation_cache<&ic_pop4, int>() == mk_body(base_blind + 4));
        ASSERT_TRUE(cipher::lookup_computation_cache<&ic_pop5, int>() == mk_body(base_blind + 5));
        ASSERT_TRUE(cipher::lookup_computation_cache<&ic_pop6, int>() == mk_body(base_blind + 6));
        ASSERT_TRUE(cipher::lookup_computation_cache<&ic_pop7, int>() == mk_body(base_blind + 7));
        ASSERT_TRUE(cipher::lookup_computation_cache<&ic_pop8, int>() == mk_body(base_blind + 8));

        ASSERT_TRUE(cipher::lookup_computation_cache_in_row<&ic_pop1, EmptyR, int>() == mk_body(base_aware + 1));
        ASSERT_TRUE(cipher::lookup_computation_cache_in_row<&ic_pop2, EmptyR, int>() == mk_body(base_aware + 2));
        ASSERT_TRUE(cipher::lookup_computation_cache_in_row<&ic_pop3, EmptyR, int>() == mk_body(base_aware + 3));
        ASSERT_TRUE(cipher::lookup_computation_cache_in_row<&ic_pop4, EmptyR, int>() == mk_body(base_aware + 4));
        ASSERT_TRUE(cipher::lookup_computation_cache_in_row<&ic_pop5, EmptyR, int>() == mk_body(base_aware + 5));
        ASSERT_TRUE(cipher::lookup_computation_cache_in_row<&ic_pop6, EmptyR, int>() == mk_body(base_aware + 6));
        ASSERT_TRUE(cipher::lookup_computation_cache_in_row<&ic_pop7, EmptyR, int>() == mk_body(base_aware + 7));
        ASSERT_TRUE(cipher::lookup_computation_cache_in_row<&ic_pop8, EmptyR, int>() == mk_body(base_aware + 8));

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
    // next.  The two blocks below share no variable but the body
    // pointer, which the second block only compares against.
    cipher::CompiledBody* lifetime_body_ptr = mk_body(0x500001);
    {
        std::fprintf(stderr, "  scenario 5a (lifetime_persist_insert): ");
        cipher::insert_computation_cache<&ic_lifetime, int>(lifetime_body_ptr);
        auto* probe = cipher::lookup_computation_cache<&ic_lifetime, int>();
        ASSERT_TRUE(probe == lifetime_body_ptr);
        std::fprintf(stderr, "PASSED\n");
    }
    {
        std::fprintf(stderr, "  scenario 5b (lifetime_persist_lookup): ");
        auto* recovered = cipher::lookup_computation_cache<&ic_lifetime, int>();
        ASSERT_TRUE(recovered != nullptr);
        ASSERT_TRUE(recovered == lifetime_body_ptr);
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

        auto* hot_body = mk_body(0x700001);
        cipher::insert_computation_cache<&ic_hotloop, int>(hot_body);

        for (int i = 0; i < 1024; ++i) {
            auto* probe = cipher::lookup_computation_cache<&ic_hotloop, int>();
            ASSERT_TRUE(probe == hot_body);
        }

        std::fprintf(stderr, "PASSED\n");
    }

    std::fprintf(stderr, "\n8 scenarios passed.\n");
    return 0;
}
