// The compile-time checks of foundation/AlignedBytes.h.

#include <foundation/AlignedBytes.h>

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation {

namespace detail::aligned_bytes_self_test {

// The arithmetic of an aligned allocation: a prefix and the elements, rounded
// up to the alignment.
static_assert(aligned_allocation_bytes<64>(16, 16, 8) == 192, "16 prefix bytes and 16 slots of 8 bytes round to 192");
static_assert(aligned_allocation_bytes<16>(0, 0, 8) == 0);
static_assert(aligned_allocation_bytes<16>(0, 3, 4) == 16);
static_assert(huge_page_bytes == std::size_t{1} << 21);

// A size that wraps is not a constant expression: the abort is not constexpr.
template <std::size_t Prefix, std::size_t Count, std::size_t ElementBytes>
concept has_constant_size = requires {
    typename std::integral_constant<std::size_t, aligned_allocation_bytes<64>(Prefix, Count, ElementBytes)>;
};
inline constexpr std::size_t max_size = ~std::size_t{0};
static_assert(has_constant_size<0, 1, 8>);
static_assert(!has_constant_size<0, (std::size_t{1} << 62), 8>, "count * element bytes wraps");
static_assert(!has_constant_size<17, (std::size_t{1} << 60) - 1, 16>, "prefix + element bytes wraps");
static_assert(!has_constant_size<0, max_size, 1>, "the round up to the alignment wraps");

// An AlignedBytes holds its first byte, its count and its alignment.  It is
// move-only, and only the allocation capability passes the gate of its mint.
static_assert(sizeof(AlignedBytes) == 3 * sizeof(std::size_t));
static_assert(!std::is_copy_constructible_v<AlignedBytes> && !std::is_copy_assignable_v<AlignedBytes>);
static_assert(std::is_nothrow_move_constructible_v<AlignedBytes> && std::is_nothrow_move_assignable_v<AlignedBytes>);
static_assert(CanMintAlignedBytes<::foundation::core::Alloc>);
static_assert(!CanMintAlignedBytes<int>, "a tag other than the allocation capability fails the gate");
static_assert(aligned_block_bytes(1000, 256) == 1024 && aligned_block_bytes(1024, 256) == 1024);
static_assert(aligned_block_bytes(3, 8) == 8);

// The storage of a block holds alignment - 1 bytes past the block, so the
// first address at the alignment in it starts the block.
// The largest block that rounds up with no wrap leaves room for the
// alignment - 1 bytes, so the sum does not wrap, and a block that wraps in
// the round up is no constant expression.
static_assert(stored_block_bytes(1000, 256) == 1024 + 255 && stored_block_bytes(3, 1) == 3);
static_assert(stored_block_bytes(max_size - 63, 64) == max_size);
template <std::size_t Bytes, std::size_t Alignment>
concept has_constant_stored_size =
    requires { typename std::integral_constant<std::size_t, stored_block_bytes(Bytes, Alignment)>; };
static_assert(has_constant_stored_size<64, 64> && !has_constant_stored_size<max_size - 62, 64>,
              "the round up to the alignment wraps");

// An UnwrittenBytes has the fields of an AlignedBytes and moves in the same
// way, but it gives no view, and a door that writes its bytes takes an
// rvalue.
template <class Block>
concept lends_view = requires(Block& block) { block.view(); };
template <class Block>
concept fills_named = requires(Block& block) { block.fill(std::uint8_t{0}); };
template <class Block>
concept fills_moved = requires(Block& block) {
    { static_cast<Block&&>(block).fill(std::uint8_t{0}) } -> std::same_as<AlignedBytes>;
};
static_assert(sizeof(UnwrittenBytes) == sizeof(AlignedBytes));
static_assert(!std::is_copy_constructible_v<UnwrittenBytes> && !std::is_copy_assignable_v<UnwrittenBytes>);
static_assert(std::is_nothrow_move_constructible_v<UnwrittenBytes>);
static_assert(!lends_view<UnwrittenBytes> && !lends_view<UnwrittenBytes const> && lends_view<AlignedBytes>);
static_assert(!fills_named<UnwrittenBytes> && fills_moved<UnwrittenBytes>);

// The lease of a write is a local of the door, so the door refuses a writer
// that can store a borrow outside the call, such as a writer with a reference
// to an Option of a View, and admits the same writer with a reference to a
// count (StoresNoBorrowOutside of foundation/core/CaptureStore.h).
template <class Kept>
struct WritesAndKeeps final {
    Kept& kept;
    void operator()(core::WriteLease<std::uint8_t>& lease) const noexcept { static_cast<void>(lease.remaining()); }
};
template <class Writer>
concept writes_moved = requires(UnwrittenBytes& block, Writer writer) {
    { static_cast<UnwrittenBytes&&>(block).write(writer) } -> std::same_as<AlignedBytes>;
};
static_assert(!writes_moved<WritesAndKeeps<core::Option<core::View<int const>>>>);
static_assert(writes_moved<WritesAndKeeps<int>>);

// A constant evaluation builds an empty block and destroys it, so a class
// with an empty block member is a literal type.
consteval bool empty_block_lives_in_a_constant_evaluation() {
    AlignedBytes block{};
    block.reset();
    [[maybe_unused]] UnwrittenBytes const unwritten{};
    return true;
}
static_assert(empty_block_lives_in_a_constant_evaluation());

}  // namespace detail::aligned_bytes_self_test

}  // namespace foundation
