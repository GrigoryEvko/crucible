// Tests of AlignedBytes and mint_aligned_bytes of foundation/AlignedBytes.h.
//
// The block keeps the alignment that the mint got at run time, also the
// alignment of a huge page.  A count of zero is the empty block.  The View
// reaches each byte, a move gives the block to the target, and an alignment
// that is not a power of two ends the process.

#include <foundation/AlignedBytes.h>

#include <foundation/Platform.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using ::foundation::AlignedBytes;
using ::foundation::mint_aligned_bytes;

// The allocation tag passes the gate of the mint, and a different tag does
// not.
static_assert(::foundation::CanMintAlignedBytes<::foundation::effects::Alloc>);
static_assert(!::foundation::CanMintAlignedBytes<::foundation::effects::IO>);

// True when the first byte of the block is on the alignment Alignment.
template <std::size_t Alignment>
[[nodiscard]] bool first_byte_is_aligned(AlignedBytes& block) noexcept {
    return ::foundation::core::address_of(block.view().get(0).expect("the block holds a byte"))
        .template is_aligned<Alignment>();
}

void test_empty_block() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    AlignedBytes const empty = mint_aligned_bytes(alloc, 0, 256);
    CRUCIBLE_FATAL_INVARIANT(empty.size() == 0);
    CRUCIBLE_FATAL_INVARIANT(empty.alignment() == 0);
    CRUCIBLE_FATAL_INVARIANT(empty.view().size() == 0);

    AlignedBytes const unminted{};
    CRUCIBLE_FATAL_INVARIANT(unminted.size() == 0);
    CRUCIBLE_FATAL_INVARIANT(unminted.view().size() == 0);
}

// The block keeps each alignment of the run time, from below the alignment
// of malloc up to a huge page.
void test_alignments() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    AlignedBytes small = mint_aligned_bytes(alloc, 3, 8);
    CRUCIBLE_FATAL_INVARIANT(small.size() == 3 && small.alignment() == 8);
    CRUCIBLE_FATAL_INVARIANT(first_byte_is_aligned<8>(small));

    AlignedBytes line = mint_aligned_bytes(alloc, 1000, 256);
    CRUCIBLE_FATAL_INVARIANT(line.size() == 1000 && line.alignment() == 256);
    CRUCIBLE_FATAL_INVARIANT(first_byte_is_aligned<256>(line));

    AlignedBytes huge = mint_aligned_bytes(alloc, ::foundation::huge_page_bytes + 1, ::foundation::huge_page_bytes);
    CRUCIBLE_FATAL_INVARIANT(huge.size() == ::foundation::huge_page_bytes + 1);
    CRUCIBLE_FATAL_INVARIANT(first_byte_is_aligned<::foundation::huge_page_bytes>(huge));
}

// A fill through the View reaches each byte, and the const View reads them.
void test_view_reaches_each_byte() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    AlignedBytes block = mint_aligned_bytes(alloc, 4096 + 17, 64);
    ::foundation::core::fill(block.view(), std::uint8_t{0xCD});
    *block.view().get(4096 + 16).expect("the last byte") = 7;
    AlignedBytes const& read_only = block;
    std::size_t total = 0;
    for (std::uint8_t const byte : read_only.view()) {
        total += byte;
    }
    CRUCIBLE_FATAL_INVARIANT(total == (4096 + 16) * std::size_t{0xCD} + 7);
    CRUCIBLE_FATAL_INVARIANT(!read_only.view().get(4096 + 17).is_some());
}

// A move gives the block to the target and leaves the source empty.  A move
// assignment gives the old block of the target back first.
void test_move() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    AlignedBytes first = mint_aligned_bytes(alloc, 512, 256);
    *first.view().get(0).expect("the first byte") = 42;
    AlignedBytes second{std::move(first)};
    CRUCIBLE_FATAL_INVARIANT(first.size() == 0 && first.alignment() == 0 && first.view().size() == 0);
    CRUCIBLE_FATAL_INVARIANT(second.size() == 512 && second.alignment() == 256);
    CRUCIBLE_FATAL_INVARIANT(*second.view().get(0).expect("the first byte") == 42);

    AlignedBytes third = mint_aligned_bytes(alloc, 64, 64);
    third = std::move(second);
    CRUCIBLE_FATAL_INVARIANT(second.size() == 0);
    CRUCIBLE_FATAL_INVARIANT(third.size() == 512 && *third.view().get(0).expect("the first byte") == 42);
    third.reset();
    CRUCIBLE_FATAL_INVARIANT(third.size() == 0 && third.view().size() == 0);
}

}  // namespace

int main() {
    test_empty_block();
    test_alignments();
    test_view_reaches_each_byte();
    test_move();

    // An alignment that is not a power of two is a defect of the caller, and
    // the precondition ends the process.  Each refused call runs in a child
    // through expect_dies of the Check family.
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::check::expect_dies(
        test, [] noexcept { (void)mint_aligned_bytes(::foundation::effects::testing::test().alloc, 64, 24); },
        ::foundation::core::TextView{"foundation: contract violation: core::has_single_bit(alignment)"});
    ::fixy::check::expect_dies(
        test, [] noexcept { (void)mint_aligned_bytes(::foundation::effects::testing::test().alloc, 64, 0); },
        ::foundation::core::TextView{"foundation: contract violation: core::has_single_bit(alignment)"});
    // A size whose round up to the alignment wraps ends the process.
    ::fixy::check::expect_dies(
        test,
        [] noexcept { (void)mint_aligned_bytes(::foundation::effects::testing::test().alloc, ~std::size_t{0}, 64); },
        ::foundation::core::TextView{
            "foundation: fatal: 0 prefix bytes and 18446744073709551615 elements of 1 bytes, rounded to "
            "64, do not fit in size_t"});
    return 0;
}
