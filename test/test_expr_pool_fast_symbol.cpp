#include <crucible/effects/Capabilities.h>
#include <crucible/Expr.h>
#include <crucible/ExprPool.h>

#include "test_assert.h"
#include <cstdio>
#include <type_traits>

using namespace crucible;

constexpr uint16_t NUM_FLAGS = ExprFlags::IS_INTEGER;

static void test_unregistered_returns_nullptr() {
    auto t = effects::testing::test();
    const auto a = t.alloc;
    ExprPool pool{a};

    assert(pool.fast_symbol(SymbolId{0}) == nullptr);
    assert(pool.fast_symbol(SymbolId{42}) == nullptr);
    assert(pool.fast_symbol(SymbolId{1000}) == nullptr);

    // Registering one identifier leaves every other lookup unchanged.
    auto e5 = pool.symbol(a, "five", SymbolId{5}, NUM_FLAGS);
    assert(pool.fast_symbol(SymbolId{0}) == nullptr);
    assert(pool.fast_symbol(SymbolId{4}) == nullptr);
    assert(pool.fast_symbol(SymbolId{5}) == e5);
    assert(pool.fast_symbol(SymbolId{6}) == nullptr);
    assert(pool.fast_symbol(SymbolId{1000}) == nullptr);

    std::printf("  test_unregistered_returns_nullptr: PASSED\n");
}

static void test_matches_slow_path() {
    auto t = effects::testing::test();
    const auto a = t.alloc;
    ExprPool pool{a};

    // Interning is idempotent, so the same identifier always yields the
    // one canonical expression and both lookups must agree on it.
    const Expr* x = pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS);
    const Expr* y = pool.symbol(a, "y", SymbolId{1}, NUM_FLAGS);
    const Expr* z = pool.symbol(a, "z", SymbolId{2}, NUM_FLAGS);

    assert(pool.fast_symbol(SymbolId{0}) == x);
    assert(pool.fast_symbol(SymbolId{1}) == y);
    assert(pool.fast_symbol(SymbolId{2}) == z);

    assert(pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS) == x);
    assert(pool.fast_symbol(SymbolId{0}) == x);

    assert(pool.symbol(a, "y", SymbolId{1}, NUM_FLAGS) == pool.fast_symbol(SymbolId{1}));
    assert(pool.symbol(a, "z", SymbolId{2}, NUM_FLAGS) == pool.fast_symbol(SymbolId{2}));

    std::printf("  test_matches_slow_path: PASSED\n");
}

static void test_sparse_sids() {
    auto t = effects::testing::test();
    const auto a = t.alloc;
    ExprPool pool{a};

    // Deliberately far apart, so the cache has to hold gaps.
    const Expr* e0 = pool.symbol(a, "zero", SymbolId{0}, NUM_FLAGS);
    const Expr* e100 = pool.symbol(a, "hundred", SymbolId{100}, NUM_FLAGS);
    const Expr* e1000 = pool.symbol(a, "kilo", SymbolId{1000}, NUM_FLAGS);

    assert(pool.fast_symbol(SymbolId{0}) == e0);
    assert(pool.fast_symbol(SymbolId{100}) == e100);
    assert(pool.fast_symbol(SymbolId{1000}) == e1000);

    assert(pool.fast_symbol(SymbolId{1}) == nullptr);
    assert(pool.fast_symbol(SymbolId{50}) == nullptr);
    assert(pool.fast_symbol(SymbolId{999}) == nullptr);

    // Filling one of those gaps.
    const Expr* e50 = pool.symbol(a, "half-hundred", SymbolId{50}, NUM_FLAGS);
    assert(pool.fast_symbol(SymbolId{50}) == e50);

    // The neighbours are untouched by it.
    assert(pool.fast_symbol(SymbolId{0}) == e0);
    assert(pool.fast_symbol(SymbolId{100}) == e100);
    assert(pool.fast_symbol(SymbolId{1000}) == e1000);

    std::printf("  test_sparse_sids: PASSED\n");
}

static void test_dense_sids_stress() {
    auto t = effects::testing::test();
    const auto a = t.alloc;
    ExprPool pool{a};

    constexpr uint32_t N = 1000;
    const Expr* refs[N];

    for (uint32_t i = 0; i < N; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "sym_%u", i);
        refs[i] = pool.symbol(a, name, SymbolId{i}, NUM_FLAGS);
    }

    for (uint32_t i = 0; i < N; ++i) {
        const Expr* got = pool.fast_symbol(SymbolId{i});
        assert(got == refs[i]);
    }

    assert(pool.fast_symbol(SymbolId{N}) == nullptr);
    assert(pool.fast_symbol(SymbolId{N + 100}) == nullptr);

    std::printf("  test_dense_sids_stress: PASSED (%u symbols)\n", N);
}

static void test_make_returns_interned_det_safe() {
    auto t = effects::testing::test();
    const auto a = t.alloc;
    ExprPool pool{a};

    const Expr* x = pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS);
    const Expr* y = pool.symbol(a, "y", SymbolId{1}, NUM_FLAGS);
    const Expr* args[] = {x, y};

    const auto wrapped = pool.make(a, Op::ADD, args);
    const Expr* raw = wrapped.peek().value();
    assert(raw == pool.add(a, x, y));

    std::printf("  test_make_returns_interned_det_safe: PASSED\n");
}

int main() {
    std::printf("test_expr_pool_fast_symbol:\n");
    static_assert(std::is_same_v<ExprPool::InternedExpr, safety::Tagged<const Expr*, safety::source::Interned>>);
    static_assert(std::is_same_v<ExprPool::PureInternedExpr, safety::det_safe::Pure<ExprPool::InternedExpr>>);
    static_assert(sizeof(ExprPool::PureInternedExpr) == sizeof(const Expr*));

    test_unregistered_returns_nullptr();
    test_matches_slow_path();
    test_sparse_sids();
    test_dense_sids_stress();
    test_make_returns_interned_det_safe();

    std::printf("test_expr_pool_fast_symbol: ALL PASSED\n");
    return 0;
}
