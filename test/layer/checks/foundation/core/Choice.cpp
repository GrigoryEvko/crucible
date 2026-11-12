// The compile-time checks of foundation/core/Choice.h.

#include <foundation/core/Choice.h>

#include <foundation/Lifetime.h>
#include <foundation/Linear.h>
#include <foundation/core/Verbs.h>

#include <cstdint>
#include <type_traits>
#include <utility>

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
    [[nodiscard]] static constexpr detail::choice_checks::SlotNumber
    empty(niche_key<detail::choice_checks::SlotNumber> const&) noexcept {
        return detail::choice_checks::SlotNumber{0xFFFFFFFFu};
    }
    [[nodiscard]] static constexpr bool is_empty(niche_key<detail::choice_checks::SlotNumber> const&,
                                                 detail::choice_checks::SlotNumber const& slot) noexcept {
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

// The niche gate: a declared niche is well formed, or the payload gate
// refuses the payload.  A class can give the niche through its own members.
struct MemberSlot {
    std::uint32_t raw = 0;
    [[nodiscard]] static constexpr MemberSlot niche_empty(niche_key<MemberSlot> const&) noexcept {
        return MemberSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(niche_key<MemberSlot> const&) const noexcept {
        return raw == 0xFFFFFFFFu;
    }
};

struct HalfMemberSlot {
    std::uint32_t raw = 0;
    [[nodiscard]] static constexpr HalfMemberSlot niche_empty(niche_key<HalfMemberSlot> const&) noexcept {
        return HalfMemberSlot{0xFFFFFFFFu};
    }
};

// A niche whose empty value is_empty does not see.
struct BlindSlot {
    std::uint32_t raw = 0;
    [[nodiscard]] static constexpr BlindSlot niche_empty(niche_key<BlindSlot> const&) noexcept {
        return BlindSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(niche_key<BlindSlot> const&) const noexcept { return raw == 0; }
};

// A niche whose members take no key.  Each caller could then get the empty
// value, so the gate refuses it.
struct KeylessSlot {
    std::uint32_t raw = 0;
    [[nodiscard]] static constexpr KeylessSlot niche_empty() noexcept { return KeylessSlot{0xFFFFFFFFu}; }
    [[nodiscard]] constexpr bool is_niche_empty() const noexcept { return raw == 0xFFFFFFFFu; }
};

// A niche whose members take the key of a different type.
struct BorrowedKeySlot {
    std::uint32_t raw = 0;
    [[nodiscard]] static constexpr BorrowedKeySlot niche_empty(niche_key<MemberSlot> const&) noexcept {
        return BorrowedKeySlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(niche_key<MemberSlot> const&) const noexcept {
        return raw == 0xFFFFFFFFu;
    }
};

static_assert(WellFormedNiche<int> && option_form_of<int> == OptionForm::plain);
static_assert(WellFormedNiche<SlotNumber> && option_form_of<SlotNumber> == OptionForm::niche);
static_assert(WellFormedNiche<MemberSlot>);
static_assert(!WellFormedNiche<HalfMemberSlot> && !ChoicePayload<HalfMemberSlot>);
static_assert(!WellFormedNiche<BlindSlot> && !ChoicePayload<BlindSlot>);
static_assert(!WellFormedNiche<KeylessSlot> && !ChoicePayload<KeylessSlot>);
static_assert(!WellFormedNiche<BorrowedKeySlot> && !ChoicePayload<BorrowedKeySlot>);

// Only the reader of a niche builds its key: the constructors are private,
// the key is not trivially copyable and has no trivial constructor, and an
// assignment of a key is deleted.  So no caller, no std::bit_cast and no
// lifetime start over bytes makes a key, and no caller reads an empty value.
// The fixtures neg_core_niche_* show each refusal of a call.
static_assert(!std::is_default_constructible_v<niche_key<SlotNumber>>);
static_assert(!std::is_copy_constructible_v<niche_key<SlotNumber>>);
static_assert(!std::is_copy_assignable_v<niche_key<SlotNumber>>);
static_assert(!std::is_trivially_copyable_v<niche_key<SlotNumber>>);
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<niche_key<SlotNumber>>);
static_assert(detail::NicheRead<SlotNumber>::holds_its_empty() && detail::NicheRead<MemberSlot>::holds_its_empty());
static_assert(option_form_of<MemberSlot> == OptionForm::niche);
static_assert(sizeof(Option<MemberSlot>) == sizeof(MemberSlot));
static_assert(Option<MemberSlot>{}.is_none());
static_assert(Option<MemberSlot>::some(MemberSlot{2}).expect("a constant slot").raw == 2);

// A count that is never zero: the empty value of its niche has zero bytes.
struct NonZeroCount {
    std::uint32_t raw = 1;
    [[nodiscard]] static constexpr NonZeroCount niche_empty(niche_key<NonZeroCount> const&) noexcept {
        return NonZeroCount{0};
    }
    [[nodiscard]] constexpr bool is_niche_empty(niche_key<NonZeroCount> const&) const noexcept { return raw == 0; }
};

// A lifetime start over bytes.  Zero bytes are an empty plain Option and an
// empty niche Option whose empty value has zero bytes.  For each other
// niche, zero bytes are a full Option that no constructor made, so the
// checked lifetime start refuses the Option.
static_assert(::foundation::lifetime::ImplicitLifetimeThroughout<Option<std::uint64_t>>);
static_assert(::foundation::lifetime::ImplicitLifetimeThroughout<Option<NonZeroCount>>);
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<Option<SlotNumber>>);
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<Option<MemberSlot>>);
static_assert(sizeof(Option<NonZeroCount>) == sizeof(NonZeroCount));
static_assert(std::is_trivially_copyable_v<Option<SlotNumber>> && std::is_trivially_copyable_v<Option<NonZeroCount>>);

// The mark of the lifetime is an empty base, so a niche Option has the
// unique object representations of its payload, and an Atomic cell can hold
// it.
static_assert(std::has_unique_object_representations_v<Option<SlotNumber>>);
static_assert(std::has_unique_object_representations_v<Option<MemberSlot>>);
static_assert(std::has_unique_object_representations_v<Option<NonZeroCount>>);

// A plain Option is the plain sum of the empty marker and its payload.
static_assert(std::is_same_v<SumSlot<NoPayload, std::uint32_t>, SumSlotOf<true, NoPayload, std::uint32_t>>);
static_assert(sizeof(Option<std::uint32_t>) == 8 && sizeof(Option<std::uint64_t>) == 16);
static_assert(sizeof(Option<std::uint8_t>) == 2);

// A large plain payload, of more than 900 words, the template depth limit of
// the compiler.  Its sum goes through memory, so its empty marker writes only
// the tag, and no zero word (SumSlotOf).
template <class Word, std::size_t Count>
struct PackedWords {
    PackedWords<Word, Count / 2> low{};
    PackedWords<Word, Count / 2> high{};
};
template <class Word>
struct PackedWords<Word, 1> {
    Word value = 0;
};
using FourKilobytes = PackedWords<std::uint8_t, 4096>;
using EightKilobytes = PackedWords<std::uint64_t, 1024>;
static_assert(sizeof(FourKilobytes) == 4096 && alignof(FourKilobytes) == 1);
static_assert(sizeof(EightKilobytes) == 8192 && alignof(EightKilobytes) == 8);
static_assert(option_form_of<FourKilobytes> == OptionForm::plain);
static_assert(option_form_of<EightKilobytes> == OptionForm::plain);
static_assert(sizeof(Option<FourKilobytes>) == 4097 && sizeof(Option<EightKilobytes>) == 8200);
static_assert(Option<FourKilobytes>{}.is_none() && Option<EightKilobytes>{}.is_none());
static_assert(Option<EightKilobytes>::some(EightKilobytes{}).is_some());

// An odd word count: the two halves of each level of the tree are different.
struct ThreeBytes {
    std::uint8_t first = 0;
    std::uint8_t second = 0;
    std::uint8_t third = 0;
};
struct OddKilobytes {
    FourKilobytes body{};
    ThreeBytes tail{};
};
static_assert(sizeof(Option<ThreeBytes>) == 4 && sizeof(Option<OddKilobytes>) == 4100);
static_assert(Option<ThreeBytes>{}.is_none() && Option<OddKilobytes>{}.is_none());

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

enum class Code32 : std::uint32_t {
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

// An error that refers to an object outside it.
struct ReferringError {
    int& target;
};

// The error gate: a scoped enum, or a trivially copyable and assignable
// class of at most 16 bytes.
static_assert(ErrorValue<Code> && ErrorValue<WideError> && ErrorValue<Unit>);
static_assert(std::is_trivially_copyable_v<ReferringError> && !ErrorValue<ReferringError>);
static_assert(!ErrorValue<int>);
static_assert(!ErrorValue<bool>);
static_assert(!ErrorValue<TooWideError>);
static_assert(!ErrorValue<CountedError>);
static_assert(!ErrorValue<Code const>);

// An Err marks a return.  It is neither a payload nor an error value.
static_assert(std::is_trivially_copyable_v<Err<Code>> && sizeof(Err<Code>) <= 16);
static_assert(!ErrorValue<Err<Code>> && !ErrorValue<Err<WideError>>);
static_assert(!ChoicePayload<Err<Code>> && !ChoicePayload<Err<WideError>>);

// Only the mark kind of Err makes a class a mark.
struct NamesMarkKind {
    using mark_kind = int;
    std::uint32_t raw = 0;
};
static_assert(ChoicePayload<NamesMarkKind> && ErrorValue<NamesMarkKind>);

// The layout: the value or the error, and one flag.  A Result of trivially
// copyable parts is trivially copyable, so the ABI gives a small one back
// in registers.
static_assert(sizeof(Result<Unit, Code>) == 2);
static_assert(sizeof(Result<std::uint32_t, Code>) == 8);
static_assert(sizeof(Result<std::uint32_t, Code32>) == 8);
static_assert(sizeof(Result<std::uint64_t, Code>) == 16);
static_assert(sizeof(Result<std::uint64_t, WideError>) == 24);
static_assert(std::is_trivially_copyable_v<Result<std::uint64_t, Code>>);
static_assert(std::is_trivially_copyable_v<Result<std::uint32_t, Code32>>);

// Zero bytes are a value or an error that no constructor made, so the
// checked lifetime start refuses each Result.
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<Result<int, Code>>);
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<Result<Unit, WideError>>);
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

// An Err of a different error converts when the error converts with no
// explicit constructor and no throw.
struct NetError {
    Code code = Code::closed;
    constexpr NetError(Code from) noexcept : code{from} {}
};

struct ExplicitNetError {
    Code code = Code::closed;
    explicit constexpr ExplicitNetError(Code from) noexcept : code{from} {}
};

static_assert(ErrorValue<NetError> && ErrorValue<ExplicitNetError>);
static_assert(ErrorConvertsTo<Code, NetError> && !ErrorConvertsTo<Code, Code>);
static_assert(!ErrorConvertsTo<Code, ExplicitNetError> && !ErrorConvertsTo<NetError, Code>);
static_assert(std::is_convertible_v<Err<Code>, Result<int, NetError>>);
static_assert(!std::is_convertible_v<Err<Code>, Result<int, ExplicitNetError>>);
static_assert(!std::is_constructible_v<Result<int, ExplicitNetError>, Err<Code>>);

// An Option and a Result do not convert to each other.
static_assert(!std::is_constructible_v<Result<int, Code>, NoValue>);
static_assert(!std::is_constructible_v<Option<int>, Err<Code>>);
static_assert(!std::is_constructible_v<Option<int>, Result<int, Code>>);

[[nodiscard]] consteval Result<int, NetError> checked_quarter(int value) noexcept {
    if (value % 4 != 0) return err(Code::closed);
    return value / 4;
}
static_assert(checked_quarter(8).expect("a multiple of four has a quarter") == 2);
static_assert(checked_quarter(5).err().expect("a different value is refused").code == Code::closed);

// ok_or makes the crossing from an Option, and some() deduces the payload.
static_assert(some(4).ok_or(Code::closed).expect("a full Option gives its value") == 4);
static_assert(Option<int>{none}.ok_or(Code::closed).err().expect("an empty Option gives the error") == Code::closed);
static_assert(std::is_same_v<decltype(some(SlotNumber{3})), Option<SlotNumber>>);
static_assert(std::is_same_v<decltype(std::declval<Option<int>>().ok_or(Code::closed)), Result<int, Code>>);

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

// The rvalue match gives up its source, so an arm that gives a reference
// is refused, and the borrow match keeps its source, so a reference into
// it is a borrow.
inline constexpr int fallback_number = 0;
using NumberBorrow = int const&;
template <class R>
constexpr bool moved_match_gives_reference = requires(R result) {
    static_cast<R&&>(result).match([](int&& value) noexcept -> NumberBorrow { return value; },
                                   [](Code) noexcept -> NumberBorrow { return fallback_number; });
};
static_assert(!moved_match_gives_reference<Result<int, Code>>);
template <class R>
constexpr bool borrowed_match_gives_reference = requires(R const& result) {
    result.match([](NumberBorrow value) noexcept -> NumberBorrow { return value; },
                 [](Code) noexcept -> NumberBorrow { return fallback_number; });
};
static_assert(borrowed_match_gives_reference<Result<int, Code>>);

// A match of a Result needs both arms.
template <class R>
constexpr bool matches_with_one_arm =
    requires(R result) { static_cast<R&&>(result).match([](int) noexcept { return 0; }); };
static_assert(!matches_with_one_arm<Result<int, Code>>);

// ── A linear payload ──────────────────────────────────────────────────

// A linear class.  Its move constructor is trivial, and its move
// assignment is user-provided, so it is not trivially copyable.
struct[[= ::foundation::linear::linear{}]] Token {
    int handle = 0;
    constexpr explicit Token(int value) noexcept : handle{value} {}
    Token(Token&&) noexcept = default;
    Token(Token const&) = delete;
    constexpr Token& operator=(Token&& other) noexcept {
        handle = other.handle;
        return *this;
    }
};

// A class that holds a Token by value and deletes its assignments.  It is
// linear through the Token.  By [class.prop] it is trivially copyable,
// because its only eligible special member is a trivial move constructor.
// GCC 16 counts the move assignment of the Token, so GCC 16 does not find
// it trivially copyable.  The marker gives the owning form in each case.
struct HoldsToken {
    Token held;
    HoldsToken(HoldsToken&&) noexcept = default;
    HoldsToken& operator=(HoldsToken&&) = delete;
};
static_assert(::foundation::linear::is_linear_v<HoldsToken> && std::is_trivially_move_constructible_v<HoldsToken>);

// The Choice family reads the marker, so each linear payload takes the
// owning form, and its Option and its Result are move-only.
static_assert(!DroppablePayload<Token> && !DroppablePayload<HoldsToken> && DroppablePayload<Owner>);
static_assert(option_form_of<Token> == OptionForm::owning && option_form_of<HoldsToken> == OptionForm::owning);
static_assert(!std::is_copy_constructible_v<Option<HoldsToken>> && !std::is_trivially_copyable_v<Option<HoldsToken>>);
static_assert(!std::is_copy_constructible_v<Result<HoldsToken, Code>>);

// A linear payload has no value_or() and no mutable cursor.  A loop over a
// mutable Option of a linear payload takes the const cursor, so a move
// through it is a copy, which the payload refuses.
template <class O, class T>
concept GivesValueOr =
    requires(O option, T fallback) { static_cast<O&&>(option).value_or(static_cast<T&&>(fallback)); };
static_assert(!GivesValueOr<Option<Token>, Token> && GivesValueOr<Option<Owner>, Owner>);
static_assert(std::is_same_v<decltype(std::declval<Option<Token>&>().begin()), OptionCursor<Token const>>);
static_assert(std::is_same_v<decltype(std::declval<Option<Owner>&>().begin()), OptionCursor<Owner>>);

// A linear error.  The const err() would copy it, so only the rvalue err()
// gives it, and value_or() would drop it.
static_assert(ErrorValue<Token> && ErrorValue<HoldsToken>);
template <class R>
concept CopiesItsError = requires(R const& result) { result.err(); };
template <class R>
concept GivesItsError = requires(R result) { static_cast<R&&>(result).err(); };
static_assert(!CopiesItsError<Result<int, Token>> && GivesItsError<Result<int, Token>>);
static_assert(CopiesItsError<Result<int, Code>>);
template <class R>
concept ResultGivesValueOr = requires(R result) { static_cast<R&&>(result).value_or(0); };
static_assert(!ResultGivesValueOr<Result<int, Token>> && ResultGivesValueOr<Result<int, Code>>);

// The roads of a linear error in a constant evaluation: the rvalue err()
// and the rvalue match give the error once, and the const match reads it.
[[nodiscard]] consteval Result<int, Token> refused_with(int handle) noexcept { return err(Token{handle}); }
static_assert(refused_with(5).err().expect("the operation was refused").handle == 5);
static_assert(refused_with(6).match([](int&&) noexcept { return 0; },
                                    [](Token&& token) noexcept { return Token{static_cast<Token&&>(token)}.handle; })
              == 6);
[[nodiscard]] consteval int read_refusal() noexcept {
    Result<int, Token> const refused = refused_with(7);
    return refused.match([](int const&) noexcept { return 0; },
                         [](Token const& token) noexcept { return token.handle; });
}
static_assert(read_refusal() == 7);

// ── The closed surface ────────────────────────────────────────────────

// The family list of plan section 11.2, and the two members of the niche
// protocol that the section designs.  CRUCIBLE_TRY is a macro, and no type
// has a member of its name.
inline constexpr TextView choice_names{
    "none NoValue Unit Err err some is_some is_none value_or ok_or is_ok is_err ok is which niche empty is_empty"};

// Each form of an Option, and each Result, gives only the shared verbs, the
// language protocols and the names of the list.
CRUCIBLE_CLOSED_SURFACE(choice_names, Option<int>);
CRUCIBLE_CLOSED_SURFACE(choice_names, Option<SlotNumber>);
CRUCIBLE_CLOSED_SURFACE(choice_names, Option<Owner>);
CRUCIBLE_CLOSED_SURFACE(choice_names, Option<Token>);
CRUCIBLE_CLOSED_SURFACE(choice_names, Result<int, Code>);
CRUCIBLE_CLOSED_SURFACE(choice_names, Result<Unit, Code>);
CRUCIBLE_CLOSED_SURFACE(choice_names, Result<Owner, Token>);
CRUCIBLE_CLOSED_SURFACE(choice_names, Err<Code>);
CRUCIBLE_CLOSED_SURFACE(choice_names, Err<Token>);
CRUCIBLE_CLOSED_SURFACE(choice_names, Unit);
CRUCIBLE_CLOSED_SURFACE(choice_names, NoValue);
CRUCIBLE_CLOSED_SURFACE(choice_names, niche<int>);
CRUCIBLE_CLOSED_SURFACE(choice_names, niche<MemberSlot>);
CRUCIBLE_CLOSED_SURFACE(choice_names, niche_key<MemberSlot>);

// The cursor of a loop over an Option takes the cursor protocol, and no
// other type of the family takes a dereference.
CRUCIBLE_CLOSED_SURFACE(choice_names, OptionCursor<int>, CursorProtocol);
CRUCIBLE_CLOSED_SURFACE(choice_names, OptionCursor<int const>, CursorProtocol);
static_assert(!closed_surface<OptionCursor<int>>(choice_names).is_closed());

}  // namespace detail::choice_checks

}  // namespace foundation::core
