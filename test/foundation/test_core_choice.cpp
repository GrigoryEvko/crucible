// Tests of foundation/core/Choice.h: Option, none, expect, value_or, match,
// ok_or, some, the loop of zero or one turns and the conversion of an
// error, also for a linear payload and a linear error.

#include <foundation/core/Choice.h>

#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Ref.h>

#include "abort_probe.h"
#include "philox_stream.h"

#include <cstdint>

namespace {

using ::foundation::core::none;
using ::foundation::core::Option;

// The count of live CountedOwner objects.  The owning form must destroy
// each payload one time and leak none.
int live_owners = 0;

class CountedOwner {
public:
    explicit CountedOwner(int handle) noexcept : handle_{handle} { ++live_owners; }
    CountedOwner(CountedOwner&& other) noexcept : handle_{other.handle_} {
        other.handle_ = 0;
        ++live_owners;
    }
    CountedOwner(CountedOwner const&) = delete;
    CountedOwner& operator=(CountedOwner const&) = delete;
    CountedOwner& operator=(CountedOwner&&) = delete;
    ~CountedOwner() { --live_owners; }

    [[nodiscard]] int handle() const noexcept { return handle_; }

private:
    int handle_ = 0;
};

// The count of live CountedToken objects.  A linear payload moves into the
// owning form, and each holder destroys each token one time.
int live_tokens = 0;

// A linear payload.  Its copy is deleted, and a move takes its handle.
class[[= ::foundation::linear::linear{}]] CountedToken {
public:
    explicit CountedToken(int handle) noexcept : handle_{handle} { ++live_tokens; }
    CountedToken(CountedToken&& other) noexcept : handle_{other.handle_} {
        other.handle_ = 0;
        ++live_tokens;
    }
    CountedToken(CountedToken const&) = delete;
    CountedToken& operator=(CountedToken const&) = delete;
    CountedToken& operator=(CountedToken&&) = delete;
    ~CountedToken() { --live_tokens; }

    [[nodiscard]] int handle() const noexcept { return handle_; }

private:
    int handle_ = 0;
};

struct SlotNumber {
    std::uint32_t raw = 0;
};

}  // namespace

template <>
struct foundation::core::niche<SlotNumber> {
    [[nodiscard]] static constexpr SlotNumber empty(foundation::core::niche_key<SlotNumber> const&) noexcept {
        return SlotNumber{0xFFFFFFFFu};
    }
    [[nodiscard]] static constexpr bool is_empty(foundation::core::niche_key<SlotNumber> const&,
                                                 SlotNumber const& slot) noexcept {
        return slot.raw == 0xFFFFFFFFu;
    }
};

namespace {

void test_empty_states() {
    Option<int> defaulted{};
    Option<int> from_marker = none;
    CRUCIBLE_FATAL_INVARIANT(defaulted.is_none() && !defaulted.is_some());
    CRUCIBLE_FATAL_INVARIANT(from_marker.is_none() && !from_marker.is_some());
    int turns = 0;
    for ([[maybe_unused]] int& value : defaulted) {
        ++turns;
    }
    CRUCIBLE_FATAL_INVARIANT(turns == 0);
}

void test_some() {
    Option<int> holder = Option<int>::some(42);
    CRUCIBLE_FATAL_INVARIANT(holder.is_some() && !holder.is_none());
    int const taken = static_cast<Option<int>&&>(holder).expect("the holder was built with a value");
    CRUCIBLE_FATAL_INVARIANT(taken == 42);
}

// The probe reads the Option after expect() on purpose: expect() leaves
// the source empty.
void test_expect_empties_the_source() {
    Option<int> holder = Option<int>::some(43);
    CRUCIBLE_FATAL_INVARIANT(static_cast<Option<int>&&>(holder).expect("the holder was built with a value") == 43);
    CRUCIBLE_FATAL_INVARIANT(holder.is_none());
}

void test_expect() {
    CRUCIBLE_FATAL_INVARIANT(Option<int>::some(7).expect("the test gives a seven") == 7);
    bool const empty_expect_aborts = ::crucible::test::aborts(
        [] { static_cast<void>(Option<int>{}.expect("the test option is empty on purpose")); });
    CRUCIBLE_FATAL_INVARIANT(empty_expect_aborts);
}

void test_loop() {
    Option<int> holder = Option<int>::some(1);
    for (int& value : holder) {
        value += 1;
    }
    Option<int> const fixed_holder = Option<int>::some(5);
    int fixed_sum = 0;
    for (int const& value : fixed_holder) {
        fixed_sum += value;
    }
    int temporary_sum = 0;
    for (int const value : Option<int>::some(3)) {
        temporary_sum += value;
    }
    CRUCIBLE_FATAL_INVARIANT(static_cast<Option<int>&&>(holder).expect("the loop changed the value") == 2);
    CRUCIBLE_FATAL_INVARIANT(fixed_sum == 5);
    CRUCIBLE_FATAL_INVARIANT(temporary_sum == 3);
}

// A read through the end cursor ends the process: a cursor never reads an
// empty Option.
void test_end_cursor_read_aborts() {
    Option<int> holder = Option<int>::some(9);
    bool const end_read_aborts = ::crucible::test::aborts([&holder] { static_cast<void>(*holder.end()); });
    CRUCIBLE_FATAL_INVARIANT(end_read_aborts);
    Option<int> empty{};
    bool const empty_read_aborts = ::crucible::test::aborts([&empty] { static_cast<void>(*empty.begin()); });
    CRUCIBLE_FATAL_INVARIANT(empty_read_aborts);
}

// The probe reads each owning Option after its move and after expect() on
// purpose: each one leaves the source empty.
void test_owning_payload() {
    {
        Option<CountedOwner> source = Option<CountedOwner>::some(CountedOwner{11});
        CRUCIBLE_FATAL_INVARIANT(live_owners == 1);
        Option<CountedOwner> target{static_cast<Option<CountedOwner>&&>(source)};
        CRUCIBLE_FATAL_INVARIANT(source.is_none() && target.is_some());
        CRUCIBLE_FATAL_INVARIANT(live_owners == 1);
        int seen_handle = 0;
        for (CountedOwner const& owner : target) {
            seen_handle = owner.handle();
        }
        CRUCIBLE_FATAL_INVARIANT(seen_handle == 11);
        CountedOwner const taken = static_cast<Option<CountedOwner>&&>(target).expect("the target took the owner");
        CRUCIBLE_FATAL_INVARIANT(taken.handle() == 11 && target.is_none());
        CRUCIBLE_FATAL_INVARIANT(live_owners == 1);
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0);
    {
        Option<CountedOwner> dropped = Option<CountedOwner>::some(CountedOwner{12});
        CRUCIBLE_FATAL_INVARIANT(live_owners == 1 && dropped.is_some());
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0);
}

// The handle of the owner that an owning Option holds, or 0 for no owner.
[[nodiscard]] int handle_of(Option<CountedOwner> const& holder) noexcept {
    return holder.match([](CountedOwner const& owner) noexcept { return owner.handle(); }, [] noexcept { return 0; });
}

// The move assignment of an owning Option drops the owner of the target one
// time and takes the owner of the source, and the source is empty after it.
// A self assignment through a second reference keeps the owner.  The probe
// reads each source after its move on purpose.
void test_owning_assignment() {
    {
        Option<CountedOwner> target = Option<CountedOwner>::some(CountedOwner{21});
        Option<CountedOwner> source = Option<CountedOwner>::some(CountedOwner{22});
        CRUCIBLE_FATAL_INVARIANT(live_owners == 2);
        target = static_cast<Option<CountedOwner>&&>(source);
        CRUCIBLE_FATAL_INVARIANT(live_owners == 1 && source.is_none() && handle_of(target) == 22);
        Option<CountedOwner>& alias = target;
        target = static_cast<Option<CountedOwner>&&>(alias);
        CRUCIBLE_FATAL_INVARIANT(live_owners == 1 && handle_of(target) == 22);
        Option<CountedOwner> empty{};
        target = static_cast<Option<CountedOwner>&&>(empty);
        CRUCIBLE_FATAL_INVARIANT(live_owners == 0 && target.is_none());
        target = Option<CountedOwner>::some(CountedOwner{23});
        CRUCIBLE_FATAL_INVARIANT(live_owners == 1 && handle_of(target) == 23);
        target = none;
        CRUCIBLE_FATAL_INVARIANT(live_owners == 0 && target.is_none());
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0);
}

// The property: two Options compare equal exactly when a plain model of one
// flag and one value gives equal pairs.  The values come from a small range,
// so equal payloads occur often.
void test_equality_agrees_with_plain_model() {
    ::crucible::test::PhiloxStream stream{0x5EEDC401CE000002u};
    for (int trial = 0; trial < 4096; ++trial) {
        std::uint64_t const left_value = stream.next_word() & 3u;
        std::uint64_t const right_value = stream.next_word() & 3u;
        bool const left_holds = (stream.next_word() & 1u) != 0;
        bool const right_holds = (stream.next_word() & 1u) != 0;
        Option<std::uint64_t> const left =
            left_holds ? Option<std::uint64_t>::some(left_value) : Option<std::uint64_t>{};
        Option<std::uint64_t> const right =
            right_holds ? Option<std::uint64_t>::some(right_value) : Option<std::uint64_t>{};
        bool const model_equal = left_holds == right_holds && (!left_holds || left_value == right_value);
        CRUCIBLE_FATAL_INVARIANT((left == right) == model_equal);
        CRUCIBLE_FATAL_INVARIANT((left != right) == !model_equal);
    }
}

// A linear payload takes the owning form, so a move of the Option moves the
// token and copies no byte of it.  The const loop reads the token with no
// move, and the rvalue match gives the token to its arm.
void test_linear_payload() {
    {
        Option<CountedToken> source = Option<CountedToken>::some(CountedToken{51});
        Option<CountedToken> target{static_cast<Option<CountedToken>&&>(source)};
        CRUCIBLE_FATAL_INVARIANT(target.is_some() && live_tokens == 1);
        Option<CountedToken> const& reader = target;
        int seen_handle = 0;
        for (CountedToken const& token : reader) {
            seen_handle = token.handle();
        }
        CRUCIBLE_FATAL_INVARIANT(seen_handle == 51 && target.is_some());
        int const matched = static_cast<Option<CountedToken>&&>(target).match(
            [](CountedToken token) noexcept {
                int const handle = token.handle();
                ::foundation::linear::drop(static_cast<CountedToken&&>(token));
                return handle;
            },
            [] noexcept { return 0; });
        CRUCIBLE_FATAL_INVARIANT(matched == 51 && live_tokens == 0);
    }
    CRUCIBLE_FATAL_INVARIANT(live_tokens == 0);
}

void test_value_or() {
    CRUCIBLE_FATAL_INVARIANT(Option<int>::some(4).value_or(9) == 4);
    CRUCIBLE_FATAL_INVARIANT(Option<int>{}.value_or(9) == 9);
    {
        Option<CountedOwner> held = Option<CountedOwner>::some(CountedOwner{21});
        CountedOwner const taken = static_cast<Option<CountedOwner>&&>(held).value_or(CountedOwner{22});
        CRUCIBLE_FATAL_INVARIANT(taken.handle() == 21);
        Option<CountedOwner> empty{};
        CountedOwner const fallback = static_cast<Option<CountedOwner>&&>(empty).value_or(CountedOwner{23});
        CRUCIBLE_FATAL_INVARIANT(fallback.handle() == 23);
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0);
}

// The probe reads the source after the rvalue match on purpose: the match
// moves the payload out first.
void test_match() {
    Option<int> const held = Option<int>::some(5);
    Option<int> const empty{};
    auto const doubled = [](int const& value) noexcept { return value * 2; };
    auto const missing = [] noexcept { return -1; };
    CRUCIBLE_FATAL_INVARIANT(held.match(doubled, missing) == 10);
    CRUCIBLE_FATAL_INVARIANT(empty.match(doubled, missing) == -1);
    CRUCIBLE_FATAL_INVARIANT(held.is_some());

    int some_runs = 0;
    int none_runs = 0;
    held.match([&some_runs](int const&) noexcept { ++some_runs; }, [&none_runs] noexcept { ++none_runs; });
    empty.match([&some_runs](int const&) noexcept { ++some_runs; }, [&none_runs] noexcept { ++none_runs; });
    CRUCIBLE_FATAL_INVARIANT(some_runs == 1 && none_runs == 1);

    {
        Option<CountedOwner> source = Option<CountedOwner>::some(CountedOwner{31});
        bool source_was_empty_in_arm = false;
        int const handle = static_cast<Option<CountedOwner>&&>(source).match(
            [&source, &source_was_empty_in_arm](CountedOwner owner) noexcept {
                source_was_empty_in_arm = source.is_none();
                return owner.handle();
            },
            [] noexcept { return 0; });
        CRUCIBLE_FATAL_INVARIANT(handle == 31 && source_was_empty_in_arm && source.is_none());
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0);
}

void test_niche_payload() {
    static_assert(sizeof(Option<SlotNumber>) == sizeof(SlotNumber));
    Option<SlotNumber> empty_slot{};
    Option<SlotNumber> held_slot = Option<SlotNumber>::some(SlotNumber{5});
    Option<SlotNumber> const copied_slot = held_slot;
    CRUCIBLE_FATAL_INVARIANT(empty_slot.is_none());
    CRUCIBLE_FATAL_INVARIANT(held_slot.is_some() && copied_slot.is_some());
    CRUCIBLE_FATAL_INVARIANT(static_cast<Option<SlotNumber>&&>(held_slot).expect("the slot was built").raw == 5);
    for (SlotNumber const& slot : copied_slot) {
        CRUCIBLE_FATAL_INVARIANT(slot.raw == 5);
    }
}

// A payload equal to the empty value of its niche would read as no value,
// so Option::some refuses it in each build.
void test_niche_empty_value_aborts() {
    bool const empty_value_aborts =
        ::crucible::test::aborts([] { static_cast<void>(Option<SlotNumber>::some(SlotNumber{0xFFFFFFFFu})); });
    CRUCIBLE_FATAL_INVARIANT(empty_value_aborts);
}

// A payload that gives the member niche protocol.  A class that cannot
// specialize niche, such as a class in an anonymous namespace, gives its
// empty value through its own members.
struct MemberSlot {
    std::uint32_t raw = 0;
    [[nodiscard]] static constexpr MemberSlot niche_empty(::foundation::core::niche_key<MemberSlot> const&) noexcept {
        return MemberSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<MemberSlot> const&) const noexcept {
        return raw == 0xFFFFFFFFu;
    }
};

void test_member_niche_payload() {
    static_assert(sizeof(Option<MemberSlot>) == sizeof(MemberSlot));
    Option<MemberSlot> empty_slot{};
    Option<MemberSlot> held_slot = Option<MemberSlot>::some(MemberSlot{6});
    CRUCIBLE_FATAL_INVARIANT(empty_slot.is_none() && held_slot.is_some());
    CRUCIBLE_FATAL_INVARIANT(static_cast<Option<MemberSlot>&&>(held_slot).expect("the slot was built").raw == 6);
    bool const empty_value_aborts =
        ::crucible::test::aborts([] { static_cast<void>(Option<MemberSlot>::some(MemberSlot{0xFFFFFFFFu})); });
    CRUCIBLE_FATAL_INVARIANT(empty_value_aborts);
}

// A class of the member protocol that states that no door builds the empty
// value, while its aggregate can still build it.  The statement gives a loop
// over a mutable Option a cursor that writes the payload.  The niche slot
// tests each payload in each build, so the false statement ends the process
// at the first Option of the empty value.
struct StatedSlot {
    std::uint32_t raw = 0;
    static constexpr bool is_niche_empty_never_built = true;
    [[nodiscard]] static constexpr StatedSlot niche_empty(::foundation::core::niche_key<StatedSlot> const&) noexcept {
        return StatedSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<StatedSlot> const&) const noexcept {
        return raw == 0xFFFFFFFFu;
    }
};

void test_member_niche_statement() {
    static_assert(sizeof(Option<StatedSlot>) == sizeof(StatedSlot));
    Option<StatedSlot> held = Option<StatedSlot>::some(StatedSlot{8});
    for (StatedSlot& slot : held) {
        slot.raw += 1;
    }
    CRUCIBLE_FATAL_INVARIANT(static_cast<Option<StatedSlot>&&>(held).expect("the slot was built").raw == 9);
    bool const false_statement_aborts =
        ::crucible::test::aborts([] { static_cast<void>(Option<StatedSlot>::some(StatedSlot{0xFFFFFFFFu})); });
    CRUCIBLE_FATAL_INVARIANT(false_statement_aborts);
}

// The validity test of a value that a bit copy made.  A decode copies wire
// bytes into a value, and the bytes can hold the empty value of its niche,
// which no valid value holds.  The test reads the niche of a test type and
// of a Ref at run time.
void test_niche_validity_of_bit_copy() {
    using ::foundation::core::detail::NicheRead;
    SlotNumber const valid_slot{9};
    SlotNumber const empty_bits_slot{0xFFFFFFFFu};
    CRUCIBLE_FATAL_INVARIANT(!NicheRead<SlotNumber>::holds_empty(valid_slot));
    CRUCIBLE_FATAL_INVARIANT(NicheRead<SlotNumber>::holds_empty(empty_bits_slot));

    int target = 5;
    auto copied = ::foundation::core::to(target);
    using Copied = decltype(copied);
    CRUCIBLE_FATAL_INVARIANT(!NicheRead<Copied>::holds_empty(copied));
    static_assert(sizeof(Copied) == sizeof(std::uintptr_t));
    std::uintptr_t const zero_word = 0;
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a bit copy of zero bytes into a Ref, as a decode of wire bytes makes")
    __builtin_memcpy(static_cast<void*>(&copied), &zero_word, sizeof(zero_word));
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    CRUCIBLE_FATAL_INVARIANT(NicheRead<Copied>::holds_empty(copied));
}

// The bytes of a plain Option of a word and of a plain Result of a word: the
// payload, and then a tag word of 2, which names no alternative.  No
// constructor writes such a tag, but a copy of bytes can.
struct ForgedWords {
    std::uint64_t payload = 7;
    std::uint64_t tag = 2;
};

// The routes of a copy of bytes over a plain Option and a plain Result: a
// memcpy and a union member.  The quarantine plugin refuses each of them out
// of a PROBE region, and the checked lifetime start refuses a third route at
// compile time (neg_core_option_plain_lifetime_start).  Each read of the state
// of a forged sum ends the process: is_some or is_ok, and == in each order of
// the operands.  So a forged tag never reads as a value.
template <class Sum>
void expect_forged_compares_abort(Sum const& forged, Sum const& real) {
    CRUCIBLE_FATAL_INVARIANT(::crucible::test::aborts([&forged, &real] { static_cast<void>(forged == real); }));
    CRUCIBLE_FATAL_INVARIANT(::crucible::test::aborts([&forged, &real] { static_cast<void>(real == forged); }));
}

void test_forged_tag_by_memcpy() {
    static_assert(sizeof(Option<std::uint64_t>) == sizeof(ForgedWords));
    Option<std::uint64_t> const real = Option<std::uint64_t>::some(std::uint64_t{7});
    Option<std::uint64_t> forged{};
    ForgedWords const words{};
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a copy of bytes writes a tag that names no alternative of an Option")
    __builtin_memcpy(static_cast<void*>(&forged), &words, sizeof(words));
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    CRUCIBLE_FATAL_INVARIANT(::crucible::test::aborts([&forged] { static_cast<void>(forged.is_some()); }));
    expect_forged_compares_abort(forged, real);
}

void test_forged_tag_by_union_member() {
    Option<std::uint64_t> const real = Option<std::uint64_t>::some(std::uint64_t{7});
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a union member reads the bytes of a different member as an Option")
    union Punned {
        ForgedWords words;
        Option<std::uint64_t> option;
    };
    Punned const punned{ForgedWords{}};
    Option<std::uint64_t> const& forged = punned.option;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    CRUCIBLE_FATAL_INVARIANT(::crucible::test::aborts([&forged] { static_cast<void>(forged.is_some()); }));
    expect_forged_compares_abort(forged, real);
}

// The error of the Result tests.
enum class Refusal : std::uint8_t {
    full,
    closed
};

void test_forged_result_tag_by_memcpy() {
    using Word = ::foundation::core::Result<std::uint64_t, Refusal>;
    static_assert(sizeof(Word) == sizeof(ForgedWords));
    Word const real{std::uint64_t{7}};
    Word forged{std::uint64_t{1}};
    ForgedWords const words{};
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a copy of bytes writes a tag that names no alternative of a Result")
    __builtin_memcpy(static_cast<void*>(&forged), &words, sizeof(words));
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    CRUCIBLE_FATAL_INVARIANT(::crucible::test::aborts([&forged] { static_cast<void>(forged.is_ok()); }));
    expect_forged_compares_abort(forged, real);
}

// A sum whose payload is a sum: an Option of a Result and a Result of an
// Option.  The union of the outer sum holds the tag of the inner sum in a
// member, and that union carries the mark closed_tag, so the quarantine
// plugin reads each member of it as no copy of bytes in each build.
void test_nested_sums() {
    using Inner = ::foundation::core::Result<std::uint32_t, Refusal>;
    Option<Inner> const held = Option<Inner>::some(Inner{std::uint32_t{7}});
    std::uint32_t const value = held.match(
        [](Inner const& inner) noexcept {
            return inner.match([](std::uint32_t const& word) noexcept { return word; },
                               [](Refusal const&) noexcept { return std::uint32_t{0}; });
        },
        [] noexcept { return std::uint32_t{0}; });
    CRUCIBLE_FATAL_INVARIANT(value == 7u);

    using Outer = ::foundation::core::Result<Option<std::uint32_t>, Refusal>;
    Outer const wrapped{Option<std::uint32_t>::some(std::uint32_t{9})};
    CRUCIBLE_FATAL_INVARIANT(wrapped.is_ok());
    CRUCIBLE_FATAL_INVARIANT(Outer{wrapped}.expect("the Result holds a value").expect("the Option is full") == 9u);
}

// A payload with a destructor, so an Option of it takes the owning form.  A
// test that ends the process in the abort probe skips the end of its locals,
// so this payload counts no object.
struct Handle {
    int value = 0;

    constexpr explicit Handle(int initial) noexcept : value{initial} {}
    Handle(Handle const&) noexcept = default;
    Handle(Handle&&) noexcept = default;
    Handle& operator=(Handle const&) noexcept = default;
    Handle& operator=(Handle&&) noexcept = default;
    ~Handle() noexcept {}
};

void clear_handle(Option<Handle>& target) noexcept { target = none; }

void replace_result(::foundation::core::Result<Handle, Refusal>& target) noexcept {
    target = ::foundation::core::Result<Handle, Refusal>{::foundation::core::err(Refusal::closed)};
}

// A borrow match of one Option whose arm moves a second Option out.  A caller
// that gives one Option for the two parameters ends the payload that the arm
// borrows.
[[nodiscard]] int match_and_take(Option<Handle> const& held, Option<Handle>& alias) noexcept {
    return held.match(
        [&alias](Handle const& handle) noexcept {
            Option<Handle> const taken{static_cast<Option<Handle>&&>(alias)};
            return handle.value + (taken.is_some() ? 1 : 0);
        },
        [] noexcept { return 0; });
}

// The rule borrow-match reads one body, so a write of the sum in a callee of
// the arm, or through a second reference, escapes it.  The owning storage
// counts the lend of the borrow match, and a write that ends the payload in
// that time ends the process.  Each Option lives in its probe, so the jump of
// the probe ends its frame with no end of the Option.
void test_lent_write_aborts() {
    bool const callee_aborts = ::crucible::test::aborts([] {
        Option<Handle> held = Option<Handle>::some(Handle{71});
        static_cast<void>(held.match(
            [&held](Handle const& handle) noexcept {
                clear_handle(held);
                return handle.value;
            },
            [] noexcept { return 0; }));
    });
    CRUCIBLE_FATAL_INVARIANT(callee_aborts);

    bool const alias_aborts = ::crucible::test::aborts([] {
        Option<Handle> held = Option<Handle>::some(Handle{72});
        static_cast<void>(match_and_take(held, held));
    });
    CRUCIBLE_FATAL_INVARIANT(alias_aborts);

    bool const result_aborts = ::crucible::test::aborts([] {
        ::foundation::core::Result<Handle, Refusal> held{Handle{75}};
        static_cast<void>(held.match(
            [&held](Handle const& handle) noexcept {
                replace_result(held);
                return handle.value;
            },
            [](Refusal const&) noexcept { return 0; }));
    });
    CRUCIBLE_FATAL_INVARIANT(result_aborts);
}

// The lend ends with the borrow match, also with a nested match, so a write
// after the match keeps the payload that the write gives.
void test_lend_ends_with_the_match() {
    Option<Handle> held = Option<Handle>::some(Handle{73});
    int const seen = held.match(
        [&held](Handle const& outer) noexcept {
            return outer.value
                 + held.match([](Handle const& inner) noexcept { return inner.value; }, [] noexcept { return 0; });
        },
        [] noexcept { return 0; });
    CRUCIBLE_FATAL_INVARIANT(seen == 146);
    held = Option<Handle>::some(Handle{74});
    CRUCIBLE_FATAL_INVARIANT(
        held.match([](Handle const& handle) noexcept { return handle.value; }, [] noexcept { return 0; }) == 74);
    held = none;
    CRUCIBLE_FATAL_INVARIANT(held.is_none());
}

// The property: for each value and each choice of empty or full, the
// Option agrees with a plain model of one flag and one value.  The loop
// makes one turn exactly when the model holds a value, and expect,
// value_or and match give the value back.
void test_agrees_with_plain_model() {
    ::crucible::test::PhiloxStream stream{0x5EEDC401CE000001u};
    for (int trial = 0; trial < 4096; ++trial) {
        std::uint64_t const model_value = stream.next_wide();
        bool const model_holds = (stream.next_word() & 1u) != 0;
        Option<std::uint64_t> wide = model_holds ? Option<std::uint64_t>::some(model_value) : Option<std::uint64_t>{};
        CRUCIBLE_FATAL_INVARIANT(wide.is_some() == model_holds);
        int turns = 0;
        for (std::uint64_t const& value : wide) {
            CRUCIBLE_FATAL_INVARIANT(value == model_value);
            ++turns;
        }
        CRUCIBLE_FATAL_INVARIANT(turns == (model_holds ? 1 : 0));
        std::uint64_t const fallback = ~model_value;
        std::uint64_t const matched = wide.match([](std::uint64_t const& value) noexcept { return value; },
                                                 [fallback] noexcept { return fallback; });
        CRUCIBLE_FATAL_INVARIANT(matched == (model_holds ? model_value : fallback));
        CRUCIBLE_FATAL_INVARIANT(static_cast<Option<std::uint64_t>&&>(wide).value_or(fallback) == matched);

        std::uint32_t const raw_slot = static_cast<std::uint32_t>(stream.below(0xFFFFFFFFu));
        Option<SlotNumber> slot = model_holds ? Option<SlotNumber>::some(SlotNumber{raw_slot}) : Option<SlotNumber>{};
        CRUCIBLE_FATAL_INVARIANT(slot.is_some() == model_holds);
        if (model_holds) {
            CRUCIBLE_FATAL_INVARIANT(static_cast<Option<SlotNumber>&&>(slot).expect("the model holds a slot").raw
                                     == raw_slot);
        }
    }
}

using ::foundation::core::err;
using ::foundation::core::Result;
using ::foundation::core::Unit;

[[nodiscard]] Result<int, Refusal> half_of(int value) noexcept {
    if (value % 2 != 0) return err(Refusal::closed);
    return value / 2;
}

void test_result_roads() {
    Result<int, Refusal> const even = half_of(8);
    Result<int, Refusal> const odd = half_of(7);
    CRUCIBLE_FATAL_INVARIANT(even.is_ok() && !even.is_err());
    CRUCIBLE_FATAL_INVARIANT(odd.is_err() && !odd.is_ok());
    CRUCIBLE_FATAL_INVARIANT(even.err().is_none());
    CRUCIBLE_FATAL_INVARIANT(odd.err().expect("an odd value is refused") == Refusal::closed);
    CRUCIBLE_FATAL_INVARIANT(half_of(8).ok().expect("an even value has a half") == 4);
    CRUCIBLE_FATAL_INVARIANT(half_of(7).ok().is_none());
    CRUCIBLE_FATAL_INVARIANT(half_of(8).value_or(-1) == 4);
    CRUCIBLE_FATAL_INVARIANT(half_of(7).value_or(-1) == -1);
    CRUCIBLE_FATAL_INVARIANT(half_of(8).expect("an even value has a half") == 4);

    auto const doubled = [](int const& value) noexcept { return value * 2; };
    auto const coded = [](Refusal refusal) noexcept { return refusal == Refusal::closed ? -2 : -3; };
    CRUCIBLE_FATAL_INVARIANT(even.match(doubled, coded) == 8);
    CRUCIBLE_FATAL_INVARIANT(odd.match(doubled, coded) == -2);
    CRUCIBLE_FATAL_INVARIANT(half_of(6).match([](int value) noexcept { return value; }, coded) == 3);

    bool const error_expect_aborts =
        ::crucible::test::aborts([] { static_cast<void>(half_of(7).expect("the test value is odd on purpose")); });
    CRUCIBLE_FATAL_INVARIANT(error_expect_aborts);
}

// A Result of Unit gives no value, so its expect() gives void.
void test_result_of_unit() {
    auto const close_when = [](bool is_open) noexcept -> Result<Unit, Refusal> {
        if (!is_open) return err(Refusal::closed);
        return Unit{};
    };
    close_when(true).expect("an open door closes");
    CRUCIBLE_FATAL_INVARIANT(close_when(false).is_err());
    bool const unit_expect_aborts =
        ::crucible::test::aborts([&close_when] { close_when(false).expect("the test door is closed on purpose"); });
    CRUCIBLE_FATAL_INVARIANT(unit_expect_aborts);
}

// A Result of a value that owns something moves the value once, and
// destroys each value one time.
void test_result_owning_value() {
    {
        Result<CountedOwner, Refusal> held{CountedOwner{41}};
        CRUCIBLE_FATAL_INVARIANT(live_owners == 1 && held.is_ok());
        Result<CountedOwner, Refusal> moved{static_cast<Result<CountedOwner, Refusal>&&>(held)};
        CRUCIBLE_FATAL_INVARIANT(live_owners == 2);
        CountedOwner const taken =
            static_cast<Result<CountedOwner, Refusal>&&>(moved).expect("the Result was built with a value");
        CRUCIBLE_FATAL_INVARIANT(taken.handle() == 41);
        Result<CountedOwner, Refusal> refused{err(Refusal::full)};
        CRUCIBLE_FATAL_INVARIANT(refused.is_err() && live_owners == 3);
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0);
}

// The move assignment of an owning Result drops the active part of the
// target one time and takes the active part of the source.  The source
// keeps its moved-from part, which its destructor ends.
void test_result_owning_assignment() {
    {
        Result<CountedOwner, Refusal> target{CountedOwner{51}};
        Result<CountedOwner, Refusal> source{CountedOwner{52}};
        CRUCIBLE_FATAL_INVARIANT(live_owners == 2);
        target = static_cast<Result<CountedOwner, Refusal>&&>(source);
        CRUCIBLE_FATAL_INVARIANT(live_owners == 2 && target.is_ok());
        int const handle = target.match([](CountedOwner const& owner) noexcept { return owner.handle(); },
                                        [](Refusal) noexcept { return 0; });
        CRUCIBLE_FATAL_INVARIANT(handle == 52);
        target = Result<CountedOwner, Refusal>{err(Refusal::closed)};
        CRUCIBLE_FATAL_INVARIANT(live_owners == 1 && target.is_err());
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0);
}

// The error of a Result can be linear, so a failed operation gives its
// resource back.  The const match reads the error, the rvalue err() moves
// it into its Option, and the rvalue match moves it into its arm.
[[nodiscard]] Result<int, CountedToken> refuse_with(int handle) noexcept { return err(CountedToken{handle}); }

void test_result_linear_error() {
    {
        Result<int, CountedToken> refused = refuse_with(61);
        Result<int, CountedToken> const& reader = refused;
        int const read_handle = reader.match([](int const&) noexcept { return 0; },
                                             [](CountedToken const& token) noexcept { return token.handle(); });
        CRUCIBLE_FATAL_INVARIANT(read_handle == 61 && live_tokens == 1);
        CountedToken const given_back = refuse_with(62).err().expect("the operation was refused");
        CRUCIBLE_FATAL_INVARIANT(given_back.handle() == 62 && live_tokens == 2);
        int const moved_handle =
            refuse_with(63).match([](int) noexcept { return 0; },
                                  [](CountedToken token) noexcept {
                                      int const handle = token.handle();
                                      ::foundation::linear::drop(static_cast<CountedToken&&>(token));
                                      return handle;
                                  });
        CRUCIBLE_FATAL_INVARIANT(moved_handle == 63 && live_tokens == 2);
        // The Result of a linear error is linear, so the const match leaves it
        // to a consumer.  The drop door ends it.
        ::foundation::linear::drop(static_cast<Result<int, CountedToken>&&>(refused));
    }
    CRUCIBLE_FATAL_INVARIANT(live_tokens == 0);
}

// The property: half_of agrees with a plain model for random values, on
// each road to the value and to the error.
void test_result_agrees_with_plain_model() {
    ::crucible::test::PhiloxStream stream{0x5EEDC401CE000002u};
    for (int trial = 0; trial < 4096; ++trial) {
        int const value = static_cast<int>(stream.below(1u << 20));
        bool const model_is_even = value % 2 == 0;
        Result<int, Refusal> const outcome = half_of(value);
        CRUCIBLE_FATAL_INVARIANT(outcome.is_ok() == model_is_even);
        int const matched =
            outcome.match([](int const& half) noexcept { return half; }, [](Refusal) noexcept { return -1; });
        CRUCIBLE_FATAL_INVARIANT(matched == (model_is_even ? value / 2 : -1));
        CRUCIBLE_FATAL_INVARIANT(half_of(value).value_or(-1) == matched);
        CRUCIBLE_FATAL_INVARIANT(half_of(value).ok().is_some() == model_is_even);
        CRUCIBLE_FATAL_INVARIANT(outcome.err().is_some() == !model_is_even);
    }
}

// ok_or gives the payload as a Result, or the error.  An owning or linear
// payload moves into the Result one time, and the Option destroys its
// moved-from payload, so the count of live objects is one.
void test_ok_or() {
    CRUCIBLE_FATAL_INVARIANT(Option<int>::some(5).ok_or(Refusal::full).expect("a full Option gives its value") == 5);
    CRUCIBLE_FATAL_INVARIANT(Option<int>{none}.ok_or(Refusal::full).err().expect("an empty Option gives the error")
                             == Refusal::full);
    {
        Option<CountedOwner> held = Option<CountedOwner>::some(CountedOwner{17});
        Result<CountedOwner, Refusal> const moved = static_cast<Option<CountedOwner>&&>(held).ok_or(Refusal::closed);
        CRUCIBLE_FATAL_INVARIANT(moved.is_ok() && live_owners == 1);
        int const read_handle = moved.match([](CountedOwner const& owner) noexcept { return owner.handle(); },
                                            [](Refusal const&) noexcept { return 0; });
        CRUCIBLE_FATAL_INVARIANT(read_handle == 17);
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0);
    {
        Option<CountedToken> held = Option<CountedToken>::some(CountedToken{23});
        Result<CountedToken, Refusal> moved = static_cast<Option<CountedToken>&&>(held).ok_or(Refusal::closed);
        CRUCIBLE_FATAL_INVARIANT(live_tokens == 1);
        CountedToken const taken = static_cast<Result<CountedToken, Refusal>&&>(moved).expect("the Option was full");
        CRUCIBLE_FATAL_INVARIANT(taken.handle() == 23);
    }
    CRUCIBLE_FATAL_INVARIANT(live_tokens == 0);
}

// The error of a callee converts to the error of the caller through an
// implicit conversion that does not throw.
struct NetRefusal {
    Refusal cause = Refusal::full;
    constexpr NetRefusal(Refusal from) noexcept : cause{from} {}
};

[[nodiscard]] Result<int, NetRefusal> quarter_of(int value) noexcept {
    if (value % 4 != 0) return err(Refusal::closed);
    return half_of(value).match([](int half) noexcept -> Result<int, NetRefusal> { return half / 2; },
                                [](Refusal refusal) noexcept -> Result<int, NetRefusal> { return err(refusal); });
}

void test_error_conversion() {
    CRUCIBLE_FATAL_INVARIANT(quarter_of(12).expect("twelve has a quarter") == 3);
    NetRefusal const refused = quarter_of(6).err().expect("six has no quarter");
    CRUCIBLE_FATAL_INVARIANT(refused.cause == Refusal::closed);
}

// some() deduces the payload from the value, with no conversion.
void test_free_some() {
    using ::foundation::core::some;
    Option<int> const seven = some(7);
    CRUCIBLE_FATAL_INVARIANT(seven.is_some());
    SlotNumber const slot{5};
    Option<SlotNumber> const held = some(slot);
    CRUCIBLE_FATAL_INVARIANT(
        held.match([](SlotNumber const& kept) noexcept { return kept.raw; }, []() noexcept { return std::uint32_t{0}; })
        == 5u);
}

}  // namespace

int main() {
    test_empty_states();
    test_some();
    test_expect_empties_the_source();
    test_expect();
    test_loop();
    test_end_cursor_read_aborts();
    test_owning_payload();
    test_owning_assignment();
    test_equality_agrees_with_plain_model();
    test_linear_payload();
    test_value_or();
    test_match();
    test_niche_payload();
    test_niche_empty_value_aborts();
    test_member_niche_payload();
    test_member_niche_statement();
    test_niche_validity_of_bit_copy();
    test_forged_tag_by_memcpy();
    test_forged_tag_by_union_member();
    test_forged_result_tag_by_memcpy();
    test_nested_sums();
    test_lent_write_aborts();
    test_lend_ends_with_the_match();
    test_agrees_with_plain_model();
    test_result_roads();
    test_result_of_unit();
    test_result_owning_value();
    test_result_owning_assignment();
    test_result_linear_error();
    test_result_agrees_with_plain_model();
    test_ok_or();
    test_error_conversion();
    test_free_some();
    return 0;
}
