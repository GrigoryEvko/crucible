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

// A wide operand list must reach the intern table intact.  The n-ary
// constructors used to collect into a 64-entry stack buffer with no bound of
// any kind, so anything past 64 operands wrote past the end of the frame.
static void test_wide_variadic_operands() {
    auto t = effects::testing::test();
    const auto a = t.alloc;
    ExprPool pool{a};

    const Expr* operands[Expr::kMaxArgs];
    for (uint32_t i = 0; i < Expr::kMaxArgs; ++i)
        operands[i] = pool.symbol(a, "s", SymbolId{i}, NUM_FLAGS);

    // 100 crosses the old buffer, and kMaxArgs sits exactly on the ceiling.
    for (size_t width : {size_t{65}, size_t{100}, size_t{Expr::kMaxArgs}}) {
        const std::span<const Expr* const> args{operands, width};
        for (Op op : {Op::MIN, Op::MAX}) {
            const Expr* built = pool.make(a, op, args).peek().value();
            assert(built->op == op);
            assert(built->nargs == width);
            // Interning is idempotent, so the same list yields one node.
            assert(built == pool.make(a, op, args).peek().value());
        }
    }

    std::printf("  test_wide_variadic_operands: PASSED\n");
}

// The same bound on the logical constructors, whose only guard used to be a
// bare assert that -DNDEBUG removed.
static void test_wide_logical_operands() {
    auto t = effects::testing::test();
    const auto a = t.alloc;
    ExprPool pool{a};

    const Expr* predicates[200];
    for (uint32_t i = 0; i < 200; ++i) {
        const Expr* sym = pool.symbol(a, "s", SymbolId{i}, NUM_FLAGS);
        predicates[i] = pool.lt(a, sym, pool.integer(a, static_cast<int64_t>(i)));
    }

    const std::span<const Expr* const> args{predicates, 200};
    for (Op op : {Op::AND, Op::OR}) {
        const Expr* built = pool.make(a, op, args).peek().value();
        assert(built->op == op);
        assert(built->nargs == 200);
        assert(built->is_boolean());
    }

    std::printf("  test_wide_logical_operands: PASSED\n");
}

// A flatten that would pass the arity ceiling keeps the child node whole.
// The result names the same value with one more level of nesting, and every
// node in it is inside the ceiling.
static void test_flatten_degrades_at_the_ceiling() {
    auto t = effects::testing::test();
    const auto a = t.alloc;
    ExprPool pool{a};

    const Expr* operands[Expr::kMaxArgs];
    for (uint32_t i = 0; i < Expr::kMaxArgs; ++i)
        operands[i] = pool.symbol(a, "s", SymbolId{i}, NUM_FLAGS);

    const Expr* wide = pool.make(a, Op::MIN, std::span<const Expr* const>{operands, Expr::kMaxArgs}).peek().value();
    assert(wide->nargs == Expr::kMaxArgs);

    // Two full-width MIN nodes cannot flatten into one, so each stays whole.
    const Expr* pair[] = {wide, operands[0]};
    const Expr* outer = pool.make(a, Op::MIN, pair).peek().value();
    assert(outer->nargs <= Expr::kMaxArgs);

    const Expr* both[] = {wide, wide};
    const Expr* merged = pool.make(a, Op::MIN, both).peek().value();
    // min(x, x) is x whichever way the flatten went.
    assert(merged == wide);

    std::printf("  test_flatten_degrades_at_the_ceiling: PASSED\n");
}

// Interning must survive the table growing underneath a lookup.  The probe
// derived its slot mask from the capacity read before the rehash, so a node
// that moved into the new upper half was missed and interned a second time.
static void test_intern_identity_across_rehash() {
    auto t = effects::testing::test();
    const auto a = t.alloc;
    // A capacity far below the working set forces many rehashes.
    ExprPool pool{a, 16};

    constexpr int64_t kCount = 20000;
    constexpr int64_t kBase = 1000000;  // above the integer cache
    const Expr* first[kCount];
    for (int64_t i = 0; i < kCount; ++i)
        first[i] = pool.integer(a, kBase + i);

    const size_t count_after_first_pass = pool.intern_size();

    // A second pass must hit every node the first pass created, and add none.
    for (int64_t i = 0; i < kCount; ++i)
        assert(pool.integer(a, kBase + i) == first[i]);
    assert(pool.intern_size() == count_after_first_pass);

    // Distinct values must stay distinct nodes.
    for (int64_t i = 1; i < kCount; ++i)
        assert(first[i] != first[i - 1]);

    std::printf("  test_intern_identity_across_rehash: PASSED (%zu nodes)\n", count_after_first_pass);
}

// Constant folding saturates at the bounds of int64 rather than wrapping,
// so the result is the same on every target and in every build mode.
static void test_constant_folding_saturates() {
    auto t = effects::testing::test();
    const auto a = t.alloc;
    ExprPool pool{a};

    constexpr int64_t kMax = INT64_MAX;
    constexpr int64_t kMin = INT64_MIN;

    const Expr* big = pool.integer(a, kMax);
    const Expr* low = pool.integer(a, kMin);
    const Expr* one = pool.integer(a, 1);

    assert(pool.add(a, big, one)->as_int() == kMax);
    assert(pool.add(a, low, pool.integer(a, -1))->as_int() == kMin);
    assert(pool.mul(a, big, big)->as_int() == kMax);
    assert(pool.mul(a, big, pool.integer(a, -2))->as_int() == kMin);
    assert(pool.neg(a, low)->as_int() == kMax);
    assert(pool.pow(a, pool.integer(a, 1000), pool.integer(a, 62))->as_int() == kMax);

    // The n-ary fold takes the same route.  A third operand keeps the list
    // off the binary fast path.
    const Expr* sym = pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS);
    const Expr* terms[] = {big, one, sym};
    const Expr* summed = pool.make(a, Op::ADD, terms).peek().value();
    bool found_constant = false;
    for (uint8_t i = 0; i < summed->nargs; ++i)
        if (summed->arg(i)->op == Op::INTEGER) {
            assert(summed->arg(i)->as_int() == kMax);
            found_constant = true;
        }
    assert(found_constant);

    // The most negative int64 over minus one has no int64 quotient.  The
    // answer saturates; it never traps and never wraps.
    const Expr* quotient = pool.floor_div(a, low, pool.integer(a, -1));
    assert(quotient->op != Op::INTEGER || quotient->as_int() == kMax);

    std::printf("  test_constant_folding_saturates: PASSED\n");
}

// A full-width sum has no slot left for a folded constant, because a 256th
// child cannot be named.  The constant goes one level up instead, which names
// the same value with every node inside the ceiling.
static void test_folded_constant_nests_at_the_ceiling() {
    auto t = effects::testing::test();
    const auto a = t.alloc;
    ExprPool pool{a};

    const Expr* operands[Expr::kMaxArgs];
    for (uint32_t i = 0; i < Expr::kMaxArgs; ++i)
        operands[i] = pool.symbol(a, "s", SymbolId{i}, NUM_FLAGS);
    const std::span<const Expr* const> all{operands, Expr::kMaxArgs};

    for (Op op : {Op::ADD, Op::MUL}) {
        const Expr* wide = pool.make(a, op, all).peek().value();
        assert(wide->op == op);
        assert(wide->nargs == Expr::kMaxArgs);

        const Expr* constant = pool.integer(a, (op == Op::ADD) ? 5 : 7);
        const Expr* both[] = {constant, wide};
        const Expr* nested = pool.make(a, op, both).peek().value();

        assert(nested->op == op);
        assert(nested->nargs == 2);
        bool keeps_wide = false;
        bool keeps_constant = false;
        for (uint8_t i = 0; i < nested->nargs; ++i) {
            if (nested->arg(i) == wide) keeps_wide = true;
            if (nested->arg(i) == constant) keeps_constant = true;
        }
        assert(keeps_wide);
        assert(keeps_constant);
        // The nested form is still a canonical node, so it interns once.
        assert(nested == pool.make(a, op, both).peek().value());
    }

    std::printf("  test_folded_constant_nests_at_the_ceiling: PASSED\n");
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
    test_wide_variadic_operands();
    test_wide_logical_operands();
    test_flatten_degrades_at_the_ceiling();
    test_intern_identity_across_rehash();
    test_constant_folding_saturates();
    test_folded_constant_nests_at_the_ceiling();

    std::printf("test_expr_pool_fast_symbol: ALL PASSED\n");
    return 0;
}
