// Microbench of the hot verbs of the small containers of the Region family on
// one thread: test and set of a BitSet and of a HeapBitSet, get of a SlotMap,
// a pair of an insert and a remove of a SlotMap, and the appends of a
// SmallList in its inline slots and past them.
//
// Each row is one pass of 4096 operations, which the cache of the core holds
// after the first pass.  The rule codegen-shapes compares the machine code of
// test, set and get with the raw forms that they replace
// (test/shape/core_shapes.cpp), so this bench gives the time and not a
// comparison.
//
// Each line gives the percentiles of one pass in nanoseconds, and the
// coefficient of variation in thousandths.  The Report family writes integers
// only, so the bench writes no fraction of a nanosecond.

#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

#include "bench_harness.h"

namespace {

constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

// The count of the operations of each pass.
constexpr std::size_t kOperations = 4096;

// The domains of the ids of the heap set and of the keys of the map.
struct BenchSlotTag final {};
struct BenchHandleTag final {};

// A time in nanoseconds, as the nearest integer.
[[nodiscard]] std::uint64_t nanoseconds(double value) noexcept { return static_cast<std::uint64_t>(value + 0.5); }

// Writes one line: the name of the pass and the percentiles of its samples.
void print_row(fixy::TextView name, bench::Report const& measured) noexcept {
    fixy::report(fixy::Sink::Out, "{}  p50={} ns  p99={} ns  p99.9={} ns  max={} ns  cv={} permille\n", name,
                 nanoseconds(measured.pct.p50), nanoseconds(measured.pct.p99), nanoseconds(measured.pct.p99_9),
                 nanoseconds(measured.pct.max), static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5));
}

}  // namespace

int main() {
    bench::elevate_priority();

    // A fixed pattern of positions below 4096.  An Index has no default value,
    // so a list holds the pattern.
    fixy::FixedList<fixy::Index<kOperations>, kOperations> positions;
    std::uint32_t pattern = 0x9E3779B9u;
    for (std::size_t step = 0; step < kOperations; ++step) {
        pattern = pattern * 1664525u + 1013904223u;
        static_cast<void>(positions.push(fixy::Index<kOperations>::check(pattern >> 20).value_or(fixy::index<0>()))
                              .expect("the list holds 4096 positions"));
    }

    fixy::BitSet<kOperations> fixed_set{};
    print_row("BitSet set, 4096 indexes", bench::run("BitSet set", [&] {
                  for (fixy::Index<kOperations> const position : positions) {
                      fixed_set.set(position);
                  }
                  bench::do_not_optimize(fixed_set);
              }));
    print_row("BitSet test, 4096 indexes", bench::run("BitSet test", [&] {
                  std::size_t held = 0;
                  for (fixy::Index<kOperations> const position : positions) {
                      held += fixed_set.test(position) ? 1u : 0u;
                  }
                  bench::do_not_optimize(held);
              }));

    fixy::HeapBitSet<BenchSlotTag> heap_set = fixy::mint_heap_bit_set<BenchSlotTag>(
        kAlloc, fixy::Quantity<fixy::unit::Element, std::uint32_t>{static_cast<std::uint32_t>(kOperations)});
    print_row("HeapBitSet set, 4096 ids", bench::run("HeapBitSet set", [&] {
                  for (fixy::Index<kOperations> const position : positions) {
                      heap_set.set(fixy::BitId<BenchSlotTag>{static_cast<std::uint32_t>(position.raw())});
                  }
                  bench::do_not_optimize(heap_set);
              }));
    print_row("HeapBitSet test, 4096 ids", bench::run("HeapBitSet test", [&] {
                  std::size_t held = 0;
                  for (fixy::Index<kOperations> const position : positions) {
                      held += heap_set.test(fixy::BitId<BenchSlotTag>{static_cast<std::uint32_t>(position.raw())}) ? 1u
                                                                                                                   : 0u;
                  }
                  bench::do_not_optimize(held);
              }));

    fixy::SlotMap<std::uint32_t, BenchHandleTag> map = fixy::mint_slot_map<std::uint32_t, BenchHandleTag>(
        kAlloc, fixy::Quantity<fixy::unit::Element, std::uint32_t>{static_cast<std::uint32_t>(kOperations)});
    fixy::FixedArray<std::uint64_t, kOperations> keys{};
    for (fixy::Index<kOperations> const index : keys.indices()) {
        keys.at(index) = map.insert(static_cast<std::uint32_t>(index.raw())).expect("the map has a free slot").raw();
    }
    print_row("SlotMap get, 4096 keys", bench::run("SlotMap get", [&] {
                  std::uint32_t sum = 0;
                  for (fixy::Index<kOperations> const position : positions) {
                      for (auto const found : map.get(fixy::SlotKey<BenchHandleTag>{keys.at(position)})) {
                          sum += *found;
                      }
                  }
                  bench::do_not_optimize(sum);
              }));
    print_row("SlotMap remove and insert, 4096 pairs", bench::run("SlotMap remove and insert", [&] {
                  for (fixy::Index<kOperations> const position : positions) {
                      fixy::SlotKey<BenchHandleTag> const key{keys.at(position)};
                      for (std::uint32_t const value : map.remove(key)) {
                          keys.at(position) = map.insert(value).expect("the remove freed a slot").raw();
                      }
                  }
                  bench::do_not_optimize(map);
              }));

    print_row("SmallList of 8 inline, 8 appends, 512 lists", bench::run("SmallList inline appends", [&] {
                  for (std::size_t list_index = 0; list_index < kOperations / 8; ++list_index) {
                      fixy::SmallList<std::uint32_t, 8> list =
                          fixy::mint_small_list<std::uint32_t, 8>(fixy::Quantity<fixy::unit::Element, std::size_t>{64});
                      for (std::uint32_t value = 0; value < 8; ++value) {
                          list.append(kAlloc, value).expect("the list is below its maximum");
                      }
                      bench::do_not_optimize(list);
                  }
              }));
    print_row("SmallList of 2 inline, 8 appends, 512 lists", bench::run("SmallList spilled appends", [&] {
                  for (std::size_t list_index = 0; list_index < kOperations / 8; ++list_index) {
                      fixy::SmallList<std::uint32_t, 2> list =
                          fixy::mint_small_list<std::uint32_t, 2>(fixy::Quantity<fixy::unit::Element, std::size_t>{64});
                      for (std::uint32_t value = 0; value < 8; ++value) {
                          list.append(kAlloc, value).expect("the list is below its maximum");
                      }
                      bench::do_not_optimize(list);
                  }
              }));
    return 0;
}
