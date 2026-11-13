// Tests of foundation/core/OneOf.h: the construction of each alternative,
// is, expect, the two matches, get, an owning alternative and a linear
// alternative, and a property test against a model of a tag and a value.

#include <foundation/core/OneOf.h>

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>

#include "abort_probe.h"
#include "philox_stream.h"

#include <cstdint>

namespace {

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
}

void test_expect() {
    CRUCIBLE_FATAL_INVARIANT(read_word(0x12345u).expect<std::uint32_t>("a wide word") == 0x12345u);
    CRUCIBLE_FATAL_INVARIANT(read_word(9u).expect<std::uint16_t>("a narrow word") == 9u);
    CRUCIBLE_FATAL_INVARIANT(read_word(0u).expect<Refusal>("a refused word") == Refusal::closed);
    bool const other_expect_aborts = ::crucible::test::aborts(
        [] { static_cast<void>(read_word(9u).expect<std::uint32_t>("the test word is narrow on purpose")); });
    CRUCIBLE_FATAL_INVARIANT(other_expect_aborts);
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

// get<T> lends the active alternative, gives no value for each other one,
// and a write through the Ref changes the alternative that the OneOf keeps.
// A const OneOf and a linear alternative lend a const borrow.
void test_get() {
    Reading reading = read_word(7u);
    CRUCIBLE_FATAL_INVARIANT(reading.get<std::uint32_t>().is_none() && reading.get<Refusal>().is_none());
    *reading.get<std::uint16_t>().expect("the reading is narrow") = std::uint16_t{9};
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
    test_each_alternative();
    test_expect();
    test_match();
    test_get();
    test_owning_alternative();
    test_linear_alternative();
    test_agrees_with_model();
    return 0;
}
