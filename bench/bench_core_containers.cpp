// Microbench of the hot verbs of the containers of the Region family on one
// thread: test and set of a BitSet and of a HeapBitSet, get of a SlotMap, a
// pair of a remove and an insert of a SlotMap, the appends of a SmallList in
// its inline slots and past them, the five verbs of a log of events in a
// SegmentList, and the six verbs of a log that a LiveList publishes to readers.
//
// Each row of a set, a map and a SmallList is one pass of 4096 operations,
// which the cache of the core holds after the first pass.  Each row of a
// container has a reference row: the raw form of the state of the art for the
// same verb, on the same positions.
//   raw bitmap          the words of 64 bits and the shift of a bit, the code
//                       of std::bitset and of a bitmap by hand.  The
//                       reference of BitSet
//   raw heap bitmap     the same words on the heap, through a pointer.  The
//                       reference of HeapBitSet
//   raw slot table      the generational table of the slotmap crate of Rust:
//                       an odd generation marks a value, and a key holds the
//                       index and the generation.  The reference of SlotMap
//   raw small vector    the push_back of llvm::SmallVector: a pointer to the
//                       first element, one compare with the capacity, and a
//                       growth that doubles the storage.  The reference of
//                       SmallList
//   raw segments        the segment table of the concurrent vectors of oneTBB
//                       and of the two-level vectors of databases, for one
//                       thread: segments that double, and the segment of an
//                       index from its top bit.  The reference of SegmentList
//   raw event vector    the push_back of std::vector, one contiguous run.  The
//                       second reference of SegmentList
//   raw live segments   the raw segments with a count on a line of its own.
//                       The writer stores the count with release order after
//                       each append, and a reader loads it with acquire order.
//                       The reference of LiveList
//
// Log rows
//   append              one append of an event of 72 bytes to a log that grows
//                       to 131072 events.  A sample is one append, so the
//                       maximum is the most expensive one of the log
//   appends             4096 appends to an empty log, and its end
//   growth              the one append that meets a full log of about 65536
//                       events.  The hook before each sample fills the log, and
//                       no sample times it
//   get                 the first word of 4096 events at random positions
//   walk                the sum of the first word of 4096 events
// The rows of a reserved run are the floor of an append: five stores of the
// event, the tail and the count into storage that is there and is warm.  A
// core with two stores for each cycle needs 2.5 cycles for them.
//
// The median and the maximum of an append into a full log follow the position
// of the log in the run, and not its type.  So each log runs two times, the
// second time in the mirrored order of the logs, and each row gives the pass
// with the lower median.
//
// The ratio of the rows appends and growth follows the place of the memory of
// each log too.  Each log has the same loads and stores for each append, and the
// time of an append is far above the cycles of its stores.  The reserved run, a
// floor with the same stores, can take more time than a SegmentList, and the raw
// segments can take less.
//
// Live rows
//   append, appends     the rows of a log, with the publish of each append
//   runs                4096 events in runs of 8, with one publish for a run, to
//                       an empty log, and its end
//   get, walk           the rows of a log, through one prefix for each pass
//   count               4096 prefixes, and the count of each one
// The rows of a published run are the floor of a live append: the five stores
// of the reserved run and the store of the published count, which are 3 cycles.
// One thread runs each row, so no row holds the cache miss of a line that a
// reader on another core took.  Each live log runs two times in mirrored order,
// as a log does.
//
// The rule codegen-shapes compares the machine code of test, set and get with
// the raw forms of test/shape/core_shapes.cpp.  This bench gives the times.
//
// Each line gives the percentiles of one pass in nanoseconds, and the
// coefficient of variation in thousandths.  A row of a container also gives
// its median over the median of its reference, in thousandths: 1000 is
// parity, and a number above 1000 is slower than the reference.  The Report
// family writes integers only, so the bench writes no fraction.

#include <fixy/Core.h>
#include <foundation/Quarantine.h>
#include <foundation/SoleWriterProof.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

#include "bench_harness.h"

namespace {

constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

// The count of the operations of each pass.
constexpr std::size_t kOperations = 4096;

// The words of 64 bits of a bitmap of kOperations bits.
constexpr std::size_t kWords = kOperations / 64;

using Positions = fixy::FixedList<fixy::Index<kOperations>, kOperations>;

// The domains of the ids of the heap set and of the keys of the map.
struct BenchSlotTag final {};
struct BenchHandleTag final {};

// A time in nanoseconds, as the nearest integer.
[[nodiscard]] std::uint64_t nanoseconds(double value) noexcept { return static_cast<std::uint64_t>(value + 0.5); }

// One measured pass: its name and its report, which the caller holds.
struct Row final {
    fixy::TextView name;
    bench::Report const& report;
};

// The row of a container and the row of its reference.
struct Pair final {
    Row container;
    Row reference;
};

// Writes one line: the name of the pass and the percentiles of its samples.
void print_row(Row const& row) noexcept {
    bench::Percentiles const& pct = row.report.pct;
    bench::print("{}  p50={} ns  p99={} ns  p99.9={} ns  max={} ns  cv={} permille\n", row.name, nanoseconds(pct.p50),
                 nanoseconds(pct.p99), nanoseconds(pct.p99_9), nanoseconds(pct.max),
                 static_cast<std::uint64_t>(pct.cv * 1000.0 + 0.5));
}

// Writes the row of a container, then the row of its reference, then the
// ratio of the two medians in thousandths.
void print_pair(Pair const& pair) noexcept {
    print_row(pair.container);
    print_row(pair.reference);
    double const container = pair.container.report.pct.p50;
    double const reference = pair.reference.report.pct.p50;
    std::uint64_t const ratio = reference > 0.0 ? static_cast<std::uint64_t>(container / reference * 1000.0 + 0.5) : 0;
    bench::print("{}  ratio to {}={} permille\n", pair.container.name, pair.reference.name, ratio);
}

// The rows of the bit sets: set and test of a fixed set and of a heap set.
struct BitReports final {
    bench::Report set;
    bench::Report test;
    bench::Report heap_set;
    bench::Report heap_test;
};

// The rows of the slot map: get, and a pair of a remove and an insert.
struct SlotReports final {
    bench::Report get;
    bench::Report remove_insert;
};

// The rows of the small list: appends in the inline slots and past them.
struct ListReports final {
    bench::Report inline_appends;
    bench::Report spilled_appends;
};

// The event of a log: 72 bytes, as an event of a session log.
struct BenchEvent final {
    std::uint64_t value = 0;
    fixy::FixedArray<std::uint64_t, 8> payload{};
};

// The appends of the row append: the count of the events of the log at the end
// of that row.
constexpr std::size_t kLogAppends = std::size_t{1} << 17;

// The samples of the row growth.  Each one fills a log of about 65536 events.
constexpr std::size_t kGrowthSamples = 2000;

// The step of the sum of the values of the events, for a fold of a walk.
constexpr auto kAddValue = [](std::uint64_t acc, BenchEvent const& event) noexcept { return acc + event.value; };

// The names and the rows of a log (the log rows).
struct LogNames final {
    fixy::TextView append;
    fixy::TextView appends;
    fixy::TextView growth;
    fixy::TextView get;
    fixy::TextView walk;
};
struct LogReports final {
    bench::Report append;
    bench::Report appends;
    bench::Report growth;
    bench::Report get;
    bench::Report walk;
};

// The two passes of the five rows of one log (the log rows).
struct LogPasses final {
    LogReports const& first;
    LogReports const& second;
};

// The pass with the lower median of the two passes of one row.  The two passes
// can swap with no change of the result, except on a tie, which gives the first.
[[nodiscard]] bench::Report const& better_pass(bench::Report const& first CRUCIBLE_LIFETIMEBOUND,
                                               bench::Report const& second CRUCIBLE_LIFETIMEBOUND) noexcept {
    return second.pct.p50 < first.pct.p50 ? second : first;
}
void better_pass(bench::Report const&&, bench::Report const&) =
    delete("the result can name the first pass, and a temporary pass ends with the full expression");
void better_pass(bench::Report const&, bench::Report const&&) =
    delete("the result can name the second pass, and a temporary pass ends with the full expression");

// Writes the five rows of a log, each with the row of its reference log and the
// ratio of the two medians.  Each row is the better of its two passes.
template <class Log, class Reference>
void print_log(LogPasses const& log, LogPasses const& reference) noexcept {
    LogNames const& names = Log::kNames;
    LogNames const& others = Reference::kNames;
    print_pair(Pair{.container = Row{names.append, better_pass(log.first.append, log.second.append)},
                    .reference = Row{others.append, better_pass(reference.first.append, reference.second.append)}});
    print_pair(Pair{.container = Row{names.appends, better_pass(log.first.appends, log.second.appends)},
                    .reference = Row{others.appends, better_pass(reference.first.appends, reference.second.appends)}});
    print_pair(Pair{.container = Row{names.growth, better_pass(log.first.growth, log.second.growth)},
                    .reference = Row{others.growth, better_pass(reference.first.growth, reference.second.growth)}});
    print_pair(Pair{.container = Row{names.get, better_pass(log.first.get, log.second.get)},
                    .reference = Row{others.get, better_pass(reference.first.get, reference.second.get)}});
    print_pair(Pair{.container = Row{names.walk, better_pass(log.first.walk, log.second.walk)},
                    .reference = Row{others.walk, better_pass(reference.first.walk, reference.second.walk)}});
}

// The log of a SegmentList.  kFullCount is the count of the events of the
// segments 0 to 13, which hold 4, 8 and so on to 32768 events.
struct SegmentLog final {
    static constexpr std::size_t kFullCount = 65532;
    static constexpr LogNames kNames{.append = "SegmentList, one append",
                                     .appends = "SegmentList, 4096 appends",
                                     .growth = "SegmentList, append into a full log",
                                     .get = "SegmentList, get of 4096 events",
                                     .walk = "SegmentList, walk of 4096 events"};

    fixy::SegmentList<BenchEvent> events{};

    void append(BenchEvent const& event) noexcept { events.append(kAlloc, event); }
    [[nodiscard]] std::uint64_t value_at(std::size_t position) const noexcept {
        return events.get(position).match([](auto event) noexcept { return event->value; },
                                          [] noexcept { return std::uint64_t{0}; });
    }
    [[nodiscard]] std::uint64_t sum() const noexcept { return events.walk().fold(std::uint64_t{0}, kAddValue); }
    void clear() noexcept { events = fixy::SegmentList<BenchEvent>{}; }
};

// The names and the rows of a log that its writer publishes (the live rows).
struct LiveNames final {
    fixy::TextView append;
    fixy::TextView appends;
    fixy::TextView runs;
    fixy::TextView get;
    fixy::TextView walk;
    fixy::TextView count;
};
struct LiveReports final {
    bench::Report append;
    bench::Report appends;
    bench::Report runs;
    bench::Report get;
    bench::Report walk;
    bench::Report count;
};

// The run of one publish of the row runs.
constexpr std::size_t kRunEvents = 8;
using EventRun = fixy::FixedArray<BenchEvent, kRunEvents>;

// The two passes of the six rows of one live log, as for a log.
struct LivePasses final {
    LiveReports const& first;
    LiveReports const& second;
};

// Writes the six rows of a live log, each with the row of its reference log and
// the ratio of the two medians.  Each row is the better of its two passes.
template <class Log, class Reference>
void print_live_log(LivePasses const& log, LivePasses const& reference) noexcept {
    LiveNames const& names = Log::kNames;
    LiveNames const& others = Reference::kNames;
    print_pair(Pair{.container = Row{names.append, better_pass(log.first.append, log.second.append)},
                    .reference = Row{others.append, better_pass(reference.first.append, reference.second.append)}});
    print_pair(Pair{.container = Row{names.appends, better_pass(log.first.appends, log.second.appends)},
                    .reference = Row{others.appends, better_pass(reference.first.appends, reference.second.appends)}});
    print_pair(Pair{.container = Row{names.runs, better_pass(log.first.runs, log.second.runs)},
                    .reference = Row{others.runs, better_pass(reference.first.runs, reference.second.runs)}});
    print_pair(Pair{.container = Row{names.get, better_pass(log.first.get, log.second.get)},
                    .reference = Row{others.get, better_pass(reference.first.get, reference.second.get)}});
    print_pair(Pair{.container = Row{names.walk, better_pass(log.first.walk, log.second.walk)},
                    .reference = Row{others.walk, better_pass(reference.first.walk, reference.second.walk)}});
    print_pair(Pair{.container = Row{names.count, better_pass(log.first.count, log.second.count)},
                    .reference = Row{others.count, better_pass(reference.first.count, reference.second.count)}});
}

// The log of a LiveList.  The log makes the proof of the one writer of its list,
// and the one function that builds the log appends to it.
class LiveLog final {
    struct AppendRole final {};
    using Proof = ::foundation::SoleWriterProof<LiveLog, AppendRole>;

    fixy::LiveList<BenchEvent, Proof> events_{};

public:
    static constexpr LiveNames kNames{.append = "LiveList, one append",
                                      .appends = "LiveList, 4096 appends",
                                      .runs = "LiveList, 4096 events in runs of 8",
                                      .get = "LiveList, get of 4096 events",
                                      .walk = "LiveList, walk of 4096 events",
                                      .count = "LiveList, 4096 prefixes"};

    void append(BenchEvent const& event) noexcept { events_.append(Proof{}, kAlloc, event); }
    void append_run(EventRun const& run) noexcept { events_.append_each(Proof{}, kAlloc, run.view()); }
    [[nodiscard]] std::uint64_t sum_at(Positions const& positions) const noexcept {
        auto const events = events_.prefix();
        std::uint64_t total = 0;
        for (fixy::Index<kOperations> const position : positions) {
            total +=
                events.get(position.raw())
                    .match([](auto event) noexcept { return event->value; }, [] noexcept { return std::uint64_t{0}; });
        }
        return total;
    }
    [[nodiscard]] std::uint64_t sum() const noexcept {
        return events_.prefix().walk().fold(std::uint64_t{0}, kAddValue);
    }
    [[nodiscard]] std::size_t count() const noexcept { return events_.prefix().size(); }
};

// The append that meets a full log.  The hook before each sample gives the log
// back and fills a new one, outside the timed region.
template <class Log>
struct GrowthBody final {
    Log log{};
    BenchEvent event{};

    void before_sample() noexcept {
        log.clear();
        for (std::size_t count = 0; count != Log::kFullCount; ++count) {
            log.append(event);
        }
    }
    void operator()() noexcept {
        log.append(event);
        bench::do_not_optimize(log);
    }
};

// The five rows of one log.
template <class Log>
[[gnu::noinline]] LogReports measure_log(Positions const& positions) noexcept {
    constexpr LogNames const& names = Log::kNames;
    BenchEvent event{};
    Log growing{};
    Log events{};
    for (std::size_t value = 0; value != kOperations; ++value) {
        event.value = value;
        events.append(event);
    }
    GrowthBody<Log> growth{};
    return LogReports{
        .append =
            bench::Run(names.append).batch(1).warmup(0).samples(kLogAppends).measure([&] { growing.append(event); }),
        .appends = bench::run(names.appends,
                              [&] {
                                  Log fresh{};
                                  for (std::size_t count = 0; count != kOperations; ++count) {
                                      fresh.append(event);
                                  }
                                  bench::do_not_optimize(fresh);
                              }),
        .growth = bench::Run(names.growth).batch(1).warmup(4).samples(kGrowthSamples).measure(growth),
        .get = bench::run(names.get,
                          [&] {
                              std::uint64_t sum = 0;
                              for (fixy::Index<kOperations> const position : positions) {
                                  sum += events.value_at(position.raw());
                              }
                              bench::do_not_optimize(sum);
                          }),
        .walk = bench::run(names.walk,
                           [&] {
                               std::uint64_t sum = events.sum();
                               bench::do_not_optimize(sum);
                           }),
    };
}

// The six rows of one live log.
template <class Log>
[[gnu::noinline]] LiveReports measure_live_log(Positions const& positions) noexcept {
    constexpr LiveNames const& names = Log::kNames;
    BenchEvent event{};
    EventRun const run{};
    Log growing{};
    Log events{};
    for (std::size_t value = 0; value != kOperations; ++value) {
        event.value = value;
        events.append(event);
    }
    return LiveReports{
        .append =
            bench::Run(names.append).batch(1).warmup(0).samples(kLogAppends).measure([&] { growing.append(event); }),
        .appends = bench::run(names.appends,
                              [&] {
                                  Log fresh{};
                                  for (std::size_t count = 0; count != kOperations; ++count) {
                                      fresh.append(event);
                                  }
                                  bench::do_not_optimize(fresh);
                              }),
        .runs = bench::run(names.runs,
                           [&] {
                               Log fresh{};
                               for (std::size_t count = 0; count != kOperations; count += kRunEvents) {
                                   fresh.append_run(run);
                               }
                               bench::do_not_optimize(fresh);
                           }),
        .get = bench::run(names.get,
                          [&] {
                              std::uint64_t sum = events.sum_at(positions);
                              bench::do_not_optimize(sum);
                          }),
        .walk = bench::run(names.walk,
                           [&] {
                               std::uint64_t sum = events.sum();
                               bench::do_not_optimize(sum);
                           }),
        .count = bench::run(names.count,
                            [&] {
                                std::size_t total = 0;
                                for (std::size_t step = 0; step != kOperations; ++step) {
                                    total += events.count();
                                }
                                bench::do_not_optimize(total);
                            }),
    };
}

// Measures.  GCC compiles main and each lambda in it for size, as code
// that runs one time, so a loop in main keeps the step of its cursor as a
// call.  Each measure of a container and of its reference is a function of
// its own, which main does not inline.

[[gnu::noinline]] BitReports measure_bit_sets(Positions const& positions) noexcept {
    fixy::BitSet<kOperations> fixed_set{};
    fixy::HeapBitSet<BenchSlotTag> heap_set = fixy::mint_heap_bit_set<BenchSlotTag>(
        kAlloc, fixy::Quantity<fixy::unit::Element, std::uint32_t>{static_cast<std::uint32_t>(kOperations)});
    return BitReports{
        .set = bench::run("BitSet set",
                          [&] {
                              for (fixy::Index<kOperations> const position : positions) {
                                  fixed_set.set(position);
                              }
                              bench::do_not_optimize(fixed_set);
                          }),
        .test = bench::run("BitSet test",
                           [&] {
                               std::size_t held = 0;
                               for (fixy::Index<kOperations> const position : positions) {
                                   held += fixed_set.test(position) ? 1u : 0u;
                               }
                               bench::do_not_optimize(held);
                           }),
        .heap_set =
            bench::run("HeapBitSet set",
                       [&] {
                           for (fixy::Index<kOperations> const position : positions) {
                               heap_set.set(fixy::BitId<BenchSlotTag>{static_cast<std::uint32_t>(position.raw())});
                           }
                           bench::do_not_optimize(heap_set);
                       }),
        .heap_test = bench::run("HeapBitSet test",
                                [&] {
                                    std::size_t held = 0;
                                    for (fixy::Index<kOperations> const position : positions) {
                                        fixy::BitId<BenchSlotTag> const id{static_cast<std::uint32_t>(position.raw())};
                                        held += heap_set.test(id) ? 1u : 0u;
                                    }
                                    bench::do_not_optimize(held);
                                }),
    };
}

[[gnu::noinline]] SlotReports measure_slot_map(Positions const& positions) noexcept {
    auto map = fixy::mint_slot_map<std::uint32_t, BenchHandleTag>(
        kAlloc, fixy::Quantity<fixy::unit::Element, std::uint32_t>{static_cast<std::uint32_t>(kOperations)});
    using Key = decltype(map)::key_type;
    fixy::FixedArray<std::uint64_t, kOperations> keys{};
    for (fixy::Index<kOperations> const index : keys.indices()) {
        std::uint64_t const key =
            map.insert(static_cast<std::uint32_t>(index.raw())).expect("the map has a free slot").raw();
        keys.set(index, key);
    }
    return SlotReports{
        .get = bench::run("SlotMap get",
                          [&] {
                              std::uint32_t sum = 0;
                              for (fixy::Index<kOperations> const position : positions) {
                                  for (auto const found : map.get(Key{keys.at(position)})) {
                                      sum += *found;
                                  }
                              }
                              bench::do_not_optimize(sum);
                          }),
        .remove_insert = bench::run("SlotMap remove and insert",
                                    [&] {
                                        for (fixy::Index<kOperations> const position : positions) {
                                            for (std::uint32_t const value : map.remove(Key{keys.at(position)})) {
                                                keys.set(position,
                                                         map.insert(value).expect("the remove freed a slot").raw());
                                            }
                                        }
                                        bench::do_not_optimize(map);
                                    }),
    };
}

[[gnu::noinline]] ListReports measure_small_lists() noexcept {
    return ListReports{
        .inline_appends = bench::run("SmallList inline appends",
                                     [&] {
                                         for (std::size_t list_index = 0; list_index < kOperations / 8; ++list_index) {
                                             fixy::SmallList<std::uint32_t, 8> list =
                                                 fixy::mint_small_list<std::uint32_t, 8>(
                                                     fixy::Quantity<fixy::unit::Element, std::size_t>{64});
                                             for (std::uint32_t value = 0; value < 8; ++value) {
                                                 list.append(kAlloc, value).expect("the list is below its maximum");
                                             }
                                             bench::do_not_optimize(list);
                                         }
                                     }),
        .spilled_appends = bench::run("SmallList spilled appends",
                                      [&] {
                                          for (std::size_t list_index = 0; list_index < kOperations / 8; ++list_index) {
                                              fixy::SmallList<std::uint32_t, 2> list =
                                                  fixy::mint_small_list<std::uint32_t, 2>(
                                                      fixy::Quantity<fixy::unit::Element, std::size_t>{64});
                                              for (std::uint32_t value = 0; value < 8; ++value) {
                                                  list.append(kAlloc, value).expect("the list is below its maximum");
                                              }
                                              bench::do_not_optimize(list);
                                          }
                                      }),
    };
}

// References.  Each one is the raw form of the state of the art for the
// verbs of one container.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the raw forms of the state of the art are the references of the containers")

// The raw slot table: the slots of the slotmap crate of Rust.  The values,
// the stack of the free indexes and the generations live on the heap.
struct RawSlotTable final {
    std::uint32_t* values = nullptr;
    std::uint32_t* free_stack = nullptr;
    std::uint32_t* generations = nullptr;
    std::uint32_t capacity = 0;
    std::uint32_t used = 0;
    std::uint32_t free_count = 0;
    std::uint32_t live = 0;

    explicit RawSlotTable(std::uint32_t slots) noexcept
        : values{static_cast<std::uint32_t*>(__builtin_calloc(slots, sizeof(std::uint32_t)))},
          free_stack{static_cast<std::uint32_t*>(__builtin_calloc(slots, sizeof(std::uint32_t)))},
          generations{static_cast<std::uint32_t*>(__builtin_calloc(slots, sizeof(std::uint32_t)))},
          capacity{slots} {
        if (values == nullptr || free_stack == nullptr || generations == nullptr) {
            fixy::fatal("the raw slot table of the bench got its storage");
        }
    }
    RawSlotTable(RawSlotTable const&) = delete("the table owns its storage");
    RawSlotTable& operator=(RawSlotTable const&) = delete("the table owns its storage");
    ~RawSlotTable() {
        __builtin_free(values);
        __builtin_free(free_stack);
        __builtin_free(generations);
    }

    // The key of the new value, or the largest word when no slot is free.
    [[nodiscard]] std::uint64_t insert(std::uint32_t value) noexcept {
        std::uint32_t index = 0;
        if (free_count != 0) {
            index = free_stack[--free_count];
        } else if (used != capacity) {
            index = used++;
            generations[index] = 0;
        } else {
            return ~std::uint64_t{0};
        }
        std::uint32_t const generation = generations[index] + 1u;
        generations[index] = generation;
        values[index] = value;
        ++live;
        return std::uint64_t{index} | (std::uint64_t{generation} << 32);
    }

    // The value of key, or null for a key that its slot does not hold.
    [[nodiscard]] std::uint32_t const* get(std::uint64_t key) const noexcept {
        std::uint32_t const index = static_cast<std::uint32_t>(key);
        if (index >= used) return nullptr;
        std::uint32_t const generation = generations[index];
        if ((key >> 32) != generation || (generation & 1u) == 0) return nullptr;
        return values + index;
    }

    // True when the slot of key held its value, which goes to value.
    [[nodiscard]] bool remove(std::uint64_t key, std::uint32_t& value) noexcept {
        std::uint32_t const* const held = get(key);
        if (held == nullptr) return false;
        std::uint32_t const index = static_cast<std::uint32_t>(key);
        value = *held;
        generations[index] += 1u;
        free_stack[free_count++] = index;
        --live;
        return true;
    }
};

// The raw small vector: the push_back of llvm::SmallVector.  The pointer to
// the first element names the inline slots until the first growth, so the
// vector does not move.
template <std::size_t Inline>
struct RawSmallVector final {
    std::uint32_t inline_slots[Inline] = {};
    std::uint32_t* first = inline_slots;
    std::size_t count = 0;
    std::size_t capacity = Inline;

    RawSmallVector() noexcept = default;
    RawSmallVector(RawSmallVector const&) = delete("the vector points into itself");
    RawSmallVector& operator=(RawSmallVector const&) = delete("the vector points into itself");
    ~RawSmallVector() {
        if (first != inline_slots) __builtin_free(first);
    }

    void push_back(std::uint32_t value) noexcept {
        if (count >= capacity) [[unlikely]] {
            grow();
        }
        first[count] = value;
        ++count;
    }

    [[gnu::cold, gnu::noinline]] void grow() noexcept {
        std::size_t const next = capacity * 2;
        auto* const target = static_cast<std::uint32_t*>(__builtin_malloc(next * sizeof(std::uint32_t)));
        if (target == nullptr) fixy::fatal("the raw small vector of the bench got its storage");
        __builtin_memcpy(target, first, count * sizeof(std::uint32_t));
        if (first != inline_slots) __builtin_free(first);
        first = target;
        capacity = next;
    }
};

// The raw segments: segment k holds 4 << k events, the directory is in the
// object, and the top bit of the index plus 4 gives the segment.
struct RawSegmentLog final {
    static constexpr std::size_t kFullCount = 65532;
    static constexpr LogNames kNames{.append = "raw segments, one append",
                                     .appends = "raw segments, 4096 appends",
                                     .growth = "raw segments, append into a full log",
                                     .get = "raw segments, get of 4096 events",
                                     .walk = "raw segments, walk of 4096 events"};

    BenchEvent* segments[32] = {};
    BenchEvent* tail = nullptr;
    BenchEvent* tail_end = nullptr;
    std::size_t count = 0;

    RawSegmentLog() noexcept = default;
    RawSegmentLog(RawSegmentLog const&) = delete("the log owns its segments");
    RawSegmentLog& operator=(RawSegmentLog const&) = delete("the log owns its segments");
    ~RawSegmentLog() { clear(); }

    void append(BenchEvent const& event) noexcept {
        if (tail == tail_end) [[unlikely]] {
            grow();
        }
        *tail++ = event;
        ++count;
    }

    [[gnu::cold, gnu::noinline]] void grow() noexcept {
        unsigned const segment = 61u - static_cast<unsigned>(__builtin_clzll(count + 4));
        std::size_t const slots = std::size_t{4} << segment;
        tail = static_cast<BenchEvent*>(__builtin_malloc(slots * sizeof(BenchEvent)));
        if (tail == nullptr) fixy::fatal("the raw segments of the bench got their storage");
        segments[segment] = tail;
        tail_end = tail + slots;
    }

    [[nodiscard]] std::uint64_t value_at(std::size_t position) const noexcept {
        if (position >= count) return 0;
        std::size_t const key = position + 4;
        unsigned const top = 63u - static_cast<unsigned>(__builtin_clzll(key));
        return segments[top - 2][key ^ (std::size_t{1} << top)].value;
    }

    [[nodiscard]] std::uint64_t sum() const noexcept {
        std::uint64_t total = 0;
        std::size_t left = count;
        for (unsigned segment = 0; left != 0; ++segment) {
            std::size_t const slots = std::size_t{4} << segment;
            std::size_t const run = left < slots ? left : slots;
            for (std::size_t offset = 0; offset < run; ++offset) {
                total += segments[segment][offset].value;
            }
            left -= run;
        }
        return total;
    }

    void clear() noexcept {
        for (BenchEvent*& segment : segments) {
            __builtin_free(segment);
            segment = nullptr;
        }
        tail = nullptr;
        tail_end = nullptr;
        count = 0;
    }
};

// The raw live segments: the raw segments, and the published count on a line of
// its own.
struct RawLiveLog final {
    static constexpr LiveNames kNames{.append = "raw live segments, one append",
                                      .appends = "raw live segments, 4096 appends",
                                      .runs = "raw live segments, 4096 events in runs of 8",
                                      .get = "raw live segments, get of 4096 events",
                                      .walk = "raw live segments, walk of 4096 events",
                                      .count = "raw live segments, 4096 counts"};

    RawSegmentLog log{};
    alignas(64) std::size_t published = 0;

    void publish() noexcept { __atomic_store_n(&published, log.count, __ATOMIC_RELEASE); }
    [[nodiscard]] std::size_t count() const noexcept { return __atomic_load_n(&published, __ATOMIC_ACQUIRE); }

    void append(BenchEvent const& event) noexcept {
        log.append(event);
        publish();
    }

    void append_run(EventRun const& run) noexcept {
        BenchEvent const* from = __builtin_addressof(run.at(fixy::index<0>()));
        std::size_t left = kRunEvents;
        while (left != 0) {
            if (log.tail == log.tail_end) [[unlikely]] {
                log.grow();
            }
            std::size_t const room = static_cast<std::size_t>(log.tail_end - log.tail);
            std::size_t const copied = left < room ? left : room;
            __builtin_memcpy(static_cast<void*>(log.tail), from, copied * sizeof(BenchEvent));
            log.tail += copied;
            log.count += copied;
            from += copied;
            left -= copied;
        }
        publish();
    }

    [[nodiscard]] std::uint64_t sum_at(Positions const& positions) const noexcept {
        std::size_t const held = count();
        std::uint64_t total = 0;
        for (fixy::Index<kOperations> const index : positions) {
            std::size_t const position = index.raw();
            if (position >= held) continue;
            std::size_t const key = position + 4;
            unsigned const top = 63u - static_cast<unsigned>(__builtin_clzll(key));
            total += log.segments[top - 2][key ^ (std::size_t{1} << top)].value;
        }
        return total;
    }

    [[nodiscard]] std::uint64_t sum() const noexcept {
        std::uint64_t total = 0;
        std::size_t left = count();
        for (unsigned segment = 0; left != 0; ++segment) {
            std::size_t const slots = std::size_t{4} << segment;
            std::size_t const run = left < slots ? left : slots;
            for (std::size_t offset = 0; offset < run; ++offset) {
                total += log.segments[segment][offset].value;
            }
            left -= run;
        }
        return total;
    }
};

// The raw event vector: the push_back of std::vector.
struct RawVectorLog final {
    static constexpr std::size_t kFullCount = 65536;
    static constexpr LogNames kNames{.append = "raw event vector, one append",
                                     .appends = "raw event vector, 4096 appends",
                                     .growth = "raw event vector, append into a full log",
                                     .get = "raw event vector, get of 4096 events",
                                     .walk = "raw event vector, walk of 4096 events"};

    BenchEvent* first = nullptr;
    std::size_t count = 0;
    std::size_t capacity = 0;

    RawVectorLog() noexcept = default;
    RawVectorLog(RawVectorLog const&) = delete("the log owns its storage");
    RawVectorLog& operator=(RawVectorLog const&) = delete("the log owns its storage");
    ~RawVectorLog() { clear(); }

    void append(BenchEvent const& event) noexcept {
        if (count == capacity) [[unlikely]] {
            grow();
        }
        first[count] = event;
        ++count;
    }

    [[gnu::cold, gnu::noinline]] void grow() noexcept {
        std::size_t const next = capacity == 0 ? 1 : capacity * 2;
        auto* const target = static_cast<BenchEvent*>(__builtin_malloc(next * sizeof(BenchEvent)));
        if (target == nullptr) fixy::fatal("the raw event vector of the bench got its storage");
        if (count != 0) __builtin_memcpy(static_cast<void*>(target), first, count * sizeof(BenchEvent));
        __builtin_free(first);
        first = target;
        capacity = next;
    }

    [[nodiscard]] std::uint64_t value_at(std::size_t position) const noexcept {
        return position < count ? first[position].value : 0;
    }

    [[nodiscard]] std::uint64_t sum() const noexcept {
        std::uint64_t total = 0;
        for (std::size_t position = 0; position < count; ++position) {
            total += first[position].value;
        }
        return total;
    }

    void clear() noexcept {
        __builtin_free(first);
        first = nullptr;
        count = 0;
        capacity = 0;
    }
};

// The rows of the reserved run, the floor of an append: one append, and 4096
// appends from the start of the run.  The published rows store the count on a
// line of its own too, the floor of a live append.
struct FloorReports final {
    bench::Report append;
    bench::Report appends;
    bench::Report published_append;
    bench::Report published_appends;
};

[[gnu::noinline]] FloorReports measure_reserved_run() noexcept {
    BenchEvent event{};
    auto* const first = static_cast<BenchEvent*>(__builtin_calloc(kLogAppends, sizeof(BenchEvent)));
    if (first == nullptr) fixy::fatal("the reserved run of the bench got its storage");
    for (std::size_t position = 0; position < kLogAppends; ++position) {
        first[position].value = position;
    }
    BenchEvent* tail = first;
    std::size_t count = 0;
    alignas(64) std::size_t published = 0;
    FloorReports reports{
        .append = bench::Run("reserved run append").batch(1).warmup(0).samples(kLogAppends).measure([&] {
            *tail++ = event;
            ++count;
            bench::do_not_optimize(tail);
            bench::do_not_optimize(count);
        }),
        .appends = bench::run("reserved run appends",
                              [&] {
                                  tail = first;
                                  count = 0;
                                  for (std::size_t step = 0; step != kOperations; ++step) {
                                      *tail++ = event;
                                      ++count;
                                      bench::do_not_optimize(tail);
                                      bench::do_not_optimize(count);
                                  }
                              }),
        .published_append =
            bench::Run("published run append").batch(1).warmup(0).samples(kLogAppends - kOperations).measure([&] {
                *tail++ = event;
                ++count;
                __atomic_store_n(&published, count, __ATOMIC_RELEASE);
                bench::do_not_optimize(tail);
                bench::do_not_optimize(count);
            }),
        .published_appends = bench::run("published run appends",
                                        [&] {
                                            tail = first;
                                            count = 0;
                                            for (std::size_t step = 0; step != kOperations; ++step) {
                                                *tail++ = event;
                                                ++count;
                                                __atomic_store_n(&published, count, __ATOMIC_RELEASE);
                                                bench::do_not_optimize(tail);
                                                bench::do_not_optimize(count);
                                            }
                                        }),
    };
    __builtin_free(first);
    return reports;
}

[[gnu::noinline]] BitReports measure_raw_bits(Positions const& positions) noexcept {
    std::uint64_t words[kWords] = {};
    auto* const heap_words = static_cast<std::uint64_t*>(__builtin_malloc(kWords * sizeof(std::uint64_t)));
    if (heap_words == nullptr) fixy::fatal("the raw heap bitmap of the bench got its storage");
    __builtin_memset(heap_words, 0, kWords * sizeof(std::uint64_t));
    BitReports reports{
        .set = bench::run("raw bitmap set",
                          [&] {
                              for (fixy::Index<kOperations> const position : positions) {
                                  std::size_t const bit = position.raw();
                                  words[bit / 64] |= std::uint64_t{1} << (bit % 64);
                              }
                              bench::do_not_optimize(words);
                          }),
        .test = bench::run("raw bitmap test",
                           [&] {
                               std::size_t held = 0;
                               for (fixy::Index<kOperations> const position : positions) {
                                   std::size_t const bit = position.raw();
                                   held += (words[bit / 64] >> (bit % 64)) & 1u;
                               }
                               bench::do_not_optimize(held);
                           }),
        .heap_set = bench::run("raw heap bitmap set",
                               [&] {
                                   for (fixy::Index<kOperations> const position : positions) {
                                       std::uint32_t const bit = static_cast<std::uint32_t>(position.raw());
                                       heap_words[bit / 64] |= std::uint64_t{1} << (bit % 64);
                                   }
                                   bench::do_not_optimize(heap_words);
                               }),
        .heap_test = bench::run("raw heap bitmap test",
                                [&] {
                                    std::size_t held = 0;
                                    for (fixy::Index<kOperations> const position : positions) {
                                        std::uint32_t const bit = static_cast<std::uint32_t>(position.raw());
                                        held += (heap_words[bit / 64] >> (bit % 64)) & 1u;
                                    }
                                    bench::do_not_optimize(held);
                                }),
    };
    __builtin_free(heap_words);
    return reports;
}

[[gnu::noinline]] SlotReports measure_raw_slots(Positions const& positions) noexcept {
    RawSlotTable table{static_cast<std::uint32_t>(kOperations)};
    std::uint64_t keys[kOperations] = {};
    for (std::size_t index = 0; index < kOperations; ++index) {
        keys[index] = table.insert(static_cast<std::uint32_t>(index));
    }
    return SlotReports{
        .get = bench::run("raw slot table get",
                          [&] {
                              std::uint32_t sum = 0;
                              for (fixy::Index<kOperations> const position : positions) {
                                  if (std::uint32_t const* const found = table.get(keys[position.raw()])) {
                                      sum += *found;
                                  }
                              }
                              bench::do_not_optimize(sum);
                          }),
        .remove_insert = bench::run("raw slot table remove and insert",
                                    [&] {
                                        for (fixy::Index<kOperations> const position : positions) {
                                            std::uint32_t value = 0;
                                            if (table.remove(keys[position.raw()], value)) {
                                                keys[position.raw()] = table.insert(value);
                                            }
                                        }
                                        bench::do_not_optimize(table);
                                    }),
    };
}

[[gnu::noinline]] ListReports measure_raw_lists() noexcept {
    return ListReports{
        .inline_appends = bench::run("raw small vector inline appends",
                                     [&] {
                                         for (std::size_t list_index = 0; list_index < kOperations / 8; ++list_index) {
                                             RawSmallVector<8> list{};
                                             for (std::uint32_t value = 0; value < 8; ++value) {
                                                 list.push_back(value);
                                             }
                                             bench::do_not_optimize(list);
                                         }
                                     }),
        .spilled_appends = bench::run("raw small vector spilled appends",
                                      [&] {
                                          for (std::size_t list_index = 0; list_index < kOperations / 8; ++list_index) {
                                              RawSmallVector<2> list{};
                                              for (std::uint32_t value = 0; value < 8; ++value) {
                                                  list.push_back(value);
                                              }
                                              bench::do_not_optimize(list);
                                          }
                                      }),
    };
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace

int main() {
    bench::elevate_priority();

    // A fixed pattern of positions below 4096.  An Index has no default value,
    // so a list holds the pattern.
    Positions positions;
    std::uint32_t pattern = 0x9E3779B9u;
    for (std::size_t step = 0; step < kOperations; ++step) {
        pattern = pattern * 1664525u + 1013904223u;
        static_cast<void>(positions.append(fixy::Index<kOperations>::check(pattern >> 20).value_or(fixy::index<0>()))
                              .expect("the list holds 4096 positions"));
    }

    BitReports const raw_bits = measure_raw_bits(positions);
    BitReports const bits = measure_bit_sets(positions);
    print_pair(
        Pair{.container = Row{"BitSet set, 4096 indexes", bits.set}, .reference = Row{"raw bitmap set", raw_bits.set}});
    print_pair(Pair{.container = Row{"BitSet test, 4096 indexes", bits.test},
                    .reference = Row{"raw bitmap test", raw_bits.test}});
    print_pair(Pair{.container = Row{"HeapBitSet set, 4096 ids", bits.heap_set},
                    .reference = Row{"raw heap bitmap set", raw_bits.heap_set}});
    print_pair(Pair{.container = Row{"HeapBitSet test, 4096 ids", bits.heap_test},
                    .reference = Row{"raw heap bitmap test", raw_bits.heap_test}});

    SlotReports const raw_slots = measure_raw_slots(positions);
    SlotReports const slots = measure_slot_map(positions);
    print_pair(Pair{.container = Row{"SlotMap get, 4096 keys", slots.get},
                    .reference = Row{"raw slot table get", raw_slots.get}});
    print_pair(Pair{.container = Row{"SlotMap remove and insert, 4096 pairs", slots.remove_insert},
                    .reference = Row{"raw slot table remove and insert", raw_slots.remove_insert}});

    ListReports const raw_lists = measure_raw_lists();
    ListReports const lists = measure_small_lists();
    print_pair(Pair{.container = Row{"SmallList of 8 inline, 8 appends, 512 lists", lists.inline_appends},
                    .reference = Row{"raw small vector inline appends", raw_lists.inline_appends}});
    print_pair(Pair{.container = Row{"SmallList of 2 inline, 8 appends, 512 lists", lists.spilled_appends},
                    .reference = Row{"raw small vector spilled appends", raw_lists.spilled_appends}});

    FloorReports const floor = measure_reserved_run();
    print_row(Row{"reserved run, one append", floor.append});
    print_row(Row{"reserved run, 4096 appends", floor.appends});
    LogReports const raw_segments = measure_log<RawSegmentLog>(positions);
    LogReports const raw_vector = measure_log<RawVectorLog>(positions);
    LogReports const segments = measure_log<SegmentLog>(positions);
    LogReports const segments_again = measure_log<SegmentLog>(positions);
    LogReports const raw_vector_again = measure_log<RawVectorLog>(positions);
    LogReports const raw_segments_again = measure_log<RawSegmentLog>(positions);
    LogPasses const segment_passes{.first = segments, .second = segments_again};
    print_log<SegmentLog, RawSegmentLog>(segment_passes,
                                         LogPasses{.first = raw_segments, .second = raw_segments_again});
    print_log<SegmentLog, RawVectorLog>(segment_passes, LogPasses{.first = raw_vector, .second = raw_vector_again});

    print_row(Row{"published run, one append", floor.published_append});
    print_row(Row{"published run, 4096 appends", floor.published_appends});
    LiveReports const raw_live = measure_live_log<RawLiveLog>(positions);
    LiveReports const live = measure_live_log<LiveLog>(positions);
    LiveReports const live_again = measure_live_log<LiveLog>(positions);
    LiveReports const raw_live_again = measure_live_log<RawLiveLog>(positions);
    print_live_log<LiveLog, RawLiveLog>(LivePasses{.first = live, .second = live_again},
                                        LivePasses{.first = raw_live, .second = raw_live_again});
    return 0;
}
