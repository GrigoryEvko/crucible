// Tests of foundation/core/OneOf.h: the construction of each alternative,
// is, which, expect, the two matches, get, an owning alternative and a linear
// alternative, and a property test against a model of a tag and a value.

#include <foundation/core/OneOf.h>

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Ref.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include "philox_stream.h"

#include <cstdint>

namespace {

using ::fixy::TestRunnerCtx;
using ::fixy::check::expect_dies;
using ::foundation::core::OneOf;

enum class Refusal : std::uint8_t {
    full,
    closed
};

// The count of live CountedOwner objects.  The owning form must destroy
// each alternative one time and leak none.
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

// The count of live CountedToken objects.
int live_tokens = 0;

// A linear alternative.  Its copy is deleted, and a move takes its handle.
class[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] CountedToken {
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

using Reading = OneOf<std::uint32_t, Refusal, std::uint16_t>;

[[nodiscard]] Reading read_word(std::uint32_t word) noexcept {
    if (word == 0) return Refusal::closed;
    if (word < 0x10000u) return static_cast<std::uint16_t>(word);
    return word;
}

void test_each_alternative() {
    Reading const wide = read_word(0x12345u);
    Reading const narrow = read_word(7u);
    Reading const refused = read_word(0u);
    CRUCIBLE_FATAL_INVARIANT(wide.is<std::uint32_t>() && !wide.is<Refusal>() && !wide.is<std::uint16_t>());
    CRUCIBLE_FATAL_INVARIANT(narrow.is<std::uint16_t>() && !narrow.is<std::uint32_t>());
    CRUCIBLE_FATAL_INVARIANT(refused.is<Refusal>());
    CRUCIBLE_FATAL_INVARIANT(wide.which().raw() == 0 && refused.which().raw() == 1 && narrow.which().raw() == 2);
}

// The oracle of the tag of Choice.h.  In a test build (has_test_oracles of
// foundation/Platform.h), the read of the tag of a OneOf compares it with the
// count, and a tag that names no alternative ends the process.  A Release
// build reads the tag with no compare, so a forged tag is undefined there.
// Each case reads has_test_oracles, and it forges a tag only when the
// switch is on.  A Release build reads the tag of a real OneOf.

// The bytes of a Reading whose tag names no alternative: the storage word,
// and a tag word whose low byte is the tag.
struct ForgedBytes {
    std::uint32_t storage = 7;
    std::uint32_t tag_word = 200;
};
static_assert(sizeof(ForgedBytes) == sizeof(Reading));

// The report of a read of a forged tag.
constexpr ::foundation::core::TextView kForgedTag{
    "foundation: fatal: a sum of the Choice family: the tag names no alternative, so a copy of bytes made this sum"};

// A copy of bytes over a plain OneOf can write a tag that names no
// alternative.  The rule forged-tag of the quarantine plugin refuses such a
// copy out of a PROBE region, and this PROBE region makes one on purpose.
[[nodiscard]] Reading forge_reading() noexcept {
    Reading forged = read_word(9u);
    ForgedBytes const bytes{};
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a copy of bytes writes a tag that names no alternative of a OneOf")
    __builtin_memcpy(static_cast<void*>(&forged), &bytes, sizeof(Reading));
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return forged;
}

// which() of a forged OneOf ends the process in a test build, so no Index
// leaves its range.  Each read of a forged tag below runs in a child through
// expect_dies of the Check family.
void test_which_refuses_a_forged_tag([[maybe_unused]] TestRunnerCtx const& test) {
    if constexpr (::foundation::has_test_oracles) {
        Reading const forged = forge_reading();
        expect_dies(test, [&forged] noexcept { static_cast<void>(forged.which()); }, kForgedTag);
    } else {
        CRUCIBLE_FATAL_INVARIANT(read_word(9u).which().raw() == 2);
    }
}

// Each read of the tag of a forged OneOf ends the process in a test build:
// the borrow match, the rvalue match, the compare of two OneOfs, is<T>() and
// get<T>().  So no arm reads the storage as an alternative that is not
// active.
void test_match_refuses_a_forged_tag([[maybe_unused]] TestRunnerCtx const& test) {
    Reading const real = read_word(9u);
    if constexpr (!::foundation::has_test_oracles) {
        CRUCIBLE_FATAL_INVARIANT(real.is<std::uint16_t>() && real == read_word(9u));
        return;
    }
    Reading const forged = forge_reading();
    Reading const same_forged = forged;
    expect_dies(
        test,
        [&forged] noexcept {
            static_cast<void>(forged.match([](std::uint32_t const&) noexcept { return 1; },
                                           [](Refusal const&) noexcept { return 2; },
                                           [](std::uint16_t const&) noexcept { return 3; }));
        },
        kForgedTag);
    expect_dies(
        test,
        [&forged] noexcept {
            static_cast<void>(Reading{forged}.match([](std::uint32_t) noexcept { return 1; },
                                                    [](Refusal) noexcept { return 2; },
                                                    [](std::uint16_t) noexcept { return 3; }));
        },
        kForgedTag);
    expect_dies(test, [&forged, &same_forged] noexcept { static_cast<void>(forged == same_forged); }, kForgedTag);
    expect_dies(test, [&forged, &real] noexcept { static_cast<void>(real == forged); }, kForgedTag);
    expect_dies(test, [&forged] noexcept { static_cast<void>(forged.is<std::uint32_t>()); }, kForgedTag);
    expect_dies(test, [&forged] noexcept { static_cast<void>(forged.get<std::uint16_t>().is_none()); }, kForgedTag);
}

void test_expect(TestRunnerCtx const& test) {
    CRUCIBLE_FATAL_INVARIANT(read_word(0x12345u).expect<std::uint32_t>("a wide word") == 0x12345u);
    CRUCIBLE_FATAL_INVARIANT(read_word(9u).expect<std::uint16_t>("a narrow word") == 9u);
    CRUCIBLE_FATAL_INVARIANT(read_word(0u).expect<Refusal>("a refused word") == Refusal::closed);
    expect_dies(
        test,
        [] noexcept { static_cast<void>(read_word(9u).expect<std::uint32_t>("the test word is narrow on purpose")); },
        ::foundation::core::TextView{"the test word is narrow on purpose"});
}

void test_match() {
    auto const describe = [](Reading reading) noexcept {
        return static_cast<Reading&&>(reading).match([](std::uint32_t word) noexcept { return word; },
                                                     [](Refusal) noexcept { return std::uint32_t{0xFFFFFFFFu}; },
                                                     [](std::uint16_t half) noexcept { return std::uint32_t{half}; });
    };
    CRUCIBLE_FATAL_INVARIANT(describe(read_word(0x12345u)) == 0x12345u);
    CRUCIBLE_FATAL_INVARIANT(describe(read_word(5u)) == 5u);
    CRUCIBLE_FATAL_INVARIANT(describe(read_word(0u)) == 0xFFFFFFFFu);

    Reading const held = read_word(6u);
    int const seen =
        held.match([](std::uint32_t const&) noexcept { return 1; }, [](Refusal const&) noexcept { return 2; },
                   [](std::uint16_t const& half) noexcept { return static_cast<int>(half); });
    CRUCIBLE_FATAL_INVARIANT(seen == 6 && held.is<std::uint16_t>());
}

// get<T> lends the active alternative as a const borrow, and gives no value
// for each other one.  The frame door writes the alternative that the OneOf
// keeps.
void test_get() {
    Reading reading = read_word(7u);
    CRUCIBLE_FATAL_INVARIANT(reading.get<std::uint32_t>().is_none() && reading.get<Refusal>().is_none());
    CRUCIBLE_FATAL_INVARIANT(*reading.get<std::uint16_t>().expect("the reading is narrow") == 7u);
    reading.with_mut([](auto) noexcept {}, [](auto) noexcept {}, [](auto half) noexcept { *half = std::uint16_t{9}; });
    CRUCIBLE_FATAL_INVARIANT(Reading{reading}.expect<std::uint16_t>("the reading stays narrow") == 9u);

    Reading const wide = read_word(0x12345u);
    CRUCIBLE_FATAL_INVARIANT(*wide.get<std::uint32_t>().expect("the reading is wide") == 0x12345u);
    CRUCIBLE_FATAL_INVARIANT(wide.get<std::uint16_t>().is_none());

    {
        OneOf<CountedToken, Refusal> held{CountedToken{61}};
        int const lent = held.get<CountedToken>().expect("the token is held")->handle();
        CRUCIBLE_FATAL_INVARIANT(lent == 61 && held.get<Refusal>().is_none() && live_tokens == 1);
        CountedToken taken =
            static_cast<OneOf<CountedToken, Refusal>&&>(held).expect<CountedToken>("the token is held");
        ::foundation::linear::drop(static_cast<CountedToken&&>(taken));
    }
    CRUCIBLE_FATAL_INVARIANT(live_tokens == 0);
}

// The probe reads each owning OneOf after its move on purpose: the source
// keeps its moved-from alternative.
void test_owning_alternative() {
    {
        OneOf<CountedOwner, Refusal> source{CountedOwner{11}};
        CRUCIBLE_FATAL_INVARIANT(live_owners == 1);
        OneOf<CountedOwner, Refusal> target{static_cast<OneOf<CountedOwner, Refusal>&&>(source)};
        CRUCIBLE_FATAL_INVARIANT(source.is<CountedOwner>() && target.is<CountedOwner>() && live_owners == 2);
        int const seen = target.match([](CountedOwner const& owner) noexcept { return owner.handle(); },
                                      [](Refusal const&) noexcept { return 0; });
        CRUCIBLE_FATAL_INVARIANT(seen == 11);
        CountedOwner const taken =
            static_cast<OneOf<CountedOwner, Refusal>&&>(target).expect<CountedOwner>("the target took the owner");
        CRUCIBLE_FATAL_INVARIANT(taken.handle() == 11 && live_owners == 3);
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0);
    {
        OneOf<CountedOwner, Refusal> refused{Refusal::full};
        CRUCIBLE_FATAL_INVARIANT(refused.is<Refusal>() && live_owners == 0);
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0);
}

// A payload with a destructor, so a OneOf of it takes the owning form.  A
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

// A OneOf of a Handle has no assignment, because the end of a Handle is code.  The move
// of a Box runs none, so a OneOf of a Box of a Handle has one.
using BoxedReading = OneOf<::foundation::core::Box<Handle>, Refusal>;

[[nodiscard]] BoxedReading box_handle(int value) noexcept {
    return BoxedReading{::foundation::core::mint_box<Handle>(::foundation::effects::testing::test().alloc, value)};
}

void replace_reading(BoxedReading& target) noexcept { target = BoxedReading{Refusal::full}; }

// The borrow match of an owning OneOf counts its lend, so a move assignment
// in a callee of the arm ends the process.  That stop is the subject of the
// test, so the PROBE region keeps the call, which the rule borrow-match
// refuses.  After the match, the same assignment keeps its value.  The write
// in the match runs in a child through expect_dies of the Check family.
void test_lent_alternative_write_aborts(TestRunnerCtx const& test) {
    expect_dies(
        test,
        [] noexcept {
            BoxedReading held = box_handle(81);
            static_cast<void>(held.match(
                [&held](::foundation::core::Box<Handle> const& handle) noexcept {
                    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the lend count of a sum stops a write in a callee of the arm")
                    replace_reading(held);
                    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
                    return ::foundation::core::with_view(handle,
                                                         [](Handle const& payload) noexcept { return payload.value; });
                },
                [](Refusal const&) noexcept { return 0; }));
        },
        ::foundation::core::TextView{
            "foundation: fatal: a write ends the payload of a sum while a borrow match lends that payload to "
            "an arm"});

    BoxedReading held = box_handle(82);
    int const seen = held.match(
        [](::foundation::core::Box<Handle> const& handle) noexcept {
            return ::foundation::core::with_view(handle, [](Handle const& payload) noexcept { return payload.value; });
        },
        [](Refusal const&) noexcept { return 0; });
    replace_reading(held);
    CRUCIBLE_FATAL_INVARIANT(seen == 82 && held.is<Refusal>());
}

// A linear alternative takes the owning form, and only the rvalue match and
// expect give it out.
void test_linear_alternative() {
    {
        OneOf<CountedToken, Refusal> held{CountedToken{51}};
        int const matched = static_cast<OneOf<CountedToken, Refusal>&&>(held).match(
            [](CountedToken token) noexcept {
                int const handle = token.handle();
                ::foundation::linear::drop(static_cast<CountedToken&&>(token));
                return handle;
            },
            [](Refusal) noexcept { return 0; });
        CRUCIBLE_FATAL_INVARIANT(matched == 51 && live_tokens == 1);
    }
    CRUCIBLE_FATAL_INVARIANT(live_tokens == 0);
}

// The probes of the run-time end of a OneOf of a linear alternative.  Each one
// ends a OneOf that no consumer took.  The rule must-consume refuses each such
// end at compile time, and the drop door refuses a class that holds such a
// OneOf, so only this region reaches the run-time end.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the test ends a linear sum that no consumer took")
// A OneOf whose active alternative is a linear value.  Only a test build
// keeps the mark that ends the process.
[[maybe_unused]] void end_owed_linear_alternative() noexcept {
    OneOf<CountedToken, Refusal> const held{CountedToken{52}};
}

// A OneOf of a linear alternative whose active alternative is droppable.
void end_droppable_active_alternative() noexcept { OneOf<CountedToken, Refusal> const held{Refusal::closed}; }
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The end of a OneOf whose active alternative is linear and that no consumer
// took ends the process in a test build.  expect and the move of the OneOf
// for its source leave nothing owed, and a droppable active alternative owes
// nothing.  A Release build keeps no mark: the end destroys the alternative
// and does not end the process.  The case reads has_test_oracles, the
// switch of the mark.
void test_owed_linear_alternative_end_aborts([[maybe_unused]] TestRunnerCtx const& test) {
    if constexpr (::foundation::has_test_oracles) {
        expect_dies(
            test, [] noexcept { end_owed_linear_alternative(); },
            ::foundation::core::TextView{
                "foundation: fatal: a Result or a OneOf ended with a linear alternative that no consumer took"});
    } else {
        int const live_before = live_tokens;
        end_owed_linear_alternative();
        CRUCIBLE_FATAL_INVARIANT(live_tokens == live_before);
    }
    {
        OneOf<CountedToken, Refusal> first{CountedToken{53}};
        OneOf<CountedToken, Refusal> second = static_cast<OneOf<CountedToken, Refusal>&&>(first);
        CountedToken taken =
            static_cast<OneOf<CountedToken, Refusal>&&>(second).expect<CountedToken>("second holds a token");
        CRUCIBLE_FATAL_INVARIANT(taken.handle() == 53);
        ::foundation::linear::drop(static_cast<CountedToken&&>(taken));
    }
    end_droppable_active_alternative();
    CRUCIBLE_FATAL_INVARIANT(live_tokens == 0);
}

// For each trial, a model of a tag and a value agrees with the OneOf: one
// is<T> holds, the match and expect<T> give the value, and a copy holds the
// same alternative.
void test_agrees_with_model() {
    ::crucible::test::PhiloxStream stream{0x5EEDC401CE000003u};
    for (int trial = 0; trial < 4096; ++trial) {
        std::uint64_t const wide = stream.next_wide();
        std::uint32_t const tag = static_cast<std::uint32_t>(stream.below(4u));
        using Model = OneOf<std::uint64_t, std::uint32_t, Refusal, std::uint8_t>;
        Model const built = [&]() noexcept -> Model {
            switch (tag) {
                case 0:
                    return wide;
                case 1:
                    return static_cast<std::uint32_t>(wide);
                case 2:
                    return (wide & 1u) != 0 ? Refusal::full : Refusal::closed;
                default:
                    return static_cast<std::uint8_t>(wide);
            }
        }();
        Model const copied = built;
        int const holds = (copied.is<std::uint64_t>() ? 1 : 0) + (copied.is<std::uint32_t>() ? 1 : 0)
                        + (copied.is<Refusal>() ? 1 : 0) + (copied.is<std::uint8_t>() ? 1 : 0);
        CRUCIBLE_FATAL_INVARIANT(holds == 1);
        std::uint64_t const read =
            copied.match([](std::uint64_t const& value) noexcept { return value; },
                         [](std::uint32_t const& value) noexcept { return std::uint64_t{value}; },
                         [](Refusal const& refusal) noexcept {
                             return refusal == Refusal::full ? std::uint64_t{1} : std::uint64_t{0};
                         },
                         [](std::uint8_t const& value) noexcept { return std::uint64_t{value}; });
        switch (tag) {
            case 0:
                CRUCIBLE_FATAL_INVARIANT(read == wide);
                CRUCIBLE_FATAL_INVARIANT(Model{built}.expect<std::uint64_t>("the model holds a wide value") == wide);
                break;
            case 1:
                CRUCIBLE_FATAL_INVARIANT(read == static_cast<std::uint32_t>(wide));
                break;
            case 2:
                CRUCIBLE_FATAL_INVARIANT(read == (wide & 1u));
                break;
            default:
                CRUCIBLE_FATAL_INVARIANT(read == static_cast<std::uint8_t>(wide));
                break;
        }
    }
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_each_alternative();
    test_which_refuses_a_forged_tag(test);
    test_match_refuses_a_forged_tag(test);
    test_expect(test);
    test_match();
    test_get();
    test_owning_alternative();
    test_lent_alternative_write_aborts(test);
    test_linear_alternative();
    test_owed_linear_alternative_end_aborts(test);
    test_agrees_with_model();
    return 0;
}
