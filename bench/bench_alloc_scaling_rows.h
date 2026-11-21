#pragma once

// The scaling rows of the allocation door of the base and of three pinned
// reference allocators, from one thread to many.
//
// THE UNITS
//   Each allocator compiles its rows in its own unit,
//   bench_alloc_scaling_ALLOCATOR.cpp, and the units compile in parallel.
//   The rows of one allocator are a large body of code under the sanitizers,
//   so no unit holds the rows of more than one allocator.
//   bench_alloc_scaling.cpp reads the arguments, runs each unit, and prints
//   the time ratios.
//
// THE ALLOCATORS
//   base       the allocation door of foundation/core/Heap.h through the
//              owners of the base (THE BASE ADAPTER).  The door calls the
//              allocator of the C library, glibc
//   jemalloc   je_malloc, je_mallocx and je_sdallocx of the pinned jemalloc
//   mimalloc   mi_malloc, mi_malloc_aligned, mi_free_size and
//              mi_free_size_aligned of the pinned mimalloc
//   snmalloc   sn_malloc, sn_aligned_alloc, sn_free_sized and
//              sn_free_aligned_sized of the pinned snmalloc
//   glibc      malloc, aligned_alloc and free of the C library of the host,
//              with no owner of the base around them.  The base calls the
//              same three, so the time of the base over the time of glibc
//              is the cost of the owners and of the door, and the time of
//              glibc over the time of a reference is the cost of glibc.
//              That holds only for a run of one allocator.  The base and
//              glibc share one heap of the C library, and the rows of the
//              base change its state, such as the threshold of mmap that
//              glibc raises after each release of a large block.  So a run
//              of the two gives each row of glibc a different heap
//   Each pinned reference is a static library whose symbols take a prefix
//   (bench/CMakeLists.txt), so no reference interposes the allocator of the
//   process.  A build with no pin of a reference has no rows of it, and the
//   bench prints which references it holds.  tcmalloc has no row: its build
//   fetches six repositories at no pinned hash, and its library has no symbol
//   prefix (utils/pins.txt).  Each release gives the bytes and the alignment
//   of its request, as each release of the base does.
//
// THE ADAPTER
//   An allocator takes its place in each row through the concept
//   AllocatorAdapter.  An adapter gives two kinds of block, and each kind
//   takes the bytes of the request and an alignment of BlockAlignment:
//     Block<Align>      a block whose bytes hold no value.  No row reads it
//                       or writes it
//     Written<Align>    a block that the owner writes with the stamp of its
//                       thread in each byte.  Its release reads the first
//                       and the last byte, and a byte with no stamp ends the
//                       process
//   An adapter of the concept ConstantAdapter also gives a third kind:
//     Constant<Size>    a block of a size that the type gives, at the
//                       natural alignment
//   Only the base gives a Constant.  A call of a reference takes its size in
//   a register, and a size that the type gives makes no other code there.
//   So the class rows of a reference take a Block of a size that the run
//   gives, and they share one loop of the harness.
//   Each request is a multiple of 16 bytes, so no allocator rounds a request
//   that another one does not round.
//
// THE BASE ADAPTER
//   A bench has no door to the detail namespace of the base, so the base
//   goes through its owners.  Block is the block of mint_aligned_bytes,
//   which writes no byte.  Written is a HeapArray of bytes from
//   mint_heap_array, which writes each byte.  Constant is a Hole, the
//   storage of a Box with no object in it.  So each row measures what a
//   caller of the base pays for the same work.
//
// THE TOUCH OF A ROW
//   The allocator rows take Block, so they measure the allocator alone.  The
//   rows of a producer and a consumer, cache-scratch and the spans take
//   Written: the owner writes each byte, the consumer reads the stamp, and
//   the cost of the write is the same for each allocator.
//
// THE ROWS
//   The eight workloads of mimalloc-bench are written again here for the
//   adapter.  No program of mimalloc-bench is copied.
//   larson         each thread replaces the block of a random slot of 1024
//                  slots, 16 to 1008 bytes.  Each 16th replaced block goes to
//                  the next thread, which releases it (THE HAND-OFF), as the
//                  objects of a server pass between its threads
//   xmalloc        pairs of threads: the even slot writes blocks of 16 to 128
//                  bytes and gives each to the odd slot, which releases it
//   cache-scratch  the main thread takes one block of 16 bytes for each
//                  thread, and each thread releases it.  Then each thread
//                  takes a block of 16 bytes, writes its first byte 64
//                  times and releases it.  An allocator that gives two
//                  threads one cache line shows the passive false sharing in
//                  the time.  Each write is one store for each allocator: a
//                  memset of a size that the compiler does not know is a
//                  call, and it would measure the compiler
//   sh6bench       each op takes 128 blocks of 16 to 2048 bytes and releases
//                  them in the order of a stack, then takes 128 again and
//                  releases them in the order of a queue
//   alloc-test     each thread replaces the block of a random slot of 4096
//                  slots, with sizes of a Pareto distribution of exponent 1
//                  to 1 KiB: each doubling of the size holds half the blocks
//                  of the one below
//   rptest         as larson, with 512 slots of 16 bytes to 16 KiB in a
//                  log-uniform distribution, and each second replaced block
//                  goes to the next thread
//   mstress        rounds of threads that start, replace 50000 blocks of 16
//                  to 1008 bytes in 1024 slots, give their 1024 live blocks
//                  to a thread of the next round, and end (THE ROUNDS)
//   mleak          rounds of threads that start, replace 50000 blocks in 1024
//                  slots, release each block and end.  The resident bytes
//                  after the rounds show the memory that ended threads keep
//   The rows of the base:
//   class          one thread takes and releases a block of each of the 64
//                  size classes of the base (foundation/alloc/Layout.h): 16
//                  to 128 bytes in steps of 16, then four classes for each
//                  doubling to 2 MiB.  The block is a Constant of the base, and a Block
//                  of a reference
//   pair           one producer and one consumer with blocks of 64 bytes,
//                  the consumer in the L3 group of the producer, in another
//                  L3 group, and on another socket.  A spread with no CPU of
//                  a place gives no row of that place
//   box channel    the base alone: a Box of 64 bytes across a
//                  PermissionedBoxChannel of the wire form, in the same two
//                  first places
//   span           one thread takes a Written of 2, 8, 64 and 256 MiB at the
//                  alignment of a huge page, writes each byte, and releases it
//
// THE HAND-OFF
//   A row that moves blocks between threads moves them through a Mailbox: a
//   ring of cells of one producer thread and one consumer thread.  A cell
//   holds an owner of a block and an atomic flag.  The producer writes the
//   owner and then sets the flag with release order, and the consumer reads
//   the flag with acquire order and then takes the owner.  The rings of
//   fixy/concurrent/ hold only trivially copyable values, and the Box
//   channel holds only a Box, so the base has no channel of an owner of
//   bytes.  Each allocator goes through the same Mailbox.
//
// THE THREADS OF A ROW
//   A row of N threads runs on the CPUs of the slots 0 to N - 1 of the
//   spread of the run (THE SPREAD OF A RUN of bench/bench_harness.h), with
//   the CPU of the consumer of a pair row as the place of the row gives it.
//   Slot 0 is the main thread.  Each other slot is a worker on a std::jthread
//   that pins itself to the CPU of its slot (THE RAW WORKERS of
//   bench/bench_thread_scaling.cpp).
//
// THE PHASES OF A ROW
//   1. The start: each thread builds its own state and fills its live set.
//   2. The window: bench::kWindows windows of equal time, as in
//      bench/bench_thread_scaling.cpp.  The row gives the items of all
//      threads in one second, the median and the spread of the windows, and
//      the efficiency over the first row of the workload.
//   3. The latency: the workers go on, and the main thread runs the op under
//      Run::measure three times (THE RESOLUTION).  The first report gives
//      p50, p99 and the maximum of one op.  The second and the third report
//      time 16 and 64 ops in each sample, and give the quantiles of the
//      mean of one op in a sample.
//   4. The stop: each thread stops its ops and waits for each other thread.
//      Then no thread gives a block to another one, and each thread
//      releases each block that it holds or that its Mailbox holds.
//   THE ROUNDS and the rows of one thread have no window: Run::measure times
//   one round or one op.
//
// THE RESOLUTION
//   The harness reads the TSC before and after each sample.  The TSC of the
//   bench VM counts in steps of 22 or 23 ticks at 2.2467 GHz: a step of
//   10 ns.  So each time of one op is a whole count of 10 ns, and a p50 of
//   30 ns against 40 ns can be one step.  A step falls at a random place in
//   an op, so the mean of many samples has no such step.  The reports of 16
//   and of 64 ops in a sample have a step of 0.6 and 0.16 ns.  The p99 of
//   such a report is the p99 of a mean of 16 or 64 ops, not the p99 of one
//   op: a slow op adds only its part of the mean.  So read the tail of one
//   op in the first report, and a change of the p50 below 10 ns in the
//   second and the third.
//
// THE STEAL OF A ROW
//   The steal column of /proc/stat counts, for each CPU, the time in which
//   the hypervisor ran other work while the CPU had work, in ticks of 10 ms.
//   A row with threads reads the column of the CPU of each slot at its start
//   and after its stop.  A row with steal prints the steal and NOT VALID:
//   other work of the host took a part of its time.  A pause below 10 ms
//   reads as no steal, and the pause check of the harness measures it.
//
// THE CHECKS
//   Each thread counts its allocations, its releases and its live bytes.  At
//   the end of each row, the sums of the allocations and of the releases of
//   all threads must agree, and the live bytes must be zero, else the
//   process ends.  The release of each Written checks its stamp.
//
// THE MEMORY OF A ROW
//   The resident bytes of the process come from /proc/self/statm: at the
//   start of the row, at the end of the window with the live bytes of all
//   threads, and after the stop.  A row gives the growth over the start for
//   each live byte.  The harness keeps the samples of each latency report
//   for the JSON tail, in storage of the C library, so the reading after the
//   stop takes their bytes off.  Each allocator keeps the pages that it got,
//   so the clean reading of one allocator comes from a run of that allocator
//   alone:
//       python3 utils/bin/loop bench --command -- BINARY jemalloc
//
// THE ARGUMENTS
//   An argument base, jemalloc, mimalloc, snmalloc or glibc selects that
//   allocator, and with no such argument each allocator of the build runs.
//   An argument that names a row, such as larson or class, selects that row,
//   and with no such argument each row runs.  Each number is one more count of threads
//   after the rows of 1 and 2 threads.  With no count, the rows have 1, 2,
//   4, 8 and 16 threads, up to the count of the slots.
//
// THE COMMANDS
//   1 to 8 threads, one L3 group:   python3 utils/bin/loop bench bench_alloc_scaling
//   16 threads, two L3 groups:      python3 utils/bin/loop bench --cores 16 bench_alloc_scaling
//   One allocator for the memory:   python3 utils/bin/loop bench --cores 9 --command -- BINARY mimalloc
//   32 to 384 threads, on a host with no other work:
//                                   build-bench/bench/bench_alloc_scaling 32 64 128 192 384
//
// THE TIME RATIOS
//   A run of more than one allocator ends with the time of the base over the
//   time of each reference for each row: the time of one item in a row with
//   a window, and the p50 of one op in a row with no window.  A ratio above
//   1.00 is a row where the base is slower.

#include <crucible/perf/ProcText.h>
#include <crucible/topology/TextFields.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <foundation/AlignedBytes.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Philox.h>
#include <foundation/effects/Effect.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>

#include "bench_harness.h"

namespace bench_alloc {

namespace eff = ::foundation::effects;
namespace topology = ::crucible::warden::detail;

using ::fixy::ByteCount;

// The threads of one row, at most.  The rows of the bench host run 384.
constexpr std::size_t kMaxThreads = 512;
constexpr std::size_t kMaxRows = 16;
// The reports and the summaries of one run, at most: each row of each
// allocator at kMaxRows counts of threads, with kLatencyReports reports of
// each row with a window.
constexpr std::size_t kMaxReports = 4096;
constexpr std::uint64_t kWindowNs = 500000000;
// The samples of one latency report.  The harness keeps them for the JSON
// tail of the run, so the count bounds the memory of the reports.
constexpr std::size_t kLatencySamples = 200000;
constexpr std::size_t kLatencyWarmup = 1000;
constexpr std::size_t kLatencyMs = 500;
// The ops of one sample of each latency report of a row with a window (THE
// RESOLUTION).
constexpr std::size_t kLatencyReports = 3;
inline constexpr ::fixy::FixedArray<std::size_t, kLatencyReports> kLatencyBatches{1, 16, 64};
constexpr std::size_t kRoundSamples = 10000;
constexpr std::size_t kRoundMs = 1000;
// The items of a worker between two reads of the phase, and of the main
// thread between two reads of the clock.
constexpr std::uint64_t kItemsPerCheck = 64;
constexpr std::uint64_t kItemsPerClock = 1024;
// The cells of one Mailbox (THE HAND-OFF).
constexpr std::size_t kMailboxCells = 256;
// The draws of one thread, which its ops read in a cycle.
constexpr std::size_t kDraws = 4096;
// The pages of /proc/self/statm.  The platform of the tree has pages of
// 4 KiB on x86-64 and on aarch64 (foundation/AlignedBytes.h).
constexpr std::size_t kPageBytes = 4096;
// The bytes of one MiB, for the text of a row.
constexpr std::size_t kMebibyte = std::size_t{1} << 20;
// The writes of the first byte of a block in each op of cache-scratch.
constexpr std::uint32_t kRewrites = 64;

// The alignment of a request.  natural is the alignment that each of the
// four allocators gives to each block.
enum class BlockAlignment : std::size_t {
    natural = 16,
    huge_page = std::size_t{2} << 20,
};

[[nodiscard]] constexpr std::size_t alignment_bytes(BlockAlignment alignment) noexcept {
    return static_cast<std::size_t>(alignment);
}

// The byte that a thread writes into each byte of a Written block.
enum class Stamp : std::uint8_t {
};

[[nodiscard]] constexpr std::uint8_t byte_of(Stamp stamp) noexcept { return static_cast<std::uint8_t>(stamp); }

// The stamp of the thread of a slot: never zero, so a byte that the kernel
// gave as zero holds no stamp.
[[nodiscard]] constexpr Stamp stamp_of(bench::ThreadSlot slot) noexcept {
    return Stamp{static_cast<std::uint8_t>(slot.raw() % 251 + 1)};
}

// The allocators of a run (THE ALLOCATORS).
enum class AllocatorKind : std::uint8_t {
    base,
    jemalloc,
    mimalloc,
    snmalloc,
    glibc,
};

[[nodiscard]] constexpr ::fixy::TextView name_of(AllocatorKind kind) noexcept {
    switch (kind) {
        case AllocatorKind::base:
            return ::fixy::TextView{"base"};
        case AllocatorKind::jemalloc:
            return ::fixy::TextView{"jemalloc"};
        case AllocatorKind::mimalloc:
            return ::fixy::TextView{"mimalloc"};
        case AllocatorKind::snmalloc:
            return ::fixy::TextView{"snmalloc"};
        case AllocatorKind::glibc:
            return ::fixy::TextView{"glibc"};
        default:
            break;
    }
    ::fixy::unreachable();
}

constexpr std::size_t kAllocatorKinds = 5;

// The rows of a run (THE ROWS).
enum class RowKind : std::uint8_t {
    larson,
    xmalloc,
    cache_scratch,
    sh6bench,
    alloc_test,
    rptest,
    mstress,
    mleak,
    size_class,
    pair,
    box_channel,
    span,
};

constexpr std::size_t kRowKinds = 12;

[[nodiscard]] constexpr ::fixy::TextView name_of(RowKind kind) noexcept {
    switch (kind) {
        case RowKind::larson:
            return ::fixy::TextView{"larson"};
        case RowKind::xmalloc:
            return ::fixy::TextView{"xmalloc"};
        case RowKind::cache_scratch:
            return ::fixy::TextView{"cache-scratch"};
        case RowKind::sh6bench:
            return ::fixy::TextView{"sh6bench"};
        case RowKind::alloc_test:
            return ::fixy::TextView{"alloc-test"};
        case RowKind::rptest:
            return ::fixy::TextView{"rptest"};
        case RowKind::mstress:
            return ::fixy::TextView{"mstress"};
        case RowKind::mleak:
            return ::fixy::TextView{"mleak"};
        case RowKind::size_class:
            return ::fixy::TextView{"class"};
        case RowKind::pair:
            return ::fixy::TextView{"pair"};
        case RowKind::box_channel:
            return ::fixy::TextView{"box-channel"};
        case RowKind::span:
            return ::fixy::TextView{"span"};
        default:
            break;
    }
    ::fixy::unreachable();
}

// Where the consumer of a pair row runs, as against its producer.
enum class Placement : std::uint8_t {
    same_l3,
    other_l3,
    other_socket,
};

[[nodiscard]] constexpr ::fixy::TextView name_of(Placement placement) noexcept {
    switch (placement) {
        case Placement::same_l3:
            return ::fixy::TextView{"same L3"};
        case Placement::other_l3:
            return ::fixy::TextView{"other L3"};
        case Placement::other_socket:
            return ::fixy::TextView{"other socket"};
        default:
            break;
    }
    ::fixy::unreachable();
}

struct ThreadCountTag final {};
using ThreadCount = ::fixy::Id<ThreadCountTag, std::uint32_t>;

// The role of the thread of a slot.  The items of a row count the ops of
// its producers.
enum class Role : std::uint8_t {
    producer,
    consumer,
};

// The allocations, the releases and the live bytes of one thread.  A block
// that one thread takes and another one releases makes the live bytes of
// each thread wrap, and the sum of all threads is the live bytes of the row
// modulo 2^64.
//
// No two counters are adjacent: a word that no door writes lies between
// them.  With adjacent counters GCC joined the update of two counters into
// one 16-byte load and one 16-byte store.  Each door updates the live bytes,
// beside the allocations in a take and beside the releases in a give.  So the
// 8-byte store of the live bytes of a take could not forward to the 16-byte
// load of the give after it, and each op of each allocator waited for the
// store to reach the cache.  GCC joins no two stores with a gap between
// them, so each load and each store of a counter is one word, and the
// layout costs no instruction.  A volatile counter also holds one word, but
// GCC then keeps the dead stores of an op around it, and the workers of a
// row lost up to 22% of their items.  Each Tally holds its own cache line,
// so the tallies of two threads in one array share no line.
struct alignas(64) Tally {
    std::uint64_t allocations = 0;
    std::uint64_t gap_after_allocations = 0;
    std::uint64_t releases = 0;
    std::uint64_t gap_after_releases = 0;
    std::uint64_t live_bytes = 0;
};

// ── The adapter ────────────────────────────────────────────────────────

// The concept of an allocator of a row (THE ADAPTER).  Each member is
// static: an allocator of this bench has one state for the process.
template <class A>
concept AllocatorAdapter =
    requires(ByteCount bytes, Stamp stamp, typename A::template Block<BlockAlignment::natural> block,
             typename A::template Written<BlockAlignment::natural> written) {
        { A::kind } -> std::convertible_to<AllocatorKind>;
        {
            A::template allocate<BlockAlignment::natural>(bytes)
        } noexcept -> std::same_as<typename A::template Block<BlockAlignment::natural>>;
        { A::bytes_of(std::as_const(block)) } noexcept -> std::same_as<ByteCount>;
        { A::template release<BlockAlignment::natural>(std::move(block)) } noexcept -> std::same_as<void>;
        {
            A::template allocate_written<BlockAlignment::natural>(bytes, stamp)
        } noexcept -> std::same_as<typename A::template Written<BlockAlignment::natural>>;
        { A::bytes_of(std::as_const(written)) } noexcept -> std::same_as<ByteCount>;
        { A::rewrite(written, stamp) } noexcept -> std::same_as<void>;
        {
            A::template release_written<BlockAlignment::natural>(std::move(written), stamp)
        } noexcept -> std::same_as<void>;
    };

// The concept of an adapter with a Constant of each size (THE ADAPTER).
template <class A>
concept ConstantAdapter = AllocatorAdapter<A> && requires(typename A::template Constant<16> constant) {
    { A::template allocate_constant<16>() } noexcept -> std::same_as<typename A::template Constant<16>>;
    { A::template release_constant<16>(std::move(constant)) } noexcept -> std::same_as<void>;
};

template <class A, BlockAlignment Align>
using BlockOf = typename A::template Block<Align>;

template <class A, BlockAlignment Align>
using WrittenOf = typename A::template Written<Align>;

// The fatal exit of a Written block whose first or last byte lost the stamp
// of its owner.
[[noreturn]] CRUCIBLE_COLD inline void refuse_lost_stamp(AllocatorKind kind, ByteCount bytes) noexcept {
    ::fixy::fatal("a block of {} bytes of {} lost the stamp of its owner: two owners got one block, or a write went "
                  "past the end of a block",
                  bytes.raw(), name_of(kind));
}

// ── The base adapter ───────────────────────────────────────────────────

// The allocation capability of the bench.
[[nodiscard]] inline auto bench_alloc() noexcept { return eff::testing::test().alloc; }

// The base through its owners (THE BASE ADAPTER).
struct BaseAdapter {
    static constexpr AllocatorKind kind = AllocatorKind::base;

    template <BlockAlignment Align>
    using Block = decltype(::foundation::mint_aligned_bytes(bench_alloc(), ::fixy::Layout::of<std::uint8_t>()));

    template <BlockAlignment Align>
    using Written = ::fixy::HeapArray<std::uint8_t, alignment_bytes(Align)>;

    template <std::size_t Size>
    using Constant = ::fixy::Hole<Size, alignment_bytes(BlockAlignment::natural)>;

    template <BlockAlignment Align>
    [[nodiscard, gnu::always_inline]] static Block<Align> allocate(ByteCount bytes) noexcept {
        ::fixy::Layout const layout = ::fixy::mint_layout(bytes, ::fixy::Alignment{alignment_bytes(Align)})
                                          .expect("each request of a row is a layout");
        return ::foundation::mint_aligned_bytes(bench_alloc(), layout);
    }

    template <class Owner>
    [[nodiscard, gnu::always_inline]] static ByteCount bytes_of(Owner const& owner) noexcept {
        return ByteCount{owner.size()};
    }

    template <BlockAlignment Align>
    [[gnu::always_inline]] static void release(Block<Align>&& block) noexcept {
        Block<Align> const ended{std::move(block)};
    }

    template <BlockAlignment Align>
    [[nodiscard, gnu::always_inline]] static Written<Align> allocate_written(ByteCount bytes, Stamp stamp) noexcept {
        return ::fixy::mint_heap_array<std::uint8_t, alignment_bytes(Align)>(bench_alloc(), bytes.raw(),
                                                                             byte_of(stamp));
    }

    // Writes the first byte of the block kRewrites times, through one Ref
    // that the loop takes before its first write.  The barrier after each
    // write keeps each store in the code.  Only cache-scratch writes a block
    // again, and its blocks have the natural alignment.
    [[gnu::always_inline]] static void rewrite(Written<BlockAlignment::natural>& block, Stamp stamp) noexcept {
        auto first = block.view().get(0).expect("a Written block holds at least 16 bytes");
        for (std::uint32_t write = 0; write < kRewrites; ++write) {
            *first = byte_of(stamp);
            bench::clobber_memory();
        }
    }

    template <BlockAlignment Align>
    [[gnu::always_inline]] static void release_written(Written<Align>&& block, Stamp stamp) noexcept {
        Written<Align> const ended{std::move(block)};
        ::fixy::View<std::uint8_t const> const bytes = ended.view();
        std::uint8_t const first = *bytes.get(0).expect("a Written block holds at least 16 bytes");
        std::uint8_t const last = *bytes.get(bytes.size() - 1).expect("a Written block holds its last byte");
        if (first != byte_of(stamp)) [[unlikely]]
            refuse_lost_stamp(kind, ByteCount{bytes.size()});
        if (last != byte_of(stamp)) [[unlikely]]
            refuse_lost_stamp(kind, ByteCount{bytes.size()});
    }

    template <std::size_t Size>
    [[nodiscard, gnu::always_inline]] static Constant<Size> allocate_constant() noexcept {
        return ::fixy::mint_hole<Size, alignment_bytes(BlockAlignment::natural)>(bench_alloc());
    }

    template <std::size_t Size>
    [[gnu::always_inline]] static void release_constant(Constant<Size>&& hole) noexcept {
        std::move(hole).consume();
    }
};

}  // namespace bench_alloc

// ── The reference adapters ─────────────────────────────────────────────

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: a reference allocator gives a raw address, which no type of the base holds")

#if defined(CRUCIBLE_BENCH_JEMALLOC)
// The calls of the pinned jemalloc with the prefix je_ (the generated
// include/jemalloc/jemalloc.h of its build).  That header is in the build
// directory, under the source root, and it holds a pragma that changes the
// system flag of the code after it, which the quarantine plugin refuses in a
// file under the root.  So the bench declares the three calls itself.
extern "C" {
void* je_malloc(std::size_t size);
void* je_mallocx(std::size_t size, int flags);
void je_sdallocx(void* ptr, std::size_t size, int flags);
}
#endif
#if defined(CRUCIBLE_BENCH_MIMALLOC)
#include <mimalloc.h>
#endif

#if defined(CRUCIBLE_BENCH_SNMALLOC)
// The calls of the shim of snmalloc with the prefix sn_
// (src/snmalloc/override/malloc.cc of the pin).  The shim has no header.
extern "C" {
void* sn_malloc(std::size_t size);
void* sn_aligned_alloc(std::size_t alignment, std::size_t size);
void sn_free_sized(void* ptr, std::size_t size);
void sn_free_aligned_sized(void* ptr, std::size_t alignment, std::size_t size);
}
#endif

namespace bench_alloc {

// The block of a reference allocator: its first byte, its bytes and its
// alignment, the three words of the block of the base.  A move leaves the
// source with no block, and a block that ends with no release ends the
// process, so the checks of a row see each lost block.
class RawBlock {
public:
    RawBlock(void* first, ByteCount bytes, BlockAlignment alignment) noexcept
        : first_{static_cast<std::uint8_t*>(first)}, bytes_{bytes.raw()}, alignment_{alignment_bytes(alignment)} {}

    RawBlock(RawBlock&& other) noexcept
        : first_{std::exchange(other.first_, nullptr)}, bytes_{other.bytes_}, alignment_{other.alignment_} {}
    RawBlock& operator=(RawBlock&& other) noexcept {
        if (first_ != nullptr) [[unlikely]]
            ::fixy::fatal("a block of a reference allocator took a second block before its release");
        first_ = std::exchange(other.first_, nullptr);
        bytes_ = other.bytes_;
        alignment_ = other.alignment_;
        return *this;
    }
    RawBlock(RawBlock const&) = delete("a block has one owner: a copy would release it two times");
    RawBlock& operator=(RawBlock const&) = delete("a block has one owner: a copy would release it two times");

    ~RawBlock() {
        if (first_ != nullptr) [[unlikely]]
            ::fixy::fatal("a block of {} bytes of a reference allocator ended with no release", bytes_);
    }

    [[nodiscard]] ByteCount bytes() const noexcept { return ByteCount{bytes_}; }

    // The first byte, which the block gives up.
    [[nodiscard]] std::uint8_t* take() noexcept { return std::exchange(first_, nullptr); }

    void write_each_byte(Stamp stamp) const noexcept { __builtin_memset(first_, byte_of(stamp), bytes_); }

    // Writes the first byte kRewrites times, with the barrier of the base
    // adapter after each write.
    void write_first_byte(Stamp stamp) const noexcept {
        for (std::uint32_t write = 0; write < kRewrites; ++write) {
            first_[0] = byte_of(stamp);
            bench::clobber_memory();
        }
    }

    [[nodiscard]] bool holds_stamp(Stamp stamp) const noexcept {
        return first_[0] == byte_of(stamp) && first_[bytes_ - 1] == byte_of(stamp);
    }

private:
    std::uint8_t* first_ = nullptr;
    std::size_t bytes_ = 0;
    std::size_t alignment_ = 0;
};

[[noreturn]] CRUCIBLE_COLD inline void refuse_no_storage(AllocatorKind kind, ByteCount bytes) noexcept {
    ::fixy::fatal("{} gave no storage for {} bytes", name_of(kind), bytes.raw());
}

// A reference allocator through the calls of Calls: allocate<Align>(bytes)
// and release<Align>(first, bytes).
template <class Calls>
struct ReferenceAdapter {
    static constexpr AllocatorKind kind = Calls::kind;

    template <BlockAlignment Align>
    using Block = RawBlock;
    template <BlockAlignment Align>
    using Written = RawBlock;

    template <BlockAlignment Align>
    [[nodiscard, gnu::always_inline]] static RawBlock allocate(ByteCount bytes) noexcept {
        void* const first = Calls::template allocate<Align>(bytes.raw());
        if (first == nullptr) [[unlikely]]
            refuse_no_storage(kind, bytes);
        return RawBlock{first, bytes, Align};
    }

    [[nodiscard, gnu::always_inline]] static ByteCount bytes_of(RawBlock const& block) noexcept {
        return block.bytes();
    }

    template <BlockAlignment Align>
    [[gnu::always_inline]] static void release(RawBlock&& block) noexcept {
        std::size_t const bytes = block.bytes().raw();
        Calls::template release<Align>(block.take(), bytes);
    }

    template <BlockAlignment Align>
    [[nodiscard, gnu::always_inline]] static RawBlock allocate_written(ByteCount bytes, Stamp stamp) noexcept {
        RawBlock block = allocate<Align>(bytes);
        block.write_each_byte(stamp);
        return block;
    }

    [[gnu::always_inline]] static void rewrite(RawBlock& block, Stamp stamp) noexcept { block.write_first_byte(stamp); }

    template <BlockAlignment Align>
    [[gnu::always_inline]] static void release_written(RawBlock&& block, Stamp stamp) noexcept {
        if (!block.holds_stamp(stamp)) [[unlikely]]
            refuse_lost_stamp(kind, block.bytes());
        release<Align>(std::move(block));
    }
};

#if defined(CRUCIBLE_BENCH_JEMALLOC)
struct JemallocCalls {
    static constexpr AllocatorKind kind = AllocatorKind::jemalloc;

    // The flags of an alignment: MALLOCX_LG_ALIGN of the binary logarithm of
    // the alignment, the low bits of the word (jemalloc.h of the pin).
    template <BlockAlignment Align>
    static constexpr int kAlignFlags = ::fixy::countr_zero(alignment_bytes(Align));

    template <BlockAlignment Align>
    [[nodiscard, gnu::always_inline]] static void* allocate(std::size_t bytes) noexcept {
        if constexpr (Align == BlockAlignment::natural) {
            return je_malloc(bytes);
        } else {
            return je_mallocx(bytes, kAlignFlags<Align>);
        }
    }

    template <BlockAlignment Align>
    [[gnu::always_inline]] static void release(void* first, std::size_t bytes) noexcept {
        if constexpr (Align == BlockAlignment::natural) {
            je_sdallocx(first, bytes, 0);
        } else {
            je_sdallocx(first, bytes, kAlignFlags<Align>);
        }
    }
};
using JemallocAdapter = ReferenceAdapter<JemallocCalls>;
#endif

#if defined(CRUCIBLE_BENCH_MIMALLOC)
struct MimallocCalls {
    static constexpr AllocatorKind kind = AllocatorKind::mimalloc;

    template <BlockAlignment Align>
    [[nodiscard, gnu::always_inline]] static void* allocate(std::size_t bytes) noexcept {
        if constexpr (Align == BlockAlignment::natural) {
            return mi_malloc(bytes);
        } else {
            return mi_malloc_aligned(bytes, alignment_bytes(Align));
        }
    }

    template <BlockAlignment Align>
    [[gnu::always_inline]] static void release(void* first, std::size_t bytes) noexcept {
        if constexpr (Align == BlockAlignment::natural) {
            mi_free_size(first, bytes);
        } else {
            mi_free_size_aligned(first, bytes, alignment_bytes(Align));
        }
    }
};
using MimallocAdapter = ReferenceAdapter<MimallocCalls>;
#endif

#if defined(CRUCIBLE_BENCH_SNMALLOC)
struct SnmallocCalls {
    static constexpr AllocatorKind kind = AllocatorKind::snmalloc;

    template <BlockAlignment Align>
    [[nodiscard, gnu::always_inline]] static void* allocate(std::size_t bytes) noexcept {
        if constexpr (Align == BlockAlignment::natural) {
            return sn_malloc(bytes);
        } else {
            return sn_aligned_alloc(alignment_bytes(Align), bytes);
        }
    }

    template <BlockAlignment Align>
    [[gnu::always_inline]] static void release(void* first, std::size_t bytes) noexcept {
        if constexpr (Align == BlockAlignment::natural) {
            sn_free_sized(first, bytes);
        } else {
            sn_free_aligned_sized(first, alignment_bytes(Align), bytes);
        }
    }
};
using SnmallocAdapter = ReferenceAdapter<SnmallocCalls>;
#endif

// The C library of the host through the builtins of GCC, which compile to
// its malloc, aligned_alloc and free, as src/foundation/os/Heap.cpp does.
// free takes no size.  Each span of the rows is a whole count of its
// alignment, as aligned_alloc asks.
struct GlibcCalls {
    static constexpr AllocatorKind kind = AllocatorKind::glibc;

    // GCC removes a malloc of a builtin whose block the code frees and never
    // reads, as a class row does.  The empty asm takes the address as an
    // operand that it can change, so the compiler cannot pair the free with
    // the malloc, and the asm emits no instruction.
    template <BlockAlignment Align>
    [[nodiscard, gnu::always_inline]] static void* allocate(std::size_t bytes) noexcept {
        void* first = nullptr;
        if constexpr (Align == BlockAlignment::natural) {
            first = __builtin_malloc(bytes);
        } else {
            first = __builtin_aligned_alloc(alignment_bytes(Align), bytes);
        }
        asm volatile("" : "+r"(first));
        return first;
    }

    template <BlockAlignment Align>
    [[gnu::always_inline]] static void release(void* first, std::size_t /*bytes*/) noexcept {
        __builtin_free(first);
    }
};
using GlibcAdapter = ReferenceAdapter<GlibcCalls>;

}  // namespace bench_alloc

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace bench_alloc {

// ── The tallied calls ──────────────────────────────────────────────────

// Each call of an adapter in a row goes through these doors, which keep the
// Tally of the thread (THE CHECKS).
[[gnu::always_inline]] inline void count_take(Tally& tally, ByteCount bytes) noexcept {
    ++tally.allocations;
    tally.live_bytes += bytes.raw();
}

[[gnu::always_inline]] inline void count_give(Tally& tally, ByteCount bytes) noexcept {
    ++tally.releases;
    tally.live_bytes -= bytes.raw();
}

template <AllocatorAdapter A, BlockAlignment Align = BlockAlignment::natural>
[[nodiscard, gnu::always_inline]] inline BlockOf<A, Align> take_block(Tally& tally, ByteCount bytes) noexcept {
    count_take(tally, bytes);
    return A::template allocate<Align>(bytes);
}

template <AllocatorAdapter A, BlockAlignment Align = BlockAlignment::natural>
[[gnu::always_inline]] inline void give_block(Tally& tally, BlockOf<A, Align>&& block) noexcept {
    count_give(tally, A::bytes_of(block));
    A::template release<Align>(std::move(block));
}

template <AllocatorAdapter A, BlockAlignment Align = BlockAlignment::natural>
[[nodiscard, gnu::always_inline]] inline WrittenOf<A, Align> take_written(Tally& tally, ByteCount bytes,
                                                                          Stamp stamp) noexcept {
    count_take(tally, bytes);
    return A::template allocate_written<Align>(bytes, stamp);
}

template <AllocatorAdapter A, BlockAlignment Align = BlockAlignment::natural>
[[gnu::always_inline]] inline void give_written(Tally& tally, WrittenOf<A, Align>&& block, Stamp stamp) noexcept {
    count_give(tally, A::bytes_of(block));
    A::template release_written<Align>(std::move(block), stamp);
}

// ── The draws ──────────────────────────────────────────────────────────

// The draws of one thread: kDraws words of Philox4x32 with the key of the
// row and of the slot, which the ops of the thread read in a cycle.  The
// draws come before the window, so no op computes a random number.
class Draws {
public:
    Draws(RowKind kind, bench::ThreadSlot slot) noexcept {
        std::uint64_t const key = (std::uint64_t{static_cast<std::uint8_t>(kind)} << 32) | slot.raw();
        for (std::size_t block = 0; block < kDraws / 4; ++block) {
            ::foundation::core::Philox4x32::Counter const words =
                ::foundation::core::Philox4x32::generate(std::uint64_t{block}, key);
            for (::fixy::Index<4> const word : words.indices()) {
                std::uint32_t const draw = words.at(word);
                draws_.with_element(
                    ::fixy::Index<kDraws>::check(block * 4 + word.raw()).expect("a draw lies below kDraws"),
                    [draw](std::uint32_t& element) noexcept { element = draw; });
            }
        }
    }

    [[nodiscard, gnu::always_inline]] std::uint32_t next() noexcept {
        std::uint32_t const draw =
            draws_.at(::fixy::Index<kDraws>::check(cursor_ & (kDraws - 1)).expect("a masked cursor lies below kDraws"));
        ++cursor_;
        return draw;
    }

private:
    ::fixy::FixedArray<std::uint32_t, kDraws> draws_{};
    std::uint64_t cursor_ = 0;
};

// A size of Low to High bytes in steps of 16, from one draw.
template <std::size_t Low, std::size_t High>
    requires(Low % 16 == 0 && High % 16 == 0 && Low <= High && Low >= 16)
[[nodiscard, gnu::always_inline]] inline ByteCount uniform_bytes(std::uint32_t draw) noexcept {
    return ByteCount{Low + 16 * (draw % ((High - Low) / 16 + 1))};
}

// A size of the Pareto distribution of exponent 1 to 1 KiB (alloc-test):
// the count of trailing zeros of the draw picks a doubling from 16 bytes, so
// each doubling holds half the blocks of the one below.
[[nodiscard, gnu::always_inline]] inline ByteCount pareto_bytes(std::uint32_t draw) noexcept {
    std::uint32_t const doubling = static_cast<std::uint32_t>(::fixy::countr_zero(draw | (std::uint32_t{1} << 6)));
    if (doubling == 6) return ByteCount{1024};
    std::size_t const low = std::size_t{16} << doubling;
    return ByteCount{low + 16 * ((draw >> 8) % (std::uint32_t{1} << doubling))};
}

// A size of a log-uniform distribution of 16 bytes to 16 KiB (rptest).
[[nodiscard, gnu::always_inline]] inline ByteCount log_uniform_bytes(std::uint32_t draw) noexcept {
    std::uint32_t const doubling = draw % 10;
    std::size_t const low = std::size_t{16} << doubling;
    return ByteCount{low + 16 * ((draw >> 8) % (std::uint32_t{1} << doubling))};
}

// ── The hand-off ───────────────────────────────────────────────────────

}  // namespace bench_alloc

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the Mailbox moves an owner of a block from one thread to another")
namespace bench_alloc {

// The ring of one producer thread and one consumer thread that moves owners
// of blocks (THE HAND-OFF).  The producer owns head_ and the consumer owns
// tail_, each on its own cache line.
template <class Item>
class Mailbox {
public:
    // Puts the item into the next cell, or gives it back when that cell is
    // full.
    [[nodiscard, gnu::always_inline]] ::fixy::Option<Item> try_push(Item&& item) noexcept {
        Cell& cell = cell_at(head_);
        if (cell.is_full.load_acquire()) return ::fixy::Option<Item>::some(std::move(item));
        cell.item = ::fixy::Option<Item>::some(std::move(item));
        cell.is_full.store_release(true);
        ++head_;
        return ::fixy::none;
    }

    // The oldest item, or none when the ring is empty.
    [[nodiscard, gnu::always_inline]] ::fixy::Option<Item> try_pop() noexcept {
        Cell& cell = cell_at(tail_);
        if (!cell.is_full.load_acquire()) return ::fixy::none;
        Item item = std::move(cell.item).expect("a full cell of a Mailbox holds an item");
        cell.is_full.store_release(false);
        ++tail_;
        return ::fixy::Option<Item>::some(std::move(item));
    }

private:
    struct alignas(64) Cell {
        ::fixy::Atomic<bool> is_full{false};
        ::fixy::Option<Item> item{};
    };

    [[nodiscard, gnu::always_inline]] Cell& cell_at(std::uint64_t position) noexcept {
        return cells_.at(::fixy::Index<kMailboxCells>::check(position & (kMailboxCells - 1))
                             .expect("a masked position lies below the cells"));
    }

    ::fixy::FixedArray<Cell, kMailboxCells> cells_{};
    alignas(64) std::uint64_t head_ = 0;
    alignas(64) std::uint64_t tail_ = 0;
};

}  // namespace bench_alloc
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace bench_alloc {

// The Mailboxes of a row, one for each thread: the Mailbox of slot k is the
// inbox of slot k.
template <class Item>
using Mailboxes = ::fixy::HeapArray<Mailbox<Item>, 64>;

template <class Item>
[[nodiscard]] Mailboxes<Item> mint_mailboxes(ThreadCount threads) noexcept {
    return ::fixy::mint_heap_array_with<Mailbox<Item>, 64>(
        bench_alloc(), threads.raw(), [](std::size_t /*position*/) noexcept { return Mailbox<Item>{}; });
}

template <class Item>
[[nodiscard, gnu::always_inline]] inline Mailbox<Item>& mailbox_of(Mailboxes<Item>& boxes,
                                                                   bench::ThreadSlot slot) noexcept {
    return *boxes.view().get(slot.raw()).expect("a slot of a row has a Mailbox");
}

// The slot after slot in a ring of the threads of a row.
[[nodiscard, gnu::always_inline]] inline bench::ThreadSlot next_slot(bench::ThreadSlot slot,
                                                                     ThreadCount threads) noexcept {
    return bench::ThreadSlot{slot.raw() + 1 == threads.raw() ? 0 : slot.raw() + 1};
}

// ── The control of a row ───────────────────────────────────────────────

// The phase of a row (THE PHASES OF A ROW).  The main thread writes it.  The
// window has one phase for each of its windows.
enum class Phase : std::uint32_t {
    Wait,
    Window0,
    Window1,
    Window2,
    Window3,
    Window4,
    Latency,
    Stop,
};

using WindowIndex = ::fixy::Index<bench::kWindows>;

[[nodiscard]] inline ::fixy::Option<WindowIndex> window_of(Phase phase) noexcept {
    std::uint32_t const raw = static_cast<std::uint32_t>(phase);
    std::uint32_t const first = static_cast<std::uint32_t>(Phase::Window0);
    if (raw < first) return ::fixy::none;
    return WindowIndex::check(raw - first);
}

[[nodiscard]] inline Phase phase_of(WindowIndex window) noexcept {
    return static_cast<Phase>(static_cast<std::uint32_t>(Phase::Window0) + window.raw());
}

// The line that one thread writes: its items in each window, its live bytes
// at the end of the window, and its tally after the stop.
struct alignas(64) ThreadLine {
    ::fixy::FixedArray<::fixy::Atomic<std::uint64_t>, bench::kWindows> window_items{};
    ::fixy::Atomic<std::uint64_t> window_live_bytes{};
    ::fixy::Atomic<std::uint64_t> allocations{};
    ::fixy::Atomic<std::uint64_t> releases{};
    ::fixy::Atomic<std::uint64_t> live_bytes{};
    ::fixy::Atomic<bool> is_published{false};
};

struct alignas(64) PhaseLine {
    ::fixy::Atomic<Phase> phase{Phase::Wait};
};

struct alignas(64) CountLine {
    ::fixy::Atomic<std::uint64_t> value{};
};

// The atomics of one row.  The main thread owns it, and it outlives each
// worker of the row.
struct RowControl {
    PhaseLine phase{};
    // The threads that built their state, and the threads that stopped.
    CountLine ready{};
    CountLine stopped{};
    ::fixy::FixedArray<ThreadLine, kMaxThreads> lines{};
};

[[nodiscard]] inline Phase read_phase(RowControl const& control) noexcept { return control.phase.phase.load_acquire(); }

[[nodiscard]] inline ThreadLine const& line_of(RowControl const& control, bench::ThreadSlot slot) noexcept {
    return *control.lines.get(slot.raw()).expect("a slot of a row is below kMaxThreads");
}

// One push into a Mailbox that waits while the Mailbox is full, and that
// stops when the row stops.  The result holds the item that no push took.
template <class Item>
[[nodiscard, gnu::always_inline]] inline ::fixy::Option<Item>
push_waiting(RowControl const& control, Mailbox<Item>& mailbox, Item&& item) noexcept {
    ::fixy::Option<Item> rest = mailbox.try_push(std::move(item));
    while (rest.is_some()) {
        if (read_phase(control) == Phase::Stop) return rest;
        CRUCIBLE_SPIN_PAUSE;
        rest = mailbox.try_push(std::move(rest).expect("the test above found an item"));
    }
    return rest;
}

// ── The workloads with a window ────────────────────────────────────────
//
// Each workload W of a row with a window gives:
//   Shared                 the state that the threads of the row share
//   Local                  the state of one thread, on its own stack
//   mint_shared(threads)   the Shared of a row, which the main thread builds
//   start(shared, slot, threads)   the Local of a thread, with its live set
//   role_of(slot)          the role of a thread
//   op(shared, local, control)     one op
//   finish(shared, local)  the release of each block of the thread, after
//                          each thread of the row stopped
//   kItemsPerOp            the items of one op of a producer
//   kRow                   its RowKind

// The slots of a thread that replaces random blocks, with its draws.
template <class Block, std::size_t Slots>
struct SlotState {
    ::fixy::FixedArray<::fixy::Option<Block>, Slots> slots{};
    Draws draws;
    Tally tally{};
    bench::ThreadSlot slot{0};
    ThreadCount threads{1};
};

// The replace of a random slot: the release of its block, or its hand-off
// to the next thread each HandOff-th op, and a new block.  The inbox of the
// thread gives back at most four blocks of other threads in each op.  A
// HandOff of zero gives no hand-off.
template <AllocatorAdapter A, std::size_t Slots, std::uint32_t HandOff, class Bytes>
[[gnu::always_inline]] inline void replace_random_slot(Mailboxes<BlockOf<A, BlockAlignment::natural>>& inboxes,
                                                       SlotState<BlockOf<A, BlockAlignment::natural>, Slots>& state,
                                                       Bytes const& bytes_of_draw) noexcept {
    using Block = BlockOf<A, BlockAlignment::natural>;
    std::uint32_t const draw = state.draws.next();
    ::fixy::Option<Block>& slot =
        state.slots.at(::fixy::Index<Slots>::check(draw & (Slots - 1)).expect("a masked draw lies below the slots"));
    if (slot.is_some()) {
        Block block = std::move(slot).expect("the test above found a block");
        bool const is_handed =
            HandOff != 0 && state.threads.raw() > 1 && (draw >> 20) % (HandOff == 0 ? 1 : HandOff) == 0;
        if (is_handed) {
            Mailbox<Block>& next = mailbox_of(inboxes, next_slot(state.slot, state.threads));
            ::fixy::Option<Block> rest = next.try_push(std::move(block));
            if (rest.is_some()) give_block<A>(state.tally, std::move(rest).expect("the test above found a block"));
        } else {
            give_block<A>(state.tally, std::move(block));
        }
    }
    slot = ::fixy::Option<Block>::some(take_block<A>(state.tally, bytes_of_draw(draw >> 8)));
    if constexpr (HandOff != 0) {
        Mailbox<Block>& own = mailbox_of(inboxes, state.slot);
        for (std::uint32_t taken = 0; taken < 4; ++taken) {
            ::fixy::Option<Block> received = own.try_pop();
            if (received.is_none()) break;
            give_block<A>(state.tally, std::move(received).expect("the test above found a block"));
        }
    }
}

// The release of each block of the slots and of the inbox of a thread.
template <AllocatorAdapter A, std::size_t Slots>
void release_slots(Mailboxes<BlockOf<A, BlockAlignment::natural>>& inboxes,
                   SlotState<BlockOf<A, BlockAlignment::natural>, Slots>& state) noexcept {
    using Block = BlockOf<A, BlockAlignment::natural>;
    for (::fixy::Option<Block>& slot : state.slots) {
        if (slot.is_none()) continue;
        give_block<A>(state.tally, std::move(slot).expect("the test above found a block"));
    }
    Mailbox<Block>& own = mailbox_of(inboxes, state.slot);
    for (::fixy::Option<Block> received = own.try_pop(); received.is_some(); received = own.try_pop())
        give_block<A>(state.tally, std::move(received).expect("the loop test found a block"));
}

// larson and rptest: the replace of a random slot with a hand-off.
template <AllocatorAdapter A, RowKind Kind, std::size_t Slots, std::uint32_t HandOff>
struct ReplaceWorkload {
    using Block = BlockOf<A, BlockAlignment::natural>;
    static constexpr RowKind kRow = Kind;
    static constexpr std::uint64_t kItemsPerOp = 1;

    struct Shared {
        Mailboxes<Block> inboxes;
    };
    using Local = SlotState<Block, Slots>;

    [[nodiscard]] static Shared mint_shared(ThreadCount threads) noexcept {
        return Shared{.inboxes = mint_mailboxes<Block>(threads)};
    }

    [[nodiscard, gnu::always_inline]] static ByteCount bytes_of_draw(std::uint32_t draw) noexcept {
        if constexpr (Kind == RowKind::rptest) {
            return log_uniform_bytes(draw);
        } else {
            return uniform_bytes<16, 1008>(draw);
        }
    }

    [[nodiscard]] static Local start(Shared& shared, bench::ThreadSlot slot, ThreadCount threads) noexcept {
        Local local{.draws = Draws{Kind, slot}, .slot = slot, .threads = threads};
        for (std::size_t filled = 0; filled < Slots; ++filled)
            replace_random_slot<A, Slots, 0>(shared.inboxes, local, bytes_of_draw);
        return local;
    }

    [[nodiscard]] static Role role_of(bench::ThreadSlot /*slot*/) noexcept { return Role::producer; }

    [[gnu::always_inline]] static void op(Shared& shared, Local& local, RowControl const& /*control*/) noexcept {
        replace_random_slot<A, Slots, HandOff>(shared.inboxes, local, bytes_of_draw);
    }

    static void finish(Shared& shared, Local& local) noexcept { release_slots<A, Slots>(shared.inboxes, local); }
};

template <AllocatorAdapter A>
using Larson = ReplaceWorkload<A, RowKind::larson, 1024, 16>;
template <AllocatorAdapter A>
using Rptest = ReplaceWorkload<A, RowKind::rptest, 512, 2>;

// alloc-test: the replace of a random slot of 4096, with no hand-off.
template <AllocatorAdapter A>
struct AllocTest {
    using Block = BlockOf<A, BlockAlignment::natural>;
    static constexpr RowKind kRow = RowKind::alloc_test;
    static constexpr std::uint64_t kItemsPerOp = 1;
    static constexpr std::size_t kSlots = 4096;

    struct Shared {
        Mailboxes<Block> inboxes;
    };
    using Local = SlotState<Block, kSlots>;

    [[nodiscard]] static Shared mint_shared(ThreadCount threads) noexcept {
        return Shared{.inboxes = mint_mailboxes<Block>(threads)};
    }

    [[nodiscard]] static Local start(Shared& shared, bench::ThreadSlot slot, ThreadCount threads) noexcept {
        Local local{.draws = Draws{kRow, slot}, .slot = slot, .threads = threads};
        for (std::size_t filled = 0; filled < kSlots; ++filled)
            replace_random_slot<A, kSlots, 0>(shared.inboxes, local, pareto_bytes);
        return local;
    }

    [[nodiscard]] static Role role_of(bench::ThreadSlot /*slot*/) noexcept { return Role::producer; }

    [[gnu::always_inline]] static void op(Shared& shared, Local& local, RowControl const& /*control*/) noexcept {
        replace_random_slot<A, kSlots, 0>(shared.inboxes, local, pareto_bytes);
    }

    static void finish(Shared& shared, Local& local) noexcept { release_slots<A, kSlots>(shared.inboxes, local); }
};

// sh6bench: 128 blocks released in the order of a stack, then 128 blocks
// released in the order of a queue.
template <AllocatorAdapter A>
struct Sh6bench {
    using Block = BlockOf<A, BlockAlignment::natural>;
    static constexpr RowKind kRow = RowKind::sh6bench;
    static constexpr std::size_t kBlocks = 128;
    static constexpr std::uint64_t kItemsPerOp = 2 * kBlocks;

    struct Shared {};
    struct Local {
        ::fixy::FixedArray<::fixy::Option<Block>, kBlocks> blocks{};
        Tally tally{};
    };

    [[nodiscard]] static Shared mint_shared(ThreadCount /*threads*/) noexcept { return Shared{}; }

    [[nodiscard]] static Local start(Shared& /*shared*/, bench::ThreadSlot /*slot*/, ThreadCount /*threads*/) noexcept {
        return Local{};
    }

    [[nodiscard]] static Role role_of(bench::ThreadSlot /*slot*/) noexcept { return Role::producer; }

    // The sizes cycle over 16 to 2048 bytes: 16 times the position plus 16.
    [[gnu::always_inline]] static void fill(Local& local) noexcept {
        for (::fixy::Index<kBlocks> const position : local.blocks.indices()) {
            local.blocks.at(position) =
                ::fixy::Option<Block>::some(take_block<A>(local.tally, ByteCount{16 * position.raw() + 16}));
        }
    }

    [[gnu::always_inline]] static void op(Shared& /*shared*/, Local& local, RowControl const& /*control*/) noexcept {
        fill(local);
        for (std::size_t position = kBlocks; position > 0; --position) {
            ::fixy::Option<Block>& held = local.blocks.at(
                ::fixy::Index<kBlocks>::check(position - 1).expect("a position of the stack lies below kBlocks"));
            give_block<A>(local.tally, std::move(held).expect("the fill above gave each position a block"));
        }
        fill(local);
        for (::fixy::Option<Block>& held : local.blocks)
            give_block<A>(local.tally, std::move(held).expect("the fill above gave each position a block"));
    }

    static void finish(Shared& /*shared*/, Local& /*local*/) noexcept {}
};

// cache-scratch: one block of the main thread for each thread, then the
// rewrite of an own block (THE ROWS).
template <AllocatorAdapter A>
struct CacheScratch {
    using Written = WrittenOf<A, BlockAlignment::natural>;
    static constexpr RowKind kRow = RowKind::cache_scratch;
    static constexpr std::uint64_t kItemsPerOp = 1;
    static constexpr ByteCount kBytes{16};

    struct Shared {
        Mailboxes<Written> given;
        Tally main_tally{};
    };
    struct Local {
        Tally tally{};
        Stamp stamp{};
    };

    // The main thread takes the block of each thread one after the other, so
    // an allocator gives them adjacent addresses, and it puts each into the
    // inbox of its thread.
    [[nodiscard]] static Shared mint_shared(ThreadCount threads) noexcept {
        Shared shared{.given = mint_mailboxes<Written>(threads)};
        for (std::uint32_t slot = 0; slot < threads.raw(); ++slot) {
            Written block = take_written<A>(shared.main_tally, kBytes, stamp_of(bench::kMeasuringSlot));
            ::fixy::Option<Written> rest = mailbox_of(shared.given, bench::ThreadSlot{slot}).try_push(std::move(block));
            if (rest.is_some()) ::fixy::fatal("an empty Mailbox took no block");
        }
        return shared;
    }

    [[nodiscard]] static Local start(Shared& shared, bench::ThreadSlot slot, ThreadCount /*threads*/) noexcept {
        Local local{.stamp = stamp_of(slot)};
        ::fixy::Option<Written> given = mailbox_of(shared.given, slot).try_pop();
        give_written<A>(local.tally, std::move(given).expect("the main thread gave each thread a block"),
                        stamp_of(bench::kMeasuringSlot));
        return local;
    }

    [[nodiscard]] static Role role_of(bench::ThreadSlot /*slot*/) noexcept { return Role::producer; }

    [[gnu::always_inline]] static void op(Shared& /*shared*/, Local& local, RowControl const& /*control*/) noexcept {
        Written block = take_written<A>(local.tally, kBytes, local.stamp);
        A::rewrite(block, local.stamp);
        give_written<A>(local.tally, std::move(block), local.stamp);
    }

    static void finish(Shared& /*shared*/, Local& /*local*/) noexcept {}
};

// xmalloc and pair: the even slot writes blocks and gives each to the odd
// slot after it, which releases it.
template <AllocatorAdapter A, RowKind Kind>
struct ProducerConsumer {
    using Written = WrittenOf<A, BlockAlignment::natural>;
    static constexpr RowKind kRow = Kind;
    static constexpr std::uint64_t kItemsPerOp = 1;

    struct Shared {
        Mailboxes<Written> inboxes;
    };
    struct Local {
        Draws draws;
        Tally tally{};
        bench::ThreadSlot slot{0};
    };

    [[nodiscard]] static Shared mint_shared(ThreadCount threads) noexcept {
        return Shared{.inboxes = mint_mailboxes<Written>(threads)};
    }

    [[nodiscard]] static Local start(Shared& /*shared*/, bench::ThreadSlot slot, ThreadCount /*threads*/) noexcept {
        return Local{.draws = Draws{Kind, slot}, .slot = slot};
    }

    [[nodiscard]] static Role role_of(bench::ThreadSlot slot) noexcept {
        return slot.raw() % 2 == 0 ? Role::producer : Role::consumer;
    }

    [[nodiscard, gnu::always_inline]] static ByteCount bytes_of_draw(std::uint32_t draw) noexcept {
        if constexpr (Kind == RowKind::pair) {
            static_cast<void>(draw);
            return ByteCount{64};
        } else {
            return uniform_bytes<16, 128>(draw);
        }
    }

    [[gnu::always_inline]] static void op(Shared& shared, Local& local, RowControl const& control) noexcept {
        if (role_of(local.slot) == Role::producer) {
            bench::ThreadSlot const consumer{local.slot.raw() + 1};
            Written block = take_written<A>(local.tally, bytes_of_draw(local.draws.next()), stamp_of(local.slot));
            ::fixy::Option<Written> rest =
                push_waiting(control, mailbox_of(shared.inboxes, consumer), std::move(block));
            if (rest.is_some()) {
                give_written<A>(local.tally, std::move(rest).expect("the test above found a block"),
                                stamp_of(local.slot));
            }
            return;
        }
        ::fixy::Option<Written> received = mailbox_of(shared.inboxes, local.slot).try_pop();
        if (received.is_none()) {
            CRUCIBLE_SPIN_PAUSE;
            return;
        }
        give_written<A>(local.tally, std::move(received).expect("the test above found a block"),
                        stamp_of(bench::ThreadSlot{local.slot.raw() - 1}));
    }

    static void finish(Shared& shared, Local& local) noexcept {
        if (role_of(local.slot) == Role::producer) return;
        Mailbox<Written>& own = mailbox_of(shared.inboxes, local.slot);
        Stamp const stamp = stamp_of(bench::ThreadSlot{local.slot.raw() - 1});
        for (::fixy::Option<Written> received = own.try_pop(); received.is_some(); received = own.try_pop())
            give_written<A>(local.tally, std::move(received).expect("the loop test found a block"), stamp);
    }
};

template <AllocatorAdapter A>
using Xmalloc = ProducerConsumer<A, RowKind::xmalloc>;
template <AllocatorAdapter A>
using Pair = ProducerConsumer<A, RowKind::pair>;

// ── The memory of the process ──────────────────────────────────────────

// The resident bytes of the process: the second field of /proc/self/statm,
// a count of pages, or zero when the file does not read (THE MEMORY OF A
// ROW).
[[nodiscard]] inline ByteCount read_resident() noexcept {
    ::fixy::FixedText<128> text{};
    if (::foundation::os::read_file(::fixy::CText{"/proc/self/statm"}, text).is_err()) return ByteCount{0};
    ::fixy::TextView const all = text.view();
    ::fixy::TextView const rest = topology::text_suffix(all, ::fixy::find(all, ' ').value_or(all.size()) + 1);
    return ByteCount{bench::detail::leading_number(rest).value_or(0) * kPageBytes};
}

// The resident bytes of a row: at its start, at the end of its window with
// the live bytes of all threads, and after its stop.  The harness keeps the
// samples of the latency report of the row after the stop, in storage of
// the C library, so the reading after the stop holds them, and harness gives
// their bytes.
struct MemoryReading {
    ByteCount start{0};
    ByteCount steady{0};
    ByteCount live{0};
    ByteCount end{0};
    ByteCount harness{0};
};

// The bytes of the samples that a report keeps.
[[nodiscard]] inline ByteCount sample_bytes_of(bench::Report const& report) noexcept {
    return ByteCount{report.samples.size() * sizeof(double)};
}

// A count of bytes as MiB with one decimal, for the text of a row.
struct MebibyteText {
    std::uint64_t whole = 0;
    std::uint64_t tenth = 0;
    bool is_negative = false;
};

[[nodiscard]] inline MebibyteText mebibytes_of(::fixy::Side<bench::side::ReadingAfter, ByteCount> after,
                                               ::fixy::Side<bench::side::ReadingBefore, ByteCount> before) noexcept {
    bool const is_negative = after.value() < before.value();
    std::size_t const grown =
        is_negative ? before.value().raw() - after.value().raw() : after.value().raw() - before.value().raw();
    std::uint64_t const tenths = (grown * 10 + kMebibyte / 2) / kMebibyte;
    return MebibyteText{.whole = tenths / 10, .tenth = tenths % 10, .is_negative = is_negative};
}

// Prints the memory of a row: the growth of the resident bytes at the end of
// the window for its live bytes, and after the stop with no count of the
// samples that the harness keeps.
inline void print_memory(MemoryReading const& memory) noexcept {
    MebibyteText const steady = mebibytes_of(::fixy::side_as<bench::side::ReadingAfter>(memory.steady),
                                             ::fixy::side_as<bench::side::ReadingBefore>(memory.start));
    MebibyteText const live = mebibytes_of(::fixy::side_as<bench::side::ReadingAfter>(memory.live),
                                           ::fixy::side_as<bench::side::ReadingBefore>(ByteCount{0}));
    MebibyteText const end =
        mebibytes_of(::fixy::side_as<bench::side::ReadingAfter>(memory.end),
                     ::fixy::side_as<bench::side::ReadingBefore>(ByteCount{memory.start.raw() + memory.harness.raw()}));
    std::size_t const live_raw = memory.live.raw();
    std::size_t const growth = memory.steady.raw() > memory.start.raw() ? memory.steady.raw() - memory.start.raw() : 0;
    std::uint64_t const centi_ratio = live_raw == 0 ? 0 : (growth * 100 + live_raw / 2) / live_raw;
    bench::detail::flush_standard_output();
    ::fixy::report(::fixy::Sink::Out,
                   "    resident: {} MiB at the start, {}{}.{} MiB at the end of the window for {}.{} MiB live "
                   "({}.{}{} resident bytes for each live byte), {}{}.{} MiB after the stop with no count of the "
                   "samples of the harness\n",
                   memory.start.raw() / kMebibyte, steady.is_negative ? ::fixy::TextView{"-"} : ::fixy::TextView{"+"},
                   steady.whole, steady.tenth, live.whole, live.tenth, centi_ratio / 100, centi_ratio % 100 / 10,
                   centi_ratio % 10, end.is_negative ? ::fixy::TextView{"-"} : ::fixy::TextView{"+"}, end.whole,
                   end.tenth);
}

// ── The results ────────────────────────────────────────────────────────

// The time of one item of a row, for the time ratios of the end of a run
// (THE TIME RATIOS).
struct Summary {
    AllocatorKind allocator = AllocatorKind::base;
    RowKind row = RowKind::larson;
    ThreadCount threads{1};
    Placement placement = Placement::same_l3;
    std::uint64_t size = 0;
    double item_ns = 0.0;
};

struct RunRecord {
    ::fixy::FixedList<bench::Report, kMaxReports> reports{};
    ::fixy::FixedList<Summary, kMaxReports> summaries{};
};

inline void add_summary(RunRecord& record, Summary const& summary) noexcept {
    static_cast<void>(record.summaries.append(summary).expect("a run has at most kMaxReports rows"));
}

// The result of one row with a window.
struct RowResult {
    ThreadCount threads{1};
    Placement placement = Placement::same_l3;
    // The items of all threads in one second, in hundredths of a million.
    std::uint64_t centi_mops = 0;
    bench::WindowSpread windows{};
    std::size_t paused_windows = 0;
    bench::Pause window_pause{};
    // The steal of the CPUs of the row from its start to its stop (THE STEAL
    // OF A ROW).
    std::uint64_t steal_ms = 0;
    MemoryReading memory{};
    // One report for each batch of kLatencyBatches (THE RESOLUTION).
    ::fixy::FixedArray<bench::Report, kLatencyReports> latency{};
};

// The fatal exit of a row whose tallies do not agree (THE CHECKS).
[[noreturn]] CRUCIBLE_COLD inline void refuse_tally(RowKind row, AllocatorKind allocator, Tally const& total) noexcept {
    ::fixy::fatal("the {} row of {} took {} blocks and released {}, and {} bytes stay live: a block was lost or "
                  "released two times",
                  name_of(row), name_of(allocator), total.allocations, total.releases, total.live_bytes);
}

inline void check_tally(RowKind row, AllocatorKind allocator, Tally const& total) noexcept {
    if (total.allocations != total.releases) [[unlikely]]
        refuse_tally(row, allocator, total);
    if (total.live_bytes != 0) [[unlikely]]
        refuse_tally(row, allocator, total);
}

inline void add_tally(Tally& total, Tally const& part) noexcept {
    total.allocations += part.allocations;
    total.releases += part.releases;
    total.live_bytes += part.live_bytes;
}

// ── The places of the threads ──────────────────────────────────────────

// The first CPU of the spread outside the L3 group of the CPU of slot 0, or
// none.  Complexity: linear in the slots of the spread.
[[nodiscard]] inline ::fixy::Option<bench::CpuId> other_l3_cpu_of(bench::Spread const& spread) noexcept {
    bench::CpuId const main_cpu = spread.of_slot(bench::kMeasuringSlot).value_or(bench::CpuId::none());
    crucible::warden::CpuSet const group = bench::read_l3_group(main_cpu);
    if (group.is_empty()) return ::fixy::none;
    for (std::uint32_t slot = 1; slot < spread.count(); ++slot) {
        bench::CpuId const cpu = spread.of_slot(bench::ThreadSlot{slot}).value_or(bench::CpuId::none());
        if (cpu.is_valid() && !group.contains(cpu.raw())) return ::fixy::some(cpu);
    }
    return ::fixy::none;
}

// The package of a CPU from sysfs, or none.
[[nodiscard]] inline ::fixy::Option<std::uint64_t> package_of(bench::CpuId cpu) noexcept {
    if (!cpu.is_valid()) return ::fixy::none;
    ::foundation::os::PathText<96> path{};
    bool const is_built = path.append(::fixy::TextView{"/sys/devices/system/cpu/cpu"}).is_ok()
                       && path.append_decimal(static_cast<std::uint64_t>(cpu.raw())).is_ok()
                       && path.append(::fixy::TextView{"/topology/physical_package_id"}).is_ok();
    ::fixy::FixedText<32> text{};
    if (!is_built || ::foundation::os::read_file(path.c_text(), text).is_err()) return ::fixy::none;
    return bench::detail::leading_number(text.view());
}

// The first CPU of the spread on another package than the CPU of slot 0, or
// none.  Complexity: linear in the slots of the spread.
[[nodiscard]] inline ::fixy::Option<bench::CpuId> other_socket_cpu_of(bench::Spread const& spread) noexcept {
    ::fixy::Option<std::uint64_t> const main_package =
        package_of(spread.of_slot(bench::kMeasuringSlot).value_or(bench::CpuId::none()));
    if (main_package.is_none()) return ::fixy::none;
    std::uint64_t const main = ::fixy::Option<std::uint64_t>{main_package}.value_or(0);
    for (std::uint32_t slot = 1; slot < spread.count(); ++slot) {
        bench::CpuId const cpu = spread.of_slot(bench::ThreadSlot{slot}).value_or(bench::CpuId::none());
        ::fixy::Option<std::uint64_t> package = package_of(cpu);
        if (package.is_some() && std::move(package).value_or(main) != main) return ::fixy::some(cpu);
    }
    return ::fixy::none;
}

// The CPU of the consumer of a pair row at a place, or none.
[[nodiscard]] inline ::fixy::Option<bench::CpuId> consumer_cpu_of(bench::Spread const& spread,
                                                                  Placement placement) noexcept {
    switch (placement) {
        case Placement::same_l3: {
            if (spread.count() < 2) return ::fixy::none;
            bench::CpuId const main_cpu = spread.of_slot(bench::kMeasuringSlot).value_or(bench::CpuId::none());
            bench::CpuId const next = spread.of_slot(bench::ThreadSlot{1}).value_or(bench::CpuId::none());
            crucible::warden::CpuSet const group = bench::read_l3_group(main_cpu);
            if (!group.is_empty() && !group.contains(next.raw())) return ::fixy::none;
            return ::fixy::some(next);
        }
        case Placement::other_l3:
            return other_l3_cpu_of(spread);
        case Placement::other_socket:
            return other_socket_cpu_of(spread);
        default:
            break;
    }
    ::fixy::unreachable();
}

// The CPU of each slot of a row: slot k of the spread, and the CPU of the
// place for the consumer of a pair row.
struct RowPlaces {
    bench::Spread const& spread;
    ::fixy::Option<bench::CpuId> consumer{};

    [[nodiscard]] bench::CpuId cpu_of(bench::ThreadSlot slot) const noexcept {
        if (slot.raw() == 1 && consumer.is_some())
            return ::fixy::Option<bench::CpuId>{consumer}.value_or(bench::CpuId::none());
        return spread.of_slot(slot).expect("a row has at most the slots of the spread");
    }
};

// ── The steal of a row ─────────────────────────────────────────────────

// The bytes of /proc/stat that a reading of the steal takes.  The line of
// each CPU comes before the lines of the interrupts, and the lines of 512
// CPUs take less than 64 KiB.
constexpr std::size_t kStatBytes = 65536;
// The steal column counts in ticks of USER_HZ, which is 100 for each second.
constexpr std::uint64_t kMsPerStealTick = 10;
// The columns of the line of a CPU before its steal column: user, nice,
// system, idle, iowait, irq and softirq.
constexpr std::size_t kColumnsBeforeSteal = 7;

// The steal ticks of the CPU of each slot of a row, or none for a slot whose
// CPU has no line (THE STEAL OF A ROW).
using StealTicks = ::fixy::FixedArray<::fixy::Option<std::uint64_t>, kMaxThreads>;

// The CPU and the steal ticks of one line of /proc/stat.
struct CpuSteal {
    std::uint64_t cpu = 0;
    std::uint64_t ticks = 0;
};

// The CPU and the steal ticks of a line "cpuN user nice ...", or none for
// each other line, such as the line "cpu" of the sum of all CPUs.
[[nodiscard]] inline ::fixy::Option<CpuSteal> cpu_steal_of(::fixy::TextView line) noexcept {
    namespace text = ::crucible::topology::text;
    namespace proc = ::crucible::perf::proc_text;
    ::fixy::TextView const word = text::first_word(line);
    if (word.size() <= 3 || text::head(word, 3) != ::fixy::TextView{"cpu"}) return ::fixy::none;
    ::fixy::Option<std::uint64_t> const cpu = text::leading_u64(text::tail(word, 3));
    if (cpu.is_none()) return ::fixy::none;
    ::fixy::TextView rest = text::tail(line, ::fixy::find(line, ' ').value_or(line.size()));
    for (std::size_t column = 0; column < kColumnsBeforeSteal; ++column)
        static_cast<void>(proc::take_decimal(rest));
    ::fixy::Option<std::uint64_t> const ticks = proc::take_decimal(rest);
    if (ticks.is_none()) return ::fixy::none;
    return ::fixy::some(CpuSteal{.cpu = ::fixy::Option<std::uint64_t>{cpu}.value_or(0),
                                 .ticks = ::fixy::Option<std::uint64_t>{ticks}.value_or(0)});
}

// The steal ticks of the CPU of each slot of the first threads slots of a
// row, or none for each slot when /proc/stat does not read.  Complexity:
// O(the CPUs of the host * threads).
[[nodiscard]] inline StealTicks read_steal(RowPlaces const& places, ThreadCount threads) noexcept {
    namespace text = ::crucible::topology::text;
    StealTicks steal{};
    ::fixy::Box<::fixy::FixedText<kStatBytes>> stat = ::fixy::mint_box<::fixy::FixedText<kStatBytes>>(bench_alloc());
    if (::foundation::os::read_file(::fixy::CText{"/proc/stat"}, *stat).is_err()) return steal;
    ::fixy::TextView rest = stat->view();
    while (rest.size() > 0) {
        ::fixy::Option<CpuSteal> const line = cpu_steal_of(text::take_line(rest));
        if (line.is_none()) continue;
        CpuSteal const found = ::fixy::Option<CpuSteal>{line}.value_or(CpuSteal{});
        for (std::uint32_t slot = 0; slot < threads.raw(); ++slot) {
            bench::CpuId const cpu = places.cpu_of(bench::ThreadSlot{slot});
            if (cpu.is_valid() && static_cast<std::uint64_t>(cpu.raw()) == found.cpu)
                steal.with_element(
                    ::fixy::Index<kMaxThreads>::check(slot).expect("a slot of a row is below kMaxThreads"),
                    [&found](::fixy::Option<std::uint64_t>& element) noexcept { element = ::fixy::some(found.ticks); });
        }
    }
    return steal;
}

// The steal of all CPUs of a row in milliseconds, from its two readings.  A
// slot with no line in one reading adds no steal.
[[nodiscard]] inline std::uint64_t
steal_ms_between(::fixy::Side<bench::side::ReadingBefore, StealTicks> before,
                 ::fixy::Side<bench::side::ReadingAfter, StealTicks> after) noexcept {
    std::uint64_t ticks = 0;
    for (::fixy::Index<kMaxThreads> const slot : before.value().indices()) {
        ::fixy::Option<std::uint64_t> const first = before.value().at(slot);
        ::fixy::Option<std::uint64_t> const last = after.value().at(slot);
        if (first.is_none() || last.is_none()) continue;
        std::uint64_t const start = ::fixy::Option<std::uint64_t>{first}.value_or(0);
        std::uint64_t const end = ::fixy::Option<std::uint64_t>{last}.value_or(0);
        ticks += end > start ? end - start : 0;
    }
    return ticks * kMsPerStealTick;
}

// ── The engine of a row with a window ──────────────────────────────────

// What the window of the main thread measured: its items, its wall time and
// the time that it lost in each window, and the pause of the whole window.
struct Window {
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> items{};
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> wall_ns{};
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> lost_ns{};
    bench::Pause pause{};
};

// The ops of a worker between two reads of the phase, and of the main
// thread between two reads of the clock: at least one op.
template <class W>
inline constexpr std::uint64_t kWorkerOps = W::kItemsPerOp >= kItemsPerCheck ? 1 : kItemsPerCheck / W::kItemsPerOp;
template <class W>
inline constexpr std::uint64_t kClockOps = W::kItemsPerOp >= kItemsPerClock ? 1 : kItemsPerClock / W::kItemsPerOp;

// The stop of a thread (THE PHASES OF A ROW): it waits for each thread of
// the row to stop, releases each of its blocks, and publishes its tally.
template <class W>
void stop_thread(RowControl& control, typename W::Shared& shared, typename W::Local& local, ThreadCount threads,
                 bench::ThreadSlot slot) noexcept {
    static_cast<void>(control.stopped.value.fetch_add_acq_rel(1));
    while (control.stopped.value.load_acquire() < threads.raw())
        CRUCIBLE_SPIN_PAUSE;
    W::finish(shared, local);
    // Each thread writes only the line of its own slot, and each field of a line is an Atomic.
    control.lines.with_element(
        ::fixy::Index<kMaxThreads>::check(slot.raw()).expect("a slot of a row is below kMaxThreads"),
        [&local](ThreadLine& line) noexcept {
            line.allocations.store_release(local.tally.allocations);
            line.releases.store_release(local.tally.releases);
            line.live_bytes.store_release(local.tally.live_bytes);
        });
}

// The loop of a worker: the items of each window, which it publishes with
// its live bytes, then the ops of the latency phase until the stop.
template <class W>
void run_worker(RowControl& control, typename W::Shared& shared, bench::ThreadSlot slot, ThreadCount threads) noexcept {
    typename W::Local local = W::start(shared, slot, threads);
    static_cast<void>(control.ready.value.fetch_add_acq_rel(1));
    while (read_phase(control) == Phase::Wait)
        CRUCIBLE_SPIN_PAUSE;
    std::uint64_t const counted = W::role_of(slot) == Role::producer ? kWorkerOps<W> * W::kItemsPerOp : 0;
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> items{};
    for (::fixy::Option<WindowIndex> window = window_of(read_phase(control)); window.is_some();
         window = window_of(read_phase(control))) {
        for (std::uint64_t count = 0; count < kWorkerOps<W>; ++count)
            W::op(shared, local, control);
        for (WindowIndex const index : window)
            items.with_element(index, [counted](std::uint64_t& item) noexcept { item += counted; });
    }
    std::uint64_t const live_bytes = local.tally.live_bytes;
    // Each worker writes only the line of its own slot, and each field of a line is an Atomic.
    control.lines.with_element(
        ::fixy::Index<kMaxThreads>::check(slot.raw()).expect("a slot of a row is below kMaxThreads"),
        [&items, live_bytes](ThreadLine& line) noexcept {
            for (WindowIndex const index : items.indices()) {
                std::uint64_t const value = items.at(index);
                // The main thread reads this Atomic only after is_published, and no other thread writes it.
                line.window_items.with_element(
                    index, [value](::fixy::Atomic<std::uint64_t>& cell) noexcept { cell.store_release(value); });
            }
            line.window_live_bytes.store_release(live_bytes);
            line.is_published.store_release(true);
        });
    while (read_phase(control) != Phase::Stop) {
        for (std::uint64_t count = 0; count < kWorkerOps<W>; ++count)
            W::op(shared, local, control);
    }
    stop_thread<W>(control, shared, local, threads, slot);
}

// The window of the main thread (THE PHASES OF A ROW of
// bench/bench_thread_scaling.cpp): it starts each window, runs the op for
// its part of kWindowNs, and then starts the latency phase.
template <class W, class Op>
[[nodiscard]] Window run_window_loop(RowControl& control, Op const& op, bench::CpuId cpu) noexcept {
    bench::CoreHold const hold{cpu};
    ::fixy::InitLoadCtx const startup{eff::testing::init()};
    eff::cap::Nondet const nondet = eff::testing::test().nondet;
    auto const clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(startup);
    auto const now = [&clock, &nondet]() noexcept {
        return clock.read(nondet).expect("a read of the monotonic clock").peek();
    };
    bench::detail::PauseFiles const pause_files = bench::detail::open_pause_files();
    auto const before = bench::detail::read_pause_before(pause_files);
    Window window{};
    bench::WindowClock window_clock = bench::detail::read_window_clock();
    std::uint64_t start = now();
    for (WindowIndex const index : ::fixy::IndexRange<bench::kWindows>{}) {
        control.phase.phase.store_release(phase_of(index));
        std::uint64_t end = start;
        std::uint64_t ops = 0;
        while (end - start < kWindowNs / bench::kWindows) {
            for (std::uint64_t count = 0; count < kClockOps<W>; ++count)
                op();
            ops += kClockOps<W>;
            end = now();
        }
        bench::WindowClock const window_end = bench::detail::read_window_clock();
        std::uint64_t const items = ops * W::kItemsPerOp;
        std::uint64_t const wall_ns = end - start;
        std::uint64_t const lost_ns =
            bench::detail::lost_between(::fixy::side_as<bench::side::ReadingBefore>(window_clock),
                                        ::fixy::side_as<bench::side::ReadingAfter>(window_end));
        window.items.with_element(index, [items](std::uint64_t& element) noexcept { element = items; });
        window.wall_ns.with_element(index, [wall_ns](std::uint64_t& element) noexcept { element = wall_ns; });
        window.lost_ns.with_element(index, [lost_ns](std::uint64_t& element) noexcept { element = lost_ns; });
        window_clock = window_end;
        start = end;
    }
    control.phase.phase.store_release(Phase::Latency);
    auto const after = bench::detail::read_pause_after(pause_files);
    window.pause = bench::detail::pause_between(before, after);
    return window;
}

// The name of a report: the allocator, the row, its size or its threads and
// its place, such as "jemalloc larson [latency of one op, 4 threads]".
[[nodiscard]] inline bench::Run run_of(AllocatorKind allocator, RowKind row, ::fixy::TextView detail) {
    ::fixy::FixedText<96> name{};
    static_cast<void>(::fixy::format(name, "{} {} [{}]", name_of(allocator), name_of(row), detail)
                          .expect("the name of a report fits in 96 characters"));
    return bench::Run{name};
}

}  // namespace bench_alloc

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the rows of the allocators run their workers on std::jthread")
namespace bench_alloc {

// The workers of one row, one std::jthread for each slot from slot 1 on (THE
// THREADS OF A ROW).  A worker pins itself to the CPU of its slot.  The join
// waits for each worker.
class RawWorkers {
public:
    template <class Body>
    void start(RowPlaces const& places, ThreadCount threads, Body const& body) {
        for (std::uint32_t slot = 1; slot < threads.raw(); ++slot) {
            bench::CpuId const cpu = places.cpu_of(bench::ThreadSlot{slot});
            threads_[slot] = std::jthread{[&body, cpu, slot]() noexcept {
                bench::pin_calling_thread_to(cpu).expect("the pin of a worker to the CPU of its slot");
                body(bench::ThreadSlot{slot});
            }};
        }
    }

    void join() noexcept {
        for (std::jthread& thread : threads_) {
            if (thread.joinable()) thread.join();
        }
    }

private:
    std::array<std::jthread, kMaxThreads> threads_{};
};

}  // namespace bench_alloc
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace bench_alloc {

// One row of a workload with a window on the first threads.count slots
// (THE PHASES OF A ROW).
template <AllocatorAdapter A, class W>
[[nodiscard]] RowResult run_window_row(RowPlaces const& places, ThreadCount threads, Placement placement) {
    ::fixy::Box<RowControl> boxed = ::fixy::mint_box<RowControl>(bench_alloc());
    RowControl& control = *boxed;
    StealTicks const steal_before = read_steal(places, threads);
    MemoryReading memory{.start = read_resident()};
    typename W::Shared shared = W::mint_shared(threads);
    RawWorkers workers{};
    workers.start(places, threads, [&control, &shared, threads](bench::ThreadSlot slot) noexcept {
        run_worker<W>(control, shared, slot, threads);
    });
    bench::CpuId const main_cpu = places.cpu_of(bench::kMeasuringSlot);
    typename W::Local local = [&] {
        bench::CoreHold const hold{main_cpu};
        return W::start(shared, bench::kMeasuringSlot, threads);
    }();
    while (control.ready.value.load_acquire() + 1 < threads.raw())
        CRUCIBLE_SPIN_PAUSE;

    auto const op = [&shared, &local, &control] [[gnu::always_inline]] () noexcept { W::op(shared, local, control); };
    Window const window = run_window_loop<W>(control, op, main_cpu);
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> items = window.items;
    std::uint64_t live_bytes = local.tally.live_bytes;
    for (std::uint32_t slot = 1; slot < threads.raw(); ++slot) {
        ThreadLine const& line = line_of(control, bench::ThreadSlot{slot});
        while (!line.is_published.load_acquire())
            op();
        for (WindowIndex const index : items.indices()) {
            std::uint64_t const published = line.window_items.at(index).load_acquire();
            items.with_element(index, [published](std::uint64_t& item) noexcept { item += published; });
        }
        live_bytes += line.window_live_bytes.load_acquire();
    }
    memory.steady = read_resident();
    memory.live = ByteCount{live_bytes};

    std::uint64_t all_items = 0;
    std::uint64_t all_wall_ns = 0;
    ::fixy::FixedArray<double, bench::kWindows> window_mops{};
    for (WindowIndex const index : items.indices()) {
        std::uint64_t const wall_ns = window.wall_ns.at(index);
        all_items += items.at(index);
        all_wall_ns += wall_ns;
        double const mops =
            wall_ns == 0 ? 0.0 : static_cast<double>(items.at(index)) * 1000.0 / static_cast<double>(wall_ns);
        window_mops.with_element(index, [mops](double& element) noexcept { element = mops; });
    }
    RowResult result{.threads = threads,
                     .placement = placement,
                     .centi_mops = all_wall_ns == 0 ? 0 : all_items * 100000 / all_wall_ns,
                     .windows = bench::spread_of_windows(window_mops.view()),
                     .paused_windows = bench::detail::paused_windows_of(window.lost_ns.view(), window.pause.basis),
                     .window_pause = window.pause};
    for (::fixy::Index<kLatencyReports> const report : kLatencyBatches.indices()) {
        std::size_t const batch = kLatencyBatches.at(report);
        ::fixy::FixedText<24> ops{};
        if (batch == 1) {
            static_cast<void>(
                ::fixy::format(ops, "latency of one op").expect("the ops of a report fit in 24 characters"));
        } else {
            static_cast<void>(
                ::fixy::format(ops, "mean of {} ops", batch).expect("the ops of a report fit in 24 characters"));
        }
        ::fixy::FixedText<64> detail{};
        static_cast<void>(::fixy::format(detail, "{}, {} threads{}{}", ops.view(), threads.raw(),
                                         W::kRow == RowKind::pair ? ::fixy::TextView{", "} : ::fixy::TextView{},
                                         W::kRow == RowKind::pair ? name_of(placement) : ::fixy::TextView{})
                              .expect("the detail of a report fits in 64 characters"));
        bench::Report measured = run_of(A::kind, W::kRow, detail.view())
                                     .core(main_cpu.raw())
                                     .batch(batch)
                                     .samples(kLatencySamples)
                                     .warmup(kLatencyWarmup)
                                     .max_wall_ms(kLatencyMs)
                                     .measure([&op] { op(); });
        result.latency.with_element(report,
                                    [&measured](bench::Report& element) noexcept { element = std::move(measured); });
    }
    control.phase.phase.store_release(Phase::Stop);
    {
        bench::CoreHold const hold{main_cpu};
        stop_thread<W>(control, shared, local, threads, bench::kMeasuringSlot);
    }
    workers.join();
    result.steal_ms = steal_ms_between(::fixy::side_as<bench::side::ReadingBefore>(steal_before),
                                       ::fixy::side_as<bench::side::ReadingAfter>(read_steal(places, threads)));

    Tally total{};
    for (std::uint32_t slot = 0; slot < threads.raw(); ++slot) {
        ThreadLine const& line = line_of(control, bench::ThreadSlot{slot});
        add_tally(total, Tally{.allocations = line.allocations.load_acquire(),
                               .releases = line.releases.load_acquire(),
                               .live_bytes = line.live_bytes.load_acquire()});
    }
    if constexpr (W::kRow == RowKind::cache_scratch) add_tally(total, shared.main_tally);
    check_tally(W::kRow, A::kind, total);
    // The Mailboxes of the row end before the last reading of the memory.
    { [[maybe_unused]] typename W::Shared const ended{std::move(shared)}; }
    memory.end = read_resident();
    std::size_t harness = 0;
    for (bench::Report const& report : result.latency)
        harness += sample_bytes_of(report).raw();
    memory.harness = ByteCount{harness};
    result.memory = memory;
    return result;
}

// The efficiency base of a workload: the producers and the throughput of its
// first row.
struct EfficiencyBase {
    std::uint64_t producers = 1;
    std::uint64_t centi_mops = 0;
};

template <class W>
[[nodiscard]] std::uint64_t producers_of(ThreadCount threads) noexcept {
    if constexpr (W::kRow == RowKind::xmalloc || W::kRow == RowKind::pair) {
        return threads.raw() / 2;
    } else {
        return threads.raw();
    }
}

// Prints one row: its threads, its throughput, its efficiency, the median
// and the spread of its windows, its pause, its memory and its latency.
template <class W>
void print_row(RowResult const& row, EfficiencyBase const& base) noexcept {
    std::uint64_t const ideal = base.centi_mops * producers_of<W>(row.threads) / base.producers;
    std::uint64_t const efficiency = ideal == 0 ? 0 : row.centi_mops * 100 / ideal;
    auto const median = static_cast<std::uint64_t>(row.windows.median * 100.0 + 0.5);
    auto const spread = static_cast<std::uint64_t>(row.windows.spread * 1000.0 + 0.5);
    bool const is_paused = row.window_pause.is_over_bound() || row.paused_windows > 0 || row.steal_ms > 0;
    bench::detail::flush_standard_output();
    ::fixy::report(::fixy::Sink::Out, "  {} threads{}{}: {}.{}{} Mitems/s, efficiency {}.{}{}", row.threads.raw(),
                   W::kRow == RowKind::pair ? ::fixy::TextView{", consumer in the "} : ::fixy::TextView{},
                   W::kRow == RowKind::pair ? name_of(row.placement) : ::fixy::TextView{}, row.centi_mops / 100,
                   row.centi_mops % 100 / 10, row.centi_mops % 10, efficiency / 100, efficiency % 100 / 10,
                   efficiency % 10);
    ::fixy::report(::fixy::Sink::Out, ", {} windows: median {}.{}{} Mitems/s, spread {}.{}%", row.windows.windows,
                   median / 100, median % 100 / 10, median % 10, spread / 10, spread % 10);
    ::fixy::report(::fixy::Sink::Out, ", window pause {} us, {} paused windows, steal {} ms{}\n",
                   row.window_pause.own_ns() / 1000, row.paused_windows, row.steal_ms,
                   is_paused ? ::fixy::TextView{"  NOT VALID"} : ::fixy::TextView{});
    print_memory(row.memory);
    for (bench::Report const& report : row.latency)
        report.print_text();
}

// The counts of threads of a workload: an xmalloc row has pairs, so it takes
// the even counts, and a pair row has two threads.
template <class W>
[[nodiscard]] bool is_count_of(ThreadCount threads) noexcept {
    if constexpr (W::kRow == RowKind::xmalloc) {
        return threads.raw() >= 2 && threads.raw() % 2 == 0;
    } else if constexpr (W::kRow == RowKind::pair) {
        return threads.raw() == 2;
    } else {
        return true;
    }
}

// The rows of one workload with a window: one row for each count, or the
// three places of a pair row.  Then the line of each row.
template <AllocatorAdapter A, class W>
void run_workload_rows(bench::Spread const& spread, ::fixy::View<ThreadCount const> counts, RunRecord& record) {
    bench::detail::flush_standard_output();
    ::fixy::report(::fixy::Sink::Out, "\n=== {} {} ===\n", name_of(A::kind), name_of(W::kRow));
    ::fixy::FixedList<RowResult, kMaxRows> rows{};
    if constexpr (W::kRow == RowKind::pair) {
        ::fixy::FixedArray<Placement, 3> const placements{Placement::same_l3, Placement::other_l3,
                                                          Placement::other_socket};
        for (Placement const placement : placements) {
            ::fixy::Option<bench::CpuId> const consumer = consumer_cpu_of(spread, placement);
            if (consumer.is_none()) {
                ::fixy::report(::fixy::Sink::Out, "  no CPU of the spread is in the place {}, so that row is absent\n",
                               name_of(placement));
                continue;
            }
            RowPlaces const places{.spread = spread, .consumer = consumer};
            static_cast<void>(rows.append(run_window_row<A, W>(places, ThreadCount{2}, placement))
                                  .expect("a workload has at most kMaxRows rows"));
        }
    } else {
        for (ThreadCount const threads : counts) {
            if (!is_count_of<W>(threads)) continue;
            RowPlaces const places{.spread = spread};
            static_cast<void>(rows.append(run_window_row<A, W>(places, threads, Placement::same_l3))
                                  .expect("a workload has at most kMaxRows rows"));
        }
    }
    if (rows.size() == 0) return;
    RowResult const& first = *rows.get(0).expect("the test above found a row");
    EfficiencyBase const base{.producers = producers_of<W>(first.threads), .centi_mops = first.centi_mops};
    for (RowResult& row : rows) {
        print_row<W>(row, base);
        double const item_ns = row.windows.median > 0.0 ? 1000.0 / row.windows.median : 0.0;
        add_summary(record, Summary{.allocator = A::kind,
                                    .row = W::kRow,
                                    .threads = row.threads,
                                    .placement = row.placement,
                                    .item_ns = item_ns});
        row.latency.with_mut([&record](auto run) noexcept {
            for (bench::Report& report : run)
                bench::add_report(record.reports, std::move(report));
        });
    }
}

// ── The rounds ─────────────────────────────────────────────────────────

// mstress and mleak: rounds of threads that start, replace blocks, and end
// (THE ROUNDS).  A thread of an mstress round gives its live blocks to bank
// round % 2 at its own slot, and releases the blocks of bank (round + 1) % 2
// at the slot (slot + round) % threads, so no two threads of a round touch
// one array.  Each array of a slot of a bank is owned by one thread of a
// round, and the join of the round orders it before the next round.
template <AllocatorAdapter A, RowKind Kind>
struct RoundWorkload {
    using Block = BlockOf<A, BlockAlignment::natural>;
    static constexpr std::size_t kSlots = 1024;
    static constexpr std::uint32_t kReplaces = 50000;
    using Blocks = ::fixy::FixedArray<::fixy::Option<Block>, kSlots>;

    struct Shared {
        // The two banks of the live blocks that one round gives to the next.
        ::fixy::HeapArray<Blocks> banks;
        ::fixy::FixedArray<Tally, kMaxThreads> tallies{};
        std::uint64_t round = 0;
        ThreadCount threads{1};
    };

    [[nodiscard]] static ::fixy::Box<Shared> mint_shared(ThreadCount threads) noexcept {
        ::fixy::Box<Shared> shared = ::fixy::mint_box<Shared>(bench_alloc());
        shared->banks = ::fixy::mint_heap_array_with<Blocks>(
            bench_alloc(), 2 * std::size_t{threads.raw()}, [](std::size_t /*position*/) noexcept { return Blocks{}; });
        shared->threads = threads;
        return shared;
    }

    [[nodiscard]] static Blocks& bank_of(Shared& shared, std::uint64_t bank, std::uint32_t slot) noexcept {
        return *shared.banks.view().get(bank * shared.threads.raw() + slot).expect("a bank holds each slot");
    }

    // The body of the thread of a slot in a round.  Each thread of a round
    // writes only the tally of its own slot.
    static void run_round_body(Shared& shared, bench::ThreadSlot slot) noexcept {
        shared.tallies.with_element(
            ::fixy::Index<kMaxThreads>::check(slot.raw()).expect("a slot of a round is below kMaxThreads"),
            [&shared, slot](Tally& tally) noexcept { run_round_with(shared, slot, tally); });
    }

    // The ops of the thread of a slot in a round, with the tally of the slot.
    static void run_round_with(Shared& shared, bench::ThreadSlot slot, Tally& tally) noexcept {
        Draws draws{Kind, bench::ThreadSlot{static_cast<std::uint32_t>(slot.raw() + shared.round * kMaxThreads)}};
        Blocks local{};
        if constexpr (Kind == RowKind::mstress) {
            std::uint32_t const from = static_cast<std::uint32_t>((slot.raw() + shared.round) % shared.threads.raw());
            bank_of(shared, (shared.round + 1) % 2, from).with_mut([&tally](auto run) noexcept {
                for (::fixy::Option<Block>& held : run) {
                    if (held.is_some()) give_block<A>(tally, std::move(held).expect("the test above found a block"));
                }
            });
        }
        for (std::uint32_t replace = 0; replace < kReplaces; ++replace) {
            std::uint32_t const draw = draws.next();
            local.with_element(
                ::fixy::Index<kSlots>::check(draw & (kSlots - 1)).expect("a masked draw lies below the slots"),
                [&tally, draw](::fixy::Option<Block>& held) noexcept {
                    if (held.is_some()) give_block<A>(tally, std::move(held).expect("the test above found a block"));
                    held = ::fixy::Option<Block>::some(take_block<A>(tally, uniform_bytes<16, 1008>(draw >> 8)));
                });
        }
        if constexpr (Kind == RowKind::mstress) {
            Blocks& given = bank_of(shared, shared.round % 2, slot.raw());
            given.with_mut([&local](auto given_run) noexcept {
                local.with_mut([&given_run](auto local_run) noexcept {
                    for (::fixy::Index<kSlots> const position : ::fixy::IndexRange<kSlots>{})
                        given_run.view().at(position) = std::move(local_run.view().at(position));
                });
            });
        } else {
            local.with_mut([&tally](auto run) noexcept {
                for (::fixy::Option<Block>& held : run) {
                    if (held.is_some()) give_block<A>(tally, std::move(held).expect("the test above found a block"));
                }
            });
        }
    }

    // The release of each block of the two banks.
    static void finish(Shared& shared) noexcept {
        shared.tallies.with_element(
            ::fixy::Index<kMaxThreads>::check(0).expect("a round has slot 0"), [&shared](Tally& tally) noexcept {
                for (Blocks& blocks : shared.banks) {
                    blocks.with_mut([&tally](auto run) noexcept {
                        for (::fixy::Option<Block>& held : run) {
                            if (held.is_some())
                                give_block<A>(tally, std::move(held).expect("the test above found a block"));
                        }
                    });
                }
            });
    }
};

}  // namespace bench_alloc

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: each round of mstress and mleak starts and joins its workers on std::jthread")
namespace bench_alloc {

// One round: a std::jthread for each slot from slot 1 on, pinned to the CPU
// of its slot, and the body of slot 0 on the main thread.
template <class Round>
void run_one_round(typename Round::Shared& shared, bench::Spread const& spread) noexcept {
    std::array<std::jthread, kMaxThreads> threads{};
    for (std::uint32_t slot = 1; slot < shared.threads.raw(); ++slot) {
        bench::CpuId const cpu = spread.of_slot(bench::ThreadSlot{slot}).expect("a round has at most the slots");
        threads[slot] = std::jthread{[&shared, cpu, slot]() noexcept {
            bench::pin_calling_thread_to(cpu).expect("the pin of a worker to the CPU of its slot");
            Round::run_round_body(shared, bench::ThreadSlot{slot});
        }};
    }
    Round::run_round_body(shared, bench::kMeasuringSlot);
    for (std::jthread& thread : threads) {
        if (thread.joinable()) thread.join();
    }
    ++shared.round;
}

}  // namespace bench_alloc
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace bench_alloc {

// The rows of a round workload: one row for each count.  The latency report
// times one round.
template <AllocatorAdapter A, RowKind Kind>
void run_round_rows(bench::Spread const& spread, ::fixy::View<ThreadCount const> counts, RunRecord& record) {
    using Round = RoundWorkload<A, Kind>;
    bench::detail::flush_standard_output();
    ::fixy::report(::fixy::Sink::Out, "\n=== {} {} ===\n", name_of(A::kind), name_of(Kind));
    bench::CpuId const main_cpu = spread.of_slot(bench::kMeasuringSlot).expect("a run has the slot of the main thread");
    RowPlaces const places{.spread = spread};
    for (ThreadCount const threads : counts) {
        StealTicks const steal_before = read_steal(places, threads);
        MemoryReading memory{.start = read_resident()};
        ::fixy::Box<typename Round::Shared> shared = Round::mint_shared(threads);
        ::fixy::FixedText<64> detail{};
        static_cast<void>(::fixy::format(detail, "one round of {} threads", threads.raw())
                              .expect("the detail of a report fits in 64 characters"));
        bench::Report report = run_of(A::kind, Kind, detail.view())
                                   .core(main_cpu.raw())
                                   .batch(1)
                                   .samples(kRoundSamples)
                                   .warmup(1)
                                   .max_wall_ms(kRoundMs)
                                   .measure([&shared, &spread] { run_one_round<Round>(*shared, spread); });
        memory.steady = read_resident();
        Tally live{};
        for (std::uint32_t slot = 0; slot < threads.raw(); ++slot)
            add_tally(live, *shared->tallies.get(slot).expect("a slot of a round is below kMaxThreads"));
        memory.live = ByteCount{live.live_bytes};
        Round::finish(*shared);
        Tally total{};
        for (std::uint32_t slot = 0; slot < threads.raw(); ++slot)
            add_tally(total, *shared->tallies.get(slot).expect("a slot of a round is below kMaxThreads"));
        check_tally(Kind, A::kind, total);
        { ::fixy::Box<typename Round::Shared> const ended{std::move(shared)}; }
        memory.end = read_resident();
        memory.harness = sample_bytes_of(report);
        std::uint64_t const items = std::uint64_t{threads.raw()} * Round::kReplaces;
        double const item_ns = report.pct.p50 / static_cast<double>(items);
        auto const centi_ns = static_cast<std::uint64_t>(item_ns * 100.0 + 0.5);
        std::uint64_t const steal_ms =
            steal_ms_between(::fixy::side_as<bench::side::ReadingBefore>(steal_before),
                             ::fixy::side_as<bench::side::ReadingAfter>(read_steal(places, threads)));
        bench::detail::flush_standard_output();
        ::fixy::report(::fixy::Sink::Out,
                       "  {} threads: {}.{}{} ns for each replace at the p50 of a round, steal {} ms{}\n",
                       threads.raw(), centi_ns / 100, centi_ns % 100 / 10, centi_ns % 10, steal_ms,
                       steal_ms > 0 ? ::fixy::TextView{"  NOT VALID"} : ::fixy::TextView{});
        print_memory(memory);
        report.print_text();
        add_summary(record, Summary{.allocator = A::kind, .row = Kind, .threads = threads, .item_ns = item_ns});
        bench::add_report(record.reports, std::move(report));
    }
}

// ── The rows of one thread ─────────────────────────────────────────────

// The bytes of the slot of the size class at the position position of the
// classes of the base, from 0 (THE SIZE CLASSES of foundation/alloc/Layout.h):
// 16 to 128 bytes in steps of 16, then four classes for each doubling to
// 2 MiB.
[[nodiscard]] consteval std::size_t class_bytes(std::size_t position) noexcept {
    return ::fixy::slot_bytes(::fixy::SizeClass{static_cast<std::uint8_t>(position + 1)}).raw();
}

constexpr std::size_t kSizeClasses = ::fixy::kSizeClassCount;

// The run of the row of one size class on the CPU cpu.  One take and release
// of a small block is shorter than the resolution of the clock, so the
// harness picks a batch of calls for each sample, and each percentile is over
// the means of the batches.
[[nodiscard]] inline bench::Run class_run_of(AllocatorKind allocator, ByteCount bytes, bench::CpuId cpu) {
    ::fixy::FixedText<64> detail{};
    static_cast<void>(::fixy::format(detail, "take and release {} bytes", bytes.raw())
                          .expect("the detail of a report fits in 64 characters"));
    bench::Run run = run_of(allocator, RowKind::size_class, detail.view());
    // The builder gives the same run back, and the measure is the call of the row.
    static_cast<void>(run.core(cpu.raw()).samples(kLatencySamples).warmup(kLatencyWarmup).max_wall_ms(kLatencyMs / 4));
    return run;
}

// Prints the report of the row of one size class and keeps it in the record.
// Gives the bytes of the samples of the report.
[[nodiscard]] inline ByteCount keep_class_report(AllocatorKind allocator, ByteCount bytes, bench::Report&& report,
                                                 RunRecord& record) {
    report.print_text();
    add_summary(
        record,
        Summary{.allocator = allocator, .row = RowKind::size_class, .size = bytes.raw(), .item_ns = report.pct.p50});
    ByteCount const samples = sample_bytes_of(report);
    bench::add_report(record.reports, std::move(report));
    return samples;
}

// The row of one size class of an adapter with a Constant: one thread takes
// and releases a Constant of the size Size.
template <ConstantAdapter A, std::size_t Size>
[[nodiscard]] ByteCount run_class_row(bench::CpuId cpu, RunRecord& record) {
    bench::Report report = class_run_of(A::kind, ByteCount{Size}, cpu).measure([] {
        A::template release_constant<Size>(A::template allocate_constant<Size>());
    });
    return keep_class_report(A::kind, ByteCount{Size}, std::move(report), record);
}

// The row of one size class of an adapter with no Constant: one thread takes
// and releases a Block of bytes bytes.  The rows of each size share this one
// body of the harness.
template <AllocatorAdapter A>
[[nodiscard]] ByteCount run_class_row(bench::CpuId cpu, RunRecord& record, ByteCount bytes) {
    bench::Report report = class_run_of(A::kind, bytes, cpu).measure([bytes] {
        A::template release<BlockAlignment::natural>(A::template allocate<BlockAlignment::natural>(bytes));
    });
    return keep_class_report(A::kind, bytes, std::move(report), record);
}

template <AllocatorAdapter A, std::size_t... Classes>
void run_class_rows(bench::CpuId cpu, RunRecord& record, std::index_sequence<Classes...> /*classes*/) {
    bench::detail::flush_standard_output();
    ::fixy::report(::fixy::Sink::Out, "\n=== {} class: a block of each size class on one thread ===\n",
                   name_of(A::kind));
    MemoryReading memory{.start = read_resident()};
    std::size_t harness = 0;
    if constexpr (ConstantAdapter<A>) {
        ((harness += run_class_row<A, class_bytes(Classes)>(cpu, record).raw()), ...);
    } else {
        ((harness += run_class_row<A>(cpu, record, ByteCount{class_bytes(Classes)}).raw()), ...);
    }
    memory.steady = read_resident();
    memory.end = memory.steady;
    memory.harness = ByteCount{harness};
    print_memory(memory);
}

// The span rows: one thread takes a Written of each size at the alignment of
// a huge page, which writes each byte, and releases it.
template <AllocatorAdapter A>
void run_span_rows(bench::CpuId cpu, RunRecord& record) {
    bench::detail::flush_standard_output();
    ::fixy::report(::fixy::Sink::Out, "\n=== {} span: take, write and release a span on one thread ===\n",
                   name_of(A::kind));
    ::fixy::FixedArray<std::size_t, 4> const spans{2, 8, 64, 256};
    for (std::size_t const mebibytes : spans) {
        MemoryReading memory{.start = read_resident()};
        ByteCount const bytes{mebibytes * kMebibyte};
        Stamp const stamp = stamp_of(bench::kMeasuringSlot);
        Tally tally{};
        ::fixy::FixedText<64> detail{};
        static_cast<void>(::fixy::format(detail, "take, write and release {} MiB", mebibytes)
                              .expect("the detail of a report fits in 64 characters"));
        bench::Report report =
            run_of(A::kind, RowKind::span, detail.view())
                .core(cpu.raw())
                .batch(1)
                .samples(kRoundSamples)
                .warmup(1)
                .max_wall_ms(kRoundMs)
                .measure([&tally, bytes, stamp] {
                    give_written<A, BlockAlignment::huge_page>(
                        tally, take_written<A, BlockAlignment::huge_page>(tally, bytes, stamp), stamp);
                });
        check_tally(RowKind::span, A::kind, tally);
        memory.steady = read_resident();
        memory.end = memory.steady;
        memory.harness = sample_bytes_of(report);
        print_memory(memory);
        report.print_text();
        add_summary(
            record,
            Summary{.allocator = A::kind, .row = RowKind::span, .size = bytes.raw(), .item_ns = report.pct.p50});
        bench::add_report(record.reports, std::move(report));
    }
}

// ── The box channel ────────────────────────────────────────────────────

// The rows of a Box across a PermissionedBoxChannel, which only the base
// has.  bench_alloc_scaling_base.cpp holds them.
void run_box_channel_rows(bench::Spread const& spread, RunRecord& record);

// ── The run ────────────────────────────────────────────────────────────

// The allocators and the rows that the arguments select (THE ARGUMENTS).
struct Selection {
    ::fixy::FixedArray<bool, kAllocatorKinds> allocators{};
    ::fixy::FixedArray<bool, kRowKinds> rows{};

    [[nodiscard]] bool has(AllocatorKind kind) const noexcept {
        return *allocators.get(static_cast<std::size_t>(kind)).expect("an allocator kind is below kAllocatorKinds");
    }
    [[nodiscard]] bool has(RowKind kind) const noexcept {
        return *rows.get(static_cast<std::size_t>(kind)).expect("a row kind is below kRowKinds");
    }
};

// The selection of the arguments, and each count of threads (THE ARGUMENTS).
struct Arguments {
    Selection selection{};
    ::fixy::FixedList<ThreadCount, kMaxRows> counts{};
};

// Each row of one allocator that the selection holds.
template <AllocatorAdapter A>
void run_allocator(bench::Spread const& spread, Arguments const& arguments, RunRecord& record) {
    if (!arguments.selection.has(A::kind)) return;
    ::fixy::View<ThreadCount const> const counts = arguments.counts.view();
    Selection const& rows = arguments.selection;
    bench::CpuId const main_cpu = spread.of_slot(bench::kMeasuringSlot).expect("a run has the slot of the main thread");
    if (rows.has(RowKind::size_class)) run_class_rows<A>(main_cpu, record, std::make_index_sequence<kSizeClasses>{});
    if (rows.has(RowKind::larson)) run_workload_rows<A, Larson<A>>(spread, counts, record);
    if (rows.has(RowKind::xmalloc)) run_workload_rows<A, Xmalloc<A>>(spread, counts, record);
    if (rows.has(RowKind::cache_scratch)) run_workload_rows<A, CacheScratch<A>>(spread, counts, record);
    if (rows.has(RowKind::sh6bench)) run_workload_rows<A, Sh6bench<A>>(spread, counts, record);
    if (rows.has(RowKind::alloc_test)) run_workload_rows<A, AllocTest<A>>(spread, counts, record);
    if (rows.has(RowKind::rptest)) run_workload_rows<A, Rptest<A>>(spread, counts, record);
    if (rows.has(RowKind::pair)) run_workload_rows<A, Pair<A>>(spread, counts, record);
    if (rows.has(RowKind::mstress)) run_round_rows<A, RowKind::mstress>(spread, counts, record);
    if (rows.has(RowKind::mleak)) run_round_rows<A, RowKind::mleak>(spread, counts, record);
    if (rows.has(RowKind::span)) run_span_rows<A>(main_cpu, record);
    if constexpr (A::kind == AllocatorKind::base) {
        if (rows.has(RowKind::box_channel)) run_box_channel_rows(spread, record);
    }
}

// The rows of each allocator of the build, each in its own unit
// bench_alloc_scaling_ALLOCATOR.cpp (THE UNITS).
void run_base_rows(bench::Spread const& spread, Arguments const& arguments, RunRecord& record);
void run_glibc_rows(bench::Spread const& spread, Arguments const& arguments, RunRecord& record);
#if defined(CRUCIBLE_BENCH_JEMALLOC)
void run_jemalloc_rows(bench::Spread const& spread, Arguments const& arguments, RunRecord& record);
#endif
#if defined(CRUCIBLE_BENCH_MIMALLOC)
void run_mimalloc_rows(bench::Spread const& spread, Arguments const& arguments, RunRecord& record);
#endif
#if defined(CRUCIBLE_BENCH_SNMALLOC)
void run_snmalloc_rows(bench::Spread const& spread, Arguments const& arguments, RunRecord& record);
#endif

}  // namespace bench_alloc
