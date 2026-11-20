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
        chunks.at(BlockIndex::check(taken).expect("a block lies below kBlocks")) = record.address_of(chunk);
        ++taken;
    }
    core::Box<Picks> picks = core::mint_box<Picks>(foundation::effects::testing::test().alloc);
    for (PickIndex const pick : core::IndexRange<kPicks>{}) {
        std::uint32_t const word = draw_of(pick.raw());
        std::uint64_t const base =
            static_cast<std::uint64_t>(chunks.at(BlockIndex::check(word & (kBlocks - 1)).expect("a masked block")));
        std::uint64_t const offset = ((word >> 12) & (alloc::kChunkBytes / 16 - 1)) * 16;
        picks->addresses.at(pick) = alloc::AddressWord{base + offset};
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
        blocks->addresses.at(PickIndex::check(block).expect("a block lies below kPicks")) = first;
    }
    core::Box<Picks> picks = core::mint_box<Picks>(foundation::effects::testing::test().alloc);
    for (PickIndex const pick : core::IndexRange<kPicks>{}) {
        picks->addresses.at(pick) = blocks->addresses.at(
            PickIndex::check(draw_of(pick.raw()) & (kBlocks - 1)).expect("a masked block lies below kPicks"));
    }
    bench::Report report = measure_reads(name, Calls{}, *picks);
    print_row(label, report, resident_mebibytes());
    for (std::size_t block = 0; block < kBlocks; ++block) {
        Calls::release(blocks->addresses.at(PickIndex::check(block).expect("a block lies below kPicks")));
    }
    return report;
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
    return 0;
}

}  // namespace

int main() { return run_bench(); }
