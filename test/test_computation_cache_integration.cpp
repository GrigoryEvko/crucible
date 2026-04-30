// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0
//
// ═══════════════════════════════════════════════════════════════════
// test_computation_cache_integration — capstone for the F09+F11
// computation-cache stack.
//
// Layer-1 unit tests (test_computation_cache.cpp) exercise ONE
// property at a time: lookup-before-insert-misses, insert-then-lookup
// round-trips, distinct-instantiations-isolated, single-slot
// concurrent first-wins, etc.  Layer-2 integration tests (this file)
// exercise SCENARIOS — sequenced multi-step flows that compose the
// unit-level invariants the way a production dispatcher does.
//
// Each scenario corresponds to a specific bug class the integration
// prevents.  If a scenario reds, the bug it pins has actually been
// regressed in production code; the unit tests would still pass (one
// property at a time) but the composition is broken.
//
//   Scenario                         Bug class prevented
//   ───────────────────────────────  ───────────────────────────────────
//   1. Compile-then-cache fast-path  redundant recompile; cache stale
//   2. Row-blind ⊥ row-aware         same compiled body served across
//                                    semantically-distinct effect rows
//   3. Cross-row body isolation      effect-row mode confusion
//   4. Population disjointness       cross-tuple slot aliasing under
//                                    realistic dispatcher load
//   5. Process-lifetime persistence  inline-static slot reset across
//                                    sub-test boundaries
//   6. Federation-key consteval-eq   keys diverge between consteval
//                                    pin and runtime usage
//   7. Hot-loop hit-rate witness     dispatcher fast-path regression
//                                    when N lookups follow ≤K inserts
// ═══════════════════════════════════════════════════════════════════

#include <crucible/cipher/ComputationCache.h>
#include <crucible/effects/EffectRow.h>

#include "test_assert.h"

#include <array>
#include <cstdint>
#include <cstdio>

namespace {

namespace cipher = ::crucible::cipher;
namespace eff    = ::crucible::effects;

using EmptyR = eff::Row<>;
using BgR    = eff::Row<eff::Effect::Bg>;
using IOR    = eff::Row<eff::Effect::IO>;
using BgIOR  = eff::Row<eff::Effect::Bg, eff::Effect::IO>;

// FOUND-F10-AUDIT (Finding B) — consteval pairwise-distinctness over
// a federation-key population.  Used to pin the load-bearing
// invariant of Scenario 4: 16 distinct (FnPtr, Args, Row) tuples
// MUST produce 16 distinct federation keys.  An O(N²) consteval
// double-loop is acceptable for small N (here N=16, 120 pair checks)
// and produces a single binary boolean witness — simpler than 120
// individual static_asserts and equally precise.
template <std::size_t N>
constexpr bool all_pairwise_distinct(
    const std::array<std::uint64_t, N>& keys) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            if (keys[i] == keys[j]) return false;
        }
    }
    return true;
}

// ── Scenario fixtures (all distinct from test_computation_cache.cpp)
// to keep the two test TUs entirely independent.  Even though they
// run in separate processes, distinct names eliminate any reader
// confusion when grepping cache-fixture sites.
inline void  ic_unary  (int)              noexcept {}
inline void  ic_binary (int, double)      noexcept {}
inline void  ic_void   ()                 noexcept {}
inline void  ic_pop1   (int)              noexcept {}
inline void  ic_pop2   (int)              noexcept {}
inline void  ic_pop3   (int)              noexcept {}
inline void  ic_pop4   (int)              noexcept {}
inline void  ic_pop5   (int)              noexcept {}
inline void  ic_pop6   (int)              noexcept {}
inline void  ic_pop7   (int)              noexcept {}
inline void  ic_pop8   (int)              noexcept {}
inline void  ic_lifetime(int)             noexcept {}
inline void  ic_hotloop (int)             noexcept {}

// Bit-pattern stub bodies — never dereferenced by the cache.
inline cipher::CompiledBody* mk_body(std::uintptr_t v) noexcept {
    return reinterpret_cast<cipher::CompiledBody*>(v);
}

// Variadic ASSERT — wraps test_assert.h's macro pattern but accepts
// expressions containing unparenthesized commas (e.g. template arg
// lists like `lookup<&fn, int>()`).  test_assert.h's `assert` macro
// is single-arg, so a comma inside a template-arg list is parsed
// as a macro-arg separator by the preprocessor.
#define ASSERT_TRUE(...)                                                  \
    do {                                                                  \
        if (!(__VA_ARGS__)) {                                             \
            std::fprintf(stderr,                                          \
                "    ASSERT_TRUE failed: %s (%s:%d)\n",                   \
                #__VA_ARGS__, __FILE__, __LINE__);                        \
            std::abort();                                                 \
        }                                                                 \
    } while (0)

}  // namespace

int main() {
    std::fprintf(stderr, "test_computation_cache_integration:\n");

    // ═══════════════════════════════════════════════════════════════════
    // Scenario 1 — Compile-then-cache fast-path
    //
    // BUG IT PREVENTS: redundant recompile.  The dispatcher's hot path
    // is "lookup; on hit, branch to cached body; on miss, compile and
    // insert".  If the cache reds the lookup-after-insert invariant,
    // every call would recompile — destroying performance and stable
    // identity of compiled bodies.  Pins the foundational fast-path
    // contract: ONE insert + ONE lookup roundtrip; subsequent lookups
    // hit deterministically.
    // ═══════════════════════════════════════════════════════════════════
    {
        std::fprintf(stderr, "  scenario 1 (compile_then_cache_fast_path): ");

        // Cold lookup misses.
        auto* cold = cipher::lookup_computation_cache<&ic_unary, int>();
        ASSERT_TRUE(cold == nullptr);

        // "Compile" a body, insert it.
        auto* body = mk_body(0x100001);
        cipher::insert_computation_cache<&ic_unary, int>(body);

        // Hot lookup hits.  Repeat 100x to exercise the cached path
        // under realistic loop pressure (no slot reset, no aliasing).
        for (int i = 0; i < 100; ++i) {
            auto* probe = cipher::lookup_computation_cache<&ic_unary, int>();
            ASSERT_TRUE(probe == body);
        }

        std::fprintf(stderr, "PASSED\n");
    }

    // ═══════════════════════════════════════════════════════════════════
    // Scenario 2 — Row-blind ⊥ row-aware disjointness
    //
    // BUG IT PREVENTS: same compiled body served across semantically-
    // distinct effect rows.  If row-blind <&fn, Args> and row-aware
    // <&fn, EmptyRow, Args> aliased (e.g. a refactor that "simplified"
    // by routing both to the same slot to save space), a body
    // compiled under no-effect-row would silently be served to row-
    // aware callers — defeating the whole purpose of the row-aware
    // dispatcher.  Closes the disjoint-slot invariant from F11
    // documented in ComputationCache.h's row-aware doc-block.
    // ═══════════════════════════════════════════════════════════════════
    {
        std::fprintf(stderr, "  scenario 2 (row_blind_aware_disjoint): ");

        // Insert into row-aware <ic_binary, EmptyR, int, double>.
        auto* body_aware = mk_body(0x200001);
        cipher::insert_computation_cache_in_row<
            &ic_binary, EmptyR, int, double>(body_aware);

        // Row-blind <ic_binary, int, double> MUST still miss — these
        // are documented-disjoint slots.
        auto* blind_probe = cipher::lookup_computation_cache<
            &ic_binary, int, double>();
        ASSERT_TRUE(blind_probe == nullptr);

        // Row-aware hits.
        auto* aware_probe = cipher::lookup_computation_cache_in_row<
            &ic_binary, EmptyR, int, double>();
        ASSERT_TRUE(aware_probe == body_aware);

        // Now insert into row-blind too.  Both slots independently
        // occupied with DIFFERENT bodies.
        auto* body_blind = mk_body(0x200002);
        cipher::insert_computation_cache<&ic_binary, int, double>(body_blind);

        auto* blind_after = cipher::lookup_computation_cache<
            &ic_binary, int, double>();
        ASSERT_TRUE(blind_after == body_blind);
        auto* aware_after = cipher::lookup_computation_cache_in_row<
            &ic_binary, EmptyR, int, double>();
        ASSERT_TRUE(aware_after == body_aware);
        ASSERT_TRUE(body_blind != body_aware);

        std::fprintf(stderr, "PASSED\n");
    }

    // ═══════════════════════════════════════════════════════════════════
    // Scenario 3 — Cross-row body isolation
    //
    // BUG IT PREVENTS: effect-row mode confusion.  Same FnPtr + same
    // Args but DIFFERENT Row types must reach disjoint compiled bodies
    // — the dispatcher's whole job is to differentiate "same logical
    // operation under different effect-row contexts" (e.g. the same
    // function compiled for Bg-only vs IO-only callers can have
    // different optimizations applied).  Pins three rows × one
    // FnPtr+Args, three distinct bodies, no cross-row leakage.
    // ═══════════════════════════════════════════════════════════════════
    {
        std::fprintf(stderr, "  scenario 3 (cross_row_body_isolation): ");

        auto* body_empty = mk_body(0x300001);
        auto* body_bg    = mk_body(0x300002);
        auto* body_io    = mk_body(0x300003);

        cipher::insert_computation_cache_in_row<
            &ic_void, EmptyR>(body_empty);
        cipher::insert_computation_cache_in_row<
            &ic_void, BgR>(body_bg);
        cipher::insert_computation_cache_in_row<
            &ic_void, IOR>(body_io);

        // Each row's slot holds ITS body, not the others'.
        auto* empty_probe = cipher::lookup_computation_cache_in_row<
            &ic_void, EmptyR>();
        auto* bg_probe = cipher::lookup_computation_cache_in_row<
            &ic_void, BgR>();
        auto* io_probe = cipher::lookup_computation_cache_in_row<
            &ic_void, IOR>();
        ASSERT_TRUE(empty_probe == body_empty);
        ASSERT_TRUE(bg_probe == body_bg);
        ASSERT_TRUE(io_probe == body_io);

        // Federation keys differ across rows (load-bearing for the
        // FOUND-F12 federation protocol — different rows must
        // serialize to different cache entries).
        static_assert(
            cipher::computation_cache_key_in_row<&ic_void, EmptyR>
            != cipher::computation_cache_key_in_row<&ic_void, BgR>);
        static_assert(
            cipher::computation_cache_key_in_row<&ic_void, BgR>
            != cipher::computation_cache_key_in_row<&ic_void, IOR>);
        static_assert(
            cipher::computation_cache_key_in_row<&ic_void, EmptyR>
            != cipher::computation_cache_key_in_row<&ic_void, IOR>);

        std::fprintf(stderr, "PASSED\n");
    }

    // ═══════════════════════════════════════════════════════════════════
    // Scenario 4 — Population disjointness under realistic load
    //
    // BUG IT PREVENTS: cross-tuple slot aliasing under realistic
    // dispatcher load.  The unit test exercises 4 distinct slots; this
    // scenario exercises 8 slots × 2 paths (row-blind + row-aware) =
    // 16 simultaneous slots in the same address space, verifying that
    // per-instantiation static atomics scale and that linker-level
    // dedup of inline templates with NTTPs continues to produce one
    // unique slot per instantiation.  This is the load-bearing
    // structural invariant of the cache design — if it reds, the
    // cache layout is broken in a way that no unit test would catch.
    // ═══════════════════════════════════════════════════════════════════
    {
        std::fprintf(stderr, "  scenario 4 (population_disjointness): ");

        // Insert distinct bodies into 8 row-blind slots.
        constexpr std::uintptr_t base_blind = 0x400000;
        cipher::insert_computation_cache<&ic_pop1, int>(mk_body(base_blind + 1));
        cipher::insert_computation_cache<&ic_pop2, int>(mk_body(base_blind + 2));
        cipher::insert_computation_cache<&ic_pop3, int>(mk_body(base_blind + 3));
        cipher::insert_computation_cache<&ic_pop4, int>(mk_body(base_blind + 4));
        cipher::insert_computation_cache<&ic_pop5, int>(mk_body(base_blind + 5));
        cipher::insert_computation_cache<&ic_pop6, int>(mk_body(base_blind + 6));
        cipher::insert_computation_cache<&ic_pop7, int>(mk_body(base_blind + 7));
        cipher::insert_computation_cache<&ic_pop8, int>(mk_body(base_blind + 8));

        // Insert distinct bodies into 8 row-aware (EmptyR) slots
        // — same FnPtrs, same Args.
        constexpr std::uintptr_t base_aware = 0x480000;
        cipher::insert_computation_cache_in_row<
            &ic_pop1, EmptyR, int>(mk_body(base_aware + 1));
        cipher::insert_computation_cache_in_row<
            &ic_pop2, EmptyR, int>(mk_body(base_aware + 2));
        cipher::insert_computation_cache_in_row<
            &ic_pop3, EmptyR, int>(mk_body(base_aware + 3));
        cipher::insert_computation_cache_in_row<
            &ic_pop4, EmptyR, int>(mk_body(base_aware + 4));
        cipher::insert_computation_cache_in_row<
            &ic_pop5, EmptyR, int>(mk_body(base_aware + 5));
        cipher::insert_computation_cache_in_row<
            &ic_pop6, EmptyR, int>(mk_body(base_aware + 6));
        cipher::insert_computation_cache_in_row<
            &ic_pop7, EmptyR, int>(mk_body(base_aware + 7));
        cipher::insert_computation_cache_in_row<
            &ic_pop8, EmptyR, int>(mk_body(base_aware + 8));

        // Verify all 16 slots hold their own body, no cross-talk.
        ASSERT_TRUE(cipher::lookup_computation_cache<&ic_pop1, int>() == mk_body(base_blind + 1));
        ASSERT_TRUE(cipher::lookup_computation_cache<&ic_pop2, int>() == mk_body(base_blind + 2));
        ASSERT_TRUE(cipher::lookup_computation_cache<&ic_pop3, int>() == mk_body(base_blind + 3));
        ASSERT_TRUE(cipher::lookup_computation_cache<&ic_pop4, int>() == mk_body(base_blind + 4));
        ASSERT_TRUE(cipher::lookup_computation_cache<&ic_pop5, int>() == mk_body(base_blind + 5));
        ASSERT_TRUE(cipher::lookup_computation_cache<&ic_pop6, int>() == mk_body(base_blind + 6));
        ASSERT_TRUE(cipher::lookup_computation_cache<&ic_pop7, int>() == mk_body(base_blind + 7));
        ASSERT_TRUE(cipher::lookup_computation_cache<&ic_pop8, int>() == mk_body(base_blind + 8));

        ASSERT_TRUE(cipher::lookup_computation_cache_in_row<&ic_pop1, EmptyR, int>()
                    == mk_body(base_aware + 1));
        ASSERT_TRUE(cipher::lookup_computation_cache_in_row<&ic_pop2, EmptyR, int>()
                    == mk_body(base_aware + 2));
        ASSERT_TRUE(cipher::lookup_computation_cache_in_row<&ic_pop3, EmptyR, int>()
                    == mk_body(base_aware + 3));
        ASSERT_TRUE(cipher::lookup_computation_cache_in_row<&ic_pop4, EmptyR, int>()
                    == mk_body(base_aware + 4));
        ASSERT_TRUE(cipher::lookup_computation_cache_in_row<&ic_pop5, EmptyR, int>()
                    == mk_body(base_aware + 5));
        ASSERT_TRUE(cipher::lookup_computation_cache_in_row<&ic_pop6, EmptyR, int>()
                    == mk_body(base_aware + 6));
        ASSERT_TRUE(cipher::lookup_computation_cache_in_row<&ic_pop7, EmptyR, int>()
                    == mk_body(base_aware + 7));
        ASSERT_TRUE(cipher::lookup_computation_cache_in_row<&ic_pop8, EmptyR, int>()
                    == mk_body(base_aware + 8));

        // FOUND-F10-AUDIT (Finding B) — federation-key disjointness
        // across the 16-slot population.  Body-pointer disjointness
        // (verified above) is necessary but not sufficient: if two
        // slots had the SAME federation key, the body-pointer
        // disjointness would be a per-instantiation-static accident,
        // and federation/serialization would alias them on the wire.
        // Pin the property at compile time — a 16×16 pairwise check
        // (120 inequalities) folded into one consteval predicate.
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
        static_assert(all_pairwise_distinct(all_keys),
            "F10 Scenario 4: all 16 (FnPtr, Args, [Row]) federation "
            "keys must be pairwise distinct — body-pointer disjointness "
            "alone is insufficient because federation serializes keys, "
            "not body pointers.");
        // Runtime peer — proves the consteval witness binds at runtime
        // call-sites too (no DSE-collapse hiding non-determinism).
        volatile bool runtime_distinct = all_pairwise_distinct(all_keys);
        ASSERT_TRUE(runtime_distinct);

        std::fprintf(stderr, "PASSED\n");
    }

    // ═══════════════════════════════════════════════════════════════════
    // Scenario 5 — Process-lifetime persistence
    //
    // BUG IT PREVENTS: inline-static slot reset across sub-test
    // boundaries.  The cache slots are `inline std::atomic<...>` per
    // template instantiation — they have static storage duration, so
    // they persist for the lifetime of the process.  A future
    // refactor that accidentally introduced thread-local storage, or
    // wrapped the slot in a function-local-static (re-init each
    // call), would break the contract that "insert in one scope is
    // visible in the next scope".  Pin the property: insert in this
    // outer block, drop the body pointer locally, then in the next
    // block (no shared variable) lookup MUST still hit because the
    // slot's lifetime is process-wide.
    // ═══════════════════════════════════════════════════════════════════
    cipher::CompiledBody* lifetime_body_ptr = mk_body(0x500001);
    {
        std::fprintf(stderr, "  scenario 5a (lifetime_persist_insert): ");
        cipher::insert_computation_cache<&ic_lifetime, int>(lifetime_body_ptr);
        auto* probe = cipher::lookup_computation_cache<&ic_lifetime, int>();
        ASSERT_TRUE(probe == lifetime_body_ptr);
        std::fprintf(stderr, "PASSED\n");
    }
    // ── Local body pointer "forgotten" between blocks (we only
    // referenced it via the cache slot from here on).
    {
        std::fprintf(stderr, "  scenario 5b (lifetime_persist_lookup): ");
        // The slot still has it.
        auto* recovered = cipher::lookup_computation_cache<&ic_lifetime, int>();
        ASSERT_TRUE(recovered != nullptr);
        ASSERT_TRUE(recovered == lifetime_body_ptr);
        std::fprintf(stderr, "PASSED\n");
    }

    // ═══════════════════════════════════════════════════════════════════
    // Scenario 6 — Federation-key consteval-runtime equivalence
    //
    // BUG IT PREVENTS: federation keys diverge between the consteval
    // pin and the runtime call site.  The cache key is a consteval
    // computation; the federation protocol (FOUND-F12) serializes
    // these keys as cross-process slot identifiers.  If a runtime
    // call site somehow saw a different key value than the
    // static_assert, federation lookups would silently miss.  Pin
    // the property: capture the consteval key in a constexpr
    // variable, capture the same key dynamically (via a runtime
    // store-and-read), assert equality.  The compiler MUST fold
    // both to the same constant — but the runtime read pins the
    // contract that no non-deterministic ABI-level wrapper got
    // injected.
    // ═══════════════════════════════════════════════════════════════════
    {
        std::fprintf(stderr, "  scenario 6 (federation_key_consteval_runtime_eq): ");

        // Consteval pin.
        constexpr std::uint64_t key_blind_consteval =
            cipher::computation_cache_key<&ic_unary, int>;
        constexpr std::uint64_t key_aware_consteval =
            cipher::computation_cache_key_in_row<&ic_unary, EmptyR, int>;

        // Runtime read (the variable is `volatile` to defeat any
        // optimizer dead-store-elimination that might collapse the
        // runtime branch back into the consteval one without
        // actually reading the call-site).
        volatile std::uint64_t key_blind_runtime =
            cipher::computation_cache_key<&ic_unary, int>;
        volatile std::uint64_t key_aware_runtime =
            cipher::computation_cache_key_in_row<&ic_unary, EmptyR, int>;

        ASSERT_TRUE(key_blind_runtime == key_blind_consteval);
        ASSERT_TRUE(key_aware_runtime == key_aware_consteval);
        // And the two are different — pinned at consteval already.
        static_assert(
            cipher::computation_cache_key<&ic_unary, int>
            != cipher::computation_cache_key_in_row<&ic_unary, EmptyR, int>);
        // Federation invariant: keys must be non-zero (a zero key
        // would alias against any uninitialized cache slot).
        ASSERT_TRUE(key_blind_consteval != 0);
        ASSERT_TRUE(key_aware_consteval != 0);

        // FOUND-F10-AUDIT (Finding A) — extend coverage from row-blind
        // + row-aware-EmptyR to ALL interesting Row shapes.  EmptyR
        // exercises the empty-effect-pack fold path; BgR exercises a
        // single-effect row; BgIOR exercises a multi-effect row whose
        // row_hash sort-fold actually does work (two atoms in the
        // pack).  Each Row shape goes through a different fold-edge
        // in row_hash_contribution<Row<Es...>>, so consteval-runtime
        // equivalence must hold separately for each.
        constexpr std::uint64_t key_bg_consteval =
            cipher::computation_cache_key_in_row<&ic_unary, BgR, int>;
        constexpr std::uint64_t key_bgio_consteval =
            cipher::computation_cache_key_in_row<&ic_unary, BgIOR, int>;

        volatile std::uint64_t key_bg_runtime =
            cipher::computation_cache_key_in_row<&ic_unary, BgR, int>;
        volatile std::uint64_t key_bgio_runtime =
            cipher::computation_cache_key_in_row<&ic_unary, BgIOR, int>;

        ASSERT_TRUE(key_bg_runtime == key_bg_consteval);
        ASSERT_TRUE(key_bgio_runtime == key_bgio_consteval);

        // All four Row shapes produce distinct keys for the same
        // (FnPtr, Args).  The four rows cover the row-shape space:
        //   EmptyR   : 0 effects (empty pack)
        //   BgR      : 1 effect  (single-atom pack)
        //   BgIOR    : 2 effects (multi-atom pack)
        //   row-blind: not a Row at all (no row_hash applied)
        // A consteval pairwise-distinctness check pins the four-way
        // separation at compile time.
        constexpr std::array<std::uint64_t, 4> row_shape_keys = {
            key_blind_consteval, key_aware_consteval,
            key_bg_consteval,    key_bgio_consteval,
        };
        static_assert(all_pairwise_distinct(row_shape_keys),
            "F10 Scenario 6: row-blind, row-aware-Empty, row-aware-Bg, "
            "row-aware-BgIO must produce four distinct federation keys "
            "— each row shape exercises a different row_hash fold-edge.");

        // All four are non-zero.
        ASSERT_TRUE(key_bg_consteval != 0);
        ASSERT_TRUE(key_bgio_consteval != 0);

        std::fprintf(stderr, "PASSED\n");
    }

    // ═══════════════════════════════════════════════════════════════════
    // Scenario 7 — Hot-loop hit-rate witness
    //
    // BUG IT PREVENTS: dispatcher fast-path regression when N
    // lookups follow ≤K inserts.  Production dispatchers run a
    // tight loop: lookup → branch on hit → invoke; on miss → compile
    // → insert → invoke.  After the cache warms, hit-rate must be
    // 100% for the established (FnPtr, Args) population.  This
    // scenario simulates 1024 sequential lookups against ONE warmed
    // slot — every lookup MUST hit; a single nullptr return would
    // indicate the slot was reset, evicted, or the lookup is
    // somehow stateful.
    // ═══════════════════════════════════════════════════════════════════
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
