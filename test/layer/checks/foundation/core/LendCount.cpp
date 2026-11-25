// The compile-time checks of foundation/core/LendCount.h.

#include <foundation/core/LendCount.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core::detail::lend_count_checks {

// The count takes the bytes of its word and no other byte.  The owner core
// copies the state of an owner by its bytes, so the count is trivially copyable.
static_assert(sizeof(LendCount<unsigned char>) == 1 && alignof(LendCount<unsigned char>) == 1);
static_assert(sizeof(LendCount<std::uint32_t>) == 4 && alignof(LendCount<std::uint32_t>) == 4);
static_assert(std::is_trivially_copyable_v<LendCount<unsigned char>>
              && std::is_trivially_copyable_v<LendCount<std::uint32_t>>);

// A word is an unsigned integer that is no bool, and no number is a count.
static_assert(LendWord<unsigned char> && LendWord<std::uint16_t> && LendWord<std::uint32_t> && LendWord<std::size_t>);
static_assert(!LendWord<int> && !LendWord<bool> && !LendWord<float> && !LendWord<unsigned char const*>);
static_assert(!std::is_convertible_v<std::uint32_t, LendCount<std::uint32_t>>
              && !std::is_constructible_v<LendCount<std::uint32_t>, std::uint32_t>
              && !std::is_aggregate_v<LendCount<std::uint32_t>>);

// A lend comes from its count alone.  It does not copy and does not move, and
// its count is no temporary.
using Lend = LendCount<std::uint32_t>::Lend;
static_assert(!std::is_default_constructible_v<Lend> && !std::is_constructible_v<Lend, std::uint32_t&>);
static_assert(!std::is_copy_constructible_v<Lend> && !std::is_move_constructible_v<Lend>);
static_assert(!std::is_copy_assignable_v<Lend> && !std::is_move_assignable_v<Lend>);
template <class Count>
concept Lends = requires(Count&& count) { static_cast<Count&&>(count).lend(); };
static_assert(Lends<LendCount<std::uint32_t>&> && !Lends<LendCount<std::uint32_t> const&>);
static_assert(!Lends<LendCount<std::uint32_t>>);

// The lends nest, and the count is zero after the last one ends.
[[nodiscard]] consteval bool counts_nested_lends() noexcept {
    LendCount<unsigned char> count{};
    if (count.is_lent()) return false;
    {
        [[maybe_unused]] auto const outer = count.lend();
        {
            [[maybe_unused]] auto const inner = count.lend();
            if (!count.is_lent()) return false;
        }
        if (!count.is_lent()) return false;
    }
    return !count.is_lent();
}
static_assert(counts_nested_lends());

}  // namespace foundation::core::detail::lend_count_checks
