// The compile-time checks of foundation/core/Choice.h.

#include <foundation/core/Choice.h>

#include <foundation/Lifetime.h>
#include <foundation/Linear.h>
#include <foundation/core/Number.h>
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

// A slot number whose niche states that no door builds the empty value.
// The aggregate can still build it, so the checks below show that a
// constant evaluation refuses a false statement.
struct TrustedSlot {
    std::uint32_t raw = 0;
};

// A niche whose statement is not a bool.
struct MisstatedSlot {
    std::uint32_t raw = 0;
};

}  // namespace detail::choice_checks

template <>
struct niche<detail::choice_checks::TrustedSlot> {
    static constexpr bool is_empty_never_built = true;
    [[nodiscard]] static constexpr detail::choice_checks::TrustedSlot
    empty(niche_key<detail::choice_checks::TrustedSlot> const&) noexcept {
        return detail::choice_checks::TrustedSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] static constexpr bool is_empty(niche_key<detail::choice_checks::TrustedSlot> const&,
                                                 detail::choice_checks::TrustedSlot const& slot) noexcept {
        return slot.raw == 0xFFFFFFFFu;
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
// match keeps its source, so a reference into it is a borrow.
inline constexpr int fallback_number = 0;
using NumberBorrow = int const&;
template <class R>
constexpr bool moved_match_gives_reference = requires(R result) {
    static_cast<R&&>(result).match([](int&& value) noexcept -> NumberBorrow { return value; },
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
static_assert(option_form_of<Token> == OptionForm::owning && option_form_of<HoldsToken> == OptionForm::owning);
static_assert(!std::is_copy_constructible_v<Option<HoldsToken>> && !std::is_trivially_copyable_v<Option<HoldsToken>>);
static_assert(!std::is_copy_constructible_v<Result<HoldsToken, Code>>);

// A linear payload has no value_or().  The loop of each Option takes the
// const cursor, also of an Option that is not const, so a move through it is
// a copy, which a linear payload refuses.
template <class O, class T>
concept GivesValueOr =
    requires(O option, T fallback) { static_cast<O&&>(option).value_or(static_cast<T&&>(fallback)); };
static_assert(!GivesValueOr<Option<Token>, Token> && GivesValueOr<Option<Owner>, Owner>);
static_assert(std::is_same_v<decltype(std::declval<Option<Token>&>().begin()), OptionCursor<Token const>>);
static_assert(std::is_same_v<decltype(std::declval<Option<Owner>&>().begin()), OptionCursor<Owner const>>);
static_assert(
    std::is_same_v<decltype(std::declval<Option<std::uint64_t>&>().begin()), OptionCursor<std::uint64_t const>>);

// The frame door writes the payload in place (foundation/core/Frame.h).  An
// arm could move a linear payload out through the RefMut and leave the
// Option full, so a linear payload has no frame door.  A const Option and a
// temporary have none.  A write through the RefMut bypasses the test of
// Option::some.  So a niche payload that an aggregate or a door can build as
// its empty value stops the build at the static assertion of the door, and a
// niche that states that no door builds the empty value takes the door.  The
// body reads the niche, so this check states only the constraint.
template <class O>
concept OptionWritesInPlace = requires(O& option) { option.with_mut([](auto) noexcept {}, [] noexcept {}); };
template <class O>
concept OptionWritesTemporary =
    requires(O option) { static_cast<O&&>(option).with_mut([](auto) noexcept {}, [] noexcept {}); };
static_assert(OptionWritesInPlace<Option<Owner>> && OptionWritesInPlace<Option<std::uint64_t>>);
static_assert(OptionWritesInPlace<Option<TrustedSlot>>);
static_assert(!OptionWritesInPlace<Option<Token>> && !OptionWritesInPlace<Option<Owner> const>);
static_assert(!OptionWritesTemporary<Option<Owner>>);

// The frame door of a Result takes the RefMut of the value and of the error,
// on a Result that is not const and of no linear part.
template <class R>
concept ResultWritesInPlace = requires(R& result) { result.with_mut([](auto) noexcept {}, [](auto) noexcept {}); };
static_assert(ResultWritesInPlace<Result<int, Code>> && ResultWritesInPlace<Result<Owner, Code>>);
static_assert(!ResultWritesInPlace<Result<int, Code> const> && !ResultWritesInPlace<Result<Token, Code>>);
static_assert(!ResultWritesInPlace<Result<int, Token>>);

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
    ::foundation::linear::drop(static_cast<Result<int, Token>&&>(refused));
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

// A self assignment keeps the payload.  The source goes through a second
// reference, as an alias in a program does.
[[nodiscard]] consteval int self_assigned_owner() noexcept {
    Option<Owner> held = Option<Owner>::some(Owner{3});
    Option<Owner>& alias = held;
    held = static_cast<Option<Owner>&&>(alias);
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
static_assert(ChoicePayload<TrustedSlot> && !ChoicePayload<MisstatedSlot>);
static_assert(sizeof(Option<TrustedSlot>) == sizeof(TrustedSlot));
static_assert(Option<TrustedSlot>::some(TrustedSlot{3}).is_some() && Option<TrustedSlot>{}.is_none());

// A slot number of the member protocol that states the fact.
struct TrustedMemberSlot {
    std::uint32_t raw = 0;
    static constexpr bool is_niche_empty_never_built = true;
    [[nodiscard]] static constexpr TrustedMemberSlot niche_empty(niche_key<TrustedMemberSlot> const&) noexcept {
        return TrustedMemberSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(niche_key<TrustedMemberSlot> const&) const noexcept {
        return raw == 0xFFFFFFFFu;
    }
};

static_assert(NicheRead<TrustedMemberSlot>::is_empty_never_built() && !NicheRead<MemberSlot>::is_empty_never_built());
static_assert(ChoicePayload<TrustedMemberSlot> && sizeof(Option<TrustedMemberSlot>) == sizeof(TrustedMemberSlot));

// The niche slot tests the payload in each build, also with the statement,
// so a false statement fails a constant test, and at run time it ends the
// process.
template <std::uint32_t Raw>
constexpr bool builds_trusted =
    requires { typename std::bool_constant<Option<TrustedSlot>::some(TrustedSlot{Raw}).is_some()>; };
static_assert(builds_trusted<3> && !builds_trusted<0xFFFFFFFFu>);

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

// ── The closed surface ────────────────────────────────────────────────

// The family list of plan section 11.2, the three members of the niche
// protocol that the section designs, with_mut, the frame door of
// foundation/core/Frame.h, and the combinators.  CRUCIBLE_TRY is a macro, and
// no type has a member of its name.
inline constexpr TextView choice_names{
    "none NoValue Unit Err err some is_some is_none value_or ok_or is_ok is_err ok is which niche empty is_empty "
    "is_empty_never_built with_mut map map_err and_then or_else filter is_some_and is_none_or is_ok_and is_err_and "
    "take fill replace"};

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
