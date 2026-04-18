// ExprPool interning benchmark — measures intern() hot path latency.
//
// Target: intern() cache hit ≤ 10 ns (median).  Benches:
//   atom hit/miss, binary/ternary hit, pointer compare, integer cache,
//   scaling 100/1000/10000, deep trees, mixed 90/10 hit/miss, constant
//   folding, identity elimination, generic make().

#include "bench_harness.h"

#include <crucible/Effects.h>
#include <crucible/ExprPool.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace crucible;

static constexpr uint16_t NUM_FLAGS =
    ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
    ExprFlags::IS_FINITE  | ExprFlags::IS_NUMBER;

// Build ADD(ADD(... ADD(x, y) ..., z_i) ..., z_j) to a given depth.
static const Expr* build_deep_tree(fx::Alloc a, ExprPool& pool, int depth) {
    const Expr* x = pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS);
    const Expr* y = pool.symbol(a, "y", SymbolId{1}, NUM_FLAGS);
    const Expr* node = pool.add(a, x, y);
    for (int i = 2; i < depth; ++i) {
        const Expr* z = pool.symbol(
            a, ("z" + std::to_string(i)).c_str(),
            SymbolId{static_cast<uint32_t>(i)}, NUM_FLAGS);
        node = pool.add(a, node, z);
    }
    return node;
}

static void populate_pool(fx::Alloc a, ExprPool& pool,
                          int n, std::vector<const Expr*>& out) {
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        out.push_back(pool.integer(a, static_cast<int64_t>(i) + 1000));
    }
}

int main() {
    std::printf("=== ExprPool Interning Benchmark ===\n");
    std::printf("    Target: intern() cache hit <= 10ns\n\n");

    fx::Test t;
    const auto a = t.alloc;

    // ── 1. Atom cache hit (integer) ──
    std::printf("── Atom (integer) cache hit ──\n");
    {
        ExprPool pool{a};
        const Expr* e42 = pool.integer(a, 42);
        bench::DoNotOptimize(e42);
        BENCH("  integer(42) [cached, hit]", 10'000'000, {
            const Expr* r = pool.integer(a, 42);
            bench::DoNotOptimize(r);
        });
        const Expr* e42b = pool.integer(a, 42);
        if (e42 != e42b) {
            std::fprintf(stderr, "  ERROR: interning broken!\n");
            return 1;
        }
    }

    // ── 2. Atom cache hit (symbol) ──
    {
        ExprPool pool{a};
        const Expr* sx = pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS);
        bench::DoNotOptimize(sx);
        BENCH("  symbol('x') [hit]", 10'000'000, {
            const Expr* r = pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS);
            bench::DoNotOptimize(r);
        });
    }

    // ── 3. Binary cache hit (add) ──
    std::printf("\n── Binary expression cache hit ──\n");
    {
        ExprPool pool{a};
        const Expr* x = pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS);
        const Expr* y = pool.symbol(a, "y", SymbolId{1}, NUM_FLAGS);
        const Expr* sum = pool.add(a, x, y);
        bench::DoNotOptimize(sum);
        BENCH("  add(x, y) [hit]", 10'000'000, {
            const Expr* r = pool.add(a, x, y);
            bench::DoNotOptimize(r);
        });
    }

    // ── 4. Binary cache hit (mul) ──
    {
        ExprPool pool{a};
        const Expr* x = pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS);
        const Expr* y = pool.symbol(a, "y", SymbolId{1}, NUM_FLAGS);
        const Expr* prod = pool.mul(a, x, y);
        bench::DoNotOptimize(prod);
        BENCH("  mul(x, y) [hit]", 10'000'000, {
            const Expr* r = pool.mul(a, x, y);
            bench::DoNotOptimize(r);
        });
    }

    // ── 5. Ternary cache hit (where) ──
    {
        ExprPool pool{a};
        const Expr* c = pool.symbol(a, "cond", SymbolId{0},
                                    ExprFlags::IS_BOOLEAN);
        const Expr* x = pool.symbol(a, "x", SymbolId{1}, NUM_FLAGS);
        const Expr* y = pool.symbol(a, "y", SymbolId{2}, NUM_FLAGS);
        const Expr* w = pool.where(a, c, x, y);
        bench::DoNotOptimize(w);
        BENCH("  where(c, x, y) [hit]", 10'000'000, {
            const Expr* r = pool.where(a, c, x, y);
            bench::DoNotOptimize(r);
        });
    }

    // ── 6. Pointer comparison ──
    std::printf("\n── Pointer comparison (post-interning) ──\n");
    {
        ExprPool pool{a};
        const Expr* ea = pool.add(a, pool.integer(a, 1), pool.integer(a, 2));
        const Expr* eb = pool.add(a, pool.integer(a, 1), pool.integer(a, 2));
        BENCH("  ptr compare (a == b)", 100'000'000, {
            bool same = (ea == eb);
            bench::DoNotOptimize(same);
        });
    }

    // ── 7. Integer cache: fast path ──
    std::printf("\n── Integer cache ([-128, 127] fast path) ──\n");
    {
        ExprPool pool{a};
        for (int64_t i = -128; i <= 127; ++i)
            bench::DoNotOptimize(pool.integer(a, i));
        BENCH("  integer(0) [int_cache hit]", 100'000'000, {
            const Expr* r = pool.integer(a, 0);
            bench::DoNotOptimize(r);
        });
        BENCH("  integer(42) [int_cache hit]", 100'000'000, {
            const Expr* r = pool.integer(a, 42);
            bench::DoNotOptimize(r);
        });
        BENCH("  integer(999) [uncached, intern]", 10'000'000, {
            const Expr* r = pool.integer(a, 999);
            bench::DoNotOptimize(r);
        });
    }

    // ── 8. Scaling: pool with 100/1000/10000 entries ──
    std::printf("\n── Scaling: cache hit with N entries in pool ──\n");
    for (int n : {100, 1000, 10000}) {
        ExprPool pool{a};
        std::vector<const Expr*> entries;
        populate_pool(a, pool, n, entries);
        const Expr* target = entries[static_cast<size_t>(n / 2)];
        int64_t target_val = target->as_int();

        char label[80];
        std::snprintf(label, sizeof(label),
                      "  integer(%ld) [hit, pool=%d]", target_val, n);
        BENCH(label, 5'000'000, {
            const Expr* r = pool.integer(a, target_val);
            bench::DoNotOptimize(r);
        });
    }

    // ── 9. Tree depth: cache hit for deep trees ──
    std::printf("\n── Tree depth: cache hit for deep expressions ──\n");
    for (int depth : {2, 4, 8}) {
        ExprPool pool{a};
        const Expr* tree = build_deep_tree(a, pool, depth);
        bench::DoNotOptimize(tree);

        char label[80];
        std::snprintf(label, sizeof(label),
                      "  build_deep_tree(depth=%d) [all hits]", depth);
        BENCH(label, 1'000'000, {
            const Expr* r = build_deep_tree(a, pool, depth);
            bench::DoNotOptimize(r);
        });
    }

    // ── 10. Cache miss: create novel expressions ──
    std::printf("\n── Cache miss: intern novel expressions ──\n");
    {
        BENCH("  integer(novel) [miss]", 1'000'000, {
            static int64_t counter = 100000;
            ExprPool pool(a, 1 << 10);
            const Expr* r = pool.integer(a, counter++);
            bench::DoNotOptimize(r);
        });
    }

    // ── 11. Mixed hit/miss workload ──
    std::printf("\n── Mixed workload: 90%% hit, 10%% miss ──\n");
    {
        ExprPool pool{a};
        std::vector<const Expr*> symbols;
        for (uint32_t i = 0; i < 100; ++i) {
            char name[16];
            std::snprintf(name, sizeof(name), "s%u", i);
            symbols.push_back(pool.symbol(a, name, SymbolId{i}, NUM_FLAGS));
        }
        std::vector<const Expr*> adds;
        for (uint32_t i = 0; i < 100; ++i) {
            adds.push_back(pool.add(a, symbols[i], symbols[(i + 1) % 100]));
        }
        uint32_t idx = 0;
        BENCH("  mixed 90/10 hit/miss", 5'000'000, {
            if (idx % 10 == 0) {
                const Expr* r = pool.mul(a,
                    adds[idx % 100],
                    pool.integer(a, static_cast<int64_t>(idx) + 10000));
                bench::DoNotOptimize(r);
            } else {
                uint32_t j = idx % 100;
                const Expr* r = pool.add(a, symbols[j], symbols[(j + 1) % 100]);
                bench::DoNotOptimize(r);
            }
            ++idx;
        });
    }

    // ── 12. Hash quality diagnostic ──
    std::printf("\n── Hash quality (probe depth) ──\n");
    {
        ExprPool pool{a};
        for (int i = 0; i < 10000; ++i) {
            pool.integer(a, static_cast<int64_t>(i) + 5000);
        }
        std::printf("  pool size:     %zu\n", pool.intern_size());
        std::printf("  pool capacity: %zu\n", pool.intern_capacity());
        double load = static_cast<double>(pool.intern_size())
                    / static_cast<double>(pool.intern_capacity());
        std::printf("  load factor:   %.1f%%\n", load * 100.0);
        std::printf("  arena bytes:   %zu\n", pool.arena_bytes());
    }

    // ── 13. Raw intern_node via make() for generic ops ──
    std::printf("\n── Generic make() cache hit ──\n");
    {
        ExprPool pool{a};
        const Expr* x = pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS);
        const Expr* y = pool.symbol(a, "y", SymbolId{1}, NUM_FLAGS);
        const Expr* args[] = {x, y};
        const Expr* e = pool.make(a, Op::ADD, args);
        bench::DoNotOptimize(e);
        BENCH("  make(ADD, {x, y}) [hit]", 10'000'000, {
            const Expr* r = pool.make(a, Op::ADD, args);
            bench::DoNotOptimize(r);
        });
    }

    // ── 14. Constant folding (does not hit intern) ──
    std::printf("\n── Constant folding (no intern) ──\n");
    {
        ExprPool pool{a};
        const Expr* three = pool.integer(a, 3);
        const Expr* five = pool.integer(a, 5);
        BENCH("  add(3, 5) [fold → 8]", 10'000'000, {
            const Expr* r = pool.add(a, three, five);
            bench::DoNotOptimize(r);
        });
        BENCH("  mul(3, 5) [fold → 15]", 10'000'000, {
            const Expr* r = pool.mul(a, three, five);
            bench::DoNotOptimize(r);
        });
    }

    // ── 15. Identity elimination (no intern) ──
    std::printf("\n── Identity elimination (no intern) ──\n");
    {
        ExprPool pool{a};
        const Expr* x = pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS);
        const Expr* zero = pool.integer(a, 0);
        const Expr* one = pool.integer(a, 1);
        BENCH("  add(x, 0) [identity → x]", 100'000'000, {
            const Expr* r = pool.add(a, x, zero);
            bench::DoNotOptimize(r);
        });
        BENCH("  mul(x, 1) [identity → x]", 100'000'000, {
            const Expr* r = pool.mul(a, x, one);
            bench::DoNotOptimize(r);
        });
    }

    std::printf("\n=== Done ===\n");
    return 0;
}
