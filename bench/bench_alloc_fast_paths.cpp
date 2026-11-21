// The fast paths of the heap state of the allocator of the base
// (foundation/alloc/HeapState.h, THE FAST PATHS) on one core, against the
// pinned mimalloc and jemalloc, with the speed of light of the core, and the
// two measures of the heap table that the owner asked for.
//
// THE ROWS
//   Each row times blocks of 16 bytes, the class 1, and gives the mean of one
//   operation in a window of kBatch operations.
//   pair         allocate and release of one block on a warm bin: the two
//                fast paths, and no cold path
//   allocate     kBatch allocations in each window: the fast path of the
//                allocation, and the refill of half a bin from the bitmap of
//                a slab each 64 blocks.  Before each window, outside the
//                time, the row releases the blocks of the window before
//   release      kBatch releases in each window, in the order of their
//                allocation: the fast path of the release, and the flush of
//                half a bin into the bitmaps each 64 blocks.  Before each
//                window, outside the time, the row takes the blocks
//   pair, call   the pair of the base through two functions that the
//                compiler cannot see through, the form of each reference
//   A reference row has the shape of its row, through mi_malloc and mi_free
//   of the pinned mimalloc 3.7.0, or je_malloc and je_free of the pinned
//   jemalloc 5.4.0.  Each call of a reference is a call.
//
// THE WINDOWS
//   Each body runs its whole window in one call, with its calls and its sum
//   in locals, and per_operation divides the times of the window by its
//   operations.  The refill and the flush are calls on the unlikely branches
//   of the loop.  GCC then loads again in each turn each value that a body
//   holds in memory, also with integer words in the bins, because a call can
//   write an object whose address left its frame (HOT LOOPS AND COLD CALLS).
//   A body of one operation, which the harness calls by reference kBatch
//   times, thus loaded the heap in each turn.  A local keeps the heap in a
//   register, as a loop in the body of a heap scope does (the codegen row
//   heap_scope_pairs).  The row pair, scope opens its scope in each window,
//   so the read of the cell runs one time for each kBatch pairs.
//
// THE SPEED OF LIGHT
//   The clock is 64 dependent adds in one block of code, one cycle each, as
//   in bench_alloc_owner_lookup.cpp.  The floor row is the loop of the pair
//   with the work replaced by its minimum: the sum of the cursor in a
//   register, one cycle for the taken branch of the loop.  The memory word
//   row is the floor of a chain through one word of memory: a load, an add
//   and a store in each step.  The model floor of each row is the largest of
//   three counts of its timed loop in the Release code: its macro-ops over the
//   6 that the core dispatches in a cycle, its loads and stores over the 3
//   memory operations of a cycle, and its stores over the 2 stores of a
//   cycle, from the Software Optimization Guide for the AMD Zen4
//   Microarchitecture, AMD 57647 revision 1.00 (2023-01): section 2.9.8
//   (page 30) for the dispatch, section 2.12 (page 39) for the three memory
//   operations of the load-store unit, at most two of them stores, and
//   section 2.9.3 (page 28) for the fusion of a compare and its branch.
//   Section 2.9.8 also limits a window of six macro-ops to four ALU
//   operations and two branches, and neither limit binds on a row here.
//   luks-a is an EPYC 9754 (Zen 4c, the Zen 4 architecture).  THE GAPS below
//   give each count.
//
// THE MEASURES OF THE OWNER
//   claim        the first allocation of a new thread: the claim of its heap
//                in the heap table, the take of a chunk of the window, and the
//                refill of half a bin.  kClaimThreads threads run one after
//                the other, and the row gives the p50, the p99 and the maximum
//   resident     the resident bytes of the process after kClaimThreads threads
//                each claimed a heap and took one block, over the threads: the
//                pages of one heap after its first use.  A first thread warms
//                the cache of thread stacks of the C library before the first
//                reading.  The kernel of luks-a gives transparent huge pages
//                only on madvise, and the page source gives the heap table no
//                advice, so each page of a heap is a base page
//
// THE GAPS
//   The counts come from the timed loops of the Release code of this file,
//   the loop of the first window from rdtsc to rdtscp in each Run::measure,
//   with each compare and its branch as one macro-op:
//     pair           13 instructions, 10 macro-ops, 4 loads, 3 stores: the
//                    memory operations give the floor, 2.33 cycles
//     pair, scope    the same loop in the body of the scope: 2.33 cycles
//     warm allocate  10 instructions, 8 macro-ops, 3 loads, 2 stores: 1.67
//                    cycles
//     warm release   10 instructions, 8 macro-ops, 3 loads, 2 stores: 1.67
//                    cycles
//     floor          4 instructions, 3 macro-ops, no memory operation: the
//                    taken branch, 1 cycle
//   The loop of the pair stores head two times, because the flush, a call on
//   the unlikely branch between the two stores, can read it.  The store of
//   head and its load in the next turn are a chain through memory in each
//   pair.
//   The rows of the stream subtract their warm rows: the refill and the flush
//   of each block.  The refill loop is 9 instructions a block, with 6 ALU
//   operations at 4 a cycle (section 2.9.8): 1.5 cycles.  The flush loop is
//   23 instructions, 19 macro-ops a block (a bt and its jb do not fuse): 3.17
//   cycles of dispatch.  The fixed work of one refill or flush adds the rest.
//
// THE COMMAND
//   python3 utils/bin/loop bench bench_alloc_fast_paths

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Quarantine.h>
#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/HeapState.h>
#include <foundation/alloc/Layout.h>
#include <foundation/alloc/Root.h>
#include <foundation/effects/Effect.h>
#include <foundation/heap/HeapScope.h>
#include <foundation/os/File.h>

#include <cstddef>
#include <cstdint>
#include <utility>

#include "bench_harness.h"

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: a reference allocator gives a raw address, which no type of the base holds")
#if defined(CRUCIBLE_BENCH_JEMALLOC)
// The calls of the pinned jemalloc with the prefix je_.  Its generated header
// is in the build directory, so the bench declares the calls itself, as
// bench_alloc_scaling_rows.h does.
extern "C" {
void* je_malloc(std::size_t size);
void je_free(void* ptr);
}
#endif
#if defined(CRUCIBLE_BENCH_MIMALLOC)
#include <mimalloc.h>
#endif

namespace {

// The calls of one reference: allocate gives the first byte of a block of 16
// bytes as an address word, and release gives the block back.  Only these
// calls hold a raw address.
#if defined(CRUCIBLE_BENCH_MIMALLOC)
struct MimallocCalls {
    [[nodiscard, gnu::always_inline]] static ::foundation::alloc::AddressWord allocate() noexcept {
        return ::foundation::alloc::AddressWord{__builtin_bit_cast(std::uint64_t, mi_malloc(16))};
    }
    [[gnu::always_inline]] static void release(::foundation::alloc::AddressWord block) noexcept {
        mi_free(__builtin_bit_cast(void*, static_cast<std::uint64_t>(block)));
    }
};
#endif
#if defined(CRUCIBLE_BENCH_JEMALLOC)
struct JemallocCalls {
    [[nodiscard, gnu::always_inline]] static ::foundation::alloc::AddressWord allocate() noexcept {
        return ::foundation::alloc::AddressWord{__builtin_bit_cast(std::uint64_t, je_malloc(16))};
    }
    [[gnu::always_inline]] static void release(::foundation::alloc::AddressWord block) noexcept {
        je_free(__builtin_bit_cast(void*, static_cast<std::uint64_t>(block)));
    }
};
#endif

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: a chain of dependent adds gives the clock of the core")
namespace {

// The dependent adds of one call of the clock body (THE SPEED OF LIGHT).
constexpr std::uint32_t kClockAdds = 64;

// kClockAdds dependent adds to the value in one block of code, which the
// compiler cannot fold.
[[gnu::always_inline]] inline void add_chain(std::uint64_t& value) noexcept {
#if defined(__x86_64__)
    asm volatile(".rept %c1\n\tadd $1, %0\n\t.endr" : "+r"(value) : "i"(kClockAdds));
#else
    asm volatile(".rept %c1\n\tadd %0, %0, #1\n\t.endr" : "+r"(value) : "i"(kClockAdds));
#endif
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

namespace alloc = ::foundation::alloc;
namespace core = ::foundation::core;
namespace os = ::foundation::os;

// The operations of one window, the windows of one row, and the windows of
// the clock (THE ROWS).
constexpr std::size_t kBatch = 1024;
constexpr std::size_t kSamples = 2048;
constexpr std::size_t kClockBatch = 256;
// The operations of one window of a warm row: half a bin of the class 1, so
// the window stays inside one bin.  The TSC steps in 10 ns, so the mean of
// one operation of such a window has a step of 0.16 ns.
constexpr std::size_t kWarmBatch = 64;
// The threads of the measures of the owner: more than the 102 slots of one
// step of the heap table, so the claims cross five steps.
constexpr std::size_t kClaimThreads = 512;

// The class of each row: 16 bytes.
constexpr alloc::SizeClass kClass{1};

// The model of the core of luks-a (THE SPEED OF LIGHT): the macro-ops that the
// core dispatches in one cycle (AMD 57647, section 2.9.8, page 30), and the
// three memory operations of the load-store unit in one cycle, at most two of
// them stores (AMD 57647, section 2.12, page 39).  A compare and
// the branch after it are one macro-op (section 2.9.3, page 28).
constexpr double kDispatchWidth = 6.0;
constexpr double kMemoryOpsPerCycle = 3.0;
constexpr double kStoresPerCycle = 2.0;
// A loop takes at least one cycle for its taken branch.
constexpr double kFloorRowCycles = 1.0;

// The work of one turn of a timed loop in the Release code (THE GAPS).
struct LoopShape {
    double macro_ops = 0.0;
    double loads = 0.0;
    double stores = 0.0;
};

// The cycles of one turn of a loop of this shape on the ports of the core:
// the largest of its dispatch, its memory operations and its stores, and one
// cycle for the taken branch.
[[nodiscard]] constexpr double floor_cycles(LoopShape shape) noexcept {
    double cycles = kFloorRowCycles;
    cycles = shape.macro_ops / kDispatchWidth > cycles ? shape.macro_ops / kDispatchWidth : cycles;
    double const memory = (shape.loads + shape.stores) / kMemoryOpsPerCycle;
    cycles = memory > cycles ? memory : cycles;
    cycles = shape.stores / kStoresPerCycle > cycles ? shape.stores / kStoresPerCycle : cycles;
    return cycles;
}

constexpr LoopShape kPairShape{.macro_ops = 10.0, .loads = 4.0, .stores = 3.0};
constexpr LoopShape kScopePairShape{.macro_ops = 10.0, .loads = 4.0, .stores = 3.0};
constexpr LoopShape kWarmAllocateShape{.macro_ops = 8.0, .loads = 3.0, .stores = 2.0};
constexpr LoopShape kWarmReleaseShape{.macro_ops = 8.0, .loads = 3.0, .stores = 2.0};
// The pair through two calls: four taken branches with a bubble for each call
// and each return (section 2.8.1.2, page 21).
constexpr double kCallPairFloorCycles = 8.0;

using BatchIndex = core::Index<kBatch>;

// The blocks of one window of a stream row.
struct Blocks {
    core::FixedArray<alloc::AddressWord, kBatch> words{};
};

[[nodiscard]] BatchIndex batch_at(std::size_t raw) noexcept {
    return BatchIndex::check(raw & (kBatch - 1)).expect("a masked position lies below kBatch");
}

// The calls of the base: the fast paths of the heap of the thread, inline.
struct HearthCalls {
    alloc::HeapState& heap;

    [[nodiscard, gnu::always_inline]] alloc::AddressWord allocate() const noexcept { return heap.allocate(kClass); }
    [[gnu::always_inline]] void release(alloc::AddressWord block) const noexcept { heap.release(kClass, block); }
};

// The two fast paths in functions that the compiler cannot see through, for
// the row pair, call.
[[nodiscard, gnu::noipa]] alloc::AddressWord allocate_through_call(alloc::HeapState& heap) noexcept {
    return heap.allocate(kClass);
}

[[gnu::noipa]] void release_through_call(alloc::HeapState& heap, alloc::AddressWord block) noexcept {
    heap.release(kClass, block);
}

struct HearthCallCalls {
    alloc::HeapState& heap;

    [[nodiscard, gnu::always_inline]] alloc::AddressWord allocate() const noexcept {
        return allocate_through_call(heap);
    }
    [[gnu::always_inline]] void release(alloc::AddressWord block) const noexcept { release_through_call(heap, block); }
};

// The body of the row pair: one window of kBatch allocations, each with its
// release, and the low word of each block in the sum (THE WINDOWS).
template <class Calls>
struct PairLoop {
    Calls calls;
    std::uint32_t sum = 0;

    [[gnu::always_inline]] void operator()() noexcept {
        Calls const local = calls;
        std::uint32_t total = sum;
        for (std::size_t turn = 0; turn < kBatch; ++turn) {
            alloc::AddressWord const block = local.allocate();
            total += static_cast<std::uint32_t>(static_cast<std::uint64_t>(block));
            local.release(block);
        }
        sum = total;
    }
};

// Allocates the blocks of one window of COUNT operations into the blocks,
// with the calls and the blocks in locals (THE WINDOWS).
template <class Calls>
[[gnu::always_inline]] inline void allocate_window(Calls const calls, Blocks& blocks,
                                                   core::ElementCount count) noexcept {
    for (std::size_t position = 0; position < count.raw(); ++position) {
        blocks.words.with_element(batch_at(position),
                                  [calls](alloc::AddressWord& word) noexcept { word = calls.allocate(); });
    }
}

// Releases the blocks of one window of COUNT operations, in the order of
// their allocation, with the calls and the blocks in locals.
template <class Calls>
[[gnu::always_inline]] inline void release_window(Calls const calls, Blocks const& blocks,
                                                  core::ElementCount count) noexcept {
    for (std::size_t position = 0; position < count.raw(); ++position) {
        calls.release(blocks.words.at(batch_at(position)));
    }
}

// The body of the row allocate: one window of kBatch allocations into the
// blocks.  Before each window it releases the blocks of the window before.
template <class Calls>
struct AllocateLoop {
    Calls calls;
    Blocks& blocks;
    std::size_t count = 0;

    void before_sample() noexcept {
        release_window(calls, blocks, core::ElementCount{count});
        count = 0;
    }

    [[gnu::always_inline]] void operator()() noexcept {
        allocate_window(calls, blocks, core::ElementCount{kBatch});
        count = kBatch;
    }

    void release_all() noexcept { before_sample(); }
};

// The body of the row release: one window of kBatch releases of the blocks,
// in the order of their allocation.  Before each window it takes the blocks.
template <class Calls>
struct ReleaseLoop {
    Calls calls;
    Blocks& blocks;

    void before_sample() noexcept { allocate_window(calls, blocks, core::ElementCount{kBatch}); }

    [[gnu::always_inline]] void operator()() noexcept { release_window(calls, blocks, core::ElementCount{kBatch}); }
};

// The body of the row warm allocate: kWarmBatch allocations from a bin that
// holds at least kWarmBatch blocks, so no refill runs.  Before each window it
// releases the blocks of the window before into the same bin, so no flush
// runs.
template <class Calls>
struct WarmAllocateLoop {
    Calls calls;
    Blocks& blocks;
    std::size_t count = 0;

    void before_sample() noexcept {
        release_window(calls, blocks, core::ElementCount{count});
        count = 0;
    }

    [[gnu::always_inline]] void operator()() noexcept {
        allocate_window(calls, blocks, core::ElementCount{kWarmBatch});
        count = kWarmBatch;
    }
};

// The body of the row warm release: kWarmBatch releases into a bin with room
// for them, so no flush runs.  Before each window it takes kWarmBatch blocks
// from the same bin, so no refill runs.
template <class Calls>
struct WarmReleaseLoop {
    Calls calls;
    Blocks& blocks;

    void before_sample() noexcept { allocate_window(calls, blocks, core::ElementCount{kWarmBatch}); }

    [[gnu::always_inline]] void operator()() noexcept { release_window(calls, blocks, core::ElementCount{kWarmBatch}); }
};

// Leaves at least kWarmBatch and at most twice kWarmBatch blocks in the bin of
// the class: kWarmBatch allocations, which refill an emptier bin, and their
// releases.
template <class Calls>
void warm_the_bin(Calls calls, Blocks& blocks) noexcept {
    allocate_window(calls, blocks, core::ElementCount{kWarmBatch});
    release_window(calls, blocks, core::ElementCount{kWarmBatch});
}

// The body of the row pair, scope: one window of kBatch pairs through the
// typed testing door of foundation/heap/HeapScope.h, each a LocalBlock of the
// class 1, in the body of a scope that the window opens (THE WINDOWS).  The
// sum of the body names no brand.
struct ScopePairLoop {
    std::uint32_t sum = 0;

    [[gnu::always_inline]] void operator()() noexcept {
        sum +=
            ::foundation::heap::mint_heap_scope(::foundation::effects::testing::test().alloc, [](auto& scope) noexcept {
                std::uint32_t total = 0;
                for (std::size_t turn = 0; turn < kBatch; ++turn) {
                    auto block = scope.template allocate<kClass>();
                    total += static_cast<std::uint32_t>(static_cast<std::uint64_t>(block.address()));
                    scope.release(std::move(block));
                }
                return total;
            });
    }
};

// The floor of a word that stays in memory: a load, an add and a store of one
// word in each step, so each load takes the data of the store before it.  The
// guide gives no latency of such a load (THE SPEED OF LIGHT).
struct MemoryWordLoop {
    std::uint64_t word = 0;

    [[gnu::always_inline]] void operator()() noexcept {
        bench::keep_in_memory(word);
        word += 8;
    }
};

// The floor of the pair: one window of kBatch sums of the cursor, with no
// allocation and no chain through memory.  The sink takes the cursor of each
// step in a register, so the loop does not fold the sums into one product,
// and it keeps the cursor and the sum in registers.
struct FloorLoop {
    std::uint32_t cursor = 0;
    std::uint32_t sum = 0;

    [[gnu::always_inline]] void operator()() noexcept {
        std::uint32_t step = cursor;
        std::uint32_t total = sum;
        for (std::size_t turn = 0; turn < kBatch; ++turn) {
            bench::do_not_optimize(step);
            total += step;
            ++step;
        }
        cursor = step;
        sum = total;
    }
};

// kClockAdds dependent adds: one cycle each on the core.
struct ClockChain {
    std::uint64_t value = 0;

    [[gnu::always_inline]] void operator()() noexcept {
        std::uint64_t local = value;
        add_chain(local);
        value = local;
    }
};

// A time in nanoseconds, as the nearest integer of picoseconds.
[[nodiscard]] std::uint64_t picoseconds(double nanoseconds) noexcept {
    return static_cast<std::uint64_t>(nanoseconds * 1000.0 + 0.5);
}

// The Report of one operation, from the Report of windows of OPERATIONS
// operations each (THE WINDOWS): each time over the count.  The cv does not
// change.
[[nodiscard]] bench::Report per_operation(bench::Report window, core::ElementCount operations) noexcept {
    double const count = static_cast<double>(operations.raw());
    bench::Percentiles& pct = window.pct;
    pct.p50 /= count;
    pct.p75 /= count;
    pct.p90 /= count;
    pct.p95 /= count;
    pct.p99 /= count;
    pct.p99_9 /= count;
    pct.p99_99 /= count;
    pct.min /= count;
    pct.max /= count;
    pct.mean /= count;
    pct.stddev /= count;
    window.cycles_per_op /= count;
    return window;
}

void print_row(fixy::TextView name, bench::Report const& measured) noexcept {
    bench::print("  {}  p50={} ps  p99={} ps  max={} ps  cv={} permille\n", name, picoseconds(measured.pct.p50),
                 picoseconds(measured.pct.p99), picoseconds(measured.pct.max),
                 static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5));
}

void print_ratio(fixy::TextView name, bench::Report const& base, bench::Report const& reference) noexcept {
    if (reference.pct.p50 <= 0.0) return;
    auto const centi = static_cast<std::uint64_t>(base.pct.p50 / reference.pct.p50 * 100.0 + 0.5);
    bench::print("  {}: {}.{}{}\n", name, centi / 100, centi % 100 / 10, centi % 10);
}

void print_light(fixy::TextView name, bench::Report const& measured, double cycles_per_ns, double floor) noexcept {
    auto const centi = [](double value) noexcept { return static_cast<std::uint64_t>(value * 100.0 + 0.5); };
    std::uint64_t const p50 = centi(measured.pct.p50 * cycles_per_ns);
    std::uint64_t const p99 = centi(measured.pct.p99 * cycles_per_ns);
    std::uint64_t const ratio = floor > 0.0 ? centi(measured.pct.p50 * cycles_per_ns / floor) : 0;
    bench::print("  {}  p50={}.{}{} cycles  p99={}.{}{} cycles  floor={}.{}{}  ratio={}.{}{}\n", name, p50 / 100,
                 p50 % 100 / 10, p50 % 10, p99 / 100, p99 % 100 / 10, p99 % 10, centi(floor) / 100,
                 centi(floor) % 100 / 10, centi(floor) % 10, ratio / 100, ratio % 100 / 10, ratio % 10);
}

// The five rows of one allocator.
struct RowSet {
    bench::Report pair{};
    bench::Report allocate{};
    bench::Report release{};
    bench::Report warm_allocate{};
    bench::Report warm_release{};
};

template <class Calls>
[[nodiscard]] RowSet run_rows(fixy::TextView label, Calls calls, Blocks& blocks) {
    RowSet rows{};
    core::ElementCount const window{kBatch};
    core::ElementCount const warm_window{kWarmBatch};
    PairLoop<Calls> pair{calls};
    rows.pair = per_operation(bench::Run(label).batch(1).samples(kSamples).measure(pair), window);
    bench::do_not_optimize(pair.sum);
    AllocateLoop<Calls> allocate{calls, blocks};
    rows.allocate = per_operation(bench::Run(label).batch(1).samples(kSamples).warmup(0).measure(allocate), window);
    allocate.release_all();
    ReleaseLoop<Calls> release{calls, blocks};
    rows.release = per_operation(bench::Run(label).batch(1).samples(kSamples).warmup(0).measure(release), window);
    warm_the_bin(calls, blocks);
    WarmAllocateLoop<Calls> warm_allocate{calls, blocks};
    rows.warm_allocate =
        per_operation(bench::Run(label).batch(1).samples(kSamples).warmup(0).measure(warm_allocate), warm_window);
    warm_allocate.before_sample();
    warm_the_bin(calls, blocks);
    WarmReleaseLoop<Calls> warm_release{calls, blocks};
    rows.warm_release =
        per_operation(bench::Run(label).batch(1).samples(kSamples).warmup(0).measure(warm_release), warm_window);
    return rows;
}

// The rows of one allocator, with its name at the start of each line.
void print_rows(fixy::TextView name, RowSet const& rows) noexcept {
    bench::print("  {}:\n", name);
    print_row("pair                   ", rows.pair);
    print_row("allocate, with refills ", rows.allocate);
    print_row("release, with flushes  ", rows.release);
    print_row("warm allocate          ", rows.warm_allocate);
    print_row("warm release           ", rows.warm_release);
}

// The ratios of the base over one reference, row by row.
void print_ratios(fixy::TextView name, RowSet const& base, RowSet const& reference) noexcept {
    bench::print("  over {}:\n", name);
    print_ratio("pair                   ", base.pair, reference.pair);
    print_ratio("allocate, with refills ", base.allocate, reference.allocate);
    print_ratio("release, with flushes  ", base.release, reference.release);
    print_ratio("warm allocate          ", base.warm_allocate, reference.warm_allocate);
    print_ratio("warm release           ", base.warm_release, reference.warm_release);
}

// The cycles of one nanosecond of the core in this run, from the clock chain.
[[nodiscard]] double measure_cycles_per_ns() {
    ClockChain chain{};
    bench::Report const report = bench::Run("clock").batch(kClockBatch).samples(kSamples).measure(chain);
    bench::do_not_optimize(chain.value);
    return report.pct.p50 > 0.0 ? kClockAdds / report.pct.p50 : 0.0;
}

// The resident bytes of the process: the second field of /proc/self/statm, a
// count of pages of 4 KiB, or zero when the file does not read.
[[nodiscard]] std::uint64_t resident_bytes() noexcept {
    core::FixedText<128> text{};
    if (os::read_file(core::CText{"/proc/self/statm"}, text).is_err()) return 0;
    core::TextView const all = text.view();
    std::size_t const space = core::find(all, ' ').value_or(all.size());
    if (space + 1 >= all.size()) return 0;
    core::TextView const pages = all.window(space + 1, all.size() - space - 1).value_or(core::TextView{});
    return bench::detail::leading_number(pages).value_or(0) * 4096;
}

// The TSC ticks of the first allocation of a new thread: the claim of its
// heap, the take of a chunk and the refill of half a bin.  The thread keeps
// its block, so its heap stays in use.
[[nodiscard]] std::uint64_t claim_ticks() noexcept {
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    auto first_allocation = [] noexcept {
        std::uint64_t const start = bench::rdtsc_start();
        alloc::AddressWord const block = alloc::current_heap().allocate(kClass);
        std::uint64_t const end = bench::rdtsc_end();
        bench::do_not_optimize(block);
        return end - start;
    };
    auto const values = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, first_allocation)
                            .expect("the host starts the thread of the bench");
    return values.at(::fixy::index<0>());
}

// The measures of the owner (THE MEASURES OF THE OWNER).
void run_owner_measures() {
    static_cast<void>(claim_ticks());
    std::uint64_t const before = resident_bytes();
    std::uint32_t const claimed_before = alloc::alloc_root_.claimed_heaps();
    ::fixy::HeapArray<double> samples =
        ::fixy::mint_heap_array(::foundation::effects::testing::test().alloc, kClaimThreads, 0.0);
    for (std::size_t thread = 0; thread < kClaimThreads; ++thread) {
        *samples.get(thread).expect("a thread of the measure lies below kClaimThreads") =
            static_cast<double>(claim_ticks()) * bench::Timer::ns_per_cycle();
    }
    std::uint64_t const after = resident_bytes();
    std::uint32_t const claimed = alloc::alloc_root_.claimed_heaps() - claimed_before;
    bench::Percentiles const claim = bench::Percentiles::compute(samples);
    bench::print("=== the measures of the owner: {} threads, one after the other, each claims a heap and takes one "
                 "block of 16 bytes\n",
                 kClaimThreads);
    bench::print("  claim: the first allocation of a thread  p50={} ns  p99={} ns  max={} ns  min={} ns\n",
                 static_cast<std::uint64_t>(claim.p50 + 0.5), static_cast<std::uint64_t>(claim.p99 + 0.5),
                 static_cast<std::uint64_t>(claim.max + 0.5), static_cast<std::uint64_t>(claim.min + 0.5));
    std::uint64_t const grown = after > before ? after - before : 0;
    bench::print("  resident: {} bytes over {} heaps, {} bytes for each heap after its first use (transparent huge "
                 "pages: madvise only, so base pages)\n",
                 grown, claimed, claimed != 0 ? grown / claimed : 0);
}

int run_bench() {
    bench::elevate_priority();
    core::Box<Blocks> blocks = core::mint_box<Blocks>(::foundation::effects::testing::test().alloc);
    alloc::HeapState& heap = alloc::claim_thread_heap();
    bench::print("=== alloc_fast_paths: blocks of 16 bytes, the mean of one operation in a window of {} operations\n",
                 kBatch);
    RowSet const hearth = run_rows("hearth", HearthCalls{heap}, *blocks);
    print_rows("hearth", hearth);
    core::ElementCount const window{kBatch};
    PairLoop<HearthCallCalls> call_pair{HearthCallCalls{heap}};
    bench::Report const hearth_call =
        per_operation(bench::Run("hearth, call").batch(1).samples(kSamples).measure(call_pair), window);
    bench::do_not_optimize(call_pair.sum);
    print_row("hearth pair, call      ", hearth_call);
    ScopePairLoop scope_pair{};
    bench::Report const hearth_scope =
        per_operation(bench::Run("hearth, scope").batch(1).samples(kSamples).measure(scope_pair), window);
    bench::do_not_optimize(scope_pair.sum);
    print_row("hearth pair, scope     ", hearth_scope);
#if defined(CRUCIBLE_BENCH_MIMALLOC)
    RowSet const mimalloc = run_rows("mimalloc", MimallocCalls{}, *blocks);
    print_rows("mimalloc 3.7.0", mimalloc);
#else
    bench::print("  mimalloc: the pin is not installed, so the bench has no row of it\n");
#endif
#if defined(CRUCIBLE_BENCH_JEMALLOC)
    RowSet const jemalloc = run_rows("jemalloc", JemallocCalls{}, *blocks);
    print_rows("jemalloc 5.4.0", jemalloc);
#else
    bench::print("  jemalloc: the pin is not installed, so the bench has no row of it\n");
#endif
    bench::print("=== the p50 of the base over the p50 of each reference: below 1.00 the base is faster\n");
#if defined(CRUCIBLE_BENCH_MIMALLOC)
    print_ratios("mimalloc", hearth, mimalloc);
    print_ratio("pair, call             ", hearth_call, mimalloc.pair);
#endif
#if defined(CRUCIBLE_BENCH_JEMALLOC)
    print_ratios("jemalloc", hearth, jemalloc);
    print_ratio("pair, call             ", hearth_call, jemalloc.pair);
#endif
    double const cycles_per_ns = measure_cycles_per_ns();
    bench::print("=== the speed of light: {}.{}{} cycles of the core in one ns (the TSC counts 2.2466 GHz)\n",
                 static_cast<std::uint64_t>(cycles_per_ns), static_cast<std::uint64_t>(cycles_per_ns * 10.0) % 10,
                 static_cast<std::uint64_t>(cycles_per_ns * 100.0) % 10);
    FloorLoop floor{};
    bench::Report const floor_row =
        per_operation(bench::Run("floor").batch(1).samples(kSamples).measure(floor), window);
    bench::do_not_optimize(floor.sum);
    MemoryWordLoop memory_word{};
    bench::Report const memory_row = bench::Run("memory word").batch(kBatch).samples(kSamples).measure(memory_word);
    bench::do_not_optimize(memory_word.word);
    print_light("floor row              ", floor_row, cycles_per_ns, kFloorRowCycles);
    print_light("memory word, measured  ", memory_row, cycles_per_ns, kFloorRowCycles);
    print_light("hearth pair            ", hearth.pair, cycles_per_ns, floor_cycles(kPairShape));
    print_light("hearth pair, scope     ", hearth_scope, cycles_per_ns, floor_cycles(kScopePairShape));
    print_light("hearth warm allocate   ", hearth.warm_allocate, cycles_per_ns, floor_cycles(kWarmAllocateShape));
    print_light("hearth warm release    ", hearth.warm_release, cycles_per_ns, floor_cycles(kWarmReleaseShape));
    print_light("hearth pair, call      ", hearth_call, cycles_per_ns, kCallPairFloorCycles);
    // The refill and the flush of each block: the stream row less its warm
    // row, in cycles of the core.
    auto const centi_cycles = [cycles_per_ns](double nanoseconds) noexcept {
        return static_cast<std::uint64_t>((nanoseconds > 0.0 ? nanoseconds : 0.0) * cycles_per_ns * 100.0 + 0.5);
    };
    std::uint64_t const refill = centi_cycles(hearth.allocate.pct.p50 - hearth.warm_allocate.pct.p50);
    std::uint64_t const flush = centi_cycles(hearth.release.pct.p50 - hearth.warm_release.pct.p50);
    bench::print("  refill for each block: {}.{}{} cycles (allocate less warm allocate, a step of 0.16 ns)\n",
                 refill / 100, refill % 100 / 10, refill % 10);
    bench::print("  flush for each block:  {}.{}{} cycles (release less warm release, a step of 0.16 ns)\n",
                 flush / 100, flush % 100 / 10, flush % 10);
    run_owner_measures();
    return 0;
}

}  // namespace

int main() { return run_bench(); }
