// ═══════════════════════════════════════════════════════════════════
// test_expr_pool_fast_symbol — PERF-2 fast_symbol(SymbolId) lookup
//
// Properties verified:
//   1. fast_symbol returns nullptr before the symbol is registered
//   2. After symbol(name, sid, flags), fast_symbol(sid) returns the
//      same Expr* as another symbol(name, sid, flags) call (interning)
//   3. fast_symbol returns nullptr for SymbolIds beyond the
//      registered range
//   4. The cache survives multiple symbol() calls and grows with sid
//   5. Cached pointer matches the slow path's interned result for
//      every registered sid
// ═══════════════════════════════════════════════════════════════════

#include <crucible/effects/Capabilities.h>
#include <crucible/Expr.h>
#include <crucible/ExprPool.h>

#include "test_assert.h"
#include <cstdio>

using namespace crucible;

constexpr uint16_t NUM_FLAGS = ExprFlags::IS_INTEGER;

// ── Test: fast_symbol returns nullptr before registration ─────────

static void test_unregistered_returns_nullptr() {
    effects::Test t{};
    const auto a = t.alloc;
    ExprPool pool{a};

    // Before any symbol() call, every sid lookup returns nullptr.
    assert(pool.fast_symbol(SymbolId{0}) == nullptr);
    assert(pool.fast_symbol(SymbolId{42}) == nullptr);
    assert(pool.fast_symbol(SymbolId{1000}) == nullptr);

    // Register sid=5; sids 0..4 still nullptr (not yet seen),
    // sid=5 hits, sid=6+ still nullptr.
    auto e5 = pool.symbol(a, "five", SymbolId{5}, NUM_FLAGS);
    assert(pool.fast_symbol(SymbolId{0}) == nullptr);
    assert(pool.fast_symbol(SymbolId{4}) == nullptr);
    assert(pool.fast_symbol(SymbolId{5}) == e5);
    assert(pool.fast_symbol(SymbolId{6}) == nullptr);
    assert(pool.fast_symbol(SymbolId{1000}) == nullptr);

    std::printf("  test_unregistered_returns_nullptr: PASSED\n");
}

// ── Test: fast_symbol matches the symbol() interning result ───────

static void test_matches_slow_path() {
    effects::Test t{};
    const auto a = t.alloc;
    ExprPool pool{a};

    // Register a handful of symbols; each fast_symbol(sid) MUST
    // return the same Expr* the slow path returns (interning is
    // idempotent — same (op, sid) pair → same canonical Expr).
    const Expr* x = pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS);
    const Expr* y = pool.symbol(a, "y", SymbolId{1}, NUM_FLAGS);
    const Expr* z = pool.symbol(a, "z", SymbolId{2}, NUM_FLAGS);

    assert(pool.fast_symbol(SymbolId{0}) == x);
    assert(pool.fast_symbol(SymbolId{1}) == y);
    assert(pool.fast_symbol(SymbolId{2}) == z);

    // Re-call the slow path: should return the same interned Expr,
    // which must still match fast_symbol.
    assert(pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS) == x);
    assert(pool.fast_symbol(SymbolId{0}) == x);

    // Pointer identity: slow path == fast path for every registered sid.
    assert(pool.symbol(a, "y", SymbolId{1}, NUM_FLAGS) ==
           pool.fast_symbol(SymbolId{1}));
    assert(pool.symbol(a, "z", SymbolId{2}, NUM_FLAGS) ==
           pool.fast_symbol(SymbolId{2}));

    std::printf("  test_matches_slow_path: PASSED\n");
}

// ── Test: cache grows sparsely — non-contiguous SymbolId ──────────

static void test_sparse_sids() {
    effects::Test t{};
    const auto a = t.alloc;
    ExprPool pool{a};

    // Register sids 0, 100, and 1000 — non-contiguous.
    const Expr* e0    = pool.symbol(a, "zero",    SymbolId{0},    NUM_FLAGS);
    const Expr* e100  = pool.symbol(a, "hundred", SymbolId{100},  NUM_FLAGS);
    const Expr* e1000 = pool.symbol(a, "kilo",    SymbolId{1000}, NUM_FLAGS);

    assert(pool.fast_symbol(SymbolId{0})    == e0);
    assert(pool.fast_symbol(SymbolId{100})  == e100);
    assert(pool.fast_symbol(SymbolId{1000}) == e1000);

    // Holes return nullptr.
    assert(pool.fast_symbol(SymbolId{1})   == nullptr);
    assert(pool.fast_symbol(SymbolId{50})  == nullptr);
    assert(pool.fast_symbol(SymbolId{999}) == nullptr);

    // Register sid=50 mid-range; previously-nullptr now hits.
    const Expr* e50 = pool.symbol(a, "half-hundred", SymbolId{50}, NUM_FLAGS);
    assert(pool.fast_symbol(SymbolId{50}) == e50);

    // Other sids unchanged.
    assert(pool.fast_symbol(SymbolId{0})    == e0);
    assert(pool.fast_symbol(SymbolId{100})  == e100);
    assert(pool.fast_symbol(SymbolId{1000}) == e1000);

    std::printf("  test_sparse_sids: PASSED\n");
}

// ── Test: large sid range stress ──────────────────────────────────

static void test_dense_sids_stress() {
    effects::Test t{};
    const auto a = t.alloc;
    ExprPool pool{a};

    constexpr uint32_t N = 1000;
    const Expr* refs[N];

    // Register sids 0..N-1 with distinct names.
    for (uint32_t i = 0; i < N; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "sym_%u", i);
        refs[i] = pool.symbol(a, name, SymbolId{i}, NUM_FLAGS);
    }

    // Every fast_symbol must hit and return the matching Expr*.
    for (uint32_t i = 0; i < N; ++i) {
        const Expr* got = pool.fast_symbol(SymbolId{i});
        assert(got == refs[i]);
    }

    // Out-of-range still nullptr.
    assert(pool.fast_symbol(SymbolId{N})       == nullptr);
    assert(pool.fast_symbol(SymbolId{N + 100}) == nullptr);

    std::printf("  test_dense_sids_stress: PASSED (%u symbols)\n", N);
}

int main() {
    std::printf("test_expr_pool_fast_symbol:\n");

    test_unregistered_returns_nullptr();
    test_matches_slow_path();
    test_sparse_sids();
    test_dense_sids_stress();

    std::printf("test_expr_pool_fast_symbol: ALL PASSED\n");
    return 0;
}
