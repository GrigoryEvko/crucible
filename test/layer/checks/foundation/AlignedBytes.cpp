// The compile-time checks of foundation/AlignedBytes.h.

#include <foundation/AlignedBytes.h>

#include <cstddef>
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

// A constant evaluation builds an empty block and destroys it, so a class
// with an empty block member is a literal type.
consteval bool empty_block_lives_in_a_constant_evaluation() {
    AlignedBytes block{};
    block.reset();
    return true;
}
static_assert(empty_block_lives_in_a_constant_evaluation());

}  // namespace detail::aligned_bytes_self_test

}  // namespace foundation
