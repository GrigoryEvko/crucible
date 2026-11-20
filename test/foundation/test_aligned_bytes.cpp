// Tests of the blocks of foundation/AlignedBytes.h.
//
// mint_aligned_bytes gives an UnwrittenBytes, which has no view.  fill and
// write give the AlignedBytes, and each byte then holds the value of its
// write.  mint_aligned_bytes_zeroed gives an AlignedBytes whose bytes each
// read zero.  The block keeps the alignment that the mint got at run time,
// also the alignment of a huge page.  A count of zero is the empty block.  A
// move gives the block to the target, and an alignment that is not a power
// of two ends the process.

#include <foundation/AlignedBytes.h>

#include <foundation/Platform.h>
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

// The allocation tag passes the gate of the two mints, and a different tag
// does not.
static_assert(::foundation::CanMintAlignedBytes<::foundation::effects::Alloc>);
static_assert(!::foundation::CanMintAlignedBytes<::foundation::effects::IO>);

// The plain mint gives the write-only state, and the zeroed mint gives the
// readable state.
static_assert(
    std::is_same_v<decltype(mint_aligned_bytes(::foundation::effects::testing::test().alloc, 1, 1)), UnwrittenBytes>);
static_assert(std::is_same_v<decltype(mint_aligned_bytes_zeroed(::foundation::effects::testing::test().alloc, 1, 1)),
                             AlignedBytes>);

// True when the first byte of the block is on the alignment Alignment.
template <std::size_t Alignment>
[[nodiscard]] bool first_byte_is_aligned(AlignedBytes& block) noexcept {
    return ::foundation::core::address_of(block.view().get(0).expect("the block holds a byte"))
        .template is_aligned<Alignment>();
}

// The sum of the bytes of a block.  Complexity: O(size()).
[[nodiscard]] std::size_t sum_of(AlignedBytes const& block) noexcept {
    std::size_t total = 0;
    for (std::uint8_t const byte : block.view()) {
        total += byte;
    }
    return total;
}

void test_empty_block() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    UnwrittenBytes const unwritten = mint_aligned_bytes(alloc, 0, 256);
    CRUCIBLE_FATAL_INVARIANT(unwritten.size() == 0);
    CRUCIBLE_FATAL_INVARIANT(unwritten.alignment() == 0);

    AlignedBytes const zeroed = mint_aligned_bytes_zeroed(alloc, 0, 256);
    CRUCIBLE_FATAL_INVARIANT(zeroed.size() == 0 && zeroed.alignment() == 0);
    CRUCIBLE_FATAL_INVARIANT(zeroed.view().size() == 0);

    AlignedBytes const filled = mint_aligned_bytes(alloc, 0, 64).fill(std::uint8_t{3});
    CRUCIBLE_FATAL_INVARIANT(filled.size() == 0 && filled.view().size() == 0);

    AlignedBytes const unminted{};
    CRUCIBLE_FATAL_INVARIANT(unminted.size() == 0);
    CRUCIBLE_FATAL_INVARIANT(unminted.view().size() == 0);
}

// The block keeps each alignment of the run time, from below the alignment
// of malloc up to a huge page, from each mint.
void test_alignments() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    AlignedBytes small = mint_aligned_bytes(alloc, 3, 8).fill(std::uint8_t{0});
    CRUCIBLE_FATAL_INVARIANT(small.size() == 3 && small.alignment() == 8);
    CRUCIBLE_FATAL_INVARIANT(first_byte_is_aligned<8>(small));

    AlignedBytes line = mint_aligned_bytes_zeroed(alloc, 1000, 256);
    CRUCIBLE_FATAL_INVARIANT(line.size() == 1000 && line.alignment() == 256);
    CRUCIBLE_FATAL_INVARIANT(first_byte_is_aligned<256>(line));

    AlignedBytes huge =
        mint_aligned_bytes_zeroed(alloc, ::foundation::huge_page_bytes + 1, ::foundation::huge_page_bytes);
    CRUCIBLE_FATAL_INVARIANT(huge.size() == ::foundation::huge_page_bytes + 1);
    CRUCIBLE_FATAL_INVARIANT(first_byte_is_aligned<::foundation::huge_page_bytes>(huge));

    AlignedBytes written = mint_aligned_bytes(alloc, 5, 4096).fill(std::uint8_t{1});
    CRUCIBLE_FATAL_INVARIANT(first_byte_is_aligned<4096>(written));
}

// The zeroed mint gives zero in each byte, at each alignment, also for a block
// that the allocator maps new and for a block of its free list that an
// earlier block wrote.
void test_zeroed_mint_reads_zero() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    {
        AlignedBytes dirty = mint_aligned_bytes(alloc, 4096, 64).fill(std::uint8_t{0xEE});
        CRUCIBLE_FATAL_INVARIANT(sum_of(dirty) == 4096 * std::size_t{0xEE});
    }
    AlignedBytes const small = mint_aligned_bytes_zeroed(alloc, 4096, 64);
    CRUCIBLE_FATAL_INVARIANT(sum_of(small) == 0);
    AlignedBytes const odd = mint_aligned_bytes_zeroed(alloc, 17, 8);
    CRUCIBLE_FATAL_INVARIANT(sum_of(odd) == 0 && odd.view().size() == 17);
    AlignedBytes const large = mint_aligned_bytes_zeroed(alloc, std::size_t{33} << 20, ::foundation::huge_page_bytes);
    CRUCIBLE_FATAL_INVARIANT(sum_of(large) == 0);
}

// fill writes its value into each byte, and the const View reads them.  A
// write through the View of the AlignedBytes reaches the byte.
void test_fill_reaches_each_byte() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    AlignedBytes block = mint_aligned_bytes(alloc, 4096 + 17, 64).fill(std::uint8_t{0xCD});
    *block.view().get(4096 + 16).expect("the last byte") = 7;
    AlignedBytes const& read_only = block;
    CRUCIBLE_FATAL_INVARIANT(sum_of(read_only) == (4096 + 16) * std::size_t{0xCD} + 7);
    CRUCIBLE_FATAL_INVARIANT(!read_only.view().get(4096 + 17).is_some());
}

// write lends a WriteLease that writes the bytes in order, and the block then
// holds them.
void test_write_in_order() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    UnwrittenBytes unwritten = mint_aligned_bytes(alloc, 6, 16);
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
    AlignedBytes first = mint_aligned_bytes_zeroed(alloc, 512, 256);
    *first.view().get(0).expect("the first byte") = 42;
    AlignedBytes second{std::move(first)};
    CRUCIBLE_FATAL_INVARIANT(first.size() == 0 && first.alignment() == 0 && first.view().size() == 0);
    CRUCIBLE_FATAL_INVARIANT(second.size() == 512 && second.alignment() == 256);
    CRUCIBLE_FATAL_INVARIANT(*second.view().get(0).expect("the first byte") == 42);

    AlignedBytes third = mint_aligned_bytes_zeroed(alloc, 64, 64);
    third = std::move(second);
    CRUCIBLE_FATAL_INVARIANT(second.size() == 0);
    CRUCIBLE_FATAL_INVARIANT(third.size() == 512 && *third.view().get(0).expect("the first byte") == 42);
    third.reset();
    CRUCIBLE_FATAL_INVARIANT(third.size() == 0 && third.view().size() == 0);

    UnwrittenBytes unwritten = mint_aligned_bytes(alloc, 128, 64);
    UnwrittenBytes taken{std::move(unwritten)};
    CRUCIBLE_FATAL_INVARIANT(unwritten.size() == 0 && taken.size() == 128 && taken.alignment() == 64);
    taken = mint_aligned_bytes(alloc, 32, 32);
    CRUCIBLE_FATAL_INVARIANT(taken.size() == 32);
}

}  // namespace

int main() {
    test_empty_block();
    test_alignments();
    test_zeroed_mint_reads_zero();
    test_fill_reaches_each_byte();
    test_write_in_order();
    test_move();

    // An alignment that is not a power of two is a defect of the caller, and
    // the precondition ends the process.  Each refused call runs in a child
    // through expect_dies of the Check family.
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::check::expect_dies(
        test, [] noexcept { (void)mint_aligned_bytes(::foundation::effects::testing::test().alloc, 64, 24); },
        ::foundation::core::TextView{"foundation: contract violation: core::has_single_bit(alignment)"});
    ::fixy::check::expect_dies(
        test, [] noexcept { (void)mint_aligned_bytes_zeroed(::foundation::effects::testing::test().alloc, 64, 0); },
        ::foundation::core::TextView{"foundation: contract violation: core::has_single_bit(alignment)"});
    // A size whose round up to the alignment wraps ends the process.
    ::fixy::check::expect_dies(
        test,
        [] noexcept { (void)mint_aligned_bytes(::foundation::effects::testing::test().alloc, ~std::size_t{0}, 64); },
        ::foundation::core::TextView{
            "foundation: fatal: 0 prefix bytes and 18446744073709551615 elements of 1 bytes, rounded to "
            "64, do not fit in size_t"});
    // A writer that leaves a byte with no write, or that gives a byte past the
    // last one, ends the process.
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            (void)mint_aligned_bytes(::foundation::effects::testing::test().alloc, 4, 4)
                .write(
                    [](::foundation::core::WriteLease<std::uint8_t>& lease) noexcept { lease.put(std::uint8_t{1}); });
        },
        ::foundation::core::TextView{
            "foundation: fatal: the writer of an UnwrittenBytes left 3 of 4 bytes with no write, and gave 0 bytes past "
            "the last one"});
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            (void)mint_aligned_bytes(::foundation::effects::testing::test().alloc, 1, 1)
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
