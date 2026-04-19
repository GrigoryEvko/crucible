// ExprPool interning microbench — every intern path individually.
//
// Target: intern() cache hit ≤ 10 ns median. The bench covers atoms
// (integer / symbol), binary ops (add, mul), ternary (where), pointer-
// equality post-interning, the [-128, 127] integer fast cache, scaling
// at 100/1000/10k pool entries, deep-tree interning, novel-expression
// miss path, 90/10 mixed workload, generic make(), constant folding,
// and identity elimination.
//
// Hash-quality is a separate non-timed diagnostic printed once before
// the timing block.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <crucible/Effects.h>
#include <crucible/ExprPool.h>

#include "bench_harness.h"

using namespace crucible;

namespace {

constexpr uint16_t NUM_FLAGS =
    ExprFlags::IS_INTEGER | ExprFlags::IS_REAL |
    ExprFlags::IS_FINITE  | ExprFlags::IS_NUMBER;

// ADD(ADD(... ADD(x, y) ..., z_i) ..., z_j) to a given depth.
const Expr* build_deep_tree(fx::Alloc a, ExprPool& pool, int depth) {
    const Expr* x    = pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS);
    const Expr* y    = pool.symbol(a, "y", SymbolId{1}, NUM_FLAGS);
    const Expr* node = pool.add(a, x, y);
    for (int i = 2; i < depth; ++i) {
        const Expr* z = pool.symbol(
            a, ("z" + std::to_string(i)).c_str(),
            SymbolId{static_cast<uint32_t>(i)}, NUM_FLAGS);
        node = pool.add(a, node, z);
    }
    return node;
}

void populate_pool(fx::Alloc a, ExprPool& pool, int n,
                   std::vector<const Expr*>& out) {
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        out.push_back(pool.integer(a, static_cast<int64_t>(i) + 1000));
    }
}

} // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    std::printf("=== expr_pool ===\n  target: intern() cache hit ≤ 10 ns median\n\n");

    fx::Test   t{};
    const auto a = t.alloc;

    // ── Pre-bench diagnostic: hash-table load factor after 10 k entries.
    {
        ExprPool pool{a};
        for (int i = 0; i < 10000; ++i) {
            pool.integer(a, static_cast<int64_t>(i) + 5000);
        }
        const double load = static_cast<double>(pool.intern_size())
                          / static_cast<double>(pool.intern_capacity());
        std::printf("  hash-table load factor: %.1f%% (%zu / %zu entries, arena %zu B)\n\n",
                    load * 100.0,
                    pool.intern_size(), pool.intern_capacity(),
                    pool.arena_bytes());
    }

    std::vector<bench::Report> reports;
    reports.reserve(22);

    // ── Atom cache hits ───────────────────────────────────────────────
    reports.push_back([&]{
        ExprPool pool{a};
        const Expr* e42 = pool.integer(a, 42);
        bench::do_not_optimize(e42);
        auto r = bench::run("integer(42)  [cached, hit]", [&]{
            const Expr* r = pool.integer(a, 42);
            bench::do_not_optimize(r);
        });
        // Sanity check — both pointers must match post-intern.
        if (pool.integer(a, 42) != e42) {
            std::fprintf(stderr, "[FATAL] interning broken!\n");
            std::exit(1);
        }
        return r;
    }());

    reports.push_back([&]{
        ExprPool pool{a};
        const Expr* sx = pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS);
        bench::do_not_optimize(sx);
        return bench::run("symbol('x') [hit]", [&]{
            const Expr* r = pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS);
            bench::do_not_optimize(r);
        });
    }());

    // ── Binary / ternary cache hits ───────────────────────────────────
    reports.push_back([&]{
        ExprPool pool{a};
        const Expr* x   = pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS);
        const Expr* y   = pool.symbol(a, "y", SymbolId{1}, NUM_FLAGS);
        const Expr* sum = pool.add(a, x, y);
        bench::do_not_optimize(sum);
        return bench::run("add(x, y) [hit]", [&]{
            const Expr* r = pool.add(a, x, y);
            bench::do_not_optimize(r);
        });
    }());

    reports.push_back([&]{
        ExprPool pool{a};
        const Expr* x    = pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS);
        const Expr* y    = pool.symbol(a, "y", SymbolId{1}, NUM_FLAGS);
        const Expr* prod = pool.mul(a, x, y);
        bench::do_not_optimize(prod);
        return bench::run("mul(x, y) [hit]", [&]{
            const Expr* r = pool.mul(a, x, y);
            bench::do_not_optimize(r);
        });
    }());

    reports.push_back([&]{
        ExprPool pool{a};
        const Expr* c = pool.symbol(a, "cond", SymbolId{0}, ExprFlags::IS_BOOLEAN);
        const Expr* x = pool.symbol(a, "x",    SymbolId{1}, NUM_FLAGS);
        const Expr* y = pool.symbol(a, "y",    SymbolId{2}, NUM_FLAGS);
        const Expr* w = pool.where(a, c, x, y);
        bench::do_not_optimize(w);
        return bench::run("where(c, x, y) [hit]", [&]{
            const Expr* r = pool.where(a, c, x, y);
            bench::do_not_optimize(r);
        });
    }());

    // ── Pointer equality post-intern (should compile to single cmp) ──
    reports.push_back([&]{
        ExprPool pool{a};
        const Expr* ea = pool.add(a, pool.integer(a, 1), pool.integer(a, 2));
        const Expr* eb = pool.add(a, pool.integer(a, 1), pool.integer(a, 2));
        return bench::run("ptr compare (ea == eb)", [&]{
            bool same = (ea == eb);
            bench::do_not_optimize(same);
        });
    }());

    // ── Integer [-128, 127] fast-cache path ───────────────────────────
    // Three Runs share a pre-populated pool fixture.
    {
        ExprPool pool{a};
        for (int64_t i = -128; i <= 127; ++i) {
            bench::do_not_optimize(pool.integer(a, i));
        }
        reports.push_back(bench::run("integer(0)    [int_cache hit]", [&]{
            const Expr* r = pool.integer(a, 0);
            bench::do_not_optimize(r);
        }));
        reports.push_back(bench::run("integer(42)   [int_cache hit]", [&]{
            const Expr* r = pool.integer(a, 42);
            bench::do_not_optimize(r);
        }));
        reports.push_back(bench::run("integer(999)  [intern]", [&]{
            const Expr* r = pool.integer(a, 999);
            bench::do_not_optimize(r);
        }));
    }

    // ── Scaling: cache hit with N entries in pool ─────────────────────
    for (int n : {100, 1000, 10000}) {
        reports.push_back([&, n]{
            ExprPool pool{a};
            std::vector<const Expr*> entries;
            populate_pool(a, pool, n, entries);
            const Expr* target     = entries[static_cast<size_t>(n / 2)];
            const int64_t target_v = target->as_int();

            char label[80];
            std::snprintf(label, sizeof(label),
                          "integer(%ld)  [hit, pool=%d]", target_v, n);
            return bench::run(label, [&]{
                const Expr* r = pool.integer(a, target_v);
                bench::do_not_optimize(r);
            });
        }());
    }

    // ── Tree depth: cache hit for deep expressions ────────────────────
    for (int depth : {2, 4, 8}) {
        reports.push_back([&, depth]{
            ExprPool pool{a};
            const Expr* tree = build_deep_tree(a, pool, depth);
            bench::do_not_optimize(tree);

            char label[80];
            std::snprintf(label, sizeof(label),
                          "build_deep_tree(depth=%d) [all hits]", depth);
            return bench::run(label, [&, depth]{
                const Expr* r = build_deep_tree(a, pool, depth);
                bench::do_not_optimize(r);
            });
        }());
    }

    // ── Cache miss — novel integer each call ─────────────────────────
    // ExprPool is re-created inside the body (auto-batch safe) because
    // the novel-integer growth would otherwise blow the pool across
    // samples. Fresh pool per body, matching the old bench's intent.
    reports.push_back([&]{
        int64_t counter = 100000;
        return bench::run("integer(novel) [miss]", [&]{
            ExprPool pool(a, 1 << 10);
            const Expr* r = pool.integer(a, counter++);
            bench::do_not_optimize(r);
        });
    }());

    // ── Mixed 90% hit / 10% miss ──────────────────────────────────────
    reports.push_back([&]{
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
        return bench::run("mixed 90/10 hit/miss", [&]{
            if (idx % 10 == 0) {
                const Expr* r = pool.mul(
                    a, adds[idx % 100],
                    pool.integer(a, static_cast<int64_t>(idx) + 10000));
                bench::do_not_optimize(r);
            } else {
                const uint32_t j = idx % 100;
                const Expr* r = pool.add(a, symbols[j], symbols[(j + 1) % 100]);
                bench::do_not_optimize(r);
            }
            ++idx;
        });
    }());

    // ── Generic make() for arbitrary Op kind ──────────────────────────
    reports.push_back([&]{
        ExprPool pool{a};
        const Expr* x = pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS);
        const Expr* y = pool.symbol(a, "y", SymbolId{1}, NUM_FLAGS);
        const Expr* args[] = {x, y};
        const Expr* e = pool.make(a, Op::ADD, args);
        bench::do_not_optimize(e);
        return bench::run("make(ADD, {x, y}) [hit]", [&]{
            const Expr* r = pool.make(a, Op::ADD, args);
            bench::do_not_optimize(r);
        });
    }());

    // ── Constant folding (doesn't touch intern) ──────────────────────
    {
        ExprPool pool{a};
        const Expr* three = pool.integer(a, 3);
        const Expr* five  = pool.integer(a, 5);
        reports.push_back(bench::run("add(3, 5) [fold → 8]", [&]{
            const Expr* r = pool.add(a, three, five);
            bench::do_not_optimize(r);
        }));
        reports.push_back(bench::run("mul(3, 5) [fold → 15]", [&]{
            const Expr* r = pool.mul(a, three, five);
            bench::do_not_optimize(r);
        }));
    }

    // ── Identity elimination (doesn't touch intern) ──────────────────
    {
        ExprPool pool{a};
        const Expr* x    = pool.symbol(a, "x", SymbolId{0}, NUM_FLAGS);
        const Expr* zero = pool.integer(a, 0);
        const Expr* one  = pool.integer(a, 1);
        reports.push_back(bench::run("add(x, 0) [identity → x]", [&]{
            const Expr* r = pool.add(a, x, zero);
            bench::do_not_optimize(r);
        }));
        reports.push_back(bench::run("mul(x, 1) [identity → x]", [&]{
            const Expr* r = pool.mul(a, x, one);
            bench::do_not_optimize(r);
        }));
    }

    // ── Scaling: construction cost vs initial_capacity ───────────────
    //
    // Default is 16384 slots (ExprPool::kDefaultInitialCapacity) — fits
    // 14078 user exprs at 87.5% load → covers ViT-scale real networks
    // (~15k DAG ops) with zero rehashes.
    //
    // Capacity choices:
    //   512      — legacy "tiny" (forces rehash chain on real graphs)
    //   16384    — current default (fits 10k+ op networks, mmap-backed)
    //   65536    — historical default (576 KB up front, more headroom)
    //   1 << 20  — warmed-up production KernelCache scale
    for (size_t cap : {size_t{512},
                       ExprPool::kDefaultInitialCapacity,
                       size_t{1u} << 16,
                       size_t{1u} << 20}) {
        char label[64];
        std::snprintf(label, sizeof(label),
                      "ExprPool ctor + dtor [init_cap=%zu]", cap);
        reports.push_back([&, cap]{
            return bench::run(label, [&, cap]{
                ExprPool pool(a, cap);
                bench::do_not_optimize(pool);
            });
        }());
    }

    // ── Scaling: N inserts into empty pool (grow-sequence cost) ──────
    //
    // Starts from the default capacity and inserts N unique integers,
    // going through the auto-rehash chain. The body constructs a fresh
    // pool each call so the grow sequence is measured from scratch.
    // Expected shape: super-linear per-insert cost at small N (rehashes
    // dominate), amortized to ~10 ns per insert at large N.
    for (size_t n_inserts : {size_t{100}, size_t{1'000}, size_t{10'000}}) {
        char label[64];
        std::snprintf(label, sizeof(label),
                      "grow sequence: %zu inserts from default", n_inserts);
        reports.push_back([&, n_inserts]{
            return bench::run(label, [&, n_inserts]{
                ExprPool pool(a);                      // default init cap
                for (size_t i = 0; i < n_inserts; ++i)
                    bench::do_not_optimize(pool.integer(a, 10'000 + static_cast<int64_t>(i)));
            });
        }());
    }

    // ── Scaling: reserve(N) + N inserts (zero-grow cost) ─────────────
    //
    // Compared against the previous trio: the delta is the cost of the
    // grow sequence. A caller that knows its final size ahead of time
    // pays the reserve() once and skips every rehash. Under the Swiss-
    // table's 87.5% load factor, reserve(N) allocates ceil(N*8/7)
    // rounded to the next power of 2 — for N=10000 that's 16384 slots.
    for (size_t n_inserts : {size_t{100}, size_t{1'000}, size_t{10'000}}) {
        char label[64];
        std::snprintf(label, sizeof(label),
                      "reserve(%zu) + %zu inserts", n_inserts, n_inserts);
        reports.push_back([&, n_inserts]{
            return bench::run(label, [&, n_inserts]{
                ExprPool pool(a);
                pool.reserve(n_inserts);
                for (size_t i = 0; i < n_inserts; ++i)
                    bench::do_not_optimize(pool.integer(a, 10'000 + static_cast<int64_t>(i)));
            });
        }());
    }

    bench::emit_reports_text(reports);

    // ── Compare: does reserve() actually pay off? ───────────────────
    //
    // Last 10 Reports are the scaling scenarios:
    //   [L-10 .. L-7]  ctor+dtor @ init_cap = 512/16384(default)/64k/1M
    //   [L-6  .. L-4]  grow-sequence: N inserts, N = 100/1000/10000
    //   [L-3  .. L-1]  reserve(N) + N inserts, same Ns
    //
    // Pair each grow-sequence scenario with its reserve counterpart —
    // the Δ is pure rehash cost that reserve() avoids. At N=10000 the
    // grow sequence triggers ~6 rehashes (512 → 16384 by doubling);
    // reserve() goes straight to 16384.
    const size_t L = reports.size();

    std::printf("\n=== compare — ctor cost vs initial_capacity ===\n");
    std::printf("  tiny 512 vs default 16384:\n  ");
    bench::compare(reports[L - 9], reports[L - 8]).print_text(stdout);
    std::printf("  default vs max (1<<20):\n  ");
    bench::compare(reports[L - 9], reports[L - 7]).print_text(stdout);

    std::printf("\n=== compare — reserve() vs grow-sequence ===\n");
    for (size_t k = 0; k < 3; ++k) {
        std::printf("  %s  →  %s:\n  ",
                    reports[L - 6 + k].name.c_str(),
                    reports[L - 3 + k].name.c_str());
        bench::compare(reports[L - 6 + k], reports[L - 3 + k]).print_text(stdout);
    }

    bench::emit_reports_json(reports, json);
    return 0;
}
