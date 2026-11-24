// The compile-time checks of foundation/core/Choice.h.

#include <foundation/core/Choice.h>

#include <foundation/Lifetime.h>
#include <foundation/Linear.h>
#include <foundation/core/Number.h>
#include <foundation/core/OneOf.h>
#include <foundation/core/Scalar.h>
#include <foundation/core/Verbs.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

// A key of no namespace of the base: a class with no public constructor that
// a file outside the base can define.  It vouches for nothing.
class OutsideKey {
    constexpr OutsideKey() noexcept {}
    constexpr OutsideKey(OutsideKey const&) noexcept {}

public:
    OutsideKey& operator=(OutsideKey const&) = delete("a key lives for one construction");
};

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

// A slot number whose niche states that no door builds the empty value.  Its
// one door builds a slot from a byte, so no slot holds the empty value, and
// the two walks of the check pass.  This file is outside the base, so the
// payload gate refuses the statement all the same.
class TrustedSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit TrustedSlot(std::uint32_t raw) noexcept : raw_{raw} {}
    friend struct ::foundation::core::niche<TrustedSlot>;

public:
    [[nodiscard]] static constexpr TrustedSlot of_byte(std::uint8_t byte) noexcept { return TrustedSlot{byte}; }
    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return raw_; }
};

// Two false statements: an aggregate builds the empty value from its member,
// and a public constructor takes a pointer with no key.
struct AggregateStatedSlot {
    std::uint32_t raw = 0;
};
class PointerStatedSlot {
    std::uint32_t raw_ = 0;

public:
    constexpr explicit PointerStatedSlot(std::nullptr_t /*address*/) noexcept {}
    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return raw_; }
};

// A constructor of two raw values with no voucher, which only the reflection
// over the parameters finds, and the same constructor with a key.
class PairStatedSlot {
    std::uint32_t raw_ = 0;

public:
    constexpr PairStatedSlot(std::uint8_t high, std::uint8_t /*low*/) noexcept : raw_{high} {}
};
class KeyedPairSlot {
    std::uint32_t raw_ = 0;

public:
    constexpr KeyedPairSlot(std::uint8_t low, niche_key<KeyedPairSlot> const& /*key*/) noexcept : raw_{low} {}
    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return raw_; }
};

// A niche whose statement is not a bool.
struct MisstatedSlot {
    std::uint32_t raw = 0;
};

}  // namespace detail::choice_checks

template <>
struct niche<detail::choice_checks::TrustedSlot> {
    static constexpr niche_statement is_empty_never_built{};
    [[nodiscard]] static constexpr detail::choice_checks::TrustedSlot
    empty(niche_key<detail::choice_checks::TrustedSlot> const&) noexcept {
        return detail::choice_checks::TrustedSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] static constexpr bool is_empty(niche_key<detail::choice_checks::TrustedSlot> const&,
                                                 detail::choice_checks::TrustedSlot const& slot) noexcept {
        return slot.raw() == 0xFFFFFFFFu;
    }
};

template <>
struct niche<detail::choice_checks::AggregateStatedSlot> {
    static constexpr niche_statement is_empty_never_built{};
    [[nodiscard]] static constexpr detail::choice_checks::AggregateStatedSlot
    empty(niche_key<detail::choice_checks::AggregateStatedSlot> const&) noexcept {
        return detail::choice_checks::AggregateStatedSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] static constexpr bool is_empty(niche_key<detail::choice_checks::AggregateStatedSlot> const&,
                                                 detail::choice_checks::AggregateStatedSlot const& slot) noexcept {
        return slot.raw == 0xFFFFFFFFu;
    }
};

template <>
struct niche<detail::choice_checks::PointerStatedSlot> {
    static constexpr niche_statement is_empty_never_built{};
    [[nodiscard]] static constexpr detail::choice_checks::PointerStatedSlot
    empty(niche_key<detail::choice_checks::PointerStatedSlot> const&) noexcept {
        return detail::choice_checks::PointerStatedSlot{nullptr};
    }
    [[nodiscard]] static constexpr bool is_empty(niche_key<detail::choice_checks::PointerStatedSlot> const&,
                                                 detail::choice_checks::PointerStatedSlot const& slot) noexcept {
        return slot.raw() == 0;
    }
};

template <>
struct niche<detail::choice_checks::MisstatedSlot> {
    static constexpr int is_empty_never_built = 1;
    [[nodiscard]] static constexpr detail::choice_checks::MisstatedSlot
    empty(niche_key<detail::choice_checks::MisstatedSlot> const&) noexcept {
        return detail::choice_checks::MisstatedSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] static constexpr bool is_empty(niche_key<detail::choice_checks::MisstatedSlot> const&,
                                                 detail::choice_checks::MisstatedSlot const& slot) noexcept {
        return slot.raw == 0xFFFFFFFFu;
    }
};

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
// in registers.  An owning Option is move-only, and its move assignment
// does not throw.
static_assert(std::is_trivially_copyable_v<Option<SlotNumber>>);
static_assert(std::is_trivially_copyable_v<Option<std::uint64_t>>);
static_assert(!std::is_trivially_copyable_v<Option<Owner>>);
static_assert(std::is_nothrow_move_constructible_v<Option<Owner>>);
static_assert(!std::is_copy_constructible_v<Option<Owner>>);
static_assert(!std::is_copy_assignable_v<Option<Owner>>);
static_assert(std::is_nothrow_move_assignable_v<Option<Owner>>);

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

// The validity test of a value that a bit copy made: true for the bits of
// the empty value, false for a valid value.  It reads the niche through a
// specialization and through the members of the class, and it gives only a
// bool.
static_assert(detail::NicheRead<SlotNumber>::holds_empty(SlotNumber{0xFFFFFFFFu}));
static_assert(!detail::NicheRead<SlotNumber>::holds_empty(SlotNumber{7}));
static_assert(detail::NicheRead<MemberSlot>::holds_empty(MemberSlot{0xFFFFFFFFu}));
static_assert(!detail::NicheRead<MemberSlot>::holds_empty(MemberSlot{7}));
static_assert(std::is_same_v<decltype(detail::NicheRead<SlotNumber>::holds_empty(SlotNumber{})), bool>);
static_assert(noexcept(detail::NicheRead<MemberSlot>::holds_empty(MemberSlot{})));

// A count that is never zero: the empty value of its niche has zero bytes.
struct NonZeroCount {
    std::uint32_t raw = 1;
    [[nodiscard]] static constexpr NonZeroCount niche_empty(niche_key<NonZeroCount> const&) noexcept {
        return NonZeroCount{0};
    }
    [[nodiscard]] constexpr bool is_niche_empty(niche_key<NonZeroCount> const&) const noexcept { return raw == 0; }
};

// A lifetime start over bytes.  Zero bytes are an empty niche Option whose
// empty value has zero bytes.  For each other niche, zero bytes are a full
// Option that no constructor made, so the checked lifetime start refuses the
// Option.  The bytes of a plain Option hold its tag, and bytes can hold a tag
// that names no alternative, so the mark closed_tag of the tag refuses it too.
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<Option<std::uint64_t>>);
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
    for (int const& value : option) {
        turns += value;
    }
    return turns;
}
static_assert(loop_turns(Option<int>::some(5)) == 5);
static_assert(loop_turns(none) == 0);

// The frame door writes the payload in place in a constant evaluation too,
// and the arm of an empty Option runs.
[[nodiscard]] consteval int written_in_place(Option<int> option) noexcept {
    int const seen = option.with_mut(
        [](auto value) noexcept {
            *value *= 2;
            return *value;
        },
        [] noexcept { return -1; });
    return seen;
}
static_assert(written_in_place(Option<int>::some(5)) == 10);
static_assert(written_in_place(none) == -1);

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
    int const seen = static_cast<Option<Owner>&&>(source).match([](Owner owner) noexcept { return owner.handle; },
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

// A temporary Option takes the deleted borrow match of an rvalue, so no arm
// gives a reference into it.  A borrow of a named Option can give one.
inline constexpr int option_fallback = 0;
using OptionBorrow = int const&;
template <class O>
concept TemporaryBorrowGivesReference = requires(O option) {
    static_cast<O&&>(option).match([](OptionBorrow value) noexcept -> OptionBorrow { return value; },
                                   [] noexcept -> OptionBorrow { return option_fallback; });
};
template <class O>
concept NamedBorrowGivesReference = requires(O const& option) {
    option.match([](OptionBorrow value) noexcept -> OptionBorrow { return value; },
                 [] noexcept -> OptionBorrow { return option_fallback; });
};
static_assert(!TemporaryBorrowGivesReference<Option<int>>);
static_assert(NamedBorrowGivesReference<Option<int>>);

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

// A sealed error: a byte seal makes its assignment user-provided, so it is
// not trivially copyable, and std::bit_cast builds none.  Its copy, move and
// end are those of its bytes.
struct SealedError {
    std::uint32_t code = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal{};
};

// A sealed payload of one word, as a sealed pointer is.
struct SealedWord {
    std::uint64_t raw = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal{};
};

// The bytes of a sum are its value when its copy, move and end are those of
// its bytes.  The ABI then gives a sum of at most 16 bytes back in registers.
template <class Sum>
concept SumOfBytes = detail::TrivialCopyShape<Sum> && std::is_nothrow_move_assignable_v<Sum>;

// The error gate: a scoped enum, or a class of at most 16 bytes whose copy,
// move and end are those of its bytes.  The niche gate reads the same
// shape.
static_assert(ErrorValue<Code> && ErrorValue<WideError> && ErrorValue<Unit>);
static_assert(std::is_trivially_copyable_v<ReferringError> && !ErrorValue<ReferringError>);
static_assert(!ErrorValue<int>);
static_assert(!ErrorValue<bool>);
static_assert(!ErrorValue<TooWideError>);
static_assert(!ErrorValue<CountedError>);
static_assert(!ErrorValue<Code const>);

// A sum of sealed parts keeps the plain slot: its copy, move and end are
// those of its bytes, and it has the size of a plain sum.  The seal of a part
// seals the sum, so std::bit_cast builds no sum that holds a sealed part.
static_assert(!std::is_trivially_copyable_v<SealedError> && detail::TrivialCopyShape<SealedError>);
static_assert(ErrorValue<SealedError> && sizeof(SealedError) == sizeof(std::uint32_t));
static_assert(PlainPayload<SealedError> && PlainPayload<SealedWord>);
static_assert(option_form_of<SealedWord> == OptionForm::plain);
static_assert(SumOfBytes<Result<int, SealedError>> && sizeof(Result<int, SealedError>) == 8);
static_assert(SumOfBytes<Result<SealedWord, Code>> && sizeof(Result<SealedWord, Code>) == 16);
static_assert(SumOfBytes<Option<SealedWord>> && sizeof(Option<SealedWord>) == 16);
static_assert(std::is_copy_assignable_v<Option<SealedWord>> && std::is_copy_assignable_v<Result<int, SealedError>>);
static_assert(!std::is_trivially_copyable_v<Result<int, SealedError>>);
static_assert(!std::is_trivially_copyable_v<Option<SealedWord>>);
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<Option<SealedWord>>);
static_assert(Result<int, SealedError>{err(SealedError{7})}.is_err());
static_assert(Result<int, SealedError>{err(SealedError{7})}.err().expect("a constant error").code == 7);
static_assert(Result<int, SealedError>{5}.expect("a constant value") == 5);

// The slot of a payload for each pair of facts: its bytes are trivially
// copyable or not, and it copies or only moves.  The plain slot builds its
// copy and its move from the copy of each payload, so only a payload that
// copies its bytes takes it.  A payload that only moves takes the owning
// slot, which moves it, also when its bytes are trivially copyable.  Each
// sum moves, and no sum uses an operation that its payload deletes.
struct BytesThatCopy {
    std::uint32_t raw = 0;
};
struct BytesThatOnlyMove {
    std::uint32_t raw = 0;
    constexpr BytesThatOnlyMove() noexcept = default;
    constexpr explicit BytesThatOnlyMove(std::uint32_t value) noexcept : raw{value} {}
    BytesThatOnlyMove(BytesThatOnlyMove const&) = delete("the payload only moves");
    BytesThatOnlyMove& operator=(BytesThatOnlyMove const&) = delete("the payload only moves");
    constexpr BytesThatOnlyMove(BytesThatOnlyMove&&) noexcept = default;
    constexpr BytesThatOnlyMove& operator=(BytesThatOnlyMove&&) noexcept = default;
    ~BytesThatOnlyMove() = default;
};
struct CopyWithABody {
    std::uint32_t raw = 0;
    constexpr CopyWithABody() noexcept = default;
    constexpr CopyWithABody(CopyWithABody const& other) noexcept : raw{other.raw} {}
    constexpr CopyWithABody& operator=(CopyWithABody const& other) noexcept {
        raw = other.raw;
        return *this;
    }
};
struct BytesThatCopyOnce {
    std::uint32_t const raw = 0;
};

static_assert(std::is_trivially_copyable_v<BytesThatCopy> && std::is_copy_constructible_v<BytesThatCopy>);
static_assert(std::is_trivially_copyable_v<BytesThatOnlyMove> && !std::is_copy_constructible_v<BytesThatOnlyMove>);
static_assert(!std::is_trivially_copyable_v<CopyWithABody> && std::is_copy_constructible_v<CopyWithABody>);
static_assert(!std::is_trivially_copyable_v<Owner> && !std::is_copy_constructible_v<Owner>);
static_assert(std::is_trivially_copyable_v<BytesThatCopyOnce> && !std::is_copy_assignable_v<BytesThatCopyOnce>);

static_assert(option_form_of<BytesThatCopy> == OptionForm::plain && is_plain_sum_v<int, BytesThatCopy>);
static_assert(option_form_of<BytesThatOnlyMove> == OptionForm::owning && !is_plain_sum_v<int, BytesThatOnlyMove>);
static_assert(option_form_of<CopyWithABody> == OptionForm::owning && !is_plain_sum_v<int, CopyWithABody>);
static_assert(option_form_of<Owner> == OptionForm::owning && !is_plain_sum_v<int, Owner>);
static_assert(option_form_of<SealedWord> == OptionForm::plain && is_plain_sum_v<int, SealedWord>);
static_assert(option_form_of<BytesThatCopyOnce> == OptionForm::plain && is_plain_sum_v<int, BytesThatCopyOnce>);

static_assert(std::is_nothrow_move_constructible_v<Option<BytesThatOnlyMove>>);
static_assert(std::is_nothrow_move_assignable_v<Option<BytesThatOnlyMove>>);
static_assert(!std::is_copy_constructible_v<Option<BytesThatOnlyMove>>);
static_assert(std::is_nothrow_move_constructible_v<Result<BytesThatOnlyMove, Code>>);
static_assert(std::is_copy_constructible_v<Option<BytesThatCopyOnce>>);
static_assert(!std::is_copy_assignable_v<Option<BytesThatCopyOnce>>);

// The plain slot of a payload with no assignment, as a closure with a capture
// by reference has none, empties and fills in place: take and fill compile,
// and the take leaves the source empty.
static_assert([] consteval {
    Option<BytesThatCopyOnce> held = Option<BytesThatCopyOnce>::some(BytesThatCopyOnce{3});
    Option<BytesThatCopyOnce> taken = held.take();
    bool const is_moved = held.is_none() && std::move(taken).expect("the take gives the payload").raw == 3;
    held.fill(BytesThatCopyOnce{4});
    return is_moved && held.is_some();
}());

// An Option of a payload that only moves moves its payload, and the source is
// then empty.
[[nodiscard]] consteval bool moves_a_payload_that_only_moves() {
    Option<BytesThatOnlyMove> held = Option<BytesThatOnlyMove>::some(BytesThatOnlyMove{7});
    Option<BytesThatOnlyMove> moved{std::move(held)};
    Option<BytesThatOnlyMove> assigned{};
    assigned = std::move(moved);
    Result<BytesThatOnlyMove, Code> result{BytesThatOnlyMove{9}};
    Result<BytesThatOnlyMove, Code> moved_result{std::move(result)};
    return std::move(assigned).expect("a constant claim").raw == 7
        && std::move(moved_result).expect("a constant claim").raw == 9;
}
static_assert(moves_a_payload_that_only_moves());

// The assignment of a sum of sealed parts copies the bytes of the source: a
// value over an error, an error over a value, and a self assignment.
[[nodiscard]] consteval bool sealed_sum_assigns_bytes() {
    Result<int, SealedError> held{err(SealedError{7})};
    Result<int, SealedError> const value{5};
    held = value;
    if (!held.is_ok()) return false;
    held = Result<int, SealedError>{err(SealedError{9})};
    if (!held.is_err()) return false;
    Result<int, SealedError> const& alias = held;
    held = alias;
    if (std::move(held).err().expect("a constant error").code != 9) return false;
    Option<SealedWord> word = Option<SealedWord>::some(SealedWord{41});
    word = Option<SealedWord>{none};
    if (word.is_some()) return false;
    word = Option<SealedWord>::some(SealedWord{42});
    return std::move(word).expect("a constant word").raw == 42;
}
static_assert(sealed_sum_assigns_bytes());

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

// The zero tail of an alternative that is smaller than the storage of a sum
// of at most 16 bytes.  A tail that does not fill the storage exactly makes
// the alternative larger than the storage, and the static_assert of the slot
// or a size here fails.  The sweep crosses each word width of the storage
// (1, 2, 4 and 8 bytes) with an alternative that ends at a word and one that
// ends inside a word, on the side of the value and on the side of the error.
enum class Code16 : std::uint16_t {
    full,
    closed
};

struct ThreeHalves {
    std::uint16_t first = 0;
    std::uint16_t second = 0;
    std::uint16_t third = 0;
};

struct ThreeWords {
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    std::uint32_t third = 0;
};

static_assert(sizeof(Result<ThreeBytes, Code>) == 4);
static_assert(sizeof(Result<ThreeHalves, Code>) == 8);
static_assert(sizeof(Result<ThreeHalves, Code16>) == 8);
static_assert(sizeof(Result<ThreeWords, Code>) == 16);
static_assert(sizeof(Result<ThreeWords, Code16>) == 16);
static_assert(sizeof(Result<ThreeWords, Code32>) == 16);
static_assert(sizeof(Result<std::uint32_t, Code16>) == 8);
static_assert(sizeof(Result<std::uint64_t, Code16>) == 16);
static_assert(sizeof(Result<std::uint64_t, Code32>) == 16);
static_assert(sizeof(Result<std::uint8_t, Code32>) == 8);
static_assert(sizeof(Result<std::uint16_t, Code32>) == 8);
static_assert(sizeof(Result<ThreeBytes, Code32>) == 8);
static_assert(sizeof(Result<std::uint8_t, WideError>) == 24);
static_assert(std::is_trivially_copyable_v<Result<ThreeWords, Code>>);
static_assert(std::is_trivially_copyable_v<Result<std::uint64_t, Code16>>);

// A constant error and a constant value of each shape read back.
static_assert(Result<std::uint64_t, Code16>{err(Code16::closed)}.err().expect("a constant error") == Code16::closed);
static_assert(Result<ThreeWords, Code>{ThreeWords{.first = 1, .second = 2, .third = 3}}.expect("a constant value").third
              == 3);
static_assert(Result<std::uint8_t, Code32>{std::uint8_t{9}}.expect("a constant value") == 9);

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

// The rvalue match gives up its source, so an arm that takes its part by
// reference is refused, and so is a result that holds a borrow.  The borrow
// match keeps its source, so a reference into it is a borrow.  The first arm
// gives a reference of the type of a borrow, and not a reference to its own
// rvalue part, which the rule view-escape refuses.
inline constexpr int fallback_number = 0;
using NumberBorrow = int const&;
template <class R>
constexpr bool moved_match_gives_reference = requires(R result) {
    static_cast<R&&>(result).match(
        [](int&& value) noexcept -> NumberBorrow {
            static_cast<void>(value);
            return fallback_number;
        },
        [](Code) noexcept -> NumberBorrow { return fallback_number; });
};
template <class R>
constexpr bool moved_match_takes_reference = requires(R result) {
    static_cast<R&&>(result).match([](int const & value) noexcept { return value; }, [](Code) noexcept { return 0; });
};
template <class R>
constexpr bool moved_match_gives_borrow = requires(R result) {
    static_cast<R&&>(result).match([](int) noexcept -> NumberBorrow { return fallback_number; },
                                   [](Code) noexcept -> NumberBorrow { return fallback_number; });
};
static_assert(!moved_match_gives_reference<Result<int, Code>>);
static_assert(!moved_match_takes_reference<Result<int, Code>> && !moved_match_gives_borrow<Result<int, Code>>);
template <class R>
constexpr bool borrowed_match_gives_reference = requires(R const& result) {
    result.match([](NumberBorrow value) noexcept -> NumberBorrow { return value; },
                 [](Code) noexcept -> NumberBorrow { return fallback_number; });
};
static_assert(borrowed_match_gives_reference<Result<int, Code>>);

// ── The arms of an rvalue match ───────────────────────────────────────

// An arm takes its part by value.  Reflection reads the call operator of a
// closure, an inherited and a static call operator, the instance of a generic
// call operator for the part, and the parameter of a function.  A reference
// parameter, a parameter of a different type, an explicit object parameter
// and a class with two call operators are refused, also when one of the two is
// a template.
struct TwoCalls {
    [[nodiscard]] int operator()(int) const noexcept { return 0; }
    [[nodiscard]] int operator()(long) const noexcept { return 1; }
};
struct TemplateAndReference {
    [[nodiscard]] int operator()(int const&) const noexcept { return 0; }
    template <class Part>
    [[nodiscard]] int operator()(Part) const noexcept {
        return 1;
    }
};
struct OneCall {
    [[nodiscard]] int operator()(int) const noexcept { return 0; }
};
struct InheritsCall : OneCall {};
struct StaticCall {
    [[nodiscard]] static int operator()(int) noexcept { return 0; }
};
struct StaticReference {
    [[nodiscard]] static int operator()(int const&) noexcept { return 0; }
};
struct ExplicitObject {
    [[nodiscard]] int operator()(this ExplicitObject const&, int) noexcept { return 0; }
};
[[nodiscard]] int arm_function(int) noexcept;
static_assert(TakesPayloadByValue<InheritsCall, int> && TakesPayloadByValue<StaticCall, int>);
static_assert(!TakesPayloadByValue<StaticReference, int> && !TakesPayloadByValue<ExplicitObject, int>);
static_assert(!TakesPayloadByValue<TemplateAndReference, int>);
static_assert(TakesPayloadByValue<decltype([](int) noexcept { return 0; }), int>);
static_assert(TakesPayloadByValue<decltype([](int const) noexcept { return 0; }), int>);
static_assert(TakesPayloadByValue<decltype([](auto) noexcept { return 0; }), int>);
static_assert(TakesPayloadByValue<decltype(&arm_function), int> && TakesPayloadByValue<decltype(arm_function)&, int>);
static_assert(!TakesPayloadByValue<decltype([](int&&) noexcept { return 0; }), int>);
static_assert(!TakesPayloadByValue<decltype([](int const&) noexcept { return 0; }), int>);
static_assert(!TakesPayloadByValue<decltype([](auto&&) noexcept { return 0; }), int>);
static_assert(!TakesPayloadByValue<decltype([](auto const&) noexcept { return 0; }), int>);
static_assert(!TakesPayloadByValue<decltype([](long) noexcept { return 0; }), int>);
static_assert(!TakesPayloadByValue<TwoCalls, int>);

// ── The arms of a borrow match ────────────────────────────────────────

// An arm of a borrow match reads its part with no conversion: its parameter
// is the part type or a const reference to it.  A generic arm passes in each
// form that a deduction from a const lvalue gives.  A class that converts, a
// reference that is not const, an rvalue reference and a volatile reference
// are refused, and so is each shape that the rvalue gate refuses.
struct Meters {
    double value = 0.0;
};
struct Feet {
    double value = 0.0;
    constexpr Feet() noexcept = default;
    constexpr Feet(Meters meters) noexcept : value{meters.value} {}
};
static_assert(TakesPayloadByBorrow<decltype([](int) noexcept { return 0; }), int>);
static_assert(TakesPayloadByBorrow<decltype([](int const) noexcept { return 0; }), int>);
static_assert(TakesPayloadByBorrow<decltype([](int const&) noexcept { return 0; }), int>);
static_assert(TakesPayloadByBorrow<decltype([](OptionBorrow) noexcept { return 0; }), int>);
static_assert(TakesPayloadByBorrow<decltype([](auto) noexcept { return 0; }), int>);
static_assert(TakesPayloadByBorrow<decltype([](auto const&) noexcept { return 0; }), int>);
static_assert(TakesPayloadByBorrow<decltype([](auto&) noexcept { return 0; }), int>);
static_assert(TakesPayloadByBorrow<decltype([](auto&&) noexcept { return 0; }), int>);
static_assert(TakesPayloadByBorrow<InheritsCall, int> && TakesPayloadByBorrow<StaticCall, int>);
static_assert(TakesPayloadByBorrow<StaticReference, int> && TakesPayloadByBorrow<decltype(&arm_function), int>);
static_assert(TakesPayloadByBorrow<decltype([](Meters const&) noexcept { return 0; }), Meters>);
static_assert(!TakesPayloadByBorrow<decltype([](Feet) noexcept { return 0; }), Meters>);
static_assert(!TakesPayloadByBorrow<decltype([](Feet const&) noexcept { return 0; }), Meters>);
static_assert(!TakesPayloadByBorrow<decltype([](long) noexcept { return 0; }), int>);
static_assert(!TakesPayloadByBorrow<decltype([](int&) noexcept { return 0; }), int>);
static_assert(!TakesPayloadByBorrow<decltype([](int&&) noexcept { return 0; }), int>);
static_assert(!TakesPayloadByBorrow<decltype([](int const&&) noexcept { return 0; }), int>);
static_assert(!TakesPayloadByBorrow<decltype([](int const volatile&) noexcept { return 0; }), int>);
static_assert(!TakesPayloadByBorrow<ExplicitObject, int> && !TakesPayloadByBorrow<TwoCalls, int>);
static_assert(!TakesPayloadByBorrow<TemplateAndReference, int>);

// A generic arm by value can call a member of an rvalue that a const
// payload does not have.  The gate reads no parameter of a template, so it
// instantiates no arm with a type that the match does not give it.
struct TakesOnce {
    int value = 0;
    [[nodiscard]] constexpr int take() && noexcept { return value; }
};
static_assert(TakesPayloadByBorrow<
              decltype([](auto held) noexcept { return static_cast<decltype(held)&&>(held).take(); }), TakesOnce>);

// A forwarding arm reads a const lvalue.  Its body can call a member that an
// rvalue does not have, and the gate still reads the arm.
struct ReadsOnlyLvalue {
    int value = 0;
    [[nodiscard]] constexpr int read() const& noexcept { return value; }
    int read() && = delete("the probe reads only an lvalue");
};
static_assert(
    TakesPayloadByBorrow<decltype([](auto&& held) noexcept { return static_cast<decltype(held)>(held).read(); }),
                         ReadsOnlyLvalue>);

// The borrow match of an Option and of a Result refuses an arm that reads
// its part through an implicit constructor, and takes the exact arms.
template <class O>
constexpr bool borrowed_match_converts = requires(O const& option) {
    option.match([](Feet feet) noexcept { return feet.value; }, []() noexcept { return 0.0; });
};
template <class O>
constexpr bool borrowed_match_reads_exactly = requires(O const& option) {
    option.match([](Meters const & meters) noexcept { return meters.value; }, []() noexcept { return 0.0; });
    option.match([](Meters meters) noexcept { return meters.value; }, []() noexcept { return 0.0; });
    option.match([](auto const & meters) noexcept { return meters.value; }, []() noexcept { return 0.0; });
};
static_assert(!borrowed_match_converts<Option<Meters>> && borrowed_match_reads_exactly<Option<Meters>>);
template <class R>
constexpr bool borrowed_result_match_converts = requires(R const& result) {
    result.match([](Feet feet) noexcept { return feet.value; }, [](Code) noexcept { return 0.0; });
};
template <class R>
constexpr bool borrowed_result_match_reads_exactly = requires(R const& result) {
    result.match([](Meters const & meters) noexcept { return meters.value; }, [](Code const&) noexcept { return 0.0; });
    result.match([](auto meters) noexcept { return meters.value; }, [](auto&&) noexcept { return 0.0; });
};
static_assert(!borrowed_result_match_converts<Result<Meters, Code>>);
static_assert(borrowed_result_match_reads_exactly<Result<Meters, Code>>);

// The result of an rvalue match holds no borrow: no reference, no address,
// and no closure that captures by reference.  A closure that captures by
// value holds a value, because the sixth GCC patch shows its fields.
struct HoldsReference {
    int const& at;
};
static_assert(HoldsNoBorrow<int> && HoldsNoBorrow<void> && HoldsNoBorrow<Code>);
static_assert(HoldsNoBorrow<Result<int, Code>> && HoldsNoBorrow<Option<Owner>> && HoldsNoBorrow<Option<SlotNumber>>);
static_assert(!HoldsNoBorrow<int const&> && !HoldsNoBorrow<int*> && !HoldsNoBorrow<int Owner::*>);
static_assert(!HoldsNoBorrow<TextView> && !HoldsNoBorrow<HoldsReference> && !HoldsNoBorrow<TextView[2]>);
static_assert(!HoldsNoBorrow<Option<TextView>> && !HoldsNoBorrow<Result<int, TextView>>);
static_assert(!HoldsNoBorrow<Option<int*>> && !HoldsNoBorrow<Option<Result<int, TextView>>>);
static_assert(HoldsNoBorrow<Option<Result<int, Code>>> && HoldsNoBorrow<Result<Option<int>, Code>>);
[[nodiscard]] consteval bool closure_holds_borrow() noexcept {
    int number = 0;
    auto const reads = [&number] noexcept { return number; };
    auto const copies = [number] noexcept { return number; };
    return !HoldsNoBorrow<decltype(reads)> && HoldsNoBorrow<decltype(copies)>
        && HoldsNoBorrow<decltype([] noexcept { return 0; })>;
}
static_assert(closure_holds_borrow());

// A temporary Result takes the deleted borrow match of an rvalue, so no arm
// gives a reference into it.
template <class R>
constexpr bool temporary_borrow_gives_reference = requires(R result) {
    static_cast<R&&>(result).match([](NumberBorrow value) noexcept -> NumberBorrow { return value; },
                                   [](Code const&) noexcept -> NumberBorrow { return fallback_number; });
};
template <class R>
constexpr bool const_temporary_borrow_matches = requires(R const result) {
    static_cast<R const&&>(result).match([](int const&) noexcept { return 0; }, [](Code const&) noexcept { return 0; });
};
static_assert(!temporary_borrow_gives_reference<Result<int, Code>>);
static_assert(!const_temporary_borrow_matches<Result<int, Code>>);

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

// A payload that copies only from an lvalue that is not const, as a Link
// does, copies, so it is droppable with no walk of the marker.
struct CopiesFromLvalue {
    CopiesFromLvalue() = default;
    CopiesFromLvalue(CopiesFromLvalue&) = default;
    CopiesFromLvalue(CopiesFromLvalue&&) = default;
};
static_assert(!std::is_copy_constructible_v<CopiesFromLvalue> && DroppablePayload<CopiesFromLvalue>);
static_assert(option_form_of<Token> == OptionForm::owning && option_form_of<HoldsToken> == OptionForm::owning);
static_assert(!std::is_copy_constructible_v<Option<HoldsToken>> && !std::is_trivially_copyable_v<Option<HoldsToken>>);
static_assert(!std::is_copy_constructible_v<Result<HoldsToken, Code>>);

// A linear payload has no value_or().  The loop of a niche or a plain Option
// takes the const cursor, also of an Option that is not const.  An owning
// Option, of a linear payload or of an owner, has no loop, because a cursor
// counts no lend of its payload (the lend count).
template <class O, class T>
concept GivesValueOr =
    requires(O option, T fallback) { static_cast<O&&>(option).value_or(static_cast<T&&>(fallback)); };
static_assert(!GivesValueOr<Option<Token>, Token> && GivesValueOr<Option<Owner>, Owner>);
template <class O>
concept LoopsOver = requires(O option) {
    option.begin();
    option.end();
};
static_assert(!LoopsOver<Option<Token>&> && !LoopsOver<Option<Token> const&>);
static_assert(!LoopsOver<Option<Owner>&> && !LoopsOver<Option<Owner> const&>);
static_assert(LoopsOver<Option<std::uint64_t>&> && LoopsOver<Option<std::uint64_t> const&>);
static_assert(
    std::is_same_v<decltype(std::declval<Option<std::uint64_t>&>().begin()), OptionCursor<std::uint64_t const>>);

// The frame door writes the payload in place (foundation/core/Frame.h).  An
// arm could move a linear payload out through the RefMut and leave the
// Option full, so a linear payload has no frame door.  A const Option and a
// temporary have none.  A write through the RefMut bypasses the test of
// Option::some.  So a niche payload that an aggregate or a door can build as
// its empty value stops the build at the static assertion of the door, and a
// niche that states that no door builds the empty value takes the door, as
// the check file of foundation/core/Ref.h shows for a Ref.  The body reads
// the niche, so this check states only the constraint.
template <class O>
concept OptionWritesInPlace = requires(O& option) { option.with_mut([](auto) noexcept {}, [] noexcept {}); };
template <class O>
concept OptionWritesTemporary =
    requires(O option) { static_cast<O&&>(option).with_mut([](auto) noexcept {}, [] noexcept {}); };
static_assert(OptionWritesInPlace<Option<Owner>> && OptionWritesInPlace<Option<std::uint64_t>>);
static_assert(!OptionWritesInPlace<Option<Token>> && !OptionWritesInPlace<Option<Owner> const>);
static_assert(!OptionWritesTemporary<Option<Owner>>);

// The frame door of a Result takes the RefMut of the value and of the error,
// on a Result that is not const and of no linear part.
template <class R>
concept ResultWritesInPlace = requires(R& result) { result.with_mut([](auto) noexcept {}, [](auto) noexcept {}); };
static_assert(ResultWritesInPlace<Result<int, Code>> && ResultWritesInPlace<Result<Owner, Code>>);
static_assert(!ResultWritesInPlace<Result<int, Code> const> && !ResultWritesInPlace<Result<Token, Code>>);
static_assert(!ResultWritesInPlace<Result<int, Token>>);

// An arm of a match or of a frame door holds no linear value by value
// (ArmsHoldNoLinearValue): the door calls the arm, and a call is no consume.
// Each form of each door refuses an arm that holds a token, and takes an arm
// that holds none.
using HoldsTokenArm = decltype([held = Token{1}](auto) noexcept { return held.handle; });
using HoldsTokenRest = decltype([held = Token{1}](auto...) noexcept { return held.handle; });
using HoldsNoTokenArm = decltype([](auto) noexcept { return 0; });
using HoldsNoTokenRest = decltype([](auto...) noexcept { return 0; });
static_assert(!ArmsHoldNoLinearValue<HoldsTokenArm> && ArmsHoldNoLinearValue<HoldsNoTokenArm, HoldsNoTokenRest>);
template <class S, class First, class Second>
concept MatchesWith = requires(S sum, First first, Second second) {
    static_cast<S&&>(sum).match(static_cast<First&&>(first), static_cast<Second&&>(second));
};
template <class S, class First, class Second>
concept WritesWith = requires(S& sum, First first, Second second) {
    sum.with_mut(static_cast<First&&>(first), static_cast<Second&&>(second));
};
static_assert(MatchesWith<Option<int>, HoldsNoTokenArm, HoldsNoTokenRest>);
static_assert(MatchesWith<Option<int> const&, HoldsNoTokenArm, HoldsNoTokenRest>);
static_assert(!MatchesWith<Option<int>, HoldsTokenArm, HoldsNoTokenRest>);
static_assert(!MatchesWith<Option<int>, HoldsNoTokenArm, HoldsTokenRest>);
static_assert(!MatchesWith<Option<int> const&, HoldsTokenArm, HoldsNoTokenRest>);
static_assert(!MatchesWith<Option<int> const, HoldsTokenArm, HoldsNoTokenRest>);
static_assert(MatchesWith<Result<int, Code>, HoldsNoTokenArm, HoldsNoTokenArm>);
static_assert(!MatchesWith<Result<int, Code>, HoldsTokenArm, HoldsNoTokenArm>);
static_assert(!MatchesWith<Result<int, Code> const&, HoldsNoTokenArm, HoldsTokenArm>);
static_assert(WritesWith<Option<int>, HoldsNoTokenArm, HoldsNoTokenRest>);
static_assert(!WritesWith<Option<int>, HoldsTokenArm, HoldsNoTokenRest>);
static_assert(WritesWith<Result<int, Code>, HoldsNoTokenArm, HoldsNoTokenArm>);
static_assert(!WritesWith<Result<int, Code>, HoldsNoTokenArm, HoldsTokenArm>);

// A combinator is a match with one arm fixed, so its function holds no
// linear value by value either.  Each combinator refuses a function that
// holds a token, and takes the same function with no token.
template <class S, class Fn>
concept MapsWith = requires(S sum, Fn fn) { static_cast<S&&>(sum).map(static_cast<Fn&&>(fn)); };
template <class S, class Fn>
concept ChainsWith = requires(S sum, Fn fn) { static_cast<S&&>(sum).and_then(static_cast<Fn&&>(fn)); };
template <class S, class Fn>
concept FiltersWith = requires(S sum, Fn fn) { static_cast<S&&>(sum).filter(static_cast<Fn&&>(fn)); };
template <class S, class Fn>
concept MapsErrorWith = requires(S sum, Fn fn) { static_cast<S&&>(sum).map_err(static_cast<Fn&&>(fn)); };
template <class S, class Fn>
concept RecoversWith = requires(S sum, Fn fn) { static_cast<S&&>(sum).or_else(static_cast<Fn&&>(fn)); };
template <class S, class Fn>
concept TestsSomeWith = requires(S const& sum, Fn fn) { sum.is_some_and(static_cast<Fn&&>(fn)); };
template <class S, class Fn>
concept TestsNoneOrWith = requires(S const& sum, Fn fn) { sum.is_none_or(static_cast<Fn&&>(fn)); };
template <class S, class Fn>
concept TestsOkWith = requires(S const& sum, Fn fn) { sum.is_ok_and(static_cast<Fn&&>(fn)); };
template <class S, class Fn>
concept TestsErrWith = requires(S const& sum, Fn fn) { sum.is_err_and(static_cast<Fn&&>(fn)); };
using ChainsWithToken = decltype([held = Token{1}](auto) noexcept { return Option<int>::some(held.handle); });
using ChainsWithNoToken = decltype([](auto) noexcept { return Option<int>{}; });
using ResultsWithToken = decltype([held = Token{1}](auto) noexcept { return Result<int, Code>{held.handle}; });
using ResultsWithNoToken = decltype([](auto) noexcept { return Result<int, Code>{0}; });
using TestsWithToken = decltype([held = Token{1}](auto const&) noexcept { return held.handle == 0; });
using TestsWithNoToken = decltype([](auto const&) noexcept { return true; });
using KeepsCodeWithToken = decltype([held = Token{1}](Code code) noexcept {
    static_cast<void>(held);
    return code;
});
using KeepsCodeWithNoToken = decltype([](Code code) noexcept { return code; });
static_assert(MapsWith<Option<int>, HoldsNoTokenArm> && !MapsWith<Option<int>, HoldsTokenArm>);
static_assert(ChainsWith<Option<int>, ChainsWithNoToken> && !ChainsWith<Option<int>, ChainsWithToken>);
static_assert(FiltersWith<Option<int>, TestsWithNoToken> && !FiltersWith<Option<int>, TestsWithToken>);
static_assert(TestsSomeWith<Option<int>, TestsWithNoToken> && !TestsSomeWith<Option<int>, TestsWithToken>);
static_assert(TestsNoneOrWith<Option<int>, TestsWithNoToken> && !TestsNoneOrWith<Option<int>, TestsWithToken>);
static_assert(MapsWith<Result<int, Code>, HoldsNoTokenArm> && !MapsWith<Result<int, Code>, HoldsTokenArm>);
static_assert(MapsErrorWith<Result<int, Code>, KeepsCodeWithNoToken>
              && !MapsErrorWith<Result<int, Code>, KeepsCodeWithToken>);
static_assert(ChainsWith<Result<int, Code>, ResultsWithNoToken> && !ChainsWith<Result<int, Code>, ResultsWithToken>);
static_assert(RecoversWith<Result<int, Code>, ResultsWithNoToken>
              && !RecoversWith<Result<int, Code>, ResultsWithToken>);
static_assert(TestsOkWith<Result<int, Code>, TestsWithNoToken> && !TestsOkWith<Result<int, Code>, TestsWithToken>);
static_assert(TestsErrWith<Result<int, Code>, TestsWithNoToken> && !TestsErrWith<Result<int, Code>, TestsWithToken>);

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

// ok() drops the error, and the rvalue err() drops the value.  So a Result
// with a linear error has no ok(), and a Result of a linear value has no
// err() that gives a linear error.
template <class R>
concept GivesItsValue = requires(R result) { static_cast<R&&>(result).ok(); };
static_assert(!GivesItsValue<Result<int, Token>> && GivesItsValue<Result<Token, Code>>);
static_assert(!GivesItsError<Result<HoldsToken, Token>> && !CopiesItsError<Result<HoldsToken, Token>>);

// The union of the storage cannot move, so the trait reads no member of it.
// Each sum with a linear alternative carries the linear marker, so the trait
// and the rules of the family linear read the sum as linear.
static_assert(!std::is_move_constructible_v<Alternatives<NoPayload, Token>>);
static_assert(::foundation::linear::is_linear_v<Option<Token>>
              && ::foundation::linear::is_linear_v<Option<HoldsToken>>);
static_assert(::foundation::linear::is_linear_v<Result<Token, Code>>);
static_assert(::foundation::linear::is_linear_v<Result<int, Token>>);
static_assert(!::foundation::linear::is_linear_v<Option<Owner>> && !::foundation::linear::is_linear_v<Option<int>>);
static_assert(!::foundation::linear::is_linear_v<Option<SlotNumber>>);
static_assert(!::foundation::linear::is_linear_v<Result<Owner, Code>>);

// The roads of a linear error in a constant evaluation: the rvalue err()
// and the rvalue match give the error once, and the const match reads it.
[[nodiscard]] consteval Result<int, Token> refused_with(int handle) noexcept { return err(Token{handle}); }
static_assert(refused_with(5).err().expect("the operation was refused").handle == 5);
static_assert(refused_with(6).match([](int) noexcept { return 0; }, [](Token token) noexcept { return token.handle; })
              == 6);
[[nodiscard]] consteval int read_refusal() noexcept {
    Result<int, Token> refused = refused_with(7);
    Result<int, Token> const& reader = refused;
    int const handle =
        reader.match([](int const&) noexcept { return 0; }, [](Token const& token) noexcept { return token.handle; });
    ::foundation::linear::drop(static_cast<Result<int, Token>&&>(refused).err().expect("refused holds an error"));
    return handle;
}
static_assert(read_refusal() == 7);

// ── Assignment ────────────────────────────────────────────────────────

// An owning sum drops what it holds in a move assignment, so a linear part
// refuses the assignment.  A plain sum keeps its trivial assignment.
static_assert(!std::is_move_assignable_v<Option<Token>> && !std::is_move_assignable_v<Option<HoldsToken>>);
static_assert(std::is_nothrow_move_assignable_v<Result<Owner, Code>>);
static_assert(!std::is_copy_assignable_v<Result<Owner, Code>>);
static_assert(!std::is_move_assignable_v<Result<int, Token>> && !std::is_move_assignable_v<Result<Token, Code>>);
static_assert(std::is_trivially_copy_assignable_v<Result<int, Code>>);
static_assert(std::is_trivially_copy_assignable_v<Option<SlotNumber>>);

// The move assignment of an owning Option in each state: a full target
// drops its payload, and the source is empty after the call.
[[nodiscard]] consteval int assigned_owner(bool target_holds, bool source_holds) noexcept {
    Option<Owner> target = target_holds ? Option<Owner>::some(Owner{1}) : Option<Owner>{};
    Option<Owner> source = source_holds ? Option<Owner>::some(Owner{2}) : Option<Owner>{};
    target = static_cast<Option<Owner>&&>(source);
    if (source.is_some()) return -1;
    return target.match([](Owner const& owner) noexcept { return owner.handle; }, [] noexcept { return 0; });
}
static_assert(assigned_owner(true, true) == 2 && assigned_owner(false, true) == 2);
static_assert(assigned_owner(true, false) == 0 && assigned_owner(false, false) == 0);

// Moves the source into the target through two names, as an alias of two
// references does in a program.  The kind self-move of the rule must-consume
// refuses a self-move that one function names.
constexpr void move_into(Option<Owner>& target, Option<Owner>& source) noexcept {
    target = static_cast<Option<Owner>&&>(source);
}

// A self assignment keeps the payload.  The move goes through two names.
[[nodiscard]] consteval int self_assigned_owner() noexcept {
    Option<Owner> held = Option<Owner>::some(Owner{3});
    move_into(held, held);
    return held.match([](Owner const& owner) noexcept { return owner.handle; }, [] noexcept { return 0; });
}
static_assert(self_assigned_owner() == 3);

// `option = none` empties an owning Option.
[[nodiscard]] consteval bool cleared_owner() noexcept {
    Option<Owner> held = Option<Owner>::some(Owner{4});
    held = none;
    return held.is_none();
}
static_assert(cleared_owner());

// The move assignment of an owning Result changes the active part.
[[nodiscard]] consteval int assigned_result() noexcept {
    Result<Owner, Code> target{Owner{5}};
    Result<Owner, Code> source{err(Code::closed)};
    target = static_cast<Result<Owner, Code>&&>(source);
    if (target.is_ok()) return -1;
    target = Result<Owner, Code>{Owner{6}};
    return target.match([](Owner const& owner) noexcept { return owner.handle; }, [](Code) noexcept { return 0; });
}
static_assert(assigned_result() == 6);

// The gate of Option needs a complete payload, so a payload names its own
// Option only where the class is complete, such as in a member function
// body.  The move assignment of that Option reads the payload there.
struct NamesItsOption {
    Owner owner{};
    [[nodiscard]] static consteval bool has_assignable_option() noexcept {
        return std::is_nothrow_move_assignable_v<Option<NamesItsOption>>;
    }
};
static_assert(NamesItsOption::has_assignable_option());

// ── Equality ──────────────────────────────────────────────────────────

// A compare reads an Equatable part: a leaf, or a class with an == that
// gives bool and does not throw.  A float and a double compare by their bits
// (the float rule of foundation/core/Equatable.h), so an Option of a float
// has ==.  A long double holds padding bits and is refused.
static_assert(Equatable<int> && Equatable<Code> && Equatable<Unit>);
static_assert(Equatable<double> && Equatable<float> && !Equatable<long double> && !Equatable<Owner>);

// The float rule: +0 and -0 differ, and the canonical NaN equals itself.
static_assert(Option<double>::some(0.0) != Option<double>::some(-0.0));
static_assert(Option<double>::some(quiet_nan_of<double>) == Option<double>::some(quiet_nan_of<double>));
static_assert(Option<float>::some(quiet_nan_of<float>) == Option<float>::some(quiet_nan_of<float>));
static_assert(Option<float>::some(1.5f) == Option<float>::some(1.5f));

// Two sums of one type compare their states and their active parts.
static_assert(Option<int>::some(3) == Option<int>::some(3));
static_assert(Option<int>::some(3) != Option<int>::some(4));
static_assert(Option<int>{} == Option<int>{} && Option<int>::some(3) != Option<int>{});
static_assert(Option<std::uint64_t>::some(std::uint64_t{9}) == Option<std::uint64_t>::some(std::uint64_t{9}));
static_assert(Result<int, Code>{2} == Result<int, Code>{2} && Result<int, Code>{2} != Result<int, Code>{3});
static_assert(Result<int, Code>{err(Code::closed)} == Result<int, Code>{err(Code::closed)});
static_assert(Result<int, Code>{err(Code::closed)} != Result<int, Code>{err(Code::full)});
static_assert(Result<int, Code>{err(Code::closed)} != Result<int, Code>{2});
static_assert(Result<Unit, Code>{Unit{}} == Result<Unit, Code>{Unit{}});

// A niche payload with a compare.  The niche Option reads its state from
// the payload, so its compare tests the niche of each side.
struct Ticket {
    std::uint32_t raw = 1;
    [[nodiscard]] static constexpr Ticket niche_empty(niche_key<Ticket> const&) noexcept { return Ticket{0}; }
    [[nodiscard]] constexpr bool is_niche_empty(niche_key<Ticket> const&) const noexcept { return raw == 0; }
    [[nodiscard]] friend constexpr bool operator==(Ticket, Ticket) noexcept = default;
};

static_assert(sizeof(Option<Ticket>) == sizeof(Ticket));
static_assert(Option<Ticket>::some(Ticket{2}) == Option<Ticket>::some(Ticket{2}));
static_assert(Option<Ticket>::some(Ticket{2}) != Option<Ticket>::some(Ticket{3}));
static_assert(Option<Ticket>{} == Option<Ticket>{} && Option<Ticket>::some(Ticket{2}) != Option<Ticket>{});

// ── The statement that no door builds the empty value ─────────────────

// A niche that states that no door builds the empty value.  The statement is
// a constant of type bool, and the payload gate refuses a statement of a
// different type.  A class of the member protocol states it with the member
// is_niche_empty_never_built, which niche<T> reads.
static_assert(NicheRead<TrustedSlot>::is_empty_never_built());
static_assert(!NicheRead<SlotNumber>::is_empty_never_built() && !NicheRead<Ticket>::is_empty_never_built());
static_assert(!ChoicePayload<MisstatedSlot>);

// A slot number of the member protocol that states the fact.  Its one door
// builds a slot from a byte.
class TrustedMemberSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit TrustedMemberSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    static constexpr niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr TrustedMemberSlot of_byte(std::uint8_t byte) noexcept {
        return TrustedMemberSlot{byte};
    }
    [[nodiscard]] static constexpr TrustedMemberSlot niche_empty(niche_key<TrustedMemberSlot> const&) noexcept {
        return TrustedMemberSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(niche_key<TrustedMemberSlot> const&) const noexcept {
        return raw_ == 0xFFFFFFFFu;
    }
};

static_assert(NicheRead<TrustedMemberSlot>::is_empty_never_built() && !NicheRead<MemberSlot>::is_empty_never_built());

// The payload gate holds the three checks of the statement, so
// a false statement does not compile.  An aggregate builds the empty value
// from its member, and a constructor from a pointer with no key builds the
// empty value from the null pointer.  The two trusted slots build a slot
// from a byte in a static door that takes no voucher, so the walk of the
// doors refuses them.  They pass the walk of the members, and this file is
// outside the base, so the gate refuses them a third time.
static_assert(!ChoicePayload<AggregateStatedSlot> && !ChoicePayload<PointerStatedSlot>);
static_assert(!BuildsNoEmptyValue<TrustedSlot> && !BuildsNoEmptyValue<TrustedMemberSlot>);
static_assert(!vouches_each_door(^^TrustedSlot) && !vouches_each_door(^^TrustedMemberSlot));
static_assert(WritesNoEmptyValue<TrustedSlot> && WritesNoEmptyValue<TrustedMemberSlot>);
static_assert(!StatedInBase<TrustedSlot> && !StatedInBase<TrustedMemberSlot>);
static_assert(!ChoicePayload<TrustedSlot> && !ChoicePayload<TrustedMemberSlot>);

// The walk of the constructors (1 of the check of the statement).  Each
// constructor other than copy and move takes a voucher.  A key has no
// constructor that a class outside can call, so a public class that only
// refuses its copy vouches for nothing, and so does a class with protected
// constructors, which a class outside builds through a derived class.
struct RawWord {
    std::uint32_t value = 0;
};
struct EmptyTag {};
class LocalKey {
    constexpr LocalKey() noexcept {}
    constexpr LocalKey(LocalKey const&) noexcept {}

public:
    LocalKey& operator=(LocalKey const&) = delete("a key lives for one construction");
    static constexpr niche_statement is_niche_voucher{};
};
struct OpenVoucher {
    constexpr OpenVoucher() noexcept = default;
    OpenVoucher(OpenVoucher const&) = delete("a voucher does not copy");
};
class ProtectedKey {
protected:
    constexpr ProtectedKey() noexcept {}
    constexpr ProtectedKey(ProtectedKey const&) noexcept {}

public:
    ProtectedKey& operator=(ProtectedKey const&) = delete("a key lives for one construction");
};
struct OpenedKey : ProtectedKey {
    constexpr OpenedKey() noexcept = default;
};
// A key vouches only with the mark of a key of the base, which niche_key
// states.  OutsideKey states no mark.  LocalKey stands in a namespace of the
// base, but its mark reads this file, which is no header of the base.  So
// the two vouch for nothing.  A class with protected constructors is no key
// even with a mark, because a class outside reaches its constructor.
static_assert(is_voucher_type(^^niche_key<KeyedPairSlot>) && is_base_key(^^niche_key<KeyedPairSlot>));
static_assert(!is_voucher_type(^^::OutsideKey) && !is_base_key(^^::OutsideKey));
static_assert(!is_voucher_type(^^LocalKey) && !is_base_key(^^LocalKey));
static_assert(!is_voucher_type(^^OpenVoucher) && !is_voucher_type(^^RawWord));
static_assert(!is_voucher_type(^^ProtectedKey) && has_reachable_constructor(^^ProtectedKey));
static_assert(!has_reachable_constructor(^^LocalKey) && !has_reachable_constructor(^^niche_key<KeyedPairSlot>));

// Constructors of the walk: a plain class around the raw word, a voucher that
// each caller builds, two raw values, a raw value beside a key, a raw value
// beside a key with protected constructors, a door from an empty tag and a
// protected door from a raw word.
class WrappedRawSlot {
    std::uint32_t raw_ = 0;

public:
    constexpr explicit WrappedRawSlot(RawWord word) noexcept : raw_{word.value} {}
};
class OpenVoucherSlot {
    std::uint32_t raw_ = 0;

public:
    constexpr OpenVoucherSlot(std::uint32_t raw, OpenVoucher const& /*voucher*/) noexcept : raw_{raw} {}
};
class ProtectedKeySlot {
    std::uint32_t raw_ = 0;

public:
    constexpr ProtectedKeySlot(std::uint32_t raw, ProtectedKey const& /*key*/) noexcept : raw_{raw} {}
};
class TagSlot {
    std::uint32_t raw_ = 1;

public:
    constexpr explicit TagSlot(EmptyTag /*tag*/) noexcept {}
};
class ProtectedDoorSlot {
    std::uint32_t raw_ = 1;

protected:
    constexpr explicit ProtectedDoorSlot(std::uint32_t raw) noexcept : raw_{raw} {}
};
static_assert(!vouches_each_door(^^WrappedRawSlot) && !BuildsNoEmptyValue<WrappedRawSlot>);
static_assert(!vouches_each_door(^^OpenVoucherSlot) && !BuildsNoEmptyValue<OpenVoucherSlot>);
static_assert(!vouches_each_door(^^PairStatedSlot) && !BuildsNoEmptyValue<PairStatedSlot>);
static_assert(vouches_each_door(^^KeyedPairSlot) && BuildsNoEmptyValue<KeyedPairSlot>);
static_assert(!vouches_each_door(^^ProtectedKeySlot) && !BuildsNoEmptyValue<ProtectedKeySlot>);
// A door from a tag carries nothing, so its body picks a constant, and the
// constant can be the empty value, as for a door from nothing.
static_assert(!vouches_each_door(^^TagSlot) && !BuildsNoEmptyValue<TagSlot>);
// A class that derives from the slot calls the protected door, and a copy of
// the derived value gives its base part as a slot.
static_assert(!vouches_each_door(^^ProtectedDoorSlot) && !BuildsNoEmptyValue<ProtectedDoorSlot>);

// Static doors of the walk, judged as constructors: a door from nothing, a
// door that gives a wrapper of the slot from a raw word, a door that gives a
// plain class with a field of the slot, a door from a raw word beside a key
// of the base, and a door from an empty tag.  A member door that is not
// static reads a slot that is not empty, so the walk of the doors does not
// judge it: its object is a voucher.
class NothingDoorSlot {
    std::uint32_t raw_ = 1;
    constexpr NothingDoorSlot() noexcept = default;

public:
    [[nodiscard]] static constexpr NothingDoorSlot make() noexcept { return NothingDoorSlot{}; }
};
template <class Held>
struct WrappedDoorValue {
    Held held;
};
class WrappedDoorSlot {
    std::uint32_t raw_ = 1;
    constexpr explicit WrappedDoorSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    [[nodiscard]] static constexpr WrappedDoorValue<WrappedDoorSlot> of(std::uint32_t raw) noexcept {
        return WrappedDoorValue<WrappedDoorSlot>{WrappedDoorSlot{raw}};
    }
};
struct HeldDoorValue;
class HolderDoorSlot {
    std::uint32_t raw_ = 1;
    constexpr explicit HolderDoorSlot(std::uint32_t raw) noexcept : raw_{raw} {}
    friend struct HeldDoorValue;

public:
    [[nodiscard]] static constexpr HeldDoorValue of(std::uint32_t raw) noexcept;
};
struct HeldDoorValue {
    HolderDoorSlot held;
};
constexpr HeldDoorValue HolderDoorSlot::of(std::uint32_t raw) noexcept { return HeldDoorValue{HolderDoorSlot{raw}}; }
class KeyedDoorSlot {
    std::uint32_t raw_ = 1;
    constexpr explicit KeyedDoorSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    [[nodiscard]] static constexpr KeyedDoorSlot of(std::uint32_t raw,
                                                    niche_key<KeyedDoorSlot> const& /*key*/) noexcept {
        return KeyedDoorSlot{raw};
    }
    [[nodiscard]] constexpr KeyedDoorSlot next(std::uint32_t step) const noexcept { return KeyedDoorSlot{raw_ + step}; }
};
class TagDoorSlot {
    std::uint32_t raw_ = 1;
    constexpr explicit TagDoorSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    [[nodiscard]] static constexpr TagDoorSlot of_tag(EmptyTag /*tag*/) noexcept { return TagDoorSlot{1u}; }
};
static_assert(!vouches_each_door(^^NothingDoorSlot) && !vouches_each_door(^^WrappedDoorSlot));
static_assert(!vouches_each_door(^^HolderDoorSlot) && vouches_each_door(^^KeyedDoorSlot));
// A static door from a tag carries nothing, as a door from nothing does.
static_assert(!vouches_each_door(^^TagDoorSlot));
static_assert(names_class(^^WrappedDoorValue<WrappedDoorSlot>, ^^WrappedDoorSlot)
              && names_class(^^WrappedDoorSlot const&, ^^WrappedDoorSlot)
              && names_class(^^Option<KeyedDoorSlot>, ^^KeyedDoorSlot));
static_assert(!names_class(^^int, ^^WrappedDoorSlot) && !names_class(^^HeldDoorValue, ^^HolderDoorSlot));
// holds_class reads the bases and the fields of each access of a class, and
// the target of a pointer or a reference.  A cycle of references reaches the
// depth of the walk, and the walk fails closed there.
struct PointsAtHeld {
    HeldDoorValue const& at;
};
struct LinkedNode {
    LinkedNode const& next;
};
constexpr auto held_door_slot = side_as<side::HeldClass>(^^HolderDoorSlot);
constexpr auto held_keyed_slot = side_as<side::HeldClass>(^^KeyedDoorSlot);
static_assert(holds_class(^^HeldDoorValue, held_door_slot) && holds_class(^^HeldDoorValue const[2], held_door_slot));
static_assert(holds_class(^^PointsAtHeld, held_door_slot) && holds_class(^^HeldDoorValue const&, held_door_slot));
static_assert(holds_class(^^Option<KeyedDoorSlot>, held_keyed_slot) && holds_class(^^LinkedNode, held_keyed_slot));
static_assert(holds_class(^^OpenedKey, side_as<side::HeldClass>(^^ProtectedKey)));
static_assert(!holds_class(^^int, held_door_slot) && !holds_class(^^HeldDoorValue, held_keyed_slot));

// Static values of the walk, judged as doors with no parameter: a static
// data member of the slot, also a private one, and a member variable
// template.  A static data member of a different type, such as a statement,
// is no door.  The door of StatementValueSlot takes a key, because a door
// from a tag is refused (TagSlot).  An array of the slot names the slot.
class StaticValueSlot {
    std::uint32_t raw_ = 1;
    constexpr explicit StaticValueSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    static StaticValueSlot const empty_value;
};
class PrivateValueSlot {
    std::uint32_t raw_ = 1;
    constexpr explicit PrivateValueSlot(std::uint32_t raw) noexcept : raw_{raw} {}
    static PrivateValueSlot const empty_value_;
};
class StatementValueSlot {
    std::uint32_t raw_ = 1;

public:
    constexpr StatementValueSlot(std::uint32_t raw, niche_key<StatementValueSlot> const& /*key*/) noexcept
        : raw_{raw} {}
    static constexpr niche_statement is_niche_empty_never_built{};
};
inline constexpr StaticValueSlot StaticValueSlot::empty_value{0xFFFFFFFFu};
inline constexpr PrivateValueSlot PrivateValueSlot::empty_value_{0xFFFFFFFFu};
class VariableTemplateSlot {
    std::uint32_t raw_ = 1;

public:
    template <std::uint32_t Raw>
    static constexpr std::uint32_t word_of = Raw;
};
static_assert(!vouches_each_door(^^StaticValueSlot) && !BuildsNoEmptyValue<StaticValueSlot>);
// A friend or a nested class copies a private static value of the slot.
static_assert(!vouches_each_door(^^PrivateValueSlot) && !BuildsNoEmptyValue<PrivateValueSlot>);
static_assert(!vouches_each_door(^^VariableTemplateSlot) && vouches_each_door(^^StatementValueSlot));

// A public constructor template is refused, because the walk reads no
// parameter of a template.  A constrained constructor that is not a template
// is read, as the widening constructor of a Ref is.  members_of lists no
// constructor whose constraint fails, so the walk reads such a constructor
// only in an instance where it can be called.
class ConstructorTemplateSlot {
    std::uint32_t raw_ = 1;

public:
    template <class Tag>
    constexpr explicit ConstructorTemplateSlot(Tag /*tag*/) noexcept {}
};
template <class Tag>
class ConstrainedSlot {
    std::uint32_t raw_ = 1;

public:
    constexpr ConstrainedSlot(Tag /*tag*/, niche_key<ConstrainedSlot> const& /*key*/) noexcept
        requires std::is_empty_v<Tag>
    {}
};
template <class Tag>
class ConstrainedTagSlot {
    std::uint32_t raw_ = 1;

public:
    constexpr explicit ConstrainedTagSlot(Tag /*tag*/) noexcept
        requires std::is_empty_v<Tag>
    {}
};
static_assert(!vouches_each_door(^^ConstructorTemplateSlot) && !BuildsNoEmptyValue<ConstructorTemplateSlot>);
static_assert(vouches_each_door(^^ConstrainedSlot<EmptyTag>) && BuildsNoEmptyValue<ConstrainedSlot<EmptyTag>>);
static_assert(!vouches_each_door(^^ConstrainedTagSlot<EmptyTag>) && vouches_each_door(^^ConstrainedTagSlot<RawWord>));
static_assert(names_class(^^StaticValueSlot const[2], ^^StaticValueSlot));

// The walk of the members (2 of the check of the statement).  Each class
// below has one member that can write a value in place: a member function
// that is not const, a member function template, a public data member, a
// mutable member, a static member function with a parameter that can write,
// and a base class.  A const member function and a deleted function write
// nothing.
class MutatorSlot {
    std::uint32_t raw_ = 1;

public:
    constexpr void set_word(std::uint32_t raw) noexcept { raw_ = raw; }
};
class MemberTemplateSlot {
    std::uint32_t raw_ = 1;

public:
    template <std::uint32_t Raw>
    [[nodiscard]] constexpr std::uint32_t with_word() const noexcept {
        return raw_ + Raw;
    }
};
class PublicFieldSlot {
    std::uint32_t hidden_ = 1;

public:
    std::uint32_t word = 1;
    [[nodiscard]] constexpr std::uint32_t hidden() const noexcept { return hidden_; }
};
class MutableSlot {
    mutable std::uint32_t raw_ = 1;

public:
    constexpr void clear() const noexcept { raw_ = 0; }
};
class StaticWriterSlot {
    std::uint32_t raw_ = 1;

public:
    static constexpr void clear(StaticWriterSlot& slot) noexcept { slot.raw_ = 0; }
};
class BasedSlot : EmptyTag {
    std::uint32_t raw_ = 1;

public:
    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return raw_; }
};
class ReaderSlot {
    std::uint32_t raw_ = 1;

public:
    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return raw_; }
    [[nodiscard]] static constexpr std::uint32_t raw_of(ReaderSlot const& slot) noexcept { return slot.raw_; }
    void clear() = delete("a reader writes nothing");
};
// A class that derives from the slot reaches a protected member, and a copy
// of the derived value gives its base part as a slot.
class ProtectedWriterSlot {
    std::uint32_t raw_ = 1;

protected:
    constexpr void set_word(std::uint32_t raw) noexcept { raw_ = raw; }
};
class ProtectedFieldSlot {
protected:
    std::uint32_t word_ = 1;
};
static_assert(!WritesNoEmptyValue<MutatorSlot> && !WritesNoEmptyValue<MemberTemplateSlot>);
static_assert(!WritesNoEmptyValue<PublicFieldSlot> && !WritesNoEmptyValue<MutableSlot>);
static_assert(!WritesNoEmptyValue<StaticWriterSlot> && !WritesNoEmptyValue<BasedSlot>);
static_assert(!WritesNoEmptyValue<ProtectedWriterSlot> && !WritesNoEmptyValue<ProtectedFieldSlot>);
static_assert(WritesNoEmptyValue<ReaderSlot>);

// The walk of the nested classes.  A nested class has the access of the slot,
// so each nested class of each access passes the arms of the own doors: a
// function that gives the slot takes a voucher, a field that holds the slot
// is refused, a writable parameter that holds the slot is refused, and a
// member template is refused.  A private empty tag, as the NullTag of a Ref,
// a reader and a keyed door pass.
class NestedTagSlot {
    std::uint32_t raw_ = 1;
    struct NullTag {};
    constexpr explicit NestedTagSlot(NullTag /*tag*/) noexcept {}
};
class NestedReaderSlot {
    std::uint32_t raw_ = 1;
    constexpr explicit NestedReaderSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    struct Door {
        [[nodiscard]] static constexpr bool is_first(NestedReaderSlot const& slot) noexcept { return slot.raw_ == 1; }
        [[nodiscard]] static constexpr NestedReaderSlot of(std::uint32_t raw,
                                                           niche_key<NestedReaderSlot> const& /*key*/) noexcept {
            return NestedReaderSlot{raw};
        }
    };
};
class NestedDoorSlot {
    std::uint32_t raw_ = 1;
    constexpr explicit NestedDoorSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    struct Door {
        [[nodiscard]] static constexpr NestedDoorSlot empty_value() noexcept { return NestedDoorSlot{0xFFFFFFFFu}; }
    };
};
class NestedMakerSlot {
    std::uint32_t raw_ = 1;
    constexpr explicit NestedMakerSlot(std::uint32_t raw) noexcept : raw_{raw} {}
    struct Maker {
        [[nodiscard]] constexpr NestedMakerSlot make() const noexcept { return NestedMakerSlot{0xFFFFFFFFu}; }
    };
};
class NestedConversionSlot {
    std::uint32_t raw_ = 1;
    constexpr explicit NestedConversionSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    struct Converter {
        constexpr operator NestedConversionSlot() const noexcept { return NestedConversionSlot{0xFFFFFFFFu}; }
    };
};
class NestedFieldSlot {
    std::uint32_t raw_ = 1;
    constexpr explicit NestedFieldSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    struct Holder;
};
struct NestedFieldSlot::Holder {
    NestedFieldSlot value{0xFFFFFFFFu};
};
class NestedWriterSlot {
    std::uint32_t raw_ = 1;
    constexpr explicit NestedWriterSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    struct Writer {
        static constexpr void clear(NestedWriterSlot& slot) noexcept { slot.raw_ = 0xFFFFFFFFu; }
    };
};
class NestedTemplateSlot {
    std::uint32_t raw_ = 1;
    constexpr explicit NestedTemplateSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    template <class Unused>
    struct Door {};
};
static_assert(vouches_each_door(^^NestedTagSlot) && WritesNoEmptyValue<NestedTagSlot>);
static_assert(vouches_each_door(^^NestedReaderSlot) && WritesNoEmptyValue<NestedReaderSlot>);
static_assert(!vouches_each_door(^^NestedDoorSlot) && !vouches_each_door(^^NestedMakerSlot));
static_assert(!vouches_each_door(^^NestedConversionSlot) && !vouches_each_door(^^NestedFieldSlot));
static_assert(!vouches_each_door(^^NestedTemplateSlot) && !vouches_each_door(^^NestedWriterSlot));

// The place of the statement (3 of the check of the statement).  A header of
// the two layers below the include root of the anchor holds a statement, and
// a path that leaves the layers through a segment that starts with a dot
// holds none.  An anchor with a different path admits no file.  The unary
// plus gives the pointer of a literal, which the Side holds.  The mark of
// this file stands outside the base, and its copy bit for bit is refused.
constexpr niche_statement check_statement{};
static_assert(!check_statement.is_in_base() && !std::is_trivially_copyable_v<niche_statement>);
static_assert(!is_base_statement(^^check_statement) && !is_base_statement(std::meta::info{}));
constexpr auto check_anchor = side_as<side::ChoiceHeader>(+"/tree/include/foundation/core/Choice.h");
static_assert(is_layer_header(check_anchor, "/tree/include/foundation/core/Ref.h"));
static_assert(is_layer_header(check_anchor, "/tree/include/fixy/Core.h"));
static_assert(!is_layer_header(check_anchor, "/tree/include/crucible/Vigil.h"));
static_assert(!is_layer_header(check_anchor, "/tree/test/foundation/neg/fixture.cpp"));
static_assert(!is_layer_header(check_anchor, "/elsewhere/include/foundation/core/Ref.h"));
static_assert(!is_layer_header(check_anchor, "/tree/include/foundation/../../test/forged.h"));
static_assert(!is_layer_header(check_anchor, "/tree/include/fixy/./Core.h"));
static_assert(!is_layer_header(check_anchor, "/tree"));
static_assert(!is_layer_header(side_as<side::ChoiceHeader>(+"/tree/include/Choice.h"), "/tree/include/fixy/Core.h"));

// A sum compares only with a sum of its own type.  The implicit
// constructors make no compare with none, a payload, an Err or a sum of a
// different type.  A payload with no compare gives the sum no compare, and a
// double payload compares by its bits.
template <class Left, class Right>
concept Compares = requires(Left const& left, Right const& right) { left == right; };
static_assert(Compares<Option<int>, Option<int>> && Compares<Result<int, Code>, Result<int, Code>>);
static_assert(!Compares<Option<int>, NoValue> && !Compares<NoValue, Option<int>>);
static_assert(!Compares<Option<int>, int> && !Compares<Option<int>, Option<long>>);
static_assert(!Compares<Result<int, Code>, int> && !Compares<Result<int, Code>, Err<Code>>);
static_assert(!Compares<Result<int, Code>, Result<long, Code>>);
static_assert(Compares<Option<double>, Option<double>> && !Compares<Option<Owner>, Option<Owner>>);
static_assert(!Compares<Option<long double>, Option<long double>>);

// ── A lookup that a payload type cannot steer ─────────────────────────

// A payload type and an overload of alternative_of_ beside it.  The
// overload is more constrained than the reader of a sum, so an unqualified
// call would reach it through argument-dependent lookup, and its body
// would stop the compile.  Each call of the family names
// detail::alternative_of_, so the overload stays out.
namespace lookup_probe {

struct Plain {
    int value = 0;
};

struct Owned {
    int handle = 0;
    constexpr explicit Owned(int start) noexcept : handle{start} {}
    constexpr Owned(Owned&& other) noexcept : handle{other.handle} { other.handle = 0; }
    Owned(Owned const&) = delete;
    Owned& operator=(Owned const&) = delete;
    Owned& operator=(Owned&&) = delete;
    constexpr ~Owned() {}
};

template <std::size_t Index, class Storage>
    requires(Index < 256)
constexpr auto& alternative_of_(Storage& storage) noexcept {
    static_assert(false, "argument-dependent lookup reached an overload beside a payload type");
    return storage;
}

}  // namespace lookup_probe

static_assert(Option<lookup_probe::Plain>::some(lookup_probe::Plain{7}).value_or(lookup_probe::Plain{}).value == 7);
static_assert(Result<lookup_probe::Plain, Code>{lookup_probe::Plain{8}}.expect("a constant value").value == 8);
[[nodiscard]] consteval int owned_handles() noexcept {
    Option<lookup_probe::Owned> const held = Option<lookup_probe::Owned>::some(lookup_probe::Owned{5});
    Result<lookup_probe::Owned, Code> const result{lookup_probe::Owned{6}};
    int const option_handle =
        held.match([](lookup_probe::Owned const& owned) noexcept { return owned.handle; }, [] noexcept { return 0; });
    int const result_handle = result.match([](lookup_probe::Owned const& owned) noexcept { return owned.handle; },
                                           [](Code) noexcept { return 0; });
    return option_handle * 10 + result_handle;
}
static_assert(owned_handles() == 56);

// ── The combinators ───────────────────────────────────────────────────

// The function objects of the probes: each takes a Part and gives a Given.
// The calls are only declared, because no probe runs them.  The probes are
// variable templates and not concepts, because they gate nothing.
template <class Part, class Given>
struct GivesFrom {
    [[nodiscard]] Given operator()(Part) const noexcept;
};
template <class Part, class Given>
struct GivesFromReference {
    [[nodiscard]] Given operator()(Part const&) const noexcept;
};
template <class Sum, class Fn>
constexpr bool maps_with = requires(Sum sum, Fn fn) { static_cast<Sum&&>(sum).map(fn); };
template <class Sum, class Fn>
constexpr bool chains_with = requires(Sum sum, Fn fn) { static_cast<Sum&&>(sum).and_then(fn); };
template <class Sum, class Fn>
constexpr bool maps_error_with = requires(Sum sum, Fn fn) { static_cast<Sum&&>(sum).map_err(fn); };
template <class Sum, class Fn>
constexpr bool recovers_with = requires(Sum sum, Fn fn) { static_cast<Sum&&>(sum).or_else(fn); };
template <class Sum, class Fn>
constexpr bool filters_with = requires(Sum sum, Fn fn) { static_cast<Sum&&>(sum).filter(fn); };
template <class Sum, class Fn>
constexpr bool tests_with = requires(Sum const& sum, Fn fn) {
    { sum.is_some_and(fn) } -> std::same_as<bool>;
    { sum.is_none_or(fn) } -> std::same_as<bool>;
};
template <class Sum, class Fn>
constexpr bool tests_value_with = requires(Sum const& sum, Fn fn) {
    { sum.is_ok_and(fn) } -> std::same_as<bool>;
};
template <class Sum, class Fn>
constexpr bool tests_error_with = requires(Sum const& sum, Fn fn) {
    { sum.is_err_and(fn) } -> std::same_as<bool>;
};

// map and and_then of an Option consume it, as the rvalue match does, so a
// named or a const Option has none.  fn takes the payload by value with no
// conversion, and its result holds no borrow.  A linear payload moves into
// fn.  and_then takes an fn that gives an Option.
static_assert(maps_with<Option<int>, GivesFrom<int, std::int64_t>>);
static_assert(std::same_as<decltype(std::declval<Option<int>>().map(GivesFrom<int, Code>{})), Option<Code>>);
static_assert(!maps_with<Option<int>&, GivesFrom<int, std::int64_t>>);
static_assert(!maps_with<Option<int> const, GivesFrom<int, std::int64_t>>);
static_assert(!maps_with<Option<int>, GivesFromReference<int, std::int64_t>>);
static_assert(!maps_with<Option<int>, GivesFrom<std::int64_t, std::int64_t>>);
static_assert(!maps_with<Option<int>, GivesFrom<int, int const*>>);
static_assert(!maps_with<Option<int>, GivesFrom<int, TextView>>);
static_assert(maps_with<Option<Token>, GivesFrom<Token, int>> && maps_with<Option<Owner>, GivesFrom<Owner, Owner>>);
static_assert(chains_with<Option<int>, GivesFrom<int, Option<Code>>>);
static_assert(!chains_with<Option<int>, GivesFrom<int, Code>>);
static_assert(!chains_with<Option<int>, GivesFrom<int, Result<Code, Code>>>);
static_assert(!chains_with<Option<int>, GivesFrom<int, Option<int const*>>>);
static_assert(!chains_with<Option<int>, GivesFromReference<int, Option<Code>>>);
static_assert(!chains_with<Option<int>&, GivesFrom<int, Option<Code>>>);

// filter drops a refused payload, so a linear payload has none.  keep reads
// the payload through a const reference and gives exactly bool.
static_assert(filters_with<Option<int>, GivesFromReference<int, bool>>);
static_assert(filters_with<Option<Owner>, GivesFromReference<Owner, bool>>);
static_assert(!filters_with<Option<Token>, GivesFromReference<Token, bool>>);
static_assert(!filters_with<Option<int>, GivesFromReference<int, int>>);
static_assert(!filters_with<Option<int>&, GivesFromReference<int, bool>>);

// is_some_and and is_none_or read through a const reference, also of a
// linear payload, and the test gives exactly bool.
static_assert(tests_with<Option<int>, GivesFromReference<int, bool>>);
static_assert(tests_with<Option<Token>, GivesFromReference<Token, bool>>);
static_assert(!tests_with<Option<int>, GivesFromReference<int, int>>);

// map of a Result passes the error, and map_err passes the value, also a
// linear value.  The new error is an error value, so a plain integer is
// refused.  An fn that takes its part by reference binds to the deleted twin.
static_assert(
    std::same_as<decltype(std::declval<Result<int, Code>>().map(GivesFrom<int, Code32>{})), Result<Code32, Code>>);
static_assert(!maps_with<Result<int, Code>&, GivesFrom<int, Code32>>);
static_assert(!maps_with<Result<int, Code>, GivesFromReference<int, Code32>>);
static_assert(!maps_with<Result<int, Code>, GivesFrom<int, int const*>>);
static_assert(
    std::same_as<decltype(std::declval<Result<int, Code>>().map_err(GivesFrom<Code, Code32>{})), Result<int, Code32>>);
static_assert(maps_error_with<Result<Token, Code>, GivesFrom<Code, Code32>>);
static_assert(maps_error_with<Result<int, Token>, GivesFrom<Token, Code>>);
static_assert(!maps_error_with<Result<int, Code>, GivesFrom<Code, int>>);
static_assert(!maps_error_with<Result<int, Code>, GivesFrom<Code, Err<Code32>>>);
static_assert(!maps_error_with<Result<int, Code>, GivesFrom<Code, ReferringError>>);
static_assert(!maps_error_with<Result<int, Code>, GivesFromReference<Code, Code32>>);

// and_then of a Result converts the error as CRUCIBLE_TRY converts it, so an
// error with no implicit conversion is refused.  or_else gives a Result of
// the same value type, so the value passes with no conversion.
static_assert(chains_with<Result<int, Code>, GivesFrom<int, Result<std::int64_t, Code>>>);
static_assert(chains_with<Result<int, Code>, GivesFrom<int, Result<std::int64_t, NetError>>>);
static_assert(!chains_with<Result<int, Code>, GivesFrom<int, Result<std::int64_t, Code32>>>);
static_assert(!chains_with<Result<int, Code>, GivesFrom<int, Result<std::int64_t, ExplicitNetError>>>);
static_assert(!chains_with<Result<int, Code>, GivesFrom<int, Option<std::int64_t>>>);
static_assert(recovers_with<Result<int, Code>, GivesFrom<Code, Result<int, Code32>>>);
static_assert(!recovers_with<Result<int, Code>, GivesFrom<Code, Result<std::int64_t, Code32>>>);
static_assert(!recovers_with<Result<int, Code>, GivesFrom<Code, Option<int>>>);
static_assert(!recovers_with<Result<int, Code>, GivesFromReference<Code, Result<int, Code32>>>);

// is_ok_and and is_err_and read through a const reference, also a linear
// part.
static_assert(tests_value_with<Result<Token, Code>, GivesFromReference<Token, bool>>);
static_assert(tests_error_with<Result<int, Token>, GivesFromReference<Token, bool>>);
static_assert(!tests_value_with<Result<int, Code>, GivesFromReference<int, int>>);

// The values in a constant evaluation, on each path.  Each combinator gives
// the value of the match that it replaces.
[[nodiscard]] constexpr bool is_even(int const& value) noexcept { return value % 2 == 0; }
static_assert(Option<int>::some(3).map([](int value) noexcept { return value * 2; }) == Option<int>::some(6));
static_assert(Option<int>{none}.map([](int value) noexcept { return value * 2; }) == Option<int>{none});
static_assert(Option<Owner>::some(Owner{7}).map([](Owner owner) noexcept { return owner.handle; })
              == Option<int>::some(7));
static_assert(Option<Token>::some(Token{8})
                  .map([](Token token) noexcept { return token; })
                  .expect("map gives the token back")
                  .handle
              == 8);
static_assert(Option<SlotNumber>::some(SlotNumber{4}).map([](SlotNumber slot) noexcept { return slot.raw; })
              == Option<std::uint32_t>::some(4u));
static_assert(Option<int>::some(4).and_then([](int value) noexcept { return is_even(value) ? some(value) : none; })
              == Option<int>::some(4));
static_assert(Option<int>::some(3).and_then([](int value) noexcept -> Option<int> {
    if (is_even(value)) return some(value);
    return none;
}) == Option<int>{none});
static_assert(Option<int>{none}.and_then([](int value) noexcept { return some(value); }) == Option<int>{none});
static_assert(Option<int>::some(4).filter([](int const& value) noexcept { return is_even(value); })
              == Option<int>::some(4));
static_assert(Option<int>::some(3).filter([](int const& value) noexcept { return is_even(value); })
              == Option<int>{none});
static_assert(Option<int>{none}.filter([](int const& value) noexcept { return is_even(value); }) == Option<int>{none});
static_assert(Option<int>::some(4).is_some_and([](int const& value) noexcept { return is_even(value); }));
static_assert(!Option<int>::some(3).is_some_and([](int const& value) noexcept { return is_even(value); }));
static_assert(!Option<int>{none}.is_some_and([](int const& value) noexcept { return is_even(value); }));
static_assert(Option<int>::some(4).is_none_or([](int const& value) noexcept { return is_even(value); }));
static_assert(!Option<int>::some(3).is_none_or([](int const& value) noexcept { return is_even(value); }));
static_assert(Option<int>{none}.is_none_or([](int const& value) noexcept { return is_even(value); }));

static_assert(checked_half(8).map([](int value) noexcept { return value + 1; }) == Result<int, Code>{5});
static_assert(checked_half(7).map([](int value) noexcept { return value + 1; })
              == Result<int, Code>{err(Code::closed)});
static_assert(checked_half(7).map_err([](Code) noexcept { return Code32::closed; })
              == Result<int, Code32>{err(Code32::closed)});
static_assert(checked_half(8).map_err([](Code) noexcept { return Code32::closed; }) == Result<int, Code32>{4});
static_assert(checked_half(8).and_then([](int value) noexcept { return checked_half(value); }) == Result<int, Code>{2});
static_assert(checked_half(12).and_then([](int value) noexcept { return checked_half(value); })
              == Result<int, Code>{3});
static_assert(checked_half(12).and_then([](int value) noexcept { return checked_half(value + 1); })
              == Result<int, Code>{err(Code::closed)});
static_assert(checked_half(7)
                  .and_then([](int value) noexcept { return checked_quarter(value); })
                  .is_err_and([](NetError const& error) noexcept { return error.code == Code::closed; }));
static_assert(checked_half(7).or_else([](Code) noexcept { return Result<int, Code32>{9}; }) == Result<int, Code32>{9});
static_assert(checked_half(8).or_else([](Code) noexcept { return Result<int, Code32>{9}; }) == Result<int, Code32>{4});
static_assert(checked_half(7).or_else([](Code) noexcept { return Result<int, Code32>{err(Code32::full)}; })
              == Result<int, Code32>{err(Code32::full)});
static_assert(checked_half(8).is_ok_and([](int const& value) noexcept { return value == 4; }));
static_assert(!checked_half(7).is_ok_and([](int const&) noexcept { return true; }));
static_assert(checked_half(7).is_err_and([](Code const& code) noexcept { return code == Code::closed; }));
static_assert(!checked_half(8).is_err_and([](Code const&) noexcept { return true; }));
static_assert(refused_with(5)
                  .map_err([](Token token) noexcept { return token; })
                  .err()
                  .expect("map_err gives the token back")
                  .handle
              == 5);

// ── Doors of the tag ──────────────────────────────────────────────────

// The doors of the tag of Choice.h.  A door of a class is a member that can
// change an object of the class: a constructor, an assignment, the
// destructor, a member function that is not const, and each member function
// template, because the reflection does not tell if a template is const.  A
// deleted function is no door.  The reflection of GCC 16 gives each
// constructor template of a class template three times, as three reflections
// that are not equal, so each pin below counts each of them.  Complexity:
// linear in the members of the class, at compile time only.
template <class Class>
[[nodiscard]] consteval std::size_t door_count() {
    std::size_t count = 0;
    for (std::meta::info const member : std::meta::members_of(^^Class, std::meta::access_context::unchecked())) {
        bool const is_template = std::meta::is_function_template(member);
        if (!is_template && !std::meta::is_function(member)) continue;
        if (!is_template && (std::meta::is_const(member) || std::meta::is_deleted(member))) continue;
        ++count;
    }
    return count;
}

// The pinned count of the doors of a class.  A failed assertion names the
// pinned count and the counted one in its note.
template <std::size_t Pinned, std::size_t Counted>
inline constexpr bool doors_are_pinned = Pinned == Counted;

// The count sees a new door: a class with one more member function that
// writes its tag has one more door, and a const member is no door.
class TagProbe {
    unsigned char tag_ = 0;

public:
    [[nodiscard]] constexpr unsigned char tag() const noexcept { return tag_; }
};

class TagProbeWithWrite {
    unsigned char tag_ = 0;

public:
    [[nodiscard]] constexpr unsigned char tag() const noexcept { return tag_; }
    constexpr void copy_tag(TagProbeWithWrite const& other) noexcept { tag_ = other.tag_; }
};

static_assert(door_count<TagProbeWithWrite>() == door_count<TagProbe>() + 1);

// The doors of each class that holds or writes the tag of a sum.  A new door
// can write a tag, so a change of a count needs a constant evaluation of the
// new door below, and then the new count here.
using ProbeCells = TaggedCells<Alternatives<int, long>, unsigned long long>;
static_assert(doors_are_pinned<8, door_count<ProbeCells>()>, "a new door of the cells: test it, then pin the count");
static_assert(doors_are_pinned<11, door_count<SumSlotOf<true, NoPayload, std::uint64_t>>()>,
              "a new door of the plain slot: test it below, then pin the count");
static_assert(doors_are_pinned<10, door_count<SumSlotOf<true, NoPayload, SealedWord>>()>,
              "a new door of the plain slot of a sealed payload: test it below, then pin the count");
static_assert(doors_are_pinned<10, door_count<SumSlotOf<false, Owner, int>>()>,
              "a new door of the owning slot: test it below, then pin the count");
static_assert(doors_are_pinned<12, door_count<OptionSlot<std::uint64_t>>()>,
              "a new door of the plain Option slot: test it below, then pin the count");
static_assert(doors_are_pinned<11, door_count<OptionSlot<Owner>>()>,
              "a new door of the owning Option slot: test it below, then pin the count");
static_assert(doors_are_pinned<12, door_count<OptionSlot<SlotNumber>>()>,
              "a new door of the niche Option slot: test it below, then pin the count");
static_assert(doors_are_pinned<12, door_count<ResultSlot<std::uint64_t, Code>>()>,
              "a new door of the Result slot: test it below, then pin the count");
static_assert(doors_are_pinned<47, door_count<Option<std::uint64_t>>()>,
              "a new door of Option: test it below, then pin the count");
static_assert(doors_are_pinned<45, door_count<Result<std::uint64_t, Code>>()>,
              "a new door of Result: test it below, then pin the count");
static_assert(doors_are_pinned<31, door_count<OneOf<std::uint32_t, Code>>()>,
              "a new door of OneOf: test it below, then pin the count");
static_assert(doors_are_pinned<29, door_count<OneOf<Owner, int>>()>,
              "a new door of an owning OneOf: test it below, then pin the count");

// The constructor of the cells and of each slot takes the index of an
// alternative as a template argument, and it exists only for an index below
// the count of the alternatives.  No constructor takes a tag as a value.
static_assert(std::is_constructible_v<ProbeCells, AlternativeAt<1>, long>);
static_assert(!std::is_constructible_v<ProbeCells, AlternativeAt<2>, long>);
static_assert(!std::is_constructible_v<ProbeCells, unsigned long long, long>);
static_assert(!std::is_constructible_v<ProbeCells, unsigned long long, AlternativeAt<1>, long>);
static_assert(!std::is_constructible_v<Alternatives<int, long>, AlternativeAt<2>, long>);
static_assert(std::is_constructible_v<SumSlot<NoPayload, std::uint64_t>, AlternativeAt<1>, std::uint64_t>);
static_assert(!std::is_constructible_v<SumSlot<NoPayload, std::uint64_t>, AlternativeAt<2>, std::uint64_t>);
static_assert(std::is_constructible_v<SumSlot<Owner, int>, AlternativeAt<1>, int>);
static_assert(!std::is_constructible_v<SumSlot<Owner, int>, AlternativeAt<2>, int>);

// The state of an Option through its borrow match: 0 for none, and the word
// of the payload plus 1 for a payload.  The match reads the member of the
// storage that the tag names.
template <class T, class Read>
[[nodiscard]] consteval std::uint64_t state_of(Option<T> const& option, Read read) {
    return option.match([&read](T const& payload) noexcept { return read(payload) + 1; },
                        [] noexcept { return std::uint64_t{0}; });
}

// Each door of an Option of a payload that copies, in a constant evaluation.
// A constant evaluation refuses a read of a member of a union that is not
// active, so a door that wrote a tag of no alternative, or of a different
// alternative, would stop the compile here.
template <class T, class Read>
[[nodiscard]] consteval bool runs_option_doors(T first, T second, Read read) {
    std::uint64_t const one = read(first) + 1;
    std::uint64_t const two = read(second) + 1;
    Option<T> held{};
    Option<T> emptied{none};
    Option<T> full = Option<T>::some(first);
    Option<T> deduced = some(second);
    Option<T> copied = full;
    Option<T> const moved = static_cast<Option<T>&&>(copied);
    bool is_held = state_of(held, read) == 0 && state_of(emptied, read) == 0 && state_of(full, read) == one
                && state_of(deduced, read) == two && state_of(moved, read) == one;
    held = full;
    emptied = Option<T>::some(second);
    is_held = is_held && state_of(held, read) == one && state_of(emptied, read) == two;
    full = Option<T>{none};
    Option<T> const taken = held.take();
    is_held = is_held && state_of(full, read) == 0 && state_of(held, read) == 0 && state_of(taken, read) == one;
    held.fill(second);
    Option<T> const old = held.replace(first);
    is_held = is_held && state_of(held, read) == one && state_of(old, read) == two;
    Option<T>{full}.expect_none("the probe holds no payload");
    T const expected = Option<T>{held}.expect("the probe holds a payload");
    T const fallback = Option<T>{full}.value_or(second);
    is_held = is_held && read(expected) == read(first) && read(fallback) == read(second);
    Result<T, Code> const ok = Option<T>{held}.ok_or(Code::closed);
    Result<T, Code> const failed = Option<T>{full}.ok_or(Code::closed);
    auto const word = [](std::uint64_t value) noexcept { return value; };
    Option<std::uint64_t> const mapped = Option<T>{held}.map([&read](T payload) noexcept { return read(payload); });
    Option<T> const kept = Option<T>{held}.filter([](T const&) noexcept { return true; });
    Option<T> const refused = Option<T>{held}.filter([](T const&) noexcept { return false; });
    Option<T> const chained = Option<T>{held}.and_then([](T payload) noexcept { return Option<T>::some(payload); });
    std::uint64_t const matched = Option<T>{held}.match([&read](T payload) noexcept { return read(payload) + 1; },
                                                        [] noexcept { return std::uint64_t{0}; });
    return is_held && ok.is_ok() && failed.is_err() && state_of(mapped, word) == one && state_of(kept, read) == one
        && state_of(refused, read) == 0 && state_of(chained, read) == one && matched == one;
}

static_assert(runs_option_doors(std::uint64_t{3}, std::uint64_t{8},
                                [](std::uint64_t value) noexcept { return value; }));
static_assert(runs_option_doors(std::uint32_t{3}, std::uint32_t{8},
                                [](std::uint32_t value) noexcept { return std::uint64_t{value}; }));
static_assert(runs_option_doors(SlotNumber{3}, SlotNumber{8},
                                [](SlotNumber const& slot) noexcept { return std::uint64_t{slot.raw}; }));
static_assert(runs_option_doors(SealedWord{3}, SealedWord{8},
                                [](SealedWord const& sealed) noexcept { return sealed.raw; }));

// The frame door of a plain Option writes the payload in place and keeps
// the tag.
[[nodiscard]] consteval bool runs_option_frame_door() {
    Option<std::uint64_t> held = Option<std::uint64_t>::some(std::uint64_t{3});
    int const seen = held.with_mut([](auto) noexcept { return 1; }, [] noexcept { return 0; });
    return seen == 1 && state_of(held, [](std::uint64_t value) noexcept { return value; }) == 4;
}
static_assert(runs_option_frame_door());

// Each door of an owning Option, in a constant evaluation.
[[nodiscard]] consteval bool runs_owning_option_doors() {
    auto const read = [](Owner const& owner) noexcept { return static_cast<std::uint64_t>(owner.handle); };
    Option<Owner> held{};
    Option<Owner> full = Option<Owner>::some(Owner{3});
    Option<Owner> moved = static_cast<Option<Owner>&&>(full);
    bool is_held = state_of(held, read) == 0 && state_of(moved, read) == 4;
    held = static_cast<Option<Owner>&&>(moved);
    Option<Owner> const taken = held.take();
    is_held = is_held && state_of(held, read) == 0 && state_of(taken, read) == 4;
    held.fill(Owner{5});
    Option<Owner> const old = held.replace(Owner{6});
    is_held = is_held && state_of(held, read) == 7 && state_of(old, read) == 6;
    Owner const out = static_cast<Option<Owner>&&>(held).expect("the probe holds an owner");
    return is_held && out.handle == 6;
}
static_assert(runs_owning_option_doors());

// Each door of a Result, in a constant evaluation.  The state is the value
// plus 1, 100 for the error full and 200 for the error closed.
[[nodiscard]] consteval bool runs_result_doors() {
    using Word = Result<std::uint64_t, Code>;
    auto const state = [](Word const& result) noexcept {
        return result.match(
            [](std::uint64_t const& value) noexcept { return value + 1; },
            [](Code const& code) noexcept { return code == Code::full ? std::uint64_t{100} : std::uint64_t{200}; });
    };
    Word value{std::uint64_t{4}};
    Word error{err(Code::full)};
    Word copied = value;
    Word const moved = static_cast<Word&&>(copied);
    value = error;
    error = Word{std::uint64_t{7}};
    bool const is_held = state(value) == 100 && state(error) == 8 && state(moved) == 5;
    Word const mapped = Word{moved}.map([](std::uint64_t word) noexcept { return word * 2; });
    Result<std::uint64_t, Code32> const remapped =
        Word{value}.map_err([](Code code) noexcept { return code == Code::full ? Code32::full : Code32::closed; });
    Word const chained = Word{moved}.and_then([](std::uint64_t word) noexcept { return Word{word + 1}; });
    Word const recovered = Word{value}.or_else([](Code) noexcept { return Word{std::uint64_t{9}}; });
    std::uint64_t const expected = Word{moved}.expect("the probe holds a value");
    std::uint64_t const fallback = Word{value}.value_or(std::uint64_t{11});
    Result<Unit, Code>{Unit{}}.expect("the probe holds a Unit");
    return is_held && state(mapped) == 9 && remapped.is_err() && state(chained) == 6 && state(recovered) == 10
        && expected == 4 && fallback == 11 && Word{moved}.ok().is_some() && Word{value}.ok().is_none()
        && value.err().is_some() && moved.err().is_none();
}
static_assert(runs_result_doors());

// Each door of a OneOf, in a constant evaluation.  The state is the value of
// the active alternative through the borrow match: the word plus 1, 100 for
// the code full and 200 for the code closed, and the half word plus 1000.
using Reading = OneOf<std::uint32_t, Code, std::uint16_t>;

[[nodiscard]] consteval std::uint64_t reading_state(Reading const& reading) {
    return reading.match(
        [](std::uint32_t const& word) noexcept { return std::uint64_t{word} + 1; },
        [](Code const& code) noexcept { return code == Code::full ? std::uint64_t{100} : std::uint64_t{200}; },
        [](std::uint16_t const& half) noexcept { return std::uint64_t{half} + 1000; });
}

[[nodiscard]] consteval bool runs_one_of_doors() {
    Reading wide{std::uint32_t{4}};
    Reading refused{Code::closed};
    Reading narrow{std::uint16_t{5}};
    Reading copied = wide;
    Reading const moved = static_cast<Reading&&>(copied);
    bool const is_built = reading_state(wide) == 5 && reading_state(refused) == 200 && reading_state(moved) == 5;
    wide = narrow;
    refused = Reading{std::uint32_t{6}};
    bool const is_held = is_built && reading_state(wide) == 1005 && reading_state(refused) == 7
                      && narrow.which().raw() == 2 && moved.which().raw() == 0 && narrow.is<std::uint16_t>()
                      && narrow.get<std::uint16_t>().is_some();
    int const written = narrow.with_mut([](auto) noexcept { return 1; }, [](auto) noexcept { return 2; },
                                        [](auto) noexcept { return 3; });
    std::uint64_t const matched = Reading{narrow}.match(
        [](std::uint32_t word) noexcept { return std::uint64_t{word}; }, [](Code) noexcept { return std::uint64_t{0}; },
        [](std::uint16_t half) noexcept { return std::uint64_t{half}; });
    std::uint16_t const expected = Reading{narrow}.expect<std::uint16_t>("the probe holds a half word");
    return is_held && written == 3 && matched == 5 && expected == 5 && Reading{narrow} == narrow;
}
static_assert(runs_one_of_doors());

[[nodiscard]] consteval bool runs_owning_one_of_doors() {
    using Held = OneOf<Owner, int>;
    Held owner{Owner{3}};
    Held number{4};
    Held moved = static_cast<Held&&>(owner);
    number = static_cast<Held&&>(moved);
    return number.which().raw() == 0
        && number.match([](Owner const& held) noexcept { return held.handle; },
                        [](int const& held) noexcept { return held; })
               == 3;
}
static_assert(runs_owning_one_of_doors());

// The byte doors of the base refuse a sum and a class that holds one (the
// doors of the tag): bit_cast and decode of the Scalar family, and the
// checked lifetime start.  The one decode of a sum from bytes is decode of
// the Protocol family, which tests the tag byte once and builds the sum
// through its constructors.
struct ForgedWords {
    std::uint64_t payload = 7;
    std::uint64_t tag = 2;
};

struct HoldsOption {
    std::uint64_t word = 0;
    Option<std::uint64_t> option{};
};

struct HoldsResult {
    std::uint64_t word = 0;
    Result<std::uint64_t, Code> result{std::uint64_t{0}};
};

struct HoldsOneOf {
    std::uint64_t word = 0;
    Reading reading{std::uint32_t{0}};
};

static_assert(!BitValid<Option<std::uint64_t>> && !BitValid<HoldsOption>);
static_assert(!BitValid<Result<std::uint64_t, Code>> && !BitValid<HoldsResult>);
static_assert(!BitValid<Reading> && !BitValid<HoldsOneOf>);
static_assert(!DecodableFrom<Option<std::uint64_t>, ForgedWords>);
static_assert(!DecodableFrom<Result<std::uint64_t, Code>, ForgedWords>);
static_assert(!DecodableFrom<OneOf<std::uint64_t, Code>, ForgedWords>);
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<HoldsOption>);
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<HoldsResult>);
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<HoldsOneOf>);

// ── The closed surface ────────────────────────────────────────────────

// The family list of plan section 11.2, the three members of the niche
// protocol that the section designs, with_mut, the frame door of
// foundation/core/Frame.h, and the combinators.  CRUCIBLE_TRY is a macro, and
// no type has a member of its name.
inline constexpr TextView choice_names{
    "none NoValue Unit Err err some is_some is_none value_or ok_or is_ok is_err ok is which niche empty is_empty "
    "is_empty_never_built is_niche_voucher with_mut map map_err and_then or_else filter is_some_and is_none_or "
    "is_ok_and is_err_and "
    "take fill replace expect_none"};

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
CRUCIBLE_CLOSED_SURFACE(choice_names, niche<TrustedSlot>);
CRUCIBLE_CLOSED_SURFACE(choice_names, niche_key<MemberSlot>);

// The cursor of a loop over an Option takes the cursor protocol, and no
// other type of the family takes a dereference.
CRUCIBLE_CLOSED_SURFACE(choice_names, OptionCursor<int>, CursorProtocol);
CRUCIBLE_CLOSED_SURFACE(choice_names, OptionCursor<int const>, CursorProtocol);
static_assert(!closed_surface<OptionCursor<int>>(choice_names).is_closed());

}  // namespace detail::choice_checks

}  // namespace foundation::core
