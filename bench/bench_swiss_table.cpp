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

#include <cstddef>
#include <cstdint>

#include <crucible/SwissTable.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/Mix.h>

#include "bench_harness.h"

using crucible::detail::BitMask;
using crucible::detail::CtrlGroup;
using ::foundation::reflect::fmix64;
using crucible::detail::h2_tag;
using crucible::detail::kEmpty;

static constexpr size_t kGroupWidth = crucible::detail::group_width();

namespace {

// Fill ctrl bytes: `occupied_pct` gets random H2 tags, rest get kEmpty.
void fill_ctrl(::fixy::View<int8_t> ctrl, double occupied_pct, uint64_t seed) {
    uint64_t state = seed;
    uint64_t position = 0;
    for (int8_t& byte : ctrl) {
        state = fmix64(state + position);
        ++position;
        const double r = static_cast<double>(state & 0xFFFFFFFF) / 4294967296.0;
        byte = (r < occupied_pct) ? static_cast<int8_t>(state & 0x7F) : kEmpty;
    }
}

// The group of control bytes from offset.  Each caller gives an offset of a
// whole group inside the run.
CtrlGroup load_group(::fixy::View<const int8_t> ctrl, size_t offset) {
    return CtrlGroup::load(ctrl.template window<kGroupWidth>(offset).expect("a group lies inside the control bytes"));
}

// Minimal Swiss-table simulator for full-probe benches. Triangular probing.
// The capacity is a power of two, so each slot index reads its element
// through the mask with no check.
struct MiniSwissTable {
    ::fixy::MaskedArray<int8_t> ctrl{};
    ::fixy::MaskedArray<uint64_t> hashes{};
    size_t capacity = 0;
    size_t count = 0;

    MiniSwissTable(::foundation::effects::Alloc alloc, size_t cap) : capacity{cap} {
        ctrl = ::fixy::mint_masked_array<int8_t>(alloc, cap);
        hashes = ::fixy::mint_masked_array<uint64_t>(alloc, cap);
        ::fixy::fill(ctrl.view(), kEmpty);
    }
    MiniSwissTable(const MiniSwissTable&) = delete;
    MiniSwissTable& operator=(const MiniSwissTable&) = delete;
    MiniSwissTable(MiniSwissTable&&) = delete;
    MiniSwissTable& operator=(MiniSwissTable&&) = delete;

    size_t insert(uint64_t h) {
        const int8_t tag = h2_tag(h);
        const size_t num_groups = capacity / kGroupWidth;
        const size_t group_mask = num_groups - 1;
        size_t g = h & group_mask;
        size_t probe = 0;
        while (true) {
            const size_t base = g * kGroupWidth;
            auto group = load_group(ctrl.view(), base);
            if (auto empties = group.match_empty(); empties) {
                const size_t idx = base + empties.lowest();
                ctrl.at_masked(idx) = tag;
                hashes.at_masked(idx) = h;
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
            auto group = load_group(ctrl.view(), base);
            auto matches = group.match(tag);
            while (matches) {
                const size_t idx = base + matches.lowest();
                if (hashes.at_masked(idx) == h) return true;
                matches.clear_lowest();
            }
            if (auto empties = group.match_empty(); empties) return false;
            ++probe;
            g = (g + probe) & group_mask;
        }
    }
};

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    bench::print("=== swiss_table ===\n  backend: ");
#if defined(__AVX512BW__)
    bench::print("AVX-512BW (64 B/group)\n");
#elif defined(__AVX2__)
    bench::print("AVX2 (32 B/group)\n");
#elif defined(__SSE2__)
    bench::print("SSE2 (16 B/group)\n");
#elif defined(__aarch64__)
    bench::print("NEON (16 B/group)\n");
#else
    bench::print("portable SWAR (16 B/group)\n");
#endif
    bench::print("  kGroupWidth: {}\n\n", kGroupWidth);

    // h2_tag(1) + load(2) + match(3) + match_empty(3) + BitMask(4) +
    // probe-step(2) + the hit and miss rows of four load factors (8).
    ::fixy::FixedList<bench::Report, 23> reports{};

    // ── h2_tag ───────────────────────────────────────────────────────
    {
        volatile uint64_t hash = 0x123456789ABCDEF0ULL;
        bench::add_report(reports, bench::run("h2_tag(hash)", [&] {
                              auto tag = h2_tag(hash);
                              bench::do_not_optimize(tag);
                          }));
    }

    // ── load() aligned vs unaligned ──────────────────────────────────
    bench::add_report(reports, [&] {
        alignas(64)::fixy::FixedArray<int8_t, kGroupWidth * 4> ctrl{};
        ctrl.with_mut([](auto run) noexcept { fill_ctrl(run.view(), 0.5, 99); });
        const CtrlGroup::GroupBytes aligned = ctrl.view().template window<kGroupWidth>(0).expect("the first group");
        return bench::run("load() aligned", [&] {
            auto g = CtrlGroup::load(aligned);
            bench::do_not_optimize(g);
        });
    }());
    bench::add_report(reports, [&] {
        alignas(64)::fixy::FixedArray<int8_t, kGroupWidth * 4> ctrl{};
        ctrl.with_mut([](auto run) noexcept { fill_ctrl(run.view(), 0.5, 99); });
        const CtrlGroup::GroupBytes unaligned = ctrl.view().template window<kGroupWidth>(3).expect("a group from 3");
        return bench::run("load() unaligned (+3)", [&] {
            auto g = CtrlGroup::load(unaligned);
            bench::do_not_optimize(g);
        });
    }());

    // ── match(h2) ─────────────────────────────────────────────────────
    bench::add_report(reports, [&] {
        ::fixy::FixedArray<int8_t, kGroupWidth> ctrl{};
        ctrl.with_mut([](auto run) noexcept { fill_ctrl(run.view(), 0.75, 42); });
        const int8_t target = 0x37;
        ctrl.with_element(::fixy::index<5>(), [target](int8_t& byte) noexcept { byte = target; });
        auto group = CtrlGroup::load(ctrl.view());
        return bench::run("match(h2) — 1 match", [&] {
            auto m = group.match(target);
            bench::do_not_optimize(m);
        });
    }());

    bench::add_report(reports, [&] {
        ::fixy::FixedArray<int8_t, kGroupWidth> ctrl{};
        ctrl.with_mut([](auto run) noexcept { fill_ctrl(run.view(), 0.75, 42); });
        const int8_t miss = 0x7F;
        ctrl.with_mut([miss](auto run) noexcept {
            for (int8_t& byte : run)
                if (byte == miss) byte = 0x01;
        });
        auto group = CtrlGroup::load(ctrl.view());
        return bench::run("match(h2) — 0 matches (miss)", [&] {
            auto m = group.match(miss);
            bench::do_not_optimize(m);
        });
    }());

    bench::add_report(reports, [&] {
        ::fixy::FixedArray<int8_t, kGroupWidth> ctrl{};
        ctrl.with_mut([](auto run) noexcept { fill_ctrl(run.view(), 0.75, 42); });
        const int8_t target = 0x37;
        ctrl.with_mut([target](auto cells) noexcept {
            for (::fixy::View<int8_t, 4> run : cells.view().template chunks<4>())
                run.at(::fixy::index<0>()) = target;
        });
        auto group = CtrlGroup::load(ctrl.view());
        return bench::run("match(h2) — multiple matches", [&] {
            auto m = group.match(target);
            bench::do_not_optimize(m);
        });
    }());

    // ── match_empty() ─────────────────────────────────────────────────
    bench::add_report(reports, [&] {
        ::fixy::FixedArray<int8_t, kGroupWidth> ctrl{};
        ctrl.with_mut([](auto run) noexcept { fill_ctrl(run.view(), 0.75, 123); });
        auto group = CtrlGroup::load(ctrl.view());
        return bench::run("match_empty() — 75% load", [&] {
            auto m = group.match_empty();
            bench::do_not_optimize(m);
        });
    }());
    bench::add_report(reports, [&] {
        ::fixy::FixedArray<int8_t, kGroupWidth> ctrl{};
        ctrl.with_mut([](auto run) noexcept { ::fixy::fill(run.view(), int8_t{0x42}); });  // all occupied
        auto group = CtrlGroup::load(ctrl.view());
        return bench::run("match_empty() — 100% full", [&] {
            auto m = group.match_empty();
            bench::do_not_optimize(m);
        });
    }());
    bench::add_report(reports, [&] {
        ::fixy::FixedArray<int8_t, kGroupWidth> ctrl{};
        ctrl.with_mut([](auto run) noexcept { ::fixy::fill(run.view(), kEmpty); });  // all empty
        auto group = CtrlGroup::load(ctrl.view());
        return bench::run("match_empty() — 0% load (all empty)", [&] {
            auto m = group.match_empty();
            bench::do_not_optimize(m);
        });
    }());

    // ── BitMask iteration ────────────────────────────────────────────
    auto iterate_mask = [](BitMask m) noexcept -> uint32_t {
        uint32_t sum = 0;
        while (m) {
            sum += m.lowest();
            m.clear_lowest();
        }
        return sum;
    };
    // The bits of a mask and the name of its row.
    struct MaskRow {
        uint16_t bits = 0;
        ::fixy::TextView label{};
    };
    constexpr ::fixy::FixedArray<MaskRow, 4> kMaskRows{
        MaskRow{0x0010, "BitMask iterate — 1 bit"}, MaskRow{0x1248, "BitMask iterate — 4 bits"},
        MaskRow{0x5555, "BitMask iterate — 8 bits"}, MaskRow{0xFFFF, "BitMask iterate — 16 bits"}};
    for (auto const& [bits, label] : kMaskRows) {
        bench::add_report(reports, [&, bits, label] {
            const BitMask m{bits};
            return bench::run(label, [&] {
                auto r = iterate_mask(m);
                bench::do_not_optimize(r);
            });
        }());
    }

    // ── Combined probe step (match + empty check) ────────────────────
    bench::add_report(reports, [&] {
        ::fixy::FixedArray<int8_t, kGroupWidth> ctrl{};
        ctrl.with_mut([](auto run) noexcept { fill_ctrl(run.view(), 0.75, 42); });
        const int8_t target = 0x37;
        ctrl.with_element(::fixy::index<5>(), [target](int8_t& byte) noexcept { byte = target; });
        auto group = CtrlGroup::load(ctrl.view());
        return bench::run("probe step (match+empty, hit)", [&] {
            auto matches = group.match(target);
            uint32_t found_idx = 0;
            while (matches) {
                found_idx = matches.lowest();
                matches.clear_lowest();
            }
            auto empties = group.match_empty();
            bench::do_not_optimize(found_idx);
            bench::do_not_optimize(empties);
        });
    }());

    bench::add_report(reports, [&] {
        ::fixy::FixedArray<int8_t, kGroupWidth> ctrl{};
        ctrl.with_mut([](auto run) noexcept { fill_ctrl(run.view(), 0.75, 42); });
        const int8_t miss = 0x7E;
        ctrl.with_mut([miss](auto run) noexcept {
            for (int8_t& byte : run)
                if (byte == miss) byte = 0x01;
        });
        auto group = CtrlGroup::load(ctrl.view());
        return bench::run("probe step (match+empty, miss→stop)", [&] {
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
    auto bench_probe_at = [&](::fixy::TextView label, double load_factor) {
        constexpr size_t TABLE_CAP = 1u << 16;  // 65536 slots
        const size_t target_count = static_cast<size_t>(TABLE_CAP * load_factor);

        // Fixture — lives for both Runs in this load factor.
        const ::foundation::effects::Alloc alloc = ::foundation::effects::testing::test().alloc;
        ::fixy::Box<MiniSwissTable> table = ::fixy::mint_box<MiniSwissTable>(alloc, alloc, TABLE_CAP);

        // The first 1024 inserted hashes and 1024 absent hashes, on the
        // heap as the vectors of the rows were.  Each load inserts more
        // than 1024 hashes.
        using Hashes = ::fixy::FixedArray<uint64_t, 1024>;
        ::fixy::Box<Hashes> inserted_hashes = ::fixy::mint_box<Hashes>(alloc);
        std::size_t inserted_count = 0;
        uint64_t seed = 0xDEADBEEF;
        for (size_t i = 0; i < target_count; ++i) {
            seed = fmix64(seed + i);
            table->insert(seed);
            if (inserted_count < 1024) {
                inserted_hashes->with_element(
                    ::fixy::Index<1024>::check(inserted_count).expect("the count is below 1024"),
                    [seed](uint64_t& hash) noexcept { hash = seed; });
                ++inserted_count;
            }
        }

        ::fixy::Box<Hashes> absent_hashes = ::fixy::mint_box<Hashes>(alloc);
        std::size_t absent_count = 0;
        seed = 0xCAFEBABE;
        while (absent_count < 1024) {
            seed = fmix64(seed + absent_count + 1);
            if (!table->find(seed)) {
                absent_hashes->with_element(::fixy::Index<1024>::check(absent_count).expect("the count is below 1024"),
                                            [seed](uint64_t& hash) noexcept { hash = seed; });
                ++absent_count;
            }
        }

        const bench::RunName name_hit = bench::format_name("probe HIT  — {}", label);
        const bench::RunName name_miss = bench::format_name("probe MISS — {}", label);

        // The mask keeps each position below 1024, so the compiler folds
        // the check of the index.  A row reads each hash through a View
        // of the heap array, as it read a vector: the address of the View,
        // then its first element.
        ::fixy::View<uint64_t const, 1024> const inserted = inserted_hashes->view();
        ::fixy::View<uint64_t const, 1024> const absent = absent_hashes->view();
        uint32_t hit_idx = 0;
        bench::add_report(reports, bench::run(name_hit, [&, t = table.view()] {
                              bool found = t->find(
                                  inserted.at(::fixy::Index<1024>::check(hit_idx & 1023).expect("a mask of 10 bits")));
                              bench::do_not_optimize(found);
                              ++hit_idx;
                          }));
        uint32_t miss_idx = 0;
        bench::add_report(reports, bench::run(name_miss, [&, t = table.view()] {
                              bool found = t->find(
                                  absent.at(::fixy::Index<1024>::check(miss_idx & 1023).expect("a mask of 10 bits")));
                              bench::do_not_optimize(found);
                              ++miss_idx;
                          }));
    };

    bench_probe_at("25% load", 0.25);
    bench_probe_at("50% load", 0.50);
    bench_probe_at("75% load", 0.75);
    bench_probe_at("87.5% load max", 0.875);

    bench::emit_reports_text(reports.view());

    // Probe scaling: 25% vs 87.5% load MISS should grow with probe
    // distance. Non-trivial delta is the expected signal.
    bench::print("\n=== compare — load-factor scaling (MISS 25% → 87.5%) ===\n  ");
    // Reports layout: h2_tag(1) + load(2) + match(3) + match_empty(3)
    //               + BitMask(4) + probe-step(2) = 15 before probe-at.
    // bench_probe_at appends 2 per call; 25/50/75/87.5 → indices
    // 15..22 (16 new). MISS at 25% = reports[16], MISS at 87.5% = reports[22].
    bench::compare(*reports.get(16).expect("the miss row at 25% load"),
                   *reports.get(22).expect("the miss row at 87.5% load"))
        .print_text();

    bench::emit_reports_json(reports.view(), json);
    return 0;
}
