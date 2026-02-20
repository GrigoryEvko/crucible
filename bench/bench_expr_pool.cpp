// ExprPool interning benchmark — measures intern() hot path latency.
//
// Target: intern() cache hit <= 10ns.
//
// Benchmarks:
//   1. Cache hit: re-intern existing expression (the hot path)
//   2. Cache miss: intern novel expression (cold path)
//   3. Scaling: pool with 100/1000/10000 entries
//   4. Tree depth: leaf, binary, 4-deep, 8-deep
//   5. Pointer comparison: verify ~1ns after interning
//   6. Integer cache: cached range [-128, 127] vs uncached
//   7. Hash quality: measure probe depth under load
//   8. Atom vs composite: overhead of args comparison

#include "bench_harness.h"

#include <crucible/ExprPool.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace crucible;

// Build a chain of ADD(ADD(... ADD(x, y) ..., z_i) ..., z_j)
// to a given depth, returning the root expression.
static const Expr* build_deep_tree(ExprPool& pool, int depth) {
    const Expr* x = pool.symbol("x", SymbolId{0},
        ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
        ExprFlags::IS_FINITE | ExprFlags::IS_POSITIVE |
        ExprFlags::IS_NONNEGATIVE | ExprFlags::IS_NUMBER);
    const Expr* y = pool.symbol("y", SymbolId{1},
        ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
        ExprFlags::IS_FINITE | ExprFlags::IS_POSITIVE |
        ExprFlags::IS_NONNEGATIVE | ExprFlags::IS_NUMBER);

    const Expr* node = pool.add(x, y);
    for (int i = 2; i < depth; ++i) {
        const Expr* z = pool.symbol(
            ("z" + std::to_string(i)).c_str(),
            SymbolId{static_cast<uint32_t>(i)},
            ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
            ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER);
        node = pool.add(node, z);
    }
    return node;
}

// Pre-populate pool with N distinct integer expressions
static void populate_pool(ExprPool& pool, int n, std::vector<const Expr*>& out) {
    out.reserve(static_cast<size_t>(n));
    // Use integers outside the cached [-128, 127] range
    for (int i = 0; i < n; ++i) {
        out.push_back(pool.integer(static_cast<int64_t>(i) + 1000));
    }
}

int main() {
    std::printf("=== ExprPool Interning Benchmark ===\n");
    std::printf("    Target: intern() cache hit <= 10ns\n\n");

    // ── 1. Atom cache hit (integer) ──
    std::printf("── Atom (integer) cache hit ──\n");
    {
        ExprPool pool;
        // Pre-intern
        const Expr* e42 = pool.integer(42);
        bench::DoNotOptimize(e42);

        BENCH("  integer(42) [cached, hit]", 10'000'000, {
            const Expr* r = pool.integer(42);
            bench::DoNotOptimize(r);
        });

        // Verify interning works
        const Expr* e42b = pool.integer(42);
        if (e42 != e42b) {
            std::printf("  ERROR: interning broken! %p != %p\n",
                        static_cast<const void*>(e42),
                        static_cast<const void*>(e42b));
            return 1;
        }
    }

    // ── 2. Atom cache hit (symbol) ──
    {
        ExprPool pool;
        const Expr* sx = pool.symbol("x", SymbolId{0},
            ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
            ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER);
        bench::DoNotOptimize(sx);

        BENCH("  symbol('x') [hit]", 10'000'000, {
            const Expr* r = pool.symbol("x", SymbolId{0},
                ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
                ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER);
            bench::DoNotOptimize(r);
        });
    }

    // ── 3. Binary cache hit (add) ──
    std::printf("\n── Binary expression cache hit ──\n");
    {
        ExprPool pool;
        const Expr* x = pool.symbol("x", SymbolId{0},
            ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
            ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER);
        const Expr* y = pool.symbol("y", SymbolId{1},
            ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
            ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER);
        const Expr* sum = pool.add(x, y);
        bench::DoNotOptimize(sum);

        BENCH("  add(x, y) [hit]", 10'000'000, {
            const Expr* r = pool.add(x, y);
            bench::DoNotOptimize(r);
        });
    }

    // ── 4. Binary cache hit (mul) ──
    {
        ExprPool pool;
        const Expr* x = pool.symbol("x", SymbolId{0},
            ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
            ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER);
        const Expr* y = pool.symbol("y", SymbolId{1},
            ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
            ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER);
        const Expr* prod = pool.mul(x, y);
        bench::DoNotOptimize(prod);

        BENCH("  mul(x, y) [hit]", 10'000'000, {
            const Expr* r = pool.mul(x, y);
            bench::DoNotOptimize(r);
        });
    }

    // ── 5. Ternary cache hit (where) ──
    {
        ExprPool pool;
        const Expr* c = pool.symbol("cond", SymbolId{0}, ExprFlags::IS_BOOLEAN);
        const Expr* x = pool.symbol("x", SymbolId{1},
            ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
            ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER);
        const Expr* y = pool.symbol("y", SymbolId{2},
            ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
            ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER);
        const Expr* w = pool.where(c, x, y);
        bench::DoNotOptimize(w);

        BENCH("  where(c, x, y) [hit]", 10'000'000, {
            const Expr* r = pool.where(c, x, y);
            bench::DoNotOptimize(r);
        });
    }

    // ── 6. Pointer comparison ──
    std::printf("\n── Pointer comparison (post-interning) ──\n");
    {
        ExprPool pool;
        const Expr* a = pool.add(pool.integer(1), pool.integer(2));
        const Expr* b = pool.add(pool.integer(1), pool.integer(2));

        BENCH("  ptr compare (a == b)", 100'000'000, {
            bool same = (a == b);
            bench::DoNotOptimize(same);
        });
    }

    // ── 7. Integer cache: fast path ──
    std::printf("\n── Integer cache ([-128, 127] fast path) ──\n");
    {
        ExprPool pool;
        // Warm up the cache
        for (int64_t i = -128; i <= 127; ++i)
            bench::DoNotOptimize(pool.integer(i));

        BENCH("  integer(0) [int_cache hit]", 100'000'000, {
            const Expr* r = pool.integer(0);
            bench::DoNotOptimize(r);
        });

        BENCH("  integer(42) [int_cache hit]", 100'000'000, {
            const Expr* r = pool.integer(42);
            bench::DoNotOptimize(r);
        });

        BENCH("  integer(999) [uncached, intern]", 10'000'000, {
            const Expr* r = pool.integer(999);
            bench::DoNotOptimize(r);
        });
    }

    // ── 8. Scaling: pool with 100/1000/10000 entries ──
    std::printf("\n── Scaling: cache hit with N entries in pool ──\n");
    for (int n : {100, 1000, 10000}) {
        ExprPool pool;
        std::vector<const Expr*> entries;
        populate_pool(pool, n, entries);

        // Pick a random existing entry to re-intern
        const Expr* target = entries[static_cast<size_t>(n / 2)];
        int64_t target_val = target->as_int();

        char label[80];
        std::snprintf(label, sizeof(label),
                      "  integer(%ld) [hit, pool=%d]", target_val, n);
        BENCH(label, 5'000'000, {
            const Expr* r = pool.integer(target_val);
            bench::DoNotOptimize(r);
        });
    }

    // ── 9. Tree depth: cache hit for increasingly deep trees ──
    std::printf("\n── Tree depth: cache hit for deep expressions ──\n");
    for (int depth : {2, 4, 8}) {
        ExprPool pool;
        const Expr* tree = build_deep_tree(pool, depth);
        bench::DoNotOptimize(tree);

        // Re-build the same tree (all sub-expressions are cache hits)
        char label[80];
        std::snprintf(label, sizeof(label),
                      "  build_deep_tree(depth=%d) [all hits]", depth);
        BENCH(label, 1'000'000, {
            const Expr* r = build_deep_tree(pool, depth);
            bench::DoNotOptimize(r);
        });
    }

    // ── 10. Cache miss: create novel expressions ──
    std::printf("\n── Cache miss: intern novel expressions ──\n");
    {
        // Each iteration creates a fresh pool to avoid growing too large
        BENCH("  integer(novel) [miss]", 1'000'000, {
            // Use a counter to generate unique values
            static int64_t counter = 100000;
            ExprPool pool(1 << 10); // small pool for quick construction
            const Expr* r = pool.integer(counter++);
            bench::DoNotOptimize(r);
        });
    }

    // ── 11. Mixed hit/miss workload ──
    std::printf("\n── Mixed workload: 90%% hit, 10%% miss ──\n");
    {
        ExprPool pool;
        // Pre-populate with 100 symbols
        std::vector<const Expr*> symbols;
        for (uint32_t i = 0; i < 100; ++i) {
            char name[16];
            std::snprintf(name, sizeof(name), "s%u", i);
            symbols.push_back(pool.symbol(name, SymbolId{i},
                ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
                ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER));
        }

        // Pre-populate with 100 binary add expressions
        std::vector<const Expr*> adds;
        for (uint32_t i = 0; i < 100; ++i) {
            adds.push_back(pool.add(symbols[i], symbols[(i + 1) % 100]));
        }

        uint32_t idx = 0;
        BENCH("  mixed 90/10 hit/miss", 5'000'000, {
            if (idx % 10 == 0) {
                // Miss: create a new expression
                const Expr* r = pool.mul(
                    adds[idx % 100],
                    pool.integer(static_cast<int64_t>(idx) + 10000));
                bench::DoNotOptimize(r);
            } else {
                // Hit: re-lookup existing add
                uint32_t j = idx % 100;
                const Expr* r = pool.add(symbols[j], symbols[(j + 1) % 100]);
                bench::DoNotOptimize(r);
            }
            ++idx;
        });
    }

    // ── 12. Hash quality diagnostic ──
    std::printf("\n── Hash quality (probe depth) ──\n");
    {
        ExprPool pool;
        // Insert 10000 distinct integers and check pool stats
        for (int i = 0; i < 10000; ++i) {
            pool.integer(static_cast<int64_t>(i) + 5000);
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
        ExprPool pool;
        const Expr* x = pool.symbol("x", SymbolId{0},
            ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
            ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER);
        const Expr* y = pool.symbol("y", SymbolId{1},
            ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
            ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER);

        // Pre-intern via make()
        const Expr* args[] = {x, y};
        const Expr* e = pool.make(Op::ADD, args);
        bench::DoNotOptimize(e);

        BENCH("  make(ADD, {x, y}) [hit]", 10'000'000, {
            const Expr* r = pool.make(Op::ADD, args);
            bench::DoNotOptimize(r);
        });
    }

    // ── 14. Constant folding (does not hit intern) ──
    std::printf("\n── Constant folding (no intern) ──\n");
    {
        ExprPool pool;
        const Expr* three = pool.integer(3);
        const Expr* five = pool.integer(5);

        BENCH("  add(3, 5) [fold → 8]", 10'000'000, {
            const Expr* r = pool.add(three, five);
            bench::DoNotOptimize(r);
        });

        BENCH("  mul(3, 5) [fold → 15]", 10'000'000, {
            const Expr* r = pool.mul(three, five);
            bench::DoNotOptimize(r);
        });
    }

    // ── 15. Identity elimination (no intern) ──
    std::printf("\n── Identity elimination (no intern) ──\n");
    {
        ExprPool pool;
        const Expr* x = pool.symbol("x", SymbolId{0},
            ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
            ExprFlags::IS_FINITE | ExprFlags::IS_NUMBER);
        const Expr* zero = pool.integer(0);
        const Expr* one = pool.integer(1);

        BENCH("  add(x, 0) [identity → x]", 100'000'000, {
            const Expr* r = pool.add(x, zero);
            bench::DoNotOptimize(r);
        });

        BENCH("  mul(x, 1) [identity → x]", 100'000'000, {
            const Expr* r = pool.mul(x, one);
            bench::DoNotOptimize(r);
        });
    }

    std::printf("\n=== Done ===\n");
    return 0;
}
