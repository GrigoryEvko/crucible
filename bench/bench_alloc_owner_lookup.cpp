// The cost of the owner read of the allocator of the base
// (foundation/alloc/ChunkMap.h, THE MAP READ) against the lookups of the
// metadata of a block in the pinned references.
//
// THE ROWS
//   Each row reads the metadata of a random block of kBlocks blocks.
//   hearth, inline   owner_of of a BiasedChunkMap, inline in the loop: one
//                    shift and one load of the entry of 2 bytes.  The blocks
//                    are in the first kBlocks chunks of a window of 16 GiB,
//                    one in each chunk, at a random byte at a step of 16
//                    bytes
//   hearth, call     the same read in a function that the compiler cannot
//                    see through, so the row pays a call as each reference
//                    row does
//   mimalloc         mi_usable_size of the pinned mimalloc 3.7.0 on kBlocks
//                    blocks of 64 KiB.  Its page meta is at an aligned place:
//                    a mask and an index of the address, then a load of the
//                    size of the block from the page meta.  A call
//   jemalloc         je_malloc_usable_size of the pinned jemalloc 5.4.0 on
//                    kBlocks blocks of 64 KiB: the read of its radix tree
//                    through the cache of the thread, and the read of the
//                    size of the class.  A call
//   A reference row reads the size of the block too, which the owner read
//   does not.  The two forms of the base bound the ratio: the inline form is
//   the cost that a free of the base pays, and the call form pays what a
//   call of a reference pays.
//
//   The blocks of a reference are 64 KiB, so they lie in 256 MiB, and the
//   blocks of the base lie in kBlocks chunks, 8 GiB.  So the rows of a
//   reference read less metadata than the rows of the base.  With blocks of
//   2 MiB, mimalloc asks for transparent huge pages, its first write in each
//   block made a huge page resident, and the process held 8 GiB.
//
// THE SEQUENCE
//   Each row reads kPicks addresses in a cycle, from a table that Philox4x32
//   fills with a fixed key before the row.  So no lookup computes a random
//   number, and each run reads the same blocks in the same order.  The table
//   holds 512 KiB, so its reads stream from the L2 cache in each row.
//
// THE WINDOWS
//   The TSC of the bench host steps in 10 ns, and one read takes about one
//   nanosecond.  So each sample times kBatch reads, and the report gives the
//   p50, the p99 and the maximum of the mean of one read in a sample.
//
// THE RATIOS
//   The run ends with the p50 of each form of the base over the p50 of each
//   reference.  Below 1.00 the base is faster.
//
// THE SPEED OF LIGHT
//   A row that beats a reference can still be far from the floor of the core.
//   So the run ends with the rows in cycles of the core, each beside the floor
//   of a model of the core and the ratio to it:
//     the clock       64 dependent adds in one block of code, one cycle each,
//                     give the cycles of one ns in this run.  The core can
//                     run at a clock that is not the clock of the TSC
//     the floor row   the throughput loop with the read replaced by its
//                     minimum: the load of the address and the sum
//     the latency     a chain in which the next address comes from the last
//                     owner, so each read waits for the read before it.  The
//                     chunks of the chain are 32 apart, one for each line of
//                     the map, and 256, 4096 and 16384 lines put the map in
//                     the L1, the L2 and past the L2.  The chunk map of the
//                     largest window is 1 MiB, less than one L3, so no steady
//                     loop keeps a map line in DRAM: only the first touch of
//                     a line after a purge reads DRAM
//     the chase       the floor of the latency at each level: a chase of one
//                     word for each line over the same count of lines
//   The window of the latency rows is a window of the ladder with no cap, 1 TiB
//   on the bench host.
//
// THE GAPS
//   hearth call     the row takes about 4.9 cycles a read against the floor
//                   of 4, the best case of the BTB.  The gap comes from the
//                   call form of the bench, and the bench does not divide its
//                   two parts: the call and the return take a bubble each, and
//                   the cursor and the sum stay in memory around the opaque
//                   call, so each step stores them and loads them again.  Each
//                   reference row has the same form, so the ratios of the call
//                   row compare the same work
//   hearth inline   the row takes about 1.66 cycles a read against its floor
//                   of 1.33.  The row with two reads in each step takes about
//                   1.31 against 1.17.  So about 0.2 of the gap of 0.33 cycle
//                   comes with the one taken branch and the one fetch block of
//                   each step of the front end.  The rest has no measured
//                   cause: a perf record of the bench kept too few samples in
//                   the loop of one row
//
// THE COMMAND
//   python3 utils/bin/loop bench bench_alloc_owner_lookup

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <foundation/Quarantine.h>
#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/Window.h>
#include <foundation/core/Philox.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/PageSource.h>

#include <cstddef>
#include <cstdint>

#include "bench_harness.h"

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: a reference allocator gives a raw address, which no type of the base holds")
#if defined(CRUCIBLE_BENCH_JEMALLOC)
// The calls of the pinned jemalloc with the prefix je_.  Its generated header
// is in the build directory, so the bench declares the calls itself, as
// bench_alloc_scaling_rows.h does.
extern "C" {
void* je_malloc(std::size_t size);
void je_free(void* ptr);
std::size_t je_malloc_usable_size(void const* ptr);
}
#endif
#if defined(CRUCIBLE_BENCH_MIMALLOC)
#include <mimalloc.h>
#endif

namespace {

// The calls of one reference: allocate(bytes) gives the first byte of a block
// as an address word, release(word) gives the block back, and a call of the
// struct gives the size of a block.  Only these calls hold a raw address.
#if defined(CRUCIBLE_BENCH_MIMALLOC)
struct MimallocCalls {
    [[nodiscard]] static ::foundation::alloc::AddressWord allocate(std::size_t bytes) noexcept {
        return ::foundation::alloc::AddressWord{__builtin_bit_cast(std::uint64_t, mi_malloc(bytes))};
    }
    static void release(::foundation::alloc::AddressWord block) noexcept {
        mi_free(__builtin_bit_cast(void*, static_cast<std::uint64_t>(block)));
    }
    [[nodiscard, gnu::always_inline]] std::uint32_t operator()(::foundation::alloc::AddressWord block) const noexcept {
        return static_cast<std::uint32_t>(
            mi_usable_size(__builtin_bit_cast(void const*, static_cast<std::uint64_t>(block))));
    }
};
#endif
#if defined(CRUCIBLE_BENCH_JEMALLOC)
struct JemallocCalls {
    [[nodiscard]] static ::foundation::alloc::AddressWord allocate(std::size_t bytes) noexcept {
        return ::foundation::alloc::AddressWord{__builtin_bit_cast(std::uint64_t, je_malloc(bytes))};
    }
    static void release(::foundation::alloc::AddressWord block) noexcept {
        je_free(__builtin_bit_cast(void*, static_cast<std::uint64_t>(block)));
    }
    [[nodiscard, gnu::always_inline]] std::uint32_t operator()(::foundation::alloc::AddressWord block) const noexcept {
        return static_cast<std::uint32_t>(
            je_malloc_usable_size(__builtin_bit_cast(void const*, static_cast<std::uint64_t>(block))));
    }
};
#endif

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

namespace alloc = ::foundation::alloc;
namespace core = ::foundation::core;
namespace os = ::foundation::os;

// The blocks of a row: one in each chunk, as the task of a free meets them.
constexpr std::size_t kBlocks = 4096;
// The addresses of the table of a row, which the reads take in a cycle.
constexpr std::size_t kPicks = 65536;
// The reads of one sample (THE WINDOWS), and the samples of one row.
constexpr std::size_t kBatch = 1024;
constexpr std::size_t kSamples = 2048;
// The window of the base rows: 16 extents, with kBlocks chunks that are not
// guards.
constexpr std::size_t kWindowBytes = std::size_t{16} << 30;
// The bytes of a block of a reference (THE ROWS).
constexpr std::size_t kReferenceBlockBytes = std::size_t{64} << 10;
// The key of the Philox stream of the table (THE SEQUENCE).
constexpr std::uint64_t kSequenceKey = 0x4f574e45525f4c4bULL;

using PickIndex = core::Index<kPicks>;
using BlockIndex = core::Index<kBlocks>;

// The addresses of a row, in the order of the reads.
struct Picks {
    core::FixedArray<alloc::AddressWord, kPicks> addresses{};
};

// The address of read CURSOR of the table: the cursor wraps at kPicks.
[[nodiscard, gnu::always_inline]] inline alloc::AddressWord pick_at(Picks const& picks, std::size_t cursor) noexcept {
    return picks.addresses.at(PickIndex::check(cursor & (kPicks - 1)).expect("a masked cursor lies below kPicks"));
}

// The word of draw INDEX of the stream of the table.
[[nodiscard]] std::uint32_t draw_of(std::size_t index) noexcept {
    core::Philox4x32::Counter const words = core::Philox4x32::generate(std::uint64_t{index / 4}, kSequenceKey);
    return words.at(core::Index<4>::check(index % 4).expect("a word of a block lies below 4"));
}

// A time in nanoseconds, as the nearest integer of picoseconds.
[[nodiscard]] std::uint64_t picoseconds(double nanoseconds) noexcept {
    return static_cast<std::uint64_t>(nanoseconds * 1000.0 + 0.5);
}

// The resident MiB of the process: the second field of /proc/self/statm, a
// count of pages of 4 KiB, or zero when the file does not read.
[[nodiscard]] std::uint64_t resident_mebibytes() noexcept {
    core::FixedText<128> text{};
    if (os::read_file(core::CText{"/proc/self/statm"}, text).is_err()) return 0;
    core::TextView const all = text.view();
    std::size_t const space = core::find(all, ' ').value_or(all.size());
    if (space + 1 >= all.size()) return 0;
    core::TextView const pages = all.window(space + 1, all.size() - space - 1).value_or(core::TextView{});
    return (bench::detail::leading_number(pages).value_or(0) * 4096) >> 20;
}

// Writes one line: the name of the row, the percentiles of one read, and the
// resident MiB of the process at the end of the measure of the row.
void print_row(fixy::TextView name, bench::Report const& measured, std::uint64_t resident) noexcept {
    bench::print("  {}  p50={} ps  p99={} ps  max={} ps  cv={} permille  resident={} MiB\n", name,
                 picoseconds(measured.pct.p50), picoseconds(measured.pct.p99), picoseconds(measured.pct.max),
                 static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5), resident);
}

// Writes the p50 of a row of the base over the p50 of a reference (THE
// RATIOS).
void print_ratio(fixy::TextView name, bench::Report const& base, bench::Report const& reference) noexcept {
    if (reference.pct.p50 <= 0.0) return;
    auto const centi = static_cast<std::uint64_t>(base.pct.p50 / reference.pct.p50 * 100.0 + 0.5);
    bench::print("  {}: {}.{}{}\n", name, centi / 100, centi % 100 / 10, centi % 10);
}

// The read of the owner in a function that the compiler cannot see through,
// for the row hearth, call.
[[nodiscard, gnu::noipa]] alloc::ChunkOwner read_owner_through_call(alloc::BiasedChunkMap map,
                                                                    alloc::AddressWord address) noexcept {
    return map.owner_of(address);
}

// The timed body of each row: one read of the next address of the table.
// The read and the state are fields of one object, and the cursor and the
// sum are 32-bit words.  A load of an address of the table then cannot alias
// a store of the state, so GCC keeps the state in registers in the loop of a
// sample and loads the map word one time (THE WINDOWS).
template <class Read>
struct ReadLoop {
    Read read;
    Picks const& picks;
    std::uint32_t cursor = 0;
    std::uint32_t sum = 0;

    [[gnu::always_inline]] void operator()() noexcept {
        sum += read(pick_at(picks, cursor));
        ++cursor;
    }
};

// The read of the owner of the row hearth, inline.
struct InlineRead {
    alloc::BiasedChunkMap map;

    [[nodiscard, gnu::always_inline]] std::uint32_t operator()(alloc::AddressWord address) const noexcept {
        return static_cast<std::uint16_t>(map.owner_of(address));
    }
};

// The read of the owner of the row hearth, call.
struct CallRead {
    alloc::BiasedChunkMap map;

    [[nodiscard, gnu::always_inline]] std::uint32_t operator()(alloc::AddressWord address) const noexcept {
        return static_cast<std::uint16_t>(read_owner_through_call(map, address));
    }
};

// Times the reads of one row (THE WINDOWS).
template <class Read>
[[nodiscard]] bench::Report measure_reads(fixy::TextView name, Read read, Picks const& picks) {
    ReadLoop<Read> loop{read, picks};
    bench::Report report = bench::Run(name).batch(kBatch).samples(kSamples).measure(loop);
    bench::do_not_optimize(loop.sum);
    return report;
}

// The window of the base rows, with an owner in each of its first kBlocks
// chunks that are not guards, and the table of a random byte of each.
struct BaseRows {
    alloc::Window window;
    core::Box<Picks> picks;
};

[[nodiscard]] BaseRows build_base_rows() noexcept {
    alloc::WindowDiagnostic diagnostic{};
    core::Result<alloc::Window, alloc::WindowFull> reserved =
        alloc::reserve_window(os::ReserveCap{kWindowBytes}, core::to(diagnostic));
    if (reserved.is_err()) fixy::fatal("{}", diagnostic.text());
    alloc::Window window = std::move(reserved).expect("a window that the ladder gave");
    alloc::WindowRecord const record = window.record();
    core::FixedArray<alloc::AddressWord, kBlocks> chunks{};
    std::size_t taken = 0;
    for (std::uint32_t index = 0; taken < kBlocks; ++index) {
        alloc::ChunkIndex const chunk{index};
        if (alloc::is_guard_chunk(chunk)) continue;
        window.set_owner(chunk, alloc::owner_entry_of(alloc::HeapId{static_cast<std::uint16_t>(taken % 64 + 1)}));
        alloc::AddressWord const address = record.address_of(chunk);
        chunks.with_element(BlockIndex::check(taken).expect("a block lies below kBlocks"),
                            [address](alloc::AddressWord& element) noexcept { element = address; });
        ++taken;
    }
    core::Box<Picks> picks = core::mint_box<Picks>(foundation::effects::testing::test().alloc);
    for (PickIndex const pick : core::IndexRange<kPicks>{}) {
        std::uint32_t const word = draw_of(pick.raw());
        std::uint64_t const base =
            static_cast<std::uint64_t>(chunks.at(BlockIndex::check(word & (kBlocks - 1)).expect("a masked block")));
        std::uint64_t const offset = ((word >> 12) & (alloc::kChunkBytes / 16 - 1)) * 16;
        picks->addresses.with_element(pick, [base, offset](alloc::AddressWord& element) noexcept {
            element = alloc::AddressWord{base + offset};
        });
    }
    return BaseRows{std::move(window), std::move(picks)};
}

// The row of one reference: kBlocks blocks of kReferenceBlockBytes, the table
// of their first bytes, the measure and its line, and the release of each
// block.
template <class Calls>
[[nodiscard]] bench::Report run_reference(fixy::TextView name, fixy::TextView label) {
    core::Box<Picks> blocks = core::mint_box<Picks>(foundation::effects::testing::test().alloc);
    for (std::size_t block = 0; block < kBlocks; ++block) {
        alloc::AddressWord const first = Calls::allocate(kReferenceBlockBytes);
        if (first == alloc::AddressWord{}) fixy::fatal("{} gave no block of {} bytes", name, kReferenceBlockBytes);
        blocks->addresses.with_element(PickIndex::check(block).expect("a block lies below kPicks"),
                                       [first](alloc::AddressWord& element) noexcept { element = first; });
    }
    core::Box<Picks> picks = core::mint_box<Picks>(foundation::effects::testing::test().alloc);
    for (PickIndex const pick : core::IndexRange<kPicks>{}) {
        alloc::AddressWord const first = blocks->addresses.at(
            PickIndex::check(draw_of(pick.raw()) & (kBlocks - 1)).expect("a masked block lies below kPicks"));
        picks->addresses.with_element(pick, [first](alloc::AddressWord& element) noexcept { element = first; });
    }
    bench::Report report = measure_reads(name, Calls{}, *picks);
    print_row(label, report, resident_mebibytes());
    for (std::size_t block = 0; block < kBlocks; ++block) {
        Calls::release(blocks->addresses.at(PickIndex::check(block).expect("a block lies below kPicks")));
    }
    return report;
}

// ── The speed of light ──────────────────────────────────────────────────

// The dependent adds of one call of the clock body, the calls of one sample of
// the clock, the chunks between two lines of the chunk map, and the lines of
// the largest chain (THE SPEED OF LIGHT).  A sample of the clock is 16,384
// adds, about 750 steps of the TSC, so the step of 10 ns moves its p50 by less
// than 0.2%.
constexpr std::uint32_t kClockAdds = 64;
constexpr std::size_t kClockBatch = 256;
constexpr std::uint64_t kChainStride = 32;
constexpr std::size_t kChainLines = 16384;
constexpr std::size_t kChaseWords = kChainLines * 16;
// The steps of one sample of a chain.
constexpr std::size_t kChainBatch = 256;

// The model of the core of luks-a, in cycles (THE SPEED OF LIGHT).  luks-a is
// an EPYC 9754, with Zen 4c cores: the Zen 4 architecture in a denser
// physical form, with an L3 of 16 MB for each complex of eight cores, half of
// the L3 of a complex of Zen 4 (Chips and Cheese, "Testing AMD's Bergamo:
// Zen 4c Spam", 2024-06-22, its first part and the section The Zen 4c CCD).
// Each number of the core comes from the Software Optimization Guide for the
// AMD Zen4 Microarchitecture, AMD 57647 revision 1.00 of 2023-01.  AMD 56665
// is the guide of Zen 3.
//   dispatch        6 macro-ops a cycle, and at most 4 ALU operations in a
//                   window of 6 macro-ops (section 2.9.8, page 30)
//   fusion          a CMP of two registers and the conditional branch after
//                   it are one macro-op (section 2.9.3, page 28)
//   ALU             4 integer pipes with an ALU each, and a branch unit of its
//                   own (section 2.10.2, page 33)
//   loads           3 memory operations a cycle, and all three can be loads
//                   (sections 2.6.2 and 2.12, pages 16 and 39)
//   taken branches  the L1 BTB predicts a direct branch with no bubble, and a
//                   call or a return with one bubble.  One BTB entry can
//                   predict a direct branch and a branch in the line of its
//                   target in one cycle (section 2.8.1.2, page 21).  With the
//                   latency of 1 cycle of the L1 BTB, the front end takes one
//                   taken branch a cycle (Chips and Cheese, "AMD's Zen 4 Part
//                   1: Frontend and Execution Engine", the section Branch
//                   Target Tracking)
//   L1 load-to-use  4 cycles for a base and a displacement, and 5 for a
//                   scaled index (section 2.12, pages 40 and 41)
//   L2 load-to-use  at least 14 cycles, from the L2 of 1 MB (section 2.6.3,
//                   page 18)
//   L3 load-to-use  50 cycles on average (section 2.6.4, page 18).  The L3 of
//                   Zen 4c can take some cycles more (Chips and Cheese, The
//                   Zen 4c CCD)
// The load of a chain step and of a chase step has a scaled index, so the
// load of each level takes the cycle of the index too.  The guide gives no
// latency for a load that takes the data of a store before it.
//
// Each floor below counts the macro-ops of the timed loop of its row in the
// Release code: the first window from rdtsc to rdtscp in each Run::measure.
// The second window is the batch probe of the harness, which a row with a
// batch does not run.
//   hearth inline   8 macro-ops and 2 loads: the dispatch bound, 8 / 6.  GCC
//                   keeps the index of the batch, and one lea gives the
//                   cursor from it
//   inline by 2     14 macro-ops for two reads: 14 / 12 for one read
//   floor row       5 macro-ops and 1 load: the taken branch, 1
//   hearth call     three fetch blocks, one bubble of the call and one of the
//                   return: 5.  The branch of the loop and the call are in the
//                   line of the target of the branch, so the BTB can predict
//                   the two as a pair: 4
//   a chain step    the shift and the add of the next address and the shift
//                   of the read, 3 ALU cycles, and one load-to-use
//   a chase step    the mask of the word, 1 ALU cycle, and one load-to-use
// The floor of a hearth step of the latency form is the chase of the same
// lines, measured in the same run, and the 2 more ALU cycles of the step.
constexpr double kInlineFloorCycles = 8.0 / 6.0;
constexpr double kUnrolledFloorCycles = 14.0 / 12.0;
constexpr double kFloorRowCycles = 1.0;
constexpr double kCallFloorCycles = 4.0;
constexpr double kChainAluCycles = 3.0;
constexpr double kChaseAluCycles = 1.0;
constexpr double kScaledIndexCycles = 1.0;
constexpr double kL1LoadCycles = 4.0 + kScaledIndexCycles;
constexpr double kL2LoadCycles = 14.0 + kScaledIndexCycles;
constexpr double kL3LoadCycles = 50.0 + kScaledIndexCycles;

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: a chain of dependent adds gives the clock of the core")
// kClockAdds dependent adds to the value in one block of code, which the
// compiler cannot fold.  A loop of one add in C++ stays a loop of an add, a
// decrement and a taken branch, and its exit and the step of the batch then
// add some cycles to each kClockAdds cycles.
[[gnu::always_inline]] inline void add_chain(std::uint64_t& value) noexcept {
#if defined(__x86_64__)
    asm volatile(".rept %c1\n\tadd $1, %0\n\t.endr" : "+r"(value) : "i"(kClockAdds));
#else
    asm volatile(".rept %c1\n\tadd %0, %0, #1\n\t.endr" : "+r"(value) : "i"(kClockAdds));
#endif
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// kClockAdds dependent adds: one cycle each on the core (THE SPEED OF LIGHT).
struct ClockChain {
    std::uint64_t value = 0;

    // The chain runs on a local, so the value stays in a register: a field
    // would put a store and a load between two adds.
    [[gnu::always_inline]] void operator()() noexcept {
        std::uint64_t local = value;
        add_chain(local);
        value = local;
    }
};

// The minimum of a read: the address that the table gives, with no load of
// the map (THE SPEED OF LIGHT).
struct FloorRead {
    [[nodiscard, gnu::always_inline]] std::uint32_t operator()(alloc::AddressWord address) const noexcept {
        return static_cast<std::uint32_t>(static_cast<std::uint64_t>(address));
    }
};

// Two reads in one call of the body: the test of the front end of the core,
// with one taken branch for two reads (THE SPEED OF LIGHT).
template <class Read>
struct ReadLoopTwo {
    Read read;
    Picks const& picks;
    std::uint32_t cursor = 0;
    std::uint32_t sum = 0;

    [[gnu::always_inline]] void operator()() noexcept {
        sum += read(pick_at(picks, cursor));
        sum += read(pick_at(picks, cursor + 1));
        cursor += 2;
    }
};

// The latency form: the entry of the chunk of position P holds the next
// position plus one, and the chunk of position P is P * kChainStride, so the
// next address comes from the last owner with no table.
struct OwnerChain {
    alloc::BiasedChunkMap map;
    std::uint64_t base = 0;
    std::uint32_t owner = 1;

    [[gnu::always_inline]] void operator()() noexcept {
        std::uint64_t const chunk = std::uint64_t{owner - 1} * kChainStride;
        owner = static_cast<std::uint16_t>(map.owner_of(alloc::AddressWord{base + (chunk << alloc::kChunkShift)}));
    }
};

// The floor of the latency form: a chase of one word for each line over the
// same count of lines, which is the load-to-use of the level that holds them.
struct ChaseTable {
    core::FixedArray<std::uint32_t, kChaseWords> next{};
};

// Each word holds the index of the word of the next position, so a step is
// one mask, which proves the bound with no compare, and one load.
struct ChaseChain {
    ChaseTable const& table;
    std::uint32_t word = 0;

    [[gnu::always_inline]] void operator()() noexcept {
        word = table.next.at(core::Index<kChaseWords>::check(word & (kChaseWords - 1)).expect("a masked word"));
    }
};

// The cycles of one nanosecond of the core in this run, from the clock chain.
[[nodiscard]] double measure_cycles_per_ns() {
    ClockChain chain{};
    bench::Report const report = bench::Run("clock").batch(kClockBatch).samples(kSamples).measure(chain);
    bench::do_not_optimize(chain.value);
    return report.pct.p50 > 0.0 ? kClockAdds / report.pct.p50 : 0.0;
}

// Writes one line of the speed of light: the cycles of the p50 and the p99,
// the floor of the model, and the ratio of the p50 to it.
void print_light(fixy::TextView name, bench::Report const& measured, double cycles_per_ns, double floor) noexcept {
    auto const centi = [](double value) noexcept { return static_cast<std::uint64_t>(value * 100.0 + 0.5); };
    std::uint64_t const p50 = centi(measured.pct.p50 * cycles_per_ns);
    std::uint64_t const p99 = centi(measured.pct.p99 * cycles_per_ns);
    std::uint64_t const ratio = floor > 0.0 ? centi(measured.pct.p50 * cycles_per_ns / floor) : 0;
    bench::print("  {}  p50={}.{}{} cycles  p99={}.{}{} cycles  floor={}.{}{}  ratio={}.{}{}\n", name, p50 / 100,
                 p50 % 100 / 10, p50 % 10, p99 / 100, p99 % 100 / 10, p99 % 10, centi(floor) / 100,
                 centi(floor) % 100 / 10, centi(floor) % 10, ratio / 100, ratio % 100 / 10, ratio % 10);
}

// A random cycle over LINES positions (Sattolo): the order of the positions
// that the cycle visits.  Complexity: linear in the lines.
void build_cycle(core::FixedArray<std::uint32_t, kChainLines>& order, std::size_t lines) noexcept {
    for (std::size_t position = 0; position < lines; ++position) {
        order.at(core::Index<kChainLines>::check(position).expect("a position below kChainLines")) =
            static_cast<std::uint32_t>(position);
    }
    for (std::size_t last = lines - 1; last > 0; --last) {
        std::size_t const other = draw_of(last + kPicks) % last;
        auto const at = [&order](std::size_t position) noexcept -> std::uint32_t& {
            return order.at(core::Index<kChainLines>::check(position).expect("a position below kChainLines"));
        };
        std::uint32_t const held = at(last);
        at(last) = at(other);
        at(other) = held;
    }
}

// The rows of the speed of light (THE SPEED OF LIGHT): the clock, the floor
// row of the throughput form, and the latency form at three levels with the
// chase floor of each level.
void run_light_rows(bench::Report const& inline_read, bench::Report const& call_read, Picks const& picks,
                    alloc::BiasedChunkMap map) {
    double const cycles_per_ns = measure_cycles_per_ns();
    bench::print("=== the speed of light: {}.{}{} cycles of the core in one ns (the TSC counts 2.2466 GHz)\n",
                 static_cast<std::uint64_t>(cycles_per_ns), static_cast<std::uint64_t>(cycles_per_ns * 10.0) % 10,
                 static_cast<std::uint64_t>(cycles_per_ns * 100.0) % 10);
    bench::Report const floor_read = measure_reads("floor, the address load alone", FloorRead{}, picks);
    print_light("throughput, hearth inline ", inline_read, cycles_per_ns, kInlineFloorCycles);
    print_light("throughput, floor row     ", floor_read, cycles_per_ns, kFloorRowCycles);
    print_light("throughput, hearth call   ", call_read, cycles_per_ns, kCallFloorCycles);
    // The unrolled row times two reads in each call of its body, so its times
    // are halved for one read.
    ReadLoopTwo<InlineRead> two{InlineRead{map}, picks};
    bench::Report unrolled = bench::Run("hearth inline, two reads").batch(kBatch / 2).samples(kSamples).measure(two);
    bench::do_not_optimize(two.sum);
    unrolled.pct.p50 /= 2.0;
    unrolled.pct.p99 /= 2.0;
    print_light("throughput, inline by 2   ", unrolled, cycles_per_ns, kUnrolledFloorCycles);

    alloc::WindowDiagnostic diagnostic{};
    core::Result<alloc::Window, alloc::WindowFull> reserved = alloc::reserve_window(core::to(diagnostic));
    if (reserved.is_err()) fixy::fatal("{}", diagnostic.text());
    alloc::Window window = std::move(reserved).expect("a window that the ladder gave");
    alloc::WindowRecord const record = window.record();
    core::Box<core::FixedArray<std::uint32_t, kChainLines>> order =
        core::mint_box<core::FixedArray<std::uint32_t, kChainLines>>(foundation::effects::testing::test().alloc);
    core::Box<ChaseTable> chase = core::mint_box<ChaseTable>(foundation::effects::testing::test().alloc);
    core::FixedArray<std::size_t, 3> const levels{std::size_t{256}, std::size_t{4096}, kChainLines};
    core::FixedArray<fixy::TextView, 3> const owner_names{
        "latency, hearth, 16 KiB of map", "latency, hearth, 256 KiB of map", "latency, hearth, 1 MiB of map "};
    core::FixedArray<fixy::TextView, 3> const chase_names{
        "latency, chase, 16 KiB       ", "latency, chase, 256 KiB      ", "latency, chase, 1 MiB        "};
    core::FixedArray<double, 3> const load_to_use{kL1LoadCycles, kL2LoadCycles, kL3LoadCycles};
    for (core::Index<3> const level : core::IndexRange<3>{}) {
        std::size_t const wanted = levels.at(level);
        std::size_t const lines =
            wanted * kChainStride <= record.chunk_count() ? wanted : record.chunk_count() / kChainStride;
        build_cycle(*order, lines);
        for (std::size_t step = 0; step < lines; ++step) {
            std::uint32_t const here =
                order->at(core::Index<kChainLines>::check(step).expect("a step below the lines"));
            std::uint32_t const next =
                order->at(core::Index<kChainLines>::check((step + 1) % lines).expect("a step below the lines"));
            window.set_owner(alloc::ChunkIndex{static_cast<std::uint32_t>(here * kChainStride)},
                             alloc::owner_entry_of(alloc::HeapId{static_cast<std::uint16_t>(next + 1)}));
            chase->next.at(core::Index<kChaseWords>::check(std::size_t{here} * 16).expect("a word of the chase")) =
                next * 16;
        }
        OwnerChain owner_chain{record.chunk_map(), static_cast<std::uint64_t>(record.base())};
        bench::Report const owner_read =
            bench::Run(owner_names.at(level)).batch(kChainBatch).samples(kSamples).measure(owner_chain);
        bench::do_not_optimize(owner_chain.owner);
        ChaseChain chase_chain{*chase};
        bench::Report const chase_read =
            bench::Run(chase_names.at(level)).batch(kChainBatch).samples(kSamples).measure(chase_chain);
        bench::do_not_optimize(chase_chain.word);
        // The floor of a hearth step is the measured chase of the same lines
        // and the two ALU cycles that a hearth step has more.
        double const chase_cycles = chase_read.pct.p50 * cycles_per_ns;
        print_light(owner_names.at(level), owner_read, cycles_per_ns, chase_cycles + kChainAluCycles - kChaseAluCycles);
        print_light(chase_names.at(level), chase_read, cycles_per_ns, load_to_use.at(level) + kChaseAluCycles);
    }
}

int run_bench() {
    bench::elevate_priority();
    bench::print("=== alloc_owner_lookup: the metadata of a random block of {} chunks, the mean of one read in a "
                 "window of {} reads\n",
                 kBlocks, kBatch);
    BaseRows const rows = build_base_rows();
    alloc::BiasedChunkMap const map = rows.window.record().chunk_map();
    bench::Report const inline_read = measure_reads("hearth, inline", InlineRead{map}, *rows.picks);
    print_row("hearth, inline", inline_read, resident_mebibytes());
    bench::Report const call_read = measure_reads("hearth, call", CallRead{map}, *rows.picks);
    print_row("hearth, call  ", call_read, resident_mebibytes());
#if defined(CRUCIBLE_BENCH_MIMALLOC)
    bench::Report const mimalloc_read = run_reference<MimallocCalls>("mimalloc 3.7.0 mi_usable_size", "mimalloc      ");
#else
    bench::print("  mimalloc: the pin is not installed, so the bench has no row of it\n");
#endif
#if defined(CRUCIBLE_BENCH_JEMALLOC)
    bench::Report const jemalloc_read =
        run_reference<JemallocCalls>("jemalloc 5.4.0 je_malloc_usable_size", "jemalloc      ");
#else
    bench::print("  jemalloc: the pin is not installed, so the bench has no row of it\n");
#endif
    bench::print("=== the p50 of the base over the p50 of each reference: below 1.00 the base is faster\n");
#if defined(CRUCIBLE_BENCH_MIMALLOC)
    print_ratio("hearth, inline / mimalloc", inline_read, mimalloc_read);
    print_ratio("hearth, call / mimalloc  ", call_read, mimalloc_read);
#endif
#if defined(CRUCIBLE_BENCH_JEMALLOC)
    print_ratio("hearth, inline / jemalloc", inline_read, jemalloc_read);
    print_ratio("hearth, call / jemalloc  ", call_read, jemalloc_read);
#endif
    run_light_rows(inline_read, call_read, *rows.picks, map);
    return 0;
}

}  // namespace

int main() { return run_bench(); }
