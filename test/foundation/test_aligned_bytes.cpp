// Tests of the blocks of foundation/AlignedBytes.h.
//
// mint_aligned_bytes gives an UnwrittenBytes, which has no view.  fill and
// write give the AlignedBytes, and each byte then holds the value of its
// write.  mint_aligned_bytes_zeroed gives an AlignedBytes whose bytes each
// read zero.  The block keeps the alignment of the Layout that the mint got
// at run time, also the alignment of a huge page.  The empty block is the
// default constructor of each state, and a Layout has no zero bytes.  A move
// gives the block to the target.  The doors of a Layout refuse an alignment
// that is not a power of two, so no such request reaches a mint.

#include <foundation/AlignedBytes.h>

#include <foundation/Platform.h>
#include <foundation/alloc/Layout.h>
#include <foundation/core/Array.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

using ::foundation::AlignedBytes;
using ::foundation::mint_aligned_bytes;
using ::foundation::mint_aligned_bytes_zeroed;
using ::foundation::UnwrittenBytes;
using ::foundation::alloc::Alignment;
using ::foundation::alloc::Layout;
using ::foundation::core::ByteCount;

// The layout of bytes bytes at the alignment, which the test gives as a
// layout.
[[nodiscard]] Layout layout_of(ByteCount bytes, Alignment alignment) noexcept {
    return ::foundation::alloc::mint_layout(bytes, alignment).expect("the test gives a layout");
}

// The allocation tag passes the gate of the two mints, and a different tag
// does not.
static_assert(::foundation::CanMintAlignedBytes<::foundation::effects::Alloc>);
static_assert(!::foundation::CanMintAlignedBytes<::foundation::effects::IO>);

// The plain mint gives the write-only state, and the zeroed mint gives the
// readable state.
static_assert(std::is_same_v<decltype(mint_aligned_bytes(::foundation::effects::testing::test().alloc,
                                                         Layout::of<std::uint8_t>())),
                             UnwrittenBytes>);
static_assert(std::is_same_v<decltype(mint_aligned_bytes_zeroed(::foundation::effects::testing::test().alloc,
                                                                Layout::of<std::uint8_t>())),
                             AlignedBytes>);

// True when the first byte of the block is on the alignment Align.
template <std::size_t Align>
[[nodiscard]] bool first_byte_is_aligned(AlignedBytes& block) noexcept {
    return ::foundation::core::address_of(block.view().get(0).expect("the block holds a byte"))
        .template is_aligned<Align>();
}

// The sum of the bytes of a block.  Complexity: O(size()).
[[nodiscard]] std::size_t sum_of(AlignedBytes const& block) noexcept {
    std::size_t total = 0;
    for (std::uint8_t const byte : block.view()) {
        total += byte;
    }
    return total;
}

// The empty block has no byte and no storage, and no Layout has zero bytes.
void test_empty_block() {
    UnwrittenBytes const unwritten{};
    CRUCIBLE_FATAL_INVARIANT(unwritten.size() == 0);
    CRUCIBLE_FATAL_INVARIANT(unwritten.alignment() == 0);

    AlignedBytes const unminted{};
    CRUCIBLE_FATAL_INVARIANT(unminted.size() == 0 && unminted.alignment() == 0);
    CRUCIBLE_FATAL_INVARIANT(unminted.view().size() == 0);

    CRUCIBLE_FATAL_INVARIANT(::foundation::alloc::mint_layout(ByteCount{0}, Alignment{256}).is_err());
}

// The block keeps each alignment of the run time, from below the alignment
// of malloc up to a huge page, from each mint.
void test_alignments() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    AlignedBytes small = mint_aligned_bytes(alloc, layout_of(ByteCount{3}, Alignment{8})).fill(std::uint8_t{0});
    CRUCIBLE_FATAL_INVARIANT(small.size() == 3 && small.alignment() == 8);
    CRUCIBLE_FATAL_INVARIANT(first_byte_is_aligned<8>(small));

    AlignedBytes line = mint_aligned_bytes_zeroed(alloc, layout_of(ByteCount{1000}, Alignment{256}));
    CRUCIBLE_FATAL_INVARIANT(line.size() == 1000 && line.alignment() == 256);
    CRUCIBLE_FATAL_INVARIANT(first_byte_is_aligned<256>(line));

    AlignedBytes huge = mint_aligned_bytes_zeroed(
        alloc, layout_of(ByteCount{::foundation::huge_page_bytes + 1}, Alignment{::foundation::huge_page_bytes}));
    CRUCIBLE_FATAL_INVARIANT(huge.size() == ::foundation::huge_page_bytes + 1);
    CRUCIBLE_FATAL_INVARIANT(first_byte_is_aligned<::foundation::huge_page_bytes>(huge));

    AlignedBytes written = mint_aligned_bytes(alloc, layout_of(ByteCount{5}, Alignment{4096})).fill(std::uint8_t{1});
    CRUCIBLE_FATAL_INVARIANT(first_byte_is_aligned<4096>(written));
}

// The zeroed mint gives zero in each byte, at each alignment, also for a block
// that the allocator maps new and for a block of its free list that an
// earlier block wrote.
void test_zeroed_mint_reads_zero() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    {
        AlignedBytes dirty =
            mint_aligned_bytes(alloc, layout_of(ByteCount{4096}, Alignment{64})).fill(std::uint8_t{0xEE});
        CRUCIBLE_FATAL_INVARIANT(sum_of(dirty) == 4096 * std::size_t{0xEE});
    }
    AlignedBytes const small = mint_aligned_bytes_zeroed(alloc, layout_of(ByteCount{4096}, Alignment{64}));
    CRUCIBLE_FATAL_INVARIANT(sum_of(small) == 0);
    AlignedBytes const odd = mint_aligned_bytes_zeroed(alloc, layout_of(ByteCount{17}, Alignment{8}));
    CRUCIBLE_FATAL_INVARIANT(sum_of(odd) == 0 && odd.view().size() == 17);
    AlignedBytes const large = mint_aligned_bytes_zeroed(
        alloc, layout_of(ByteCount{std::size_t{33} << 20}, Alignment{::foundation::huge_page_bytes}));
    CRUCIBLE_FATAL_INVARIANT(sum_of(large) == 0);
}

// fill writes its value into each byte, and the const View reads them.  A
// write through the View of the AlignedBytes reaches the byte.
void test_fill_reaches_each_byte() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    AlignedBytes block =
        mint_aligned_bytes(alloc, layout_of(ByteCount{4096 + 17}, Alignment{64})).fill(std::uint8_t{0xCD});
    *block.view().get(4096 + 16).expect("the last byte") = 7;
    AlignedBytes const& read_only = block;
    CRUCIBLE_FATAL_INVARIANT(sum_of(read_only) == (4096 + 16) * std::size_t{0xCD} + 7);
    CRUCIBLE_FATAL_INVARIANT(!read_only.view().get(4096 + 17).is_some());
}

// write lends a WriteLease that writes the bytes in order, and the block then
// holds them.
void test_write_in_order() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    UnwrittenBytes unwritten = mint_aligned_bytes(alloc, layout_of(ByteCount{6}, Alignment{16}));
    AlignedBytes const block =
        std::move(unwritten).write([](::foundation::core::WriteLease<std::uint8_t>& lease) noexcept {
            lease.put(std::uint8_t{1});
            lease.put(std::uint8_t{2});
            ::foundation::core::FixedArray<std::uint8_t, 4> const rest{3, 4, 5, 6};
            lease.put_run(rest.view());
        });
    CRUCIBLE_FATAL_INVARIANT(unwritten.size() == 0);
    CRUCIBLE_FATAL_INVARIANT(block.size() == 6 && sum_of(block) == 21);
    CRUCIBLE_FATAL_INVARIANT(*block.view().get(5).expect("the last byte") == 6);
}

// A move gives the block to the target and leaves the source empty.  A move
// assignment gives the old block of the target back first.
void test_move() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    AlignedBytes first = mint_aligned_bytes_zeroed(alloc, layout_of(ByteCount{512}, Alignment{256}));
    *first.view().get(0).expect("the first byte") = 42;
    AlignedBytes second{std::move(first)};
    CRUCIBLE_FATAL_INVARIANT(first.size() == 0 && first.alignment() == 0 && first.view().size() == 0);
    CRUCIBLE_FATAL_INVARIANT(second.size() == 512 && second.alignment() == 256);
    CRUCIBLE_FATAL_INVARIANT(*second.view().get(0).expect("the first byte") == 42);

    AlignedBytes third = mint_aligned_bytes_zeroed(alloc, layout_of(ByteCount{64}, Alignment{64}));
    third = std::move(second);
    CRUCIBLE_FATAL_INVARIANT(second.size() == 0);
    CRUCIBLE_FATAL_INVARIANT(third.size() == 512 && *third.view().get(0).expect("the first byte") == 42);
    third.reset();
    CRUCIBLE_FATAL_INVARIANT(third.size() == 0 && third.view().size() == 0);

    UnwrittenBytes unwritten = mint_aligned_bytes(alloc, layout_of(ByteCount{128}, Alignment{64}));
    UnwrittenBytes taken{std::move(unwritten)};
    CRUCIBLE_FATAL_INVARIANT(unwritten.size() == 0 && taken.size() == 128 && taken.alignment() == 64);
    taken = mint_aligned_bytes(alloc, layout_of(ByteCount{32}, Alignment{32}));
    CRUCIBLE_FATAL_INVARIANT(taken.size() == 32);
}

// The doors of a Layout refuse an alignment that is not a power of two, and
// a size whose round up to the alignment passes the largest object, with an
// error.  So no such request reaches a mint, and the process goes on.
void test_layouts_refuse_bad_requests() {
    CRUCIBLE_FATAL_INVARIANT(::foundation::alloc::mint_layout(ByteCount{64}, Alignment{24}).is_err());
    CRUCIBLE_FATAL_INVARIANT(::foundation::alloc::mint_layout(ByteCount{64}, Alignment{0}).is_err());
    CRUCIBLE_FATAL_INVARIANT(::foundation::alloc::mint_layout(ByteCount{~std::size_t{0}}, Alignment{64}).is_err());
}

}  // namespace

int main() {
    test_empty_block();
    test_alignments();
    test_zeroed_mint_reads_zero();
    test_fill_reaches_each_byte();
    test_write_in_order();
    test_move();
    test_layouts_refuse_bad_requests();

    // A writer that leaves a byte with no write, or that gives a byte past the
    // last one, ends the process.  Each refused call runs in a child through
    // expect_dies of the Check family.
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            (void)mint_aligned_bytes(::foundation::effects::testing::test().alloc,
                                     layout_of(ByteCount{4}, Alignment{4}))
                .write(
                    [](::foundation::core::WriteLease<std::uint8_t>& lease) noexcept { lease.put(std::uint8_t{1}); });
        },
        ::foundation::core::TextView{
            "foundation: fatal: the writer of an UnwrittenBytes left 3 of 4 bytes with no write, and gave 0 bytes past "
            "the last one"});
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            (void)mint_aligned_bytes(::foundation::effects::testing::test().alloc, Layout::of<std::uint8_t>())
                .write([](::foundation::core::WriteLease<std::uint8_t>& lease) noexcept {
                    lease.put(std::uint8_t{1});
                    lease.put(std::uint8_t{2});
                });
        },
        ::foundation::core::TextView{
            "foundation: fatal: the writer of an UnwrittenBytes left 0 of 1 bytes with no write, and gave 1 bytes past "
            "the last one"});
    return 0;
}
