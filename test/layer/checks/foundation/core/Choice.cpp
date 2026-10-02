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
static_assert(Option<int>::some(9).expect("a constant nine") == 9);
static_assert(Option<SlotNumber>::some(SlotNumber{3}).expect("a constant slot").raw == 3);
static_assert(Option<SlotNumber>{}.is_none());

// No unwrap without a reason: the fatal unwrap is expect(), and a search
// for `expect(` finds each one.
template <class O>
concept Consumes = requires(O option) { static_cast<O&&>(option).consume(); };
static_assert(!Consumes<Option<int>>);
static_assert(!Consumes<Option<Owner>>);

// The niche refuses its empty value at a constant evaluation too: the
// fatal exit is not a constant expression, so the build stops.  The probe
// is a variable template and not a concept, because it gates nothing.
template <std::uint32_t Raw>
constexpr bool builds_slot =
    requires { typename std::integral_constant<bool, Option<SlotNumber>::some(SlotNumber{Raw}).is_some()>; };
static_assert(builds_slot<3>);
static_assert(!builds_slot<0xFFFFFFFFu>);

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
    return source.is_none() && target.is_some()
        && static_cast<Option<Owner>&&>(target).expect("the target took the owner").handle == 4;
}
static_assert(moved_owner_leaves_source_empty());

[[nodiscard]] consteval bool expect_leaves_source_empty() noexcept {
    Option<int> source = Option<int>::some(1);
    int const taken = static_cast<Option<int>&&>(source).expect("the source was built with a value");
    return taken == 1 && source.is_none();
}
static_assert(expect_leaves_source_empty());

// value_or gives the payload or the fallback.
static_assert(Option<int>::some(4).value_or(9) == 4);
static_assert(Option<int>{}.value_or(9) == 9);
static_assert(Option<SlotNumber>{}.value_or(SlotNumber{8}).raw == 8);

// The total match: each arm gives the same type, and the empty arm runs
// for an empty Option.
static_assert(Option<int>::some(6).match([](int value) noexcept { return value * 2; }, [] noexcept { return -1; })
              == 12);
static_assert(Option<int>{}.match([](int value) noexcept { return value * 2; }, [] noexcept { return -1; }) == -1);

[[nodiscard]] consteval bool borrowed_match_keeps_the_payload() noexcept {
    Option<int> const holder = Option<int>::some(3);
    int const seen = holder.match([](int const& value) noexcept { return value; }, [] noexcept { return 0; });
    return seen == 3 && holder.is_some();
}
static_assert(borrowed_match_keeps_the_payload());

[[nodiscard]] consteval bool moved_match_empties_the_source() noexcept {
    Option<Owner> source = Option<Owner>::some(Owner{5});
    int const seen = static_cast<Option<Owner>&&>(source).match([](Owner&& owner) noexcept { return owner.handle; },
                                                                [] noexcept { return 0; });
    return seen == 5 && source.is_none();
}
static_assert(moved_match_empties_the_source());

// A match needs both arms, and the arms must give the same type.
template <class O>
concept MatchesWithOneArm = requires(O option) { static_cast<O&&>(option).match([](int) noexcept { return 0; }); };
template <class O>
concept MatchesWithTwoTypes =
    requires(O option) { static_cast<O&&>(option).match([](int) noexcept { return 0; }, [] noexcept { return 0L; }); };
static_assert(!MatchesWithOneArm<Option<int>>);
static_assert(!MatchesWithTwoTypes<Option<int>>);

// ── Result ────────────────────────────────────────────────────────────

enum class Code : std::uint8_t {
    full,
    closed
};

struct WideError {
    std::uint64_t first = 0;
    std::uint64_t second = 0;
};

struct TooWideError {
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    std::uint64_t third = 0;
};

struct CountedError {
    int count = 0;
    CountedError() = default;
    CountedError(CountedError const& other) noexcept : count{other.count + 1} {}
};

// The error gate: a scoped enum, or a trivially copyable class of at most
// 16 bytes.
static_assert(ErrorValue<Code> && ErrorValue<WideError> && ErrorValue<Unit>);
static_assert(!ErrorValue<int>);
static_assert(!ErrorValue<bool>);
static_assert(!ErrorValue<TooWideError>);
static_assert(!ErrorValue<CountedError>);
static_assert(!ErrorValue<Code const>);

// The layout: the value or the error, and one flag.  A Result of trivially
// copyable parts is trivially copyable, so the ABI gives a small one back
// in registers.
static_assert(sizeof(Result<Unit, Code>) == 2);
static_assert(sizeof(Result<std::uint32_t, Code>) == 8);
static_assert(sizeof(Result<std::uint64_t, WideError>) == 24);
static_assert(std::is_trivially_copyable_v<Result<std::uint64_t, Code>>);
static_assert(!std::is_trivially_copyable_v<Result<Owner, Code>>);
static_assert(std::is_nothrow_move_constructible_v<Result<Owner, Code>>);
static_assert(!std::is_copy_constructible_v<Result<Owner, Code>>);

// A value and an Err convert, and nothing else does.  A Result of an Err
// would make the two conversions meet, so it is refused.
static_assert(std::is_convertible_v<int, Result<int, Code>>);
static_assert(std::is_convertible_v<Err<Code>, Result<int, Code>>);
static_assert(!std::is_convertible_v<Code, Result<int, Code>>);
static_assert(!std::is_convertible_v<Result<int, Code>, bool>);
template <class T, class E>
concept FormsResult = requires { typename Result<T, E>; };
static_assert(FormsResult<int, Code>);
static_assert(!FormsResult<Err<Code>, Code>);
static_assert(!FormsResult<int, int>);

// The roads in a constant evaluation.
[[nodiscard]] consteval Result<int, Code> checked_half(int value) noexcept {
    if (value % 2 != 0) return err(Code::closed);
    return value / 2;
}
static_assert(checked_half(8).is_ok() && checked_half(7).is_err());
static_assert(checked_half(8).expect("an even value has a half") == 4);
static_assert(checked_half(7).value_or(-1) == -1);
static_assert(checked_half(7).err().expect("an odd value is refused") == Code::closed);
static_assert(checked_half(8).ok().expect("an even value has a half") == 4);
static_assert(checked_half(6).match([](int value) noexcept { return value; }, [](Code) noexcept { return -1; }) == 3);

// The error of an expect() is not a constant expression, so the build
// stops.  The probe is a variable template and not a concept, because it
// gates nothing.
template <int Value>
constexpr bool expects_half = requires { typename std::integral_constant<int, checked_half(Value).expect("even")>; };
static_assert(expects_half<4>);
static_assert(!expects_half<5>);

// A match of a Result needs both arms.
template <class R>
constexpr bool matches_with_one_arm =
    requires(R result) { static_cast<R&&>(result).match([](int) noexcept { return 0; }); };
static_assert(!matches_with_one_arm<Result<int, Code>>);

}  // namespace detail::choice_checks

}  // namespace foundation::core
