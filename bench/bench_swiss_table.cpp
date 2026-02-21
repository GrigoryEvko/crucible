// Benchmark for SwissCtrl SIMD control byte operations.
//
// Measures:
//   1. CtrlGroup::match(h2)     — single-group H2 tag matching
//   2. CtrlGroup::match_empty() — single-group empty slot detection
//   3. BitMask iteration        — traversing match results
//   4. Full probe sequences at various load factors (25%, 50%, 75%)
//   5. Portable fallback vs SIMD comparison (when available)
//
// Target: probe operations <= 5ns.
//
// Build:
//   cmake --preset bench && cmake --build --preset bench -j$(nproc)
//   ./build-bench/bench/bench_swiss_ctrl

#include "bench_harness.h"

#include <crucible/SwissTable.h>
#include <crucible/Expr.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using crucible::detail::BitMask;
using crucible::detail::CtrlGroup;
using crucible::detail::h2_tag;
using crucible::detail::kEmpty;
using crucible::detail::kGroupWidth;
using crucible::detail::fmix64;

// ============================================================
// Helpers
// ============================================================

// Fill a control byte array with a realistic distribution:
// - `occupied_pct` fraction of slots get random H2 tags (0..127)
// - remaining slots get kEmpty (0x80)
// - optionally plant a specific h2 tag at a specific position
static void fill_ctrl(
    int8_t* ctrl,
    size_t capacity,
    double occupied_pct,
    uint64_t seed) {
  uint64_t state = seed;
  for (size_t i = 0; i < capacity; ++i) {
    state = fmix64(state + i);
    double r = static_cast<double>(state & 0xFFFFFFFF) / 4294967296.0;
    if (r < occupied_pct) {
      ctrl[i] = static_cast<int8_t>(state & 0x7F); // random H2 tag
    } else {
      ctrl[i] = kEmpty;
    }
  }
}

// Plant a specific h2 tag at the given offset within a group.
static void plant_tag(int8_t* ctrl, size_t group_base, size_t offset, int8_t h2) {
  ctrl[group_base + offset] = h2;
}

// Simple Swiss table simulator for probe benchmarks.
// Uses the same probe sequence as ExprPool (triangular probing).
struct MiniSwissTable {
  int8_t* ctrl = nullptr;
  uint64_t* hashes = nullptr;  // full hash stored per slot for verification
  size_t capacity = 0;
  size_t count = 0;

  MiniSwissTable() = default;

  explicit MiniSwissTable(size_t cap) : capacity(cap) {
    ctrl = static_cast<int8_t*>(std::malloc(cap));
    hashes = static_cast<uint64_t*>(std::calloc(cap, sizeof(uint64_t)));
    if (!ctrl || !hashes) std::abort();
    std::memset(ctrl, 0x80, cap); // all empty
  }

  ~MiniSwissTable() {
    std::free(ctrl);
    std::free(hashes);
  }

  MiniSwissTable(const MiniSwissTable&) = delete;
  MiniSwissTable& operator=(const MiniSwissTable&) = delete;
  MiniSwissTable(MiniSwissTable&&) = delete;
  MiniSwissTable& operator=(MiniSwissTable&&) = delete;

  // Insert a hash into the table. Returns the slot index.
  size_t insert(uint64_t h) {
    int8_t tag = h2_tag(h);
    size_t num_groups = capacity / kGroupWidth;
    size_t group_mask = num_groups - 1;
    size_t g = h & group_mask;
    size_t probe = 0;

    while (true) {
      size_t base = g * kGroupWidth;
      auto group = CtrlGroup::load(&ctrl[base]);
      auto empties = group.match_empty();
      if (empties) {
        size_t idx = base + empties.lowest();
        ctrl[idx] = tag;
        hashes[idx] = h;
        ++count;
        return idx;
      }
      ++probe;
      g = (g + probe) & group_mask;
    }
  }

  // Find a hash in the table. Returns true if found.
  [[nodiscard]] bool find(uint64_t h) const {
    int8_t tag = h2_tag(h);
    size_t num_groups = capacity / kGroupWidth;
    size_t group_mask = num_groups - 1;
    size_t g = h & group_mask;
    size_t probe = 0;

    while (true) {
      size_t base = g * kGroupWidth;
      auto group = CtrlGroup::load(&ctrl[base]);

      auto matches = group.match(tag);
      while (matches) {
        size_t idx = base + matches.lowest();
        if (hashes[idx] == h)
          return true;
        matches.clear_lowest();
      }

      auto empties = group.match_empty();
      if (empties)
        return false;

      ++probe;
      g = (g + probe) & group_mask;
    }
  }
};

// ============================================================
// Benchmarks
// ============================================================

static void bench_match_h2() {
  std::printf("--- CtrlGroup::match(h2) ---\n");

  // Set up a group with known contents
  alignas(64) int8_t ctrl[kGroupWidth];
  fill_ctrl(ctrl, kGroupWidth, 0.75, 42);
  int8_t target_h2 = 0x37;
  ctrl[5] = target_h2; // plant one match

  auto group = CtrlGroup::load(ctrl);

  BENCH_CHECK("match(h2) — 1 match in group", 10'000'000, 0.3, {
    auto m = group.match(target_h2);
    bench::DoNotOptimize(m);
  });

  // No matches
  int8_t miss_h2 = 0x7F;
  // Make sure miss_h2 is not in the group
  for (size_t i = 0; i < kGroupWidth; ++i)
    if (ctrl[i] == miss_h2) ctrl[i] = 0x01;
  group = CtrlGroup::load(ctrl);

  BENCH_CHECK("match(h2) — 0 matches (miss)", 10'000'000, 0.3, {
    auto m = group.match(miss_h2);
    bench::DoNotOptimize(m);
  });

  // Multiple matches
  for (size_t i = 0; i < kGroupWidth; i += 4)
    ctrl[i] = target_h2;
  group = CtrlGroup::load(ctrl);
  size_t match_count = 0;
  auto test = group.match(target_h2);
  while (test) { ++match_count; test.clear_lowest(); }

  BENCH_CHECK("match(h2) — multiple matches", 10'000'000, 0.3, {
    auto m = group.match(target_h2);
    bench::DoNotOptimize(m);
  });

  std::printf("  (match_count=%zu for multi-match test)\n", match_count);
}

static void bench_match_empty() {
  std::printf("\n--- CtrlGroup::match_empty() ---\n");

  // 75% full — 25% empty slots
  alignas(64) int8_t ctrl[kGroupWidth];
  fill_ctrl(ctrl, kGroupWidth, 0.75, 123);
  auto group = CtrlGroup::load(ctrl);

  BENCH_CHECK("match_empty() — 75% load", 10'000'000, 0.3, {
    auto m = group.match_empty();
    bench::DoNotOptimize(m);
  });

  // All occupied — no empties
  std::memset(ctrl, 0x42, kGroupWidth); // all occupied with H2=0x42
  group = CtrlGroup::load(ctrl);

  BENCH_CHECK("match_empty() — 100% full (no empty)", 10'000'000, 0.3, {
    auto m = group.match_empty();
    bench::DoNotOptimize(m);
  });

  // All empty
  std::memset(ctrl, 0x80, kGroupWidth);
  group = CtrlGroup::load(ctrl);

  BENCH_CHECK("match_empty() — 0% load (all empty)", 10'000'000, 0.3, {
    auto m = group.match_empty();
    bench::DoNotOptimize(m);
  });
}

static void bench_bitmask_iteration() {
  std::printf("\n--- BitMask iteration ---\n");

  // Iterate a mask with N set bits
  auto iterate_mask = [](BitMask m) -> uint32_t {
    uint32_t sum = 0;
    while (m) {
      sum += m.lowest();
      m.clear_lowest();
    }
    return sum;
  };

  // 1 bit set
  BitMask m1{0x0010};
  BENCH_CHECK("BitMask iterate — 1 bit", 10'000'000, 0.3, {
    auto r = iterate_mask(m1);
    bench::DoNotOptimize(r);
  });

  // 4 bits set
  BitMask m4{0x1248};
  BENCH_CHECK("BitMask iterate — 4 bits", 10'000'000, 0.3, {
    auto r = iterate_mask(m4);
    bench::DoNotOptimize(r);
  });

  // 8 bits set (50% of 16-byte group)
  BitMask m8{0x5555};
  BENCH_CHECK("BitMask iterate — 8 bits", 10'000'000, 0.3, {
    auto r = iterate_mask(m8);
    bench::DoNotOptimize(r);
  });

  // 16 bits set (full 16-byte group)
  BitMask m16{0xFFFF};
  BENCH_CHECK("BitMask iterate — 16 bits", 10'000'000, 0.3, {
    auto r = iterate_mask(m16);
    bench::DoNotOptimize(r);
  });
}

static void bench_load() {
  std::printf("\n--- CtrlGroup::load() ---\n");

  // Aligned load
  alignas(64) int8_t ctrl_aligned[kGroupWidth * 4];
  fill_ctrl(ctrl_aligned, kGroupWidth * 4, 0.5, 99);

  BENCH_CHECK("load() — aligned", 10'000'000, 0.3, {
    auto g = CtrlGroup::load(ctrl_aligned);
    bench::DoNotOptimize(g);
  });

  // Unaligned load (offset by 3 bytes)
  int8_t* unaligned = ctrl_aligned + 3;
  BENCH_CHECK("load() — unaligned (+3)", 10'000'000, 0.3, {
    auto g = CtrlGroup::load(unaligned);
    bench::DoNotOptimize(g);
  });
}

static void bench_combined_probe_step() {
  std::printf("\n--- Combined probe step (match + empty check) ---\n");

  alignas(64) int8_t ctrl[kGroupWidth];
  fill_ctrl(ctrl, kGroupWidth, 0.75, 42);
  int8_t target_h2 = 0x37;
  ctrl[5] = target_h2;
  auto group = CtrlGroup::load(ctrl);

  // Simulate one probe step: match + iterate + empty check
  BENCH_CHECK("probe step (match+empty, hit)", 10'000'000, 0.8, {
    auto matches = group.match(target_h2);
    uint32_t found_idx = 0;
    while (matches) {
      found_idx = matches.lowest();
      matches.clear_lowest();
    }
    auto empties = group.match_empty();
    bench::DoNotOptimize(found_idx);
    bench::DoNotOptimize(empties);
  });

  // Simulate one probe step: miss (no match, has empties → stop)
  int8_t miss_h2 = 0x7E;
  for (size_t i = 0; i < kGroupWidth; ++i)
    if (ctrl[i] == miss_h2) ctrl[i] = 0x01;
  group = CtrlGroup::load(ctrl);

  BENCH_CHECK("probe step (match+empty, miss→stop)", 10'000'000, 0.8, {
    auto matches = group.match(miss_h2);
    bool found = static_cast<bool>(matches);
    auto empties = group.match_empty();
    bool stop = static_cast<bool>(empties);
    bench::DoNotOptimize(found);
    bench::DoNotOptimize(stop);
  });
}

static void bench_full_probe(const char* label, double load_factor,
                             double max_hit_ns, double max_miss_ns) {
  // Build a table at the specified load factor, then probe for
  // known-present and known-absent keys.
  constexpr size_t TABLE_CAP = 1 << 16; // 65536 slots
  size_t target_count = static_cast<size_t>(TABLE_CAP * load_factor);

  MiniSwissTable table(TABLE_CAP);

  // Insert known keys
  uint64_t inserted_hashes[1024];
  size_t n_inserted = 0;
  uint64_t seed = 0xDEADBEEF;
  for (size_t i = 0; i < target_count; ++i) {
    seed = fmix64(seed + i);
    table.insert(seed);
    if (n_inserted < 1024)
      inserted_hashes[n_inserted++] = seed;
  }

  // Generate absent keys (probe for keys that were NOT inserted)
  uint64_t absent_hashes[1024];
  size_t n_absent = 0;
  seed = 0xCAFEBABE;
  while (n_absent < 1024) {
    seed = fmix64(seed + n_absent + 1);
    if (!table.find(seed))
      absent_hashes[n_absent++] = seed;
  }

  char name_hit[128];
  char name_miss[128];
  std::snprintf(name_hit, sizeof(name_hit), "probe HIT — %s", label);
  std::snprintf(name_miss, sizeof(name_miss), "probe MISS — %s", label);

  // Probe for known-present keys
  uint32_t hit_idx = 0;
  BENCH_CHECK(name_hit, 1'000'000, max_hit_ns, {
    bool found = table.find(inserted_hashes[hit_idx & 1023]);
    bench::DoNotOptimize(found);
    ++hit_idx;
  });

  // Probe for known-absent keys
  uint32_t miss_idx = 0;
  BENCH_CHECK(name_miss, 1'000'000, max_miss_ns, {
    bool found = table.find(absent_hashes[miss_idx & 1023]);
    bench::DoNotOptimize(found);
    ++miss_idx;
  });
}

static void bench_h2_tag() {
  std::printf("\n--- h2_tag() ---\n");

  volatile uint64_t hash = 0x123456789ABCDEF0ULL;
  BENCH_CHECK("h2_tag(hash)", 10'000'000, 0.3, {
    auto tag = h2_tag(hash);
    bench::DoNotOptimize(tag);
  });
}

// ============================================================
// Main
// ============================================================

int main() {
  std::printf("=== SwissCtrl SIMD Benchmark ===\n");
  std::printf("  Backend: ");
#if defined(__AVX512BW__)
  std::printf("AVX-512BW (64 bytes/group)\n");
#elif defined(__AVX2__)
  std::printf("AVX2 (32 bytes/group)\n");
#elif defined(__SSE2__)
  std::printf("SSE2 (16 bytes/group)\n");
#elif defined(__aarch64__)
  std::printf("NEON (16 bytes/group)\n");
#else
  std::printf("Portable SWAR (16 bytes/group)\n");
#endif
  std::printf("  kGroupWidth: %zu\n\n", kGroupWidth);

  bench_h2_tag();
  bench_load();
  bench_match_h2();
  bench_match_empty();
  bench_bitmask_iteration();
  bench_combined_probe_step();

  std::printf("\n--- Full probe sequences ---\n");
  bench_full_probe("25% load", 0.25, 2.3, 2.6);
  bench_full_probe("50% load", 0.50, 2.0, 2.4);
  bench_full_probe("75% load", 0.75, 2.0, 2.4);
  bench_full_probe("87.5% load (max)", 0.875, 2.0, 3.3);

  std::printf("\nDone.\n");
  return 0;
}
