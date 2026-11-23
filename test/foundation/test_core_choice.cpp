// Tests of foundation/core/Choice.h: Option, none, expect, expect_none,
// value_or, match, ok_or, some, take, fill, replace, the loop of zero or one
// turns and the conversion of an error, also for a linear payload and a linear
// error.

#include <foundation/core/Choice.h>

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Ref.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include "philox_stream.h"

#include <cstdint>

namespace {

using ::fixy::check::expect_dies;
using ::foundation::core::Box;
using ::foundation::core::none;
using ::foundation::core::Option;

// The test context of the death tests below.  Each death test runs its body
// in a child through expect_dies of the Check family.
[[nodiscard]] ::fixy::TestRunnerCtx test_context() noexcept {
    return ::fixy::TestRunnerCtx{::foundation::effects::testing::test()};
}

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
    for ([[maybe_unused]] int const& value : defaulted) {
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
    expect_dies(
        test_context(), [] noexcept { static_cast<void>(Option<int>{}.expect("the test option is empty on purpose")); },
        ::foundation::core::TextView{"the test option is empty on purpose"});
}

// The loop reads the payload, also of an Option that is not const, and the
// frame door writes it in place.
void test_loop() {
    Option<int> holder = Option<int>::some(1);
    static_assert(std::is_same_v<decltype(*holder.begin()), int const&>);
    int turns = 0;
    for (int const& value : holder) {
        turns += value;
    }
    CRUCIBLE_FATAL_INVARIANT(turns == 1);
    holder.with_mut([](auto value) noexcept { *value += 1; }, [] noexcept {});
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
    expect_dies(
        test_context(), [&holder] noexcept { static_cast<void>(*holder.end()); },
        ::foundation::core::TextView{"foundation: fatal: a read through the end cursor of an Option"});
    Option<int> empty{};
    expect_dies(
        test_context(), [&empty] noexcept { static_cast<void>(*empty.begin()); },
        ::foundation::core::TextView{"foundation: fatal: a read through the end cursor of an Option"});
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
        int const seen_handle =
            target.match([](CountedOwner const& owner) noexcept { return owner.handle(); }, [] noexcept { return 0; });
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

// Moves the source into the target through two names, as an alias of two
// references does in a program.  The kind self-move of the rule must-consume
// refuses a self-move that one function names.
template <typename Value>
[[gnu::noinline]] void move_into(Value& target, Value& source) noexcept {
    target = static_cast<Value&&>(source);
}

// The move assignment of an owning Option drops the owner of the target one
// time and takes the owner of the source, and the source is empty after it.
// A self assignment through two names keeps the owner.  The probe reads each
// source after its move on purpose.
void test_owning_assignment() {
    {
        Option<CountedOwner> target = Option<CountedOwner>::some(CountedOwner{21});
        Option<CountedOwner> source = Option<CountedOwner>::some(CountedOwner{22});
        CRUCIBLE_FATAL_INVARIANT(live_owners == 2);
        target = static_cast<Option<CountedOwner>&&>(source);
        CRUCIBLE_FATAL_INVARIANT(live_owners == 1 && source.is_none() && handle_of(target) == 22);
        move_into(target, target);
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
// token and copies no byte of it.  The borrow match of a const Option reads
// the token with no move, and the rvalue match gives the token to its arm.
void test_linear_payload() {
    {
        Option<CountedToken> source = Option<CountedToken>::some(CountedToken{51});
        Option<CountedToken> target{static_cast<Option<CountedToken>&&>(source)};
        CRUCIBLE_FATAL_INVARIANT(target.is_some() && live_tokens == 1);
        Option<CountedToken> const& reader = target;
        int const seen_handle =
            reader.match([](CountedToken const& token) noexcept { return token.handle(); }, [] noexcept { return 0; });
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
    expect_dies(
        test_context(), [] noexcept { static_cast<void>(Option<SlotNumber>::some(SlotNumber{0xFFFFFFFFu})); },
        ::foundation::core::TextView{
            "foundation: fatal: Option::some got the empty value of the niche of its payload"});
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
    expect_dies(
        test_context(), [] noexcept { static_cast<void>(Option<MemberSlot>::some(MemberSlot{0xFFFFFFFFu})); },
        ::foundation::core::TextView{
            "foundation: fatal: Option::some got the empty value of the niche of its payload"});
}

// A payload of the base whose niche states that no door builds the empty
// value: a Ref, which never holds the null pointer.  Only a type of the base
// states the fact (the check of the statement of foundation/core/Choice.h),
// and the fixtures neg_core_option_niche_statement_* hold each refusal.  The
// statement gives the Option the frame door that writes a new Ref in place,
// and the storage of the Option does not test the payload in some and fill.
// A cell with a brand of its own, so the Ref of each cell has one type.
struct StatedCellBrand {};
struct StatedCell {
    using brand_type = StatedCellBrand;
    int value = 0;
};

void test_stated_niche() {
    using CellRef = ::foundation::core::Ref<StatedCell, StatedCellBrand>;
    static_assert(sizeof(Option<CellRef>) == sizeof(std::uintptr_t));
    StatedCell first{8};
    StatedCell second{9};
    Option<CellRef> held = Option<CellRef>::some(::foundation::core::to(first));
    held.with_mut([&second](auto slot) noexcept { *slot = ::foundation::core::to(second); }, [] noexcept {});
    CRUCIBLE_FATAL_INVARIANT(static_cast<Option<CellRef>&&>(held).expect("the Ref was built")->value == 9);
    Option<CellRef> filled{};
    filled.fill(::foundation::core::to(first));
    CRUCIBLE_FATAL_INVARIANT(static_cast<Option<CellRef>&&>(filled).expect("fill built the Ref")->value == 8);
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

// The oracle of the tag of Choice.h.  A copy of bytes over a plain Option or
// a plain Result, a memcpy or a union member, writes a tag that names no
// alternative.  The quarantine plugin refuses each such route out of a PROBE
// region, and the checked lifetime start refuses a third route at compile
// time (neg_core_option_plain_lifetime_start).  In a test build
// (CRUCIBLE_QTT_TRACK_CONSUME), the read of the tag compares it with the
// count, so each read of the state of a forged sum ends the process: is_some
// or is_ok, and == in each order of the operands.  A Release build reads the
// tag with no compare, so a forged tag is undefined there.  Each case reads
// checks_read_tags_ of Choice.h, the switch of the oracle, and it forges a
// tag only when the switch is on.  A Release build reads the tag of a real
// sum.
template <class Sum>
void expect_forged_compares_abort(Sum const& forged, Sum const& real, ::foundation::core::TextView refusal) {
    expect_dies(test_context(), [&forged, &real] noexcept { static_cast<void>(forged == real); }, refusal);
    expect_dies(test_context(), [&forged, &real] noexcept { static_cast<void>(real == forged); }, refusal);
}

// The report of a read of a forged tag.
[[maybe_unused]] constexpr ::foundation::core::TextView kForgedTag{
    "foundation: fatal: a sum of the Choice family: the tag names no alternative, so a copy of bytes made this sum"};

void test_forged_tag_by_memcpy() {
    static_assert(sizeof(Option<std::uint64_t>) == sizeof(ForgedWords));
    Option<std::uint64_t> const real = Option<std::uint64_t>::some(std::uint64_t{7});
    if constexpr (!::foundation::core::detail::checks_read_tags_) {
        CRUCIBLE_FATAL_INVARIANT(real.is_some() && real == Option<std::uint64_t>::some(std::uint64_t{7}));
        return;
    }
    Option<std::uint64_t> forged{};
    ForgedWords const words{};
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a copy of bytes writes a tag that names no alternative of an Option")
    __builtin_memcpy(static_cast<void*>(&forged), &words, sizeof(words));
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    expect_dies(test_context(), [&forged] noexcept { static_cast<void>(forged.is_some()); }, kForgedTag);
    expect_forged_compares_abort(forged, real, kForgedTag);
}

void test_forged_tag_by_union_member() {
    Option<std::uint64_t> const real = Option<std::uint64_t>::some(std::uint64_t{7});
    if constexpr (!::foundation::core::detail::checks_read_tags_) {
        CRUCIBLE_FATAL_INVARIANT(real.is_some());
        return;
    }
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a union member reads the bytes of a different member as an Option")
    union Punned {
        ForgedWords words;
        Option<std::uint64_t> option;
    };
    Punned const punned{ForgedWords{}};
    Option<std::uint64_t> const& forged = punned.option;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    expect_dies(test_context(), [&forged] noexcept { static_cast<void>(forged.is_some()); }, kForgedTag);
    expect_forged_compares_abort(forged, real, kForgedTag);
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
    if constexpr (!::foundation::core::detail::checks_read_tags_) {
        CRUCIBLE_FATAL_INVARIANT(real.is_ok());
        return;
    }
    Word forged{std::uint64_t{1}};
    ForgedWords const words{};
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a copy of bytes writes a tag that names no alternative of a Result")
    __builtin_memcpy(static_cast<void*>(&forged), &words, sizeof(words));
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    expect_dies(test_context(), [&forged] noexcept { static_cast<void>(forged.is_ok()); }, kForgedTag);
    expect_forged_compares_abort(forged, real, kForgedTag);
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
// death test ends its child with no end of its locals, so this payload
// counts no object.
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
// that time ends the process.  Each Option lives in the body of its death
// test, which runs in a child.
void test_lent_write_aborts() {
    expect_dies(
        test_context(),
        [] noexcept {
            Option<Handle> held = Option<Handle>::some(Handle{71});
            static_cast<void>(held.match(
                [&held](Handle const& handle) noexcept {
                    clear_handle(held);
                    return handle.value;
                },
                [] noexcept { return 0; }));
        },
        ::foundation::core::TextView{
            "foundation: fatal: a write ends the payload of a sum while a borrow match lends that payload to "
            "an arm"});

    expect_dies(
        test_context(),
        [] noexcept {
            Option<Handle> held = Option<Handle>::some(Handle{72});
            static_cast<void>(match_and_take(held, held));
        },
        ::foundation::core::TextView{
            "foundation: fatal: a write ends the payload of a sum while a borrow match lends that payload to "
            "an arm"});

    expect_dies(
        test_context(),
        [] noexcept {
            ::foundation::core::Result<Handle, Refusal> held{Handle{75}};
            static_cast<void>(held.match(
                [&held](Handle const& handle) noexcept {
                    replace_result(held);
                    return handle.value;
                },
                [](Refusal const&) noexcept { return 0; }));
        },
        ::foundation::core::TextView{
            "foundation: fatal: a write ends the payload of a sum while a borrow match lends that payload to "
            "an arm"});
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

    expect_dies(
        test_context(), [] noexcept { static_cast<void>(half_of(7).expect("the test value is odd on purpose")); },
        ::foundation::core::TextView{"the test value is odd on purpose"});
}

// A Result of Unit gives no value, so its expect() gives void.
void test_result_of_unit() {
    auto const close_when = [](bool is_open) noexcept -> Result<Unit, Refusal> {
        if (!is_open) return err(Refusal::closed);
        return Unit{};
    };
    close_when(true).expect("an open door closes");
    CRUCIBLE_FATAL_INVARIANT(close_when(false).is_err());
    expect_dies(
        test_context(), [&close_when] noexcept { close_when(false).expect("the test door is closed on purpose"); },
        ::foundation::core::TextView{"the test door is closed on purpose"});
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
        // to a consumer, here the rvalue err().
        CountedToken kept = static_cast<Result<int, CountedToken>&&>(refused).err().expect("refused holds an error");
        CRUCIBLE_FATAL_INVARIANT(kept.handle() == 61);
        ::foundation::linear::drop(static_cast<CountedToken&&>(kept));
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

// The combinators of an Option.  map, and_then and filter move the payload
// out first, so the source is empty after each one, and each payload ends
// one time.  The probe reads each source after its combinator on purpose.
void test_option_combinators() {
    auto const doubled = [](int value) noexcept { return value * 2; };
    CRUCIBLE_FATAL_INVARIANT(Option<int>::some(4).map(doubled) == Option<int>::some(8));
    CRUCIBLE_FATAL_INVARIANT(Option<int>{none}.map(doubled) == Option<int>{none});
    auto const half_when_even = [](int value) noexcept -> Option<int> {
        if (value % 2 != 0) return none;
        return Option<int>::some(value / 2);
    };
    CRUCIBLE_FATAL_INVARIANT(Option<int>::some(6).and_then(half_when_even) == Option<int>::some(3));
    CRUCIBLE_FATAL_INVARIANT(Option<int>::some(5).and_then(half_when_even) == Option<int>{none});
    CRUCIBLE_FATAL_INVARIANT(Option<int>{none}.and_then(half_when_even) == Option<int>{none});
    auto const is_even = [](int const& value) noexcept { return value % 2 == 0; };
    CRUCIBLE_FATAL_INVARIANT(Option<int>::some(6).filter(is_even) == Option<int>::some(6));
    CRUCIBLE_FATAL_INVARIANT(Option<int>::some(5).filter(is_even) == Option<int>{none});
    Option<int> const held = Option<int>::some(6);
    Option<int> const empty{};
    CRUCIBLE_FATAL_INVARIANT(held.is_some_and(is_even) && !empty.is_some_and(is_even));
    CRUCIBLE_FATAL_INVARIANT(held.is_none_or(is_even) && empty.is_none_or(is_even));
    CRUCIBLE_FATAL_INVARIANT(!Option<int>::some(5).is_some_and(is_even) && !Option<int>::some(5).is_none_or(is_even));
    CRUCIBLE_FATAL_INVARIANT(held.is_some());

    Option<SlotNumber> const slot = Option<SlotNumber>::some(SlotNumber{9});
    CRUCIBLE_FATAL_INVARIANT(slot.is_some_and([](SlotNumber const& kept) noexcept { return kept.raw == 9u; }));
    CRUCIBLE_FATAL_INVARIANT(Option<SlotNumber>{slot}.map([](SlotNumber kept) noexcept { return kept.raw; })
                             == Option<std::uint32_t>::some(9u));
    {
        Option<CountedOwner> source = Option<CountedOwner>::some(CountedOwner{81});
        bool source_was_empty_in_fn = false;
        Option<int> const handle = static_cast<Option<CountedOwner>&&>(source).map(
            [&source, &source_was_empty_in_fn](CountedOwner owner) noexcept {
                source_was_empty_in_fn = source.is_none();
                return owner.handle();
            });
        CRUCIBLE_FATAL_INVARIANT(handle == Option<int>::some(81) && source_was_empty_in_fn && source.is_none());
        CRUCIBLE_FATAL_INVARIANT(live_owners == 0);
        Option<CountedOwner> refused = Option<CountedOwner>::some(CountedOwner{82});
        Option<CountedOwner> const kept = static_cast<Option<CountedOwner>&&>(refused).filter(
            [](CountedOwner const& owner) noexcept { return owner.handle() == 0; });
        CRUCIBLE_FATAL_INVARIANT(kept.is_none() && refused.is_none() && live_owners == 0);
        Option<CountedOwner> accepted = Option<CountedOwner>::some(CountedOwner{83});
        Option<CountedOwner> const passed = static_cast<Option<CountedOwner>&&>(accepted).filter(
            [](CountedOwner const& owner) noexcept { return owner.handle() == 83; });
        CRUCIBLE_FATAL_INVARIANT(handle_of(passed) == 83 && accepted.is_none() && live_owners == 1);
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0);
}

// A linear payload moves into the function of map and and_then one time,
// and a test reads it through a const reference with no move.
void test_option_combinators_linear() {
    {
        Option<CountedToken> held = Option<CountedToken>::some(CountedToken{91});
        bool const is_read = held.is_some_and([](CountedToken const& token) noexcept { return token.handle() == 91; });
        CRUCIBLE_FATAL_INVARIANT(is_read && held.is_some() && live_tokens == 1);
        Option<int> const handle = static_cast<Option<CountedToken>&&>(held).map([](CountedToken token) noexcept {
            int const raw = token.handle();
            ::foundation::linear::drop(static_cast<CountedToken&&>(token));
            return raw;
        });
        CRUCIBLE_FATAL_INVARIANT(handle == Option<int>::some(91) && live_tokens == 0);
        Option<CountedToken> chained =
            Option<CountedToken>::some(CountedToken{92}).and_then([](CountedToken token) noexcept {
                return Option<CountedToken>::some(static_cast<CountedToken&&>(token));
            });
        CRUCIBLE_FATAL_INVARIANT(live_tokens == 1);
        CountedToken taken = static_cast<Option<CountedToken>&&>(chained).expect("and_then kept the token");
        CRUCIBLE_FATAL_INVARIANT(taken.handle() == 92);
        ::foundation::linear::drop(static_cast<CountedToken&&>(taken));
    }
    CRUCIBLE_FATAL_INVARIANT(live_tokens == 0);
}

// The combinators of a Result.  map and and_then pass the error, and map_err
// and or_else pass the value, also an owning value and a linear error.
void test_result_combinators() {
    auto const doubled = [](int value) noexcept { return value * 2; };
    auto const widened = [](Refusal refusal) noexcept { return NetRefusal{refusal}; };
    CRUCIBLE_FATAL_INVARIANT((half_of(8).map(doubled) == Result<int, Refusal>{8}));
    CRUCIBLE_FATAL_INVARIANT(half_of(7).map(doubled).err().expect("map passes the error") == Refusal::closed);
    CRUCIBLE_FATAL_INVARIANT(half_of(8).map_err(widened).expect("map_err passes the value") == 4);
    CRUCIBLE_FATAL_INVARIANT(half_of(7).map_err(widened).err().expect("map_err converts the error").cause
                             == Refusal::closed);
    CRUCIBLE_FATAL_INVARIANT((half_of(8).and_then(half_of) == Result<int, Refusal>{2}));
    CRUCIBLE_FATAL_INVARIANT(half_of(16).and_then(quarter_of).expect("eight has a quarter") == 2);
    CRUCIBLE_FATAL_INVARIANT(half_of(12).and_then(quarter_of).err().expect("six has no quarter").cause
                             == Refusal::closed);
    CRUCIBLE_FATAL_INVARIANT(half_of(7).and_then(quarter_of).err().expect("and_then converts the error").cause
                             == Refusal::closed);
    auto const recovered = [](Refusal) noexcept { return Result<int, NetRefusal>{-1}; };
    CRUCIBLE_FATAL_INVARIANT(half_of(7).or_else(recovered).expect("or_else recovers the error") == -1);
    CRUCIBLE_FATAL_INVARIANT(half_of(8).or_else(recovered).expect("or_else passes the value") == 4);
    Result<int, Refusal> const even = half_of(8);
    Result<int, Refusal> const odd = half_of(7);
    auto const is_four = [](int const& value) noexcept { return value == 4; };
    auto const is_closed = [](Refusal const& refusal) noexcept { return refusal == Refusal::closed; };
    CRUCIBLE_FATAL_INVARIANT(even.is_ok_and(is_four) && !odd.is_ok_and(is_four));
    CRUCIBLE_FATAL_INVARIANT(odd.is_err_and(is_closed) && !even.is_err_and(is_closed));
    {
        Result<CountedOwner, Refusal> held{CountedOwner{84}};
        Result<CountedOwner, NetRefusal> converted =
            static_cast<Result<CountedOwner, Refusal>&&>(held).map_err(widened);
        CRUCIBLE_FATAL_INVARIANT(live_owners == 2);
        CountedOwner const taken =
            static_cast<Result<CountedOwner, NetRefusal>&&>(converted).expect("map_err passed the owner");
        CRUCIBLE_FATAL_INVARIANT(taken.handle() == 84);
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0);
    {
        Result<int, Refusal> const refused = refuse_with(64).map_err([](CountedToken token) noexcept {
            ::foundation::linear::drop(static_cast<CountedToken&&>(token));
            return Refusal::full;
        });
        CRUCIBLE_FATAL_INVARIANT(
            refused.is_err_and([](Refusal const& refusal) noexcept { return refusal == Refusal::full; }));
        CRUCIBLE_FATAL_INVARIANT(live_tokens == 0);
    }
}

// A test reads its part through a const reference and counts the lend, as
// the borrow match does.  A write that ends the payload while holds runs
// ends the process.
void test_combinator_lend_aborts() {
    expect_dies(
        test_context(),
        [] noexcept {
            Option<Handle> held = Option<Handle>::some(Handle{76});
            static_cast<void>(held.is_some_and([&held](Handle const& handle) noexcept {
                clear_handle(held);
                return handle.value == 76;
            }));
        },
        ::foundation::core::TextView{
            "foundation: fatal: a write ends the payload of a sum while a borrow match lends that payload to "
            "an arm"});
    expect_dies(
        test_context(),
        [] noexcept {
            Result<Handle, Refusal> held{Handle{77}};
            static_cast<void>(held.is_ok_and([&held](Handle const& handle) noexcept {
                replace_result(held);
                return handle.value == 77;
            }));
        },
        ::foundation::core::TextView{
            "foundation: fatal: a write ends the payload of a sum while a borrow match lends that payload to "
            "an arm"});
}

// The property: each combinator gives the value of the match that it
// replaces, for random values and each state.
void test_combinators_agree_with_match() {
    ::crucible::test::PhiloxStream stream{0x5EEDC401CE000003u};
    auto const scaled = [](std::uint64_t value) noexcept { return value * 3u + 1u; };
    auto const is_odd = [](std::uint64_t const& value) noexcept { return (value & 1u) != 0; };
    for (int trial = 0; trial < 4096; ++trial) {
        std::uint64_t const model_value = stream.next_wide();
        bool const model_holds = (stream.next_word() & 1u) != 0;
        std::uint64_t const fallback = ~model_value;
        auto const option = [&]() noexcept {
            return model_holds ? Option<std::uint64_t>::some(model_value) : Option<std::uint64_t>{};
        };
        std::uint64_t const matched = option().match([&](std::uint64_t value) noexcept { return scaled(value); },
                                                     [&] noexcept { return fallback; });
        CRUCIBLE_FATAL_INVARIANT(option().map(scaled).value_or(fallback) == matched);
        bool const is_kept = model_holds && is_odd(model_value);
        CRUCIBLE_FATAL_INVARIANT(option().filter(is_odd).is_some() == is_kept);
        CRUCIBLE_FATAL_INVARIANT(option().is_some_and(is_odd) == is_kept);
        CRUCIBLE_FATAL_INVARIANT(option().is_none_or(is_odd) == (!model_holds || is_odd(model_value)));

        int const value = static_cast<int>(stream.below(1u << 20));
        bool const model_is_even = value % 2 == 0;
        int const mapped = half_of(value).map([](int half) noexcept { return half + 1; }).value_or(-1);
        CRUCIBLE_FATAL_INVARIANT(mapped == (model_is_even ? value / 2 + 1 : -1));
        CRUCIBLE_FATAL_INVARIANT(
            half_of(value)
                .map_err([](Refusal) noexcept { return NetRefusal{Refusal::full}; })
                .is_err_and([](NetRefusal const& refusal) noexcept { return refusal.cause == Refusal::full; })
            == !model_is_even);
    }
}

// as_ref gives the payload of a named niche or plain Option as an Option of
// a Ref, and the Option keeps its payload.  An owning Option has no as_ref
// (the fixture neg_core_as_ref_owning).
void test_as_ref() {
    using ::foundation::core::as_ref;
    Option<int> const held = Option<int>::some(42);
    Option<int> const empty{};
    CRUCIBLE_FATAL_INVARIANT(as_ref(held).is_some_and([](auto const& ref) noexcept { return *ref == 42; }));
    CRUCIBLE_FATAL_INVARIANT(as_ref(empty).is_none() && held.is_some());
}

// take, fill and replace of a named Option, in the plain, the niche and the
// owning form.  take empties the named Option, fill builds a payload in an
// empty one, and replace gives the old payload back, also when the named
// Option was empty.
void test_named_option_doors() {
    Option<int> held{};
    held.fill(4);
    CRUCIBLE_FATAL_INVARIANT(held == Option<int>::some(4));
    Option<int> const old = held.replace(5);
    CRUCIBLE_FATAL_INVARIANT(old == Option<int>::some(4) && held == Option<int>::some(5));
    Option<int> const taken = held.take();
    CRUCIBLE_FATAL_INVARIANT(taken == Option<int>::some(5) && held.is_none());
    Option<int> const nothing = held.take();
    CRUCIBLE_FATAL_INVARIANT(nothing.is_none() && held.is_none());
    Option<int> const was_empty = held.replace(6);
    CRUCIBLE_FATAL_INVARIANT(was_empty.is_none() && held == Option<int>::some(6));

    Option<SlotNumber> slot{};
    slot.fill(SlotNumber{7});
    CRUCIBLE_FATAL_INVARIANT(slot.is_some());
    Option<SlotNumber> const old_slot = slot.replace(SlotNumber{8});
    CRUCIBLE_FATAL_INVARIANT(old_slot.is_some_and([](SlotNumber const& number) noexcept { return number.raw == 7; }));
    CRUCIBLE_FATAL_INVARIANT(slot.take().expect("the slot holds eight").raw == 8 && slot.is_none());

    {
        Option<CountedOwner> owner{};
        owner.fill(CountedOwner{41});
        CRUCIBLE_FATAL_INVARIANT(live_owners == 1 && handle_of(owner) == 41);
        Option<CountedOwner> const previous = owner.replace(CountedOwner{42});
        CRUCIBLE_FATAL_INVARIANT(live_owners == 2 && handle_of(previous) == 41 && handle_of(owner) == 42);
        Option<CountedOwner> const moved = owner.take();
        CRUCIBLE_FATAL_INVARIANT(live_owners == 2 && owner.is_none() && handle_of(moved) == 42);
        owner.fill(CountedOwner{43});
        CRUCIBLE_FATAL_INVARIANT(live_owners == 3 && handle_of(owner) == 43);
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0);
}

// replace builds the new payload before it takes the old one, so a value
// that names the old payload gives its own value.  A take first would clear
// the payload, and the Ref would then read the zero words of the empty
// Option.
void test_replace_reads_the_old_payload_first() {
    using ::foundation::core::as_ref;
    Option<std::uint64_t> aliased = Option<std::uint64_t>::some(9u);
    auto const inner = as_ref(aliased).expect("the Option holds nine");
    Option<std::uint64_t> const before = aliased.replace(*inner);
    CRUCIBLE_FATAL_INVARIANT(before == Option<std::uint64_t>::some(9u));
    CRUCIBLE_FATAL_INVARIANT(aliased == Option<std::uint64_t>::some(9u));
}

// A linear payload moves through one named Option, as in a loop over a
// linear session handle: each turn takes the token, builds the next one,
// and fills the Option.  replace gives the old token back, and the test
// consumes it.
void test_named_option_doors_linear() {
    {
        Option<CountedToken> held{};
        held.fill(CountedToken{61});
        CRUCIBLE_FATAL_INVARIANT(live_tokens == 1);
        {
            Option<CountedToken> previous = held.replace(CountedToken{62});
            CRUCIBLE_FATAL_INVARIANT(live_tokens == 2 && previous.is_some() && held.is_some());
            CountedToken first =
                static_cast<Option<CountedToken>&&>(previous).expect("replace gave the first token back");
            CRUCIBLE_FATAL_INVARIANT(first.handle() == 61);
            ::foundation::linear::drop(static_cast<CountedToken&&>(first));
        }
        CRUCIBLE_FATAL_INVARIANT(live_tokens == 1);
        for (int turn = 0; turn < 3; ++turn) {
            CountedToken current = held.take().expect("the loop keeps a token in the Option");
            CRUCIBLE_FATAL_INVARIANT(held.is_none() && current.handle() == 62 + turn);
            held.fill(CountedToken{current.handle() + 1});
            ::foundation::linear::drop(static_cast<CountedToken&&>(current));
        }
        CRUCIBLE_FATAL_INVARIANT(live_tokens == 1);
        CountedToken last = static_cast<Option<CountedToken>&&>(held).expect("the loop leaves the last token");
        CRUCIBLE_FATAL_INVARIANT(last.handle() == 65);
        ::foundation::linear::drop(static_cast<CountedToken&&>(last));
    }
    CRUCIBLE_FATAL_INVARIANT(live_tokens == 0);
}

// A linear payload whose end is not trivial, so an Option of it takes the
// owning form, and a constant evaluation can build and end it.  No lifetime
// start over bytes builds it, so no copy of bytes forges a token.
class[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] ConstantToken {
public:
    constexpr explicit ConstantToken(int handle) noexcept : handle_{handle} {}
    constexpr ConstantToken(ConstantToken&& other) noexcept : handle_{other.handle_} { other.handle_ = 0; }
    ConstantToken(ConstantToken const&) = delete;
    ConstantToken& operator=(ConstantToken const&) = delete;
    ConstantToken& operator=(ConstantToken&&) = delete;
    constexpr ~ConstantToken() {}

    [[nodiscard]] constexpr int handle() const noexcept { return handle_; }

private:
    int handle_ = 0;
};

// In a constant evaluation, the clear of take ends the payload and builds the
// zero words, so fill starts the storage over the zero words.
consteval int sum_of_doors_in_constant_evaluation() {
    Option<ConstantToken> held = Option<ConstantToken>::some(ConstantToken{1});
    ConstantToken first = held.take().expect("the Option holds the first token");
    held.fill(ConstantToken{first.handle() + 1});
    Option<ConstantToken> second = held.replace(ConstantToken{3});
    ConstantToken taken = static_cast<Option<ConstantToken>&&>(second).expect("replace gave the second token");
    ConstantToken third = static_cast<Option<ConstantToken>&&>(held).expect("the Option holds the third token");
    int const sum = first.handle() * 100 + taken.handle() * 10 + third.handle();
    ::foundation::linear::drop(static_cast<ConstantToken&&>(first));
    ::foundation::linear::drop(static_cast<ConstantToken&&>(taken));
    ::foundation::linear::drop(static_cast<ConstantToken&&>(third));
    return sum;
}

static_assert(sum_of_doors_in_constant_evaluation() == 123);

// A droppable payload whose end is not trivial, so an Option of it takes the
// owning form, and a constant evaluation can move it and assign it.
class ConstantOwner {
public:
    constexpr explicit ConstantOwner(int handle) noexcept : handle_{handle} {}
    constexpr ConstantOwner(ConstantOwner&& other) noexcept : handle_{other.handle_} { other.handle_ = 0; }
    ConstantOwner(ConstantOwner const&) = delete;
    ConstantOwner& operator=(ConstantOwner const&) = delete;
    ConstantOwner& operator=(ConstantOwner&&) = delete;
    constexpr ~ConstantOwner() {}

    [[nodiscard]] constexpr int handle() const noexcept { return handle_; }

private:
    int handle_ = 0;
};

// A consumer ends the payload of an owning Option, and the storage then holds
// no alternative that a constructor built.  So a move of that Option reads no
// alternative of its storage, and a constant evaluation accepts the move.  A
// move and a move assignment of a full Option give its payload, and the
// source is empty after each one.
consteval int moves_in_constant_evaluation() {
    Option<ConstantOwner> emptied = Option<ConstantOwner>::some(ConstantOwner{1});
    ConstantOwner const first =
        static_cast<Option<ConstantOwner>&&>(emptied).expect("the Option holds the first owner");
    Option<ConstantOwner> moved_empty{static_cast<Option<ConstantOwner>&&>(emptied)};

    Option<ConstantOwner> taken = Option<ConstantOwner>::some(ConstantOwner{2});
    Option<ConstantOwner> second = taken.take();
    Option<ConstantOwner> const moved_taken{static_cast<Option<ConstantOwner>&&>(taken)};

    Option<ConstantOwner> full = Option<ConstantOwner>::some(ConstantOwner{3});
    Option<ConstantOwner> moved_full{static_cast<Option<ConstantOwner>&&>(full)};
    Option<ConstantOwner> target = Option<ConstantOwner>::some(ConstantOwner{4});
    target = static_cast<Option<ConstantOwner>&&>(moved_full);

    Option<ConstantOwner> cleared = Option<ConstantOwner>::some(ConstantOwner{5});
    cleared = static_cast<Option<ConstantOwner>&&>(moved_empty);

    bool const are_sources_empty = emptied.is_none() && moved_empty.is_none() && moved_taken.is_none() && full.is_none()
                                && moved_full.is_none() && cleared.is_none();
    int const held =
        target.match([](ConstantOwner const& owner) noexcept { return owner.handle(); }, []() noexcept { return 0; });
    int const second_handle =
        static_cast<Option<ConstantOwner>&&>(second).expect("take gave the second owner").handle();
    return are_sources_empty ? first.handle() * 100 + second_handle * 10 + held : -1;
}

static_assert(moves_in_constant_evaluation() == 123);

// The arena of a large region.  A Box of it is a payload whose end is not
// trivial.
struct RegionArena {
    std::uint64_t used = 0;
};

// The seed of a large region.  An odd seed builds a region.
enum class RegionSeed : std::uint64_t {
};

// A payload of more than one cache line whose end is not trivial, as the
// region of a build: an owning Option of the Box of its arena, and words.
class LargeRegion {
public:
    explicit LargeRegion(RegionSeed seed) noexcept : seed_{seed} {}
    LargeRegion(LargeRegion&& other) noexcept
        : arena_{static_cast<Option<Box<RegionArena>>&&>(other.arena_)}, seed_{other.seed_}, words_{other.words_} {}
    LargeRegion(LargeRegion const&) = delete;
    LargeRegion& operator=(LargeRegion const&) = delete;
    LargeRegion& operator=(LargeRegion&&) = delete;
    ~LargeRegion() = default;

    [[nodiscard]] std::uint64_t first() const noexcept { return static_cast<std::uint64_t>(seed_); }

private:
    Option<Box<RegionArena>> arena_{};
    RegionSeed seed_{};
    ::foundation::core::FixedArray<std::uint64_t, 11> words_{};
};

// Fills the Option with the candidate when its seed is odd.  The door does
// not inline, so the Option of the caller escapes, as an Option that a
// closure writes.
CRUCIBLE_NOINLINE void fill_odd_region(Option<LargeRegion>& built, LargeRegion candidate) noexcept {
    if ((candidate.first() & 1u) != 0) {
        built = Option<LargeRegion>::some(static_cast<LargeRegion&&>(candidate));
    }
}

// The end of an escaped Option of a large region after expect and a fence.
// GCC does not fold the flag of the Option across the fence, so the end keeps
// a branch that reads the payload.  The late uninit pass walks past the fence.
// So the clear must write the zero words over the ended payload, or
// -Wmaybe-uninitialized reports the read at -O2.
std::uint64_t sum_regions_across_a_fence() noexcept {
    std::uint64_t total = 0;
    for (std::uint64_t index = 0; index < 7; ++index) {
        Option<LargeRegion> built{none};
        fill_odd_region(built, LargeRegion{RegionSeed{index}});
        if (built.is_none()) {
            continue;
        }
        LargeRegion const region = static_cast<Option<LargeRegion>&&>(built).expect("the seed is odd");
        ::foundation::core::fence_seq_cst();
        total += region.first();
    }
    return total;
}

// take builds a local Option, moves the region into it and gives the local
// back.  The empty Option of the arena moves with the region.  So the move of
// an empty source must write the zero words, or the move of the local reads
// the storage of that Option with no store.
CRUCIBLE_NOINLINE Option<LargeRegion> take_region(Option<LargeRegion>& held) noexcept { return held.take(); }

// Each empty state of an owning slot whose payload has an end that is not
// trivial holds written zero words: the default slot, the move of an empty
// source, and the slot after a clear.  Each shape of this test compiles with
// -Wmaybe-uninitialized at each level of the gate, and it keeps its values.
void test_large_owning_payload_end() {
    CRUCIBLE_FATAL_INVARIANT(sum_regions_across_a_fence() == 1 + 3 + 5);
    Option<LargeRegion> held = Option<LargeRegion>::some(LargeRegion{RegionSeed{9}});
    Option<LargeRegion> const taken = take_region(held);
    CRUCIBLE_FATAL_INVARIANT(held.is_none());
    CRUCIBLE_FATAL_INVARIANT(taken.is_some_and([](LargeRegion const& region) noexcept { return region.first() == 9; }));
}

void replace_handle(Option<Handle>& target) noexcept { static_cast<void>(target.replace(Handle{79})); }

// fill of a full Option ends the process in each form, because the fill
// would drop the payload.  fill of the empty value of a niche ends it too.
// replace ends the old payload through the clear of take, so a borrow match
// that lends the payload ends the process.
void test_named_option_doors_abort() {
    constexpr ::foundation::core::TextView kFullFill{
        "foundation: fatal: Option::fill got a full Option, and a fill would drop the payload that it holds"};
    expect_dies(
        test_context(),
        [] noexcept {
            Option<int> held = Option<int>::some(1);
            held.fill(2);
        },
        kFullFill);
    expect_dies(
        test_context(),
        [] noexcept {
            Option<SlotNumber> held = Option<SlotNumber>::some(SlotNumber{3});
            held.fill(SlotNumber{4});
        },
        kFullFill);
    expect_dies(
        test_context(),
        [] noexcept {
            Option<Handle> held = Option<Handle>::some(Handle{5});
            held.fill(Handle{6});
        },
        kFullFill);
    expect_dies(
        test_context(),
        [] noexcept {
            Option<SlotNumber> held{};
            held.fill(SlotNumber{0xFFFFFFFFu});
        },
        ::foundation::core::TextView{
            "foundation: fatal: Option::fill got the empty value of the niche of its payload"});
    expect_dies(
        test_context(),
        [] noexcept {
            Option<Handle> held = Option<Handle>::some(Handle{78});
            static_cast<void>(held.match(
                [&held](Handle const& handle) noexcept {
                    replace_handle(held);
                    return handle.value;
                },
                [] noexcept { return 0; }));
        },
        ::foundation::core::TextView{
            "foundation: fatal: a write ends the payload of a sum while a borrow match lends that payload to "
            "an arm"});
}

// expect_none ends an empty Option in each form, named or temporary.  A full
// Option ends the process with the reason of the call, so no payload drops,
// also a linear payload.
void test_expect_none() {
    Option<int> plain{};
    static_cast<Option<int>&&>(plain).expect_none("the plain Option is empty");
    Option<SlotNumber>{}.expect_none("the niche Option is empty");
    Option<CountedOwner> owner{};
    static_cast<Option<CountedOwner>&&>(owner).expect_none("the owning Option is empty");
    {
        Option<CountedToken> held = Option<CountedToken>::some(CountedToken{71});
        CountedToken token = held.take().expect("the Option holds a token");
        static_cast<Option<CountedToken>&&>(held).expect_none("take emptied the Option");
        CRUCIBLE_FATAL_INVARIANT(token.handle() == 71 && live_tokens == 1);
        ::foundation::linear::drop(static_cast<CountedToken&&>(token));
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0 && live_tokens == 0);
    expect_dies(
        test_context(), [] noexcept { Option<int>::some(1).expect_none("the plain Option is full on purpose"); },
        ::foundation::core::TextView{"the plain Option is full on purpose"});
    expect_dies(
        test_context(),
        [] noexcept { Option<SlotNumber>::some(SlotNumber{2}).expect_none("the niche Option is full on purpose"); },
        ::foundation::core::TextView{"the niche Option is full on purpose"});
    expect_dies(
        test_context(),
        [] noexcept {
            Option<CountedOwner>::some(CountedOwner{3}).expect_none("the owning Option is full on purpose");
        },
        ::foundation::core::TextView{"the owning Option is full on purpose"});
    expect_dies(
        test_context(),
        [] noexcept {
            Option<CountedToken>::some(CountedToken{4}).expect_none("the linear Option is full on purpose");
        },
        ::foundation::core::TextView{"the linear Option is full on purpose"});
}

// The probes of the run-time end of a linear sum.  Each one ends a sum that no
// consumer took.  The rule must-consume refuses each such end at compile time,
// and the drop door refuses a class that holds such a sum, so only this region
// reaches the run-time end.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the test ends a linear sum that no consumer took")
// A full Option of a linear payload.
void end_full_linear_option() noexcept {
    Option<CountedToken> const held = Option<CountedToken>::some(CountedToken{82});
}

// A Result whose active alternative is a linear value.  Only a test build
// keeps the mark that ends the process.
[[maybe_unused]] void end_owed_linear_value() noexcept { Result<CountedToken, Refusal> const held{CountedToken{91}}; }

// A Result whose active alternative is a linear error.
[[maybe_unused]] void end_owed_linear_error() noexcept { Result<int, CountedToken> const refused = refuse_with(92); }

// A Result of a linear error whose active alternative is a droppable value.
void end_droppable_active_value() noexcept { Result<int, CountedToken> const kept{94}; }
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The end of a full Option of a linear payload ends the process.  The end of
// an empty one ends no payload, and a droppable payload still ends with its
// Option.
void test_full_linear_end_aborts() {
    constexpr ::foundation::core::TextView kFullEnd{
        "foundation: fatal: an Option of a linear payload ended with its payload: consume it with take, or end it with "
        "a named door"};
    expect_dies(test_context(), [] noexcept { end_full_linear_option(); }, kFullEnd);
    Option<CountedToken>{}.expect_none("an empty Option holds no token");
    {
        Option<CountedOwner> const owner = Option<CountedOwner>::some(CountedOwner{83});
        CRUCIBLE_FATAL_INVARIANT(live_owners == 1);
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0 && live_tokens == 0);
}

// The end of a Result whose active alternative is linear and that no consumer
// took ends the process in a test build.  A consumer, and the move of the
// Result for its source, leave nothing owed, and a Result whose active
// alternative is droppable owes nothing.  A Release build keeps no mark: the
// end destroys the alternative and does not end the process.  The case reads
// tracks_alternative_ends_ of Choice.h, the switch of the mark.
void test_owed_alternative_end_aborts() {
    constexpr ::foundation::core::TextView kOwedEnd{
        "foundation: fatal: a Result or a OneOf ended with a linear alternative that no consumer took"};
    if constexpr (::foundation::core::detail::tracks_alternative_ends_) {
        expect_dies(test_context(), [] noexcept { end_owed_linear_value(); }, kOwedEnd);
        expect_dies(test_context(), [] noexcept { end_owed_linear_error(); }, kOwedEnd);
    } else {
        int const live_before = live_tokens;
        end_owed_linear_value();
        end_owed_linear_error();
        CRUCIBLE_FATAL_INVARIANT(live_tokens == live_before);
    }
    {
        Result<CountedToken, Refusal> first{CountedToken{93}};
        Result<CountedToken, Refusal> second = static_cast<Result<CountedToken, Refusal>&&>(first);
        CountedToken taken = static_cast<Result<CountedToken, Refusal>&&>(second).expect("second holds a token");
        CRUCIBLE_FATAL_INVARIANT(taken.handle() == 93);
        ::foundation::linear::drop(static_cast<CountedToken&&>(taken));
    }
    end_droppable_active_value();
    CRUCIBLE_FATAL_INVARIANT(live_tokens == 0);
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
    test_stated_niche();
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
    test_option_combinators();
    test_option_combinators_linear();
    test_result_combinators();
    test_combinator_lend_aborts();
    test_combinators_agree_with_match();
    test_as_ref();
    test_named_option_doors();
    test_replace_reads_the_old_payload_first();
    test_named_option_doors_linear();
    test_named_option_doors_abort();
    test_large_owning_payload_end();
    test_expect_none();
    test_full_linear_end_aborts();
    test_owed_alternative_end_aborts();
    return 0;
}
