// SwissCtrl SIMD control-byte operations.
//
// Covers:
//   1. CtrlGroup::match(h2)     — single-group H2 tag matching
//   2. CtrlGroup::match_empty() — single-group empty slot detection
//   3. BitMask iteration        — traversing match results
//   4. CtrlGroup::load() aligned vs. unaligned
//   5. h2_tag() hash extraction
//   6. Combined match+empty probe step
//   7. Full probe sequences at 25/50/75/87.5% load factors (hit + miss)
//
// Target: probe operations ≤ 5 ns.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <crucible/Expr.h>
#include <crucible/SwissTable.h>

#include "bench_harness.h"

using crucible::detail::BitMask;
using crucible::detail::CtrlGroup;
using crucible::detail::fmix64;
using crucible::detail::h2_tag;
using crucible::detail::kEmpty;
using crucible::detail::kGroupWidth;

namespace {

// Fill ctrl bytes: `occupied_pct` gets random H2 tags, rest get kEmpty.
void fill_ctrl(int8_t* ctrl, size_t capacity, double occupied_pct, uint64_t seed) {
    uint64_t state = seed;
    for (size_t i = 0; i < capacity; ++i) {
        state = fmix64(state + i);
        const double r = static_cast<double>(state & 0xFFFFFFFF) / 4294967296.0;
        ctrl[i] = (r < occupied_pct) ? static_cast<int8_t>(state & 0x7F) : kEmpty;
    }
}

// Minimal Swiss-table simulator for full-probe benches. Triangular probing.
struct MiniSwissTable {
    int8_t*   ctrl   = nullptr;
    uint64_t* hashes = nullptr;
    size_t    capacity = 0;
    size_t    count    = 0;

    explicit MiniSwissTable(size_t cap) : capacity{cap} {
        ctrl   = static_cast<int8_t*>(std::malloc(cap));
        hashes = static_cast<uint64_t*>(std::calloc(cap, sizeof(uint64_t)));
        if (!ctrl || !hashes) std::abort();
        std::memset(ctrl, 0x80, cap);  // all empty
    }
    ~MiniSwissTable() { std::free(ctrl); std::free(hashes); }
    MiniSwissTable(const MiniSwissTable&)            = delete;
    MiniSwissTable& operator=(const MiniSwissTable&) = delete;
    MiniSwissTable(MiniSwissTable&&)                 = delete;
    MiniSwissTable& operator=(MiniSwissTable&&)      = delete;

    size_t insert(uint64_t h) {
        const int8_t tag = h2_tag(h);
        const size_t num_groups = capacity / kGroupWidth;
        const size_t group_mask = num_groups - 1;
        size_t g = h & group_mask;
        size_t probe = 0;
        while (true) {
            const size_t base = g * kGroupWidth;
            auto group = CtrlGroup::load(&ctrl[base]);
            if (auto empties = group.match_empty(); empties) {
                const size_t idx = base + empties.lowest();
                ctrl[idx] = tag;
                hashes[idx] = h;
                ++count;
                return idx;
            }
            ++probe;
            g = (g + probe) & group_mask;
        }
    }

    [[nodiscard]] bool find(uint64_t h) const {
        const int8_t tag = h2_tag(h);
        const size_t num_groups = capacity / kGroupWidth;
        const size_t group_mask = num_groups - 1;
        size_t g = h & group_mask;
        size_t probe = 0;
        while (true) {
            const size_t base = g * kGroupWidth;
            auto group = CtrlGroup::load(&ctrl[base]);
            auto matches = group.match(tag);
            while (matches) {
                const size_t idx = base + matches.lowest();
                if (hashes[idx] == h) return true;
                matches.clear_lowest();
            }
            if (auto empties = group.match_empty(); empties) return false;
            ++probe;
            g = (g + probe) & group_mask;
        }
    }
};

} // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    std::printf("=== swiss_table ===\n  backend: ");
#if defined(__AVX512BW__)
    std::printf("AVX-512BW (64 B/group)\n");
#elif defined(__AVX2__)
    std::printf("AVX2 (32 B/group)\n");
#elif defined(__SSE2__)
    std::printf("SSE2 (16 B/group)\n");
#elif defined(__aarch64__)
    std::printf("NEON (16 B/group)\n");
#else
    std::printf("portable SWAR (16 B/group)\n");
#endif
    std::printf("  kGroupWidth: %zu\n\n", kGroupWidth);

    std::vector<bench::Report> reports;
    reports.reserve(32);

    // ── h2_tag ───────────────────────────────────────────────────────
    {
        volatile uint64_t hash = 0x123456789ABCDEF0ULL;
        reports.push_back(bench::run("h2_tag(hash)", [&]{
            auto tag = h2_tag(hash);
            bench::do_not_optimize(tag);
        }));
    }

    // ── load() aligned vs unaligned ──────────────────────────────────
    reports.push_back([&]{
        alignas(64) int8_t ctrl[kGroupWidth * 4];
        fill_ctrl(ctrl, kGroupWidth * 4, 0.5, 99);
        return bench::run("load() aligned", [&]{
            auto g = CtrlGroup::load(ctrl);
            bench::do_not_optimize(g);
        });
    }());
    reports.push_back([&]{
        alignas(64) int8_t ctrl[kGroupWidth * 4];
        fill_ctrl(ctrl, kGroupWidth * 4, 0.5, 99);
        const int8_t* unaligned = ctrl + 3;
        return bench::run("load() unaligned (+3)", [&]{
            auto g = CtrlGroup::load(unaligned);
            bench::do_not_optimize(g);
        });
    }());

    // ── match(h2) ─────────────────────────────────────────────────────
    reports.push_back([&]{
        alignas(64) int8_t ctrl[kGroupWidth];
        fill_ctrl(ctrl, kGroupWidth, 0.75, 42);
        const int8_t target = 0x37;
        ctrl[5] = target;
        auto group = CtrlGroup::load(ctrl);
        return bench::run("match(h2) — 1 match", [&]{
            auto m = group.match(target);
            bench::do_not_optimize(m);
        });
    }());

    reports.push_back([&]{
        alignas(64) int8_t ctrl[kGroupWidth];
        fill_ctrl(ctrl, kGroupWidth, 0.75, 42);
        const int8_t miss = 0x7F;
        for (size_t i = 0; i < kGroupWidth; ++i)
            if (ctrl[i] == miss) ctrl[i] = 0x01;
        auto group = CtrlGroup::load(ctrl);
        return bench::run("match(h2) — 0 matches (miss)", [&]{
            auto m = group.match(miss);
            bench::do_not_optimize(m);
        });
    }());

    reports.push_back([&]{
        alignas(64) int8_t ctrl[kGroupWidth];
        fill_ctrl(ctrl, kGroupWidth, 0.75, 42);
        const int8_t target = 0x37;
        for (size_t i = 0; i < kGroupWidth; i += 4) ctrl[i] = target;
        auto group = CtrlGroup::load(ctrl);
        return bench::run("match(h2) — multiple matches", [&]{
            auto m = group.match(target);
            bench::do_not_optimize(m);
        });
    }());

    // ── match_empty() ─────────────────────────────────────────────────
    reports.push_back([&]{
        alignas(64) int8_t ctrl[kGroupWidth];
        fill_ctrl(ctrl, kGroupWidth, 0.75, 123);
        auto group = CtrlGroup::load(ctrl);
        return bench::run("match_empty() — 75% load", [&]{
            auto m = group.match_empty();
            bench::do_not_optimize(m);
        });
    }());
    reports.push_back([&]{
        alignas(64) int8_t ctrl[kGroupWidth];
        std::memset(ctrl, 0x42, kGroupWidth);  // all occupied
        auto group = CtrlGroup::load(ctrl);
        return bench::run("match_empty() — 100% full", [&]{
            auto m = group.match_empty();
            bench::do_not_optimize(m);
        });
    }());
    reports.push_back([&]{
        alignas(64) int8_t ctrl[kGroupWidth];
        std::memset(ctrl, 0x80, kGroupWidth);  // all empty
        auto group = CtrlGroup::load(ctrl);
        return bench::run("match_empty() — 0% load (all empty)", [&]{
            auto m = group.match_empty();
            bench::do_not_optimize(m);
        });
    }());

    // ── BitMask iteration ────────────────────────────────────────────
    auto iterate_mask = [](BitMask m) noexcept -> uint32_t {
        uint32_t sum = 0;
        while (m) { sum += m.lowest(); m.clear_lowest(); }
        return sum;
    };
    for (auto [bits, label] : std::initializer_list<std::pair<uint16_t, const char*>>{
             {0x0010, "BitMask iterate — 1 bit"},
             {0x1248, "BitMask iterate — 4 bits"},
             {0x5555, "BitMask iterate — 8 bits"},
             {0xFFFF, "BitMask iterate — 16 bits"}}) {
        reports.push_back([&, bits, label]{
            const BitMask m{bits};
            return bench::run(label, [&]{
                auto r = iterate_mask(m);
                bench::do_not_optimize(r);
            });
        }());
    }

    // ── Combined probe step (match + empty check) ────────────────────
    reports.push_back([&]{
        alignas(64) int8_t ctrl[kGroupWidth];
        fill_ctrl(ctrl, kGroupWidth, 0.75, 42);
        const int8_t target = 0x37;
        ctrl[5] = target;
        auto group = CtrlGroup::load(ctrl);
        return bench::run("probe step (match+empty, hit)", [&]{
            auto matches = group.match(target);
            uint32_t found_idx = 0;
            while (matches) { found_idx = matches.lowest(); matches.clear_lowest(); }
            auto empties = group.match_empty();
            bench::do_not_optimize(found_idx);
            bench::do_not_optimize(empties);
        });
    }());

    reports.push_back([&]{
        alignas(64) int8_t ctrl[kGroupWidth];
        fill_ctrl(ctrl, kGroupWidth, 0.75, 42);
        const int8_t miss = 0x7E;
        for (size_t i = 0; i < kGroupWidth; ++i)
            if (ctrl[i] == miss) ctrl[i] = 0x01;
        auto group = CtrlGroup::load(ctrl);
        return bench::run("probe step (match+empty, miss→stop)", [&]{
            auto matches = group.match(miss);
            bool found = static_cast<bool>(matches);
            auto empties = group.match_empty();
            bool stop = static_cast<bool>(empties);
            bench::do_not_optimize(found);
            bench::do_not_optimize(stop);
        });
    }());

    // ── Full probe sequences at varying load factors ────────────────
    // Each load factor contributes 2 Reports (hit + miss). Factored
    // into a lambda that returns a pair of Reports captured through
    // the outer reports vector.
    auto bench_probe_at = [&](const char* label, double load_factor) {
        constexpr size_t TABLE_CAP = 1u << 16;  // 65536 slots
        const size_t target_count = static_cast<size_t>(TABLE_CAP * load_factor);

        // Fixture — lives for both Runs in this load factor.
        auto table = std::make_unique<MiniSwissTable>(TABLE_CAP);

        std::vector<uint64_t> inserted_hashes;
        inserted_hashes.reserve(1024);
        uint64_t seed = 0xDEADBEEF;
        for (size_t i = 0; i < target_count; ++i) {
            seed = fmix64(seed + i);
            table->insert(seed);
            if (inserted_hashes.size() < 1024) inserted_hashes.push_back(seed);
        }

        std::vector<uint64_t> absent_hashes;
        absent_hashes.reserve(1024);
        seed = 0xCAFEBABE;
        while (absent_hashes.size() < 1024) {
            seed = fmix64(seed + absent_hashes.size() + 1);
            if (!table->find(seed)) absent_hashes.push_back(seed);
        }

        char name_hit[64], name_miss[64];
        std::snprintf(name_hit,  sizeof(name_hit),  "probe HIT  — %s", label);
        std::snprintf(name_miss, sizeof(name_miss), "probe MISS — %s", label);

        uint32_t hit_idx = 0;
        reports.push_back(bench::run(name_hit, [&, t = table.get()]{
            bool found = t->find(inserted_hashes[hit_idx & 1023]);
            bench::do_not_optimize(found);
            ++hit_idx;
        }));
        uint32_t miss_idx = 0;
        reports.push_back(bench::run(name_miss, [&, t = table.get()]{
            bool found = t->find(absent_hashes[miss_idx & 1023]);
            bench::do_not_optimize(found);
            ++miss_idx;
        }));
    };

    bench_probe_at("25% load",       0.25);
    bench_probe_at("50% load",       0.50);
    bench_probe_at("75% load",       0.75);
    bench_probe_at("87.5% load max", 0.875);

    bench::emit_reports_text(reports);

    // Probe scaling: 25% vs 87.5% load MISS should grow with probe
    // distance. Non-trivial delta is the expected signal.
    std::printf("\n=== compare — load-factor scaling (MISS 25%% → 87.5%%) ===\n  ");
    // Reports layout: h2_tag(1) + load(2) + match(3) + match_empty(3)
    //               + BitMask(4) + probe-step(2) = 15 before probe-at.
    // bench_probe_at appends 2 per call; 25/50/75/87.5 → indices
    // 15..22 (16 new). MISS at 25% = reports[16], MISS at 87.5% = reports[22].
    bench::compare(reports[16], reports[22]).print_text(stdout);

    bench::emit_reports_json(reports, json);
    return 0;
}
