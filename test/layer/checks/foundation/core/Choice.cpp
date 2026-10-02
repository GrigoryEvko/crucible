// The compile-time checks of foundation/core/Choice.h.

#include <foundation/core/Choice.h>

#include <cstdint>
#include <type_traits>

namespace foundation::core {

namespace detail::choice_checks {

// A payload with an empty value that no valid payload holds: a slot
// number with the largest value as its sentinel.
struct SlotNumber {
    std::uint32_t raw = 0;
};

// A payload that owns something: its move and its destructor are not
// trivial.
struct Owner {
    int handle = 0;
    constexpr Owner() noexcept = default;
    constexpr explicit Owner(int value) noexcept : handle{value} {}
    constexpr Owner(Owner&& other) noexcept : handle{other.handle} { other.handle = 0; }
    Owner(Owner const&) = delete;
    Owner& operator=(Owner const&) = delete;
    Owner& operator=(Owner&&) = delete;
    constexpr ~Owner() {}
};

}  // namespace detail::choice_checks

template <>
struct niche<detail::choice_checks::SlotNumber> {
    [[nodiscard]] static constexpr detail::choice_checks::SlotNumber empty() noexcept {
        return detail::choice_checks::SlotNumber{0xFFFFFFFFu};
    }
    [[nodiscard]] static constexpr bool is_empty(detail::choice_checks::SlotNumber const& slot) noexcept {
        return slot.raw == 0xFFFFFFFFu;
    }
};

namespace detail::choice_checks {

// The three forms and their layout.
static_assert(option_form_of<SlotNumber> == OptionForm::niche);
static_assert(option_form_of<std::uint64_t> == OptionForm::plain);
static_assert(option_form_of<Owner> == OptionForm::owning);
static_assert(sizeof(Option<SlotNumber>) == sizeof(SlotNumber));
static_assert(sizeof(Option<std::uint8_t>) == 2);
static_assert(sizeof(Option<std::uint32_t>) == 8);
static_assert(sizeof(Option<std::uint64_t>) == 16);
static_assert(sizeof(Option<Owner>) == 8);
static_assert(sizeof(OptionCursor<int>) == sizeof(void*));
static_assert(sizeof(ExpectWhy) == 4 * sizeof(void*));

// A niche or plain Option is trivially copyable, so the ABI gives it back
// in registers.  An owning Option is move-only.
static_assert(std::is_trivially_copyable_v<Option<SlotNumber>>);
static_assert(std::is_trivially_copyable_v<Option<std::uint64_t>>);
static_assert(!std::is_trivially_copyable_v<Option<Owner>>);
static_assert(std::is_nothrow_move_constructible_v<Option<Owner>>);
static_assert(!std::is_copy_constructible_v<Option<Owner>>);
static_assert(!std::is_copy_assignable_v<Option<Owner>>);
static_assert(!std::is_move_assignable_v<Option<Owner>>);

// The payload gate.
static_assert(ChoicePayload<int>);
static_assert(ChoicePayload<Owner>);
static_assert(!ChoicePayload<int const>);
static_assert(!ChoicePayload<int volatile>);
static_assert(!ChoicePayload<int&>);
static_assert(!ChoicePayload<int[4]>);
static_assert(!ChoicePayload<void>);
static_assert(!ChoicePayload<NoValue>);

// No dereference and no conversion to bool.
template <class O>
concept Dereferences = requires(O option) { *option; };
template <class O>
concept Arrows = requires(O option) { option.operator->(); };
static_assert(!Dereferences<Option<int>>);
static_assert(!Arrows<Option<int>>);
static_assert(!std::is_convertible_v<Option<int>, bool>);
static_assert(!std::is_constructible_v<bool, Option<int>>);

// A braced list with no elements builds an empty Option, and it does not
// build the marker.
template <class T>
concept BuildsFromEmptyList = requires(void (&sink)(T)) { sink({}); };
static_assert(!BuildsFromEmptyList<NoValue>);
static_assert(BuildsFromEmptyList<Option<int>>);

// The operations in a constant evaluation.
static_assert(Option<int>{}.is_none());
static_assert(Option<int>{none}.is_none());
static_assert(Option<int>::some(7).is_some());
static_assert(Option<int>::some(7).consume() == 7);
static_assert(Option<int>::some(9).expect("a constant nine") == 9);
static_assert(Option<SlotNumber>::some(SlotNumber{3}).consume().raw == 3);
static_assert(Option<SlotNumber>{}.is_none());

[[nodiscard]] consteval int loop_turns(Option<int> option) noexcept {
    int turns = 0;
    for (int& value : option) {
        turns += value;
    }
    return turns;
}
static_assert(loop_turns(Option<int>::some(5)) == 5);
static_assert(loop_turns(none) == 0);

[[nodiscard]] consteval bool moved_owner_leaves_source_empty() noexcept {
    Option<Owner> source = Option<Owner>::some(Owner{4});
    Option<Owner> target{static_cast<Option<Owner>&&>(source)};
    return source.is_none() && target.is_some() && static_cast<Option<Owner>&&>(target).consume().handle == 4;
}
static_assert(moved_owner_leaves_source_empty());

[[nodiscard]] consteval bool consume_leaves_source_empty() noexcept {
    Option<int> source = Option<int>::some(1);
    int const taken = static_cast<Option<int>&&>(source).consume();
    return taken == 1 && source.is_none();
}
static_assert(consume_leaves_source_empty());

}  // namespace detail::choice_checks

}  // namespace foundation::core
