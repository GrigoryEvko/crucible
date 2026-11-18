// The compile-time checks of foundation/core/Full.h.

#include <foundation/core/Full.h>

#include <type_traits>
#include <utility>

namespace foundation::core::detail::full_checks {

// A linear value: it moves, it has no copy, and the seal makes its
// assignment user-provided, so it refuses a lifetime start over bytes.
struct[[= ::foundation::linear::linear{}]] Coin final {
    int value = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr explicit Coin(int held) noexcept : value{held} {}
    constexpr Coin(Coin&& other) noexcept : value{other.value} {}
    constexpr Coin& operator=(Coin&& other) noexcept {
        value = other.value;
        return *this;
    }
    Coin(Coin const&) = delete("a coin moves and does not copy");
    Coin& operator=(Coin const&) = delete("a coin moves and does not copy");
};

// The Full of a value that is not linear is empty, and two compare equal.
static_assert(std::is_empty_v<Full<int>> && std::is_trivially_copyable_v<Full<int>>);
static_assert(std::is_empty_v<Full<char>> && Full<char>{} == Full<char>{});

// The Full of a linear value holds it, is linear itself, moves and does not
// copy, and gives the value back through take() of an rvalue only.
static_assert(::foundation::linear::is_linear_v<Full<Coin>> && !std::is_empty_v<Full<Coin>>);
static_assert(std::is_nothrow_move_constructible_v<Full<Coin>> && !std::is_copy_constructible_v<Full<Coin>>);
static_assert(!std::is_trivially_copyable_v<Full<Coin>>);

template <class Refusal>
concept TakesFromLvalue = requires(Refusal& refusal) { refusal.take(); };
static_assert(!TakesFromLvalue<Full<Coin>>);

[[nodiscard]] consteval int gives_the_value_back() noexcept {
    Full<Coin> refusal{Coin{7}};
    Coin back = std::move(refusal).take();
    int const value = back.value;
    ::foundation::linear::drop(std::move(back));
    return value;
}
static_assert(gives_the_value_back() == 7);

}  // namespace foundation::core::detail::full_checks
