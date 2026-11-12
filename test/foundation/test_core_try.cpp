// Tests of CRUCIBLE_TRY of foundation/core/Choice.h: each kind of operand,
// the conversion of an error, a linear value and a linear error, the
// placeholder `_`, a chain of steps and a use in a constant evaluation.

#include <foundation/core/Choice.h>

#include <foundation/Linear.h>
#include <foundation/Platform.h>

#include <cstdint>

namespace {

using ::foundation::core::err;
using ::foundation::core::none;
using ::foundation::core::Option;
using ::foundation::core::Result;
using ::foundation::core::some;
using ::foundation::core::Unit;

enum class Refusal : std::uint8_t {
    odd,
    negative
};

// The error of a caller.  It takes the error of a callee through an
// implicit conversion that does not throw.
struct NetRefusal {
    Refusal cause = Refusal::odd;
    constexpr NetRefusal(Refusal from) noexcept : cause{from} {}
};

[[nodiscard]] constexpr Result<int, Refusal> half_of(int value) noexcept {
    if (value < 0) return err(Refusal::negative);
    if (value % 2 != 0) return err(Refusal::odd);
    return value / 2;
}

[[nodiscard]] constexpr Option<int> positive(int value) noexcept {
    if (value <= 0) return none;
    return some(value);
}

// A temporary operand of each kind.
[[nodiscard]] constexpr Result<int, Refusal> quarter_of(int value) noexcept {
    CRUCIBLE_TRY(half, half_of(value));
    CRUCIBLE_TRY(quarter, half_of(half));
    return quarter;
}

[[nodiscard]] constexpr Option<int> positive_sum(int left, int right) noexcept {
    CRUCIBLE_TRY(first, positive(left));
    CRUCIBLE_TRY(second, positive(right));
    return some(first + second);
}

void test_temporary_operands() {
    CRUCIBLE_FATAL_INVARIANT(quarter_of(12).expect("twelve has a quarter") == 3);
    CRUCIBLE_FATAL_INVARIANT(quarter_of(6).err().expect("six has no quarter") == Refusal::odd);
    CRUCIBLE_FATAL_INVARIANT(quarter_of(-4).err().expect("a negative value is refused") == Refusal::negative);
    CRUCIBLE_FATAL_INVARIANT(positive_sum(2, 3).expect("two positive values have a sum") == 5);
    CRUCIBLE_FATAL_INVARIANT(positive_sum(2, -3).is_none());
    CRUCIBLE_FATAL_INVARIANT(positive_sum(-2, 3).is_none());
}

// An operand that a local holds, given with a cast to an rvalue.
[[nodiscard]] Result<int, Refusal> moved_half(int value) noexcept {
    Result<int, Refusal> held = half_of(value);
    CRUCIBLE_TRY(half, static_cast<Result<int, Refusal>&&>(held));
    return half + 1;
}

[[nodiscard]] Option<int> moved_positive(int value) noexcept {
    Option<int> held = positive(value);
    CRUCIBLE_TRY(kept, static_cast<Option<int>&&>(held));
    return some(kept * 2);
}

void test_moved_operands() {
    CRUCIBLE_FATAL_INVARIANT(moved_half(8).expect("eight has a half") == 5);
    CRUCIBLE_FATAL_INVARIANT(moved_half(7).is_err());
    CRUCIBLE_FATAL_INVARIANT(moved_positive(4).expect("four is positive") == 8);
    CRUCIBLE_FATAL_INVARIANT(moved_positive(0).is_none());
}

// The error of a callee converts to the error of the caller.
[[nodiscard]] Result<int, NetRefusal> net_half(int value) noexcept {
    CRUCIBLE_TRY(half, half_of(value));
    return half;
}

void test_error_conversion() {
    CRUCIBLE_FATAL_INVARIANT(net_half(10).expect("ten has a half") == 5);
    NetRefusal const refused = net_half(3).err().expect("three has no half");
    CRUCIBLE_FATAL_INVARIANT(refused.cause == Refusal::odd);
}

// The count of live CountedToken objects.
int live_tokens = 0;

// A linear value.  Its copy is deleted, and a move takes its handle.
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

[[nodiscard]] Result<CountedToken, Refusal> open_token(int handle) noexcept {
    if (handle < 0) return err(Refusal::negative);
    return CountedToken{handle};
}

[[nodiscard]] Result<int, Refusal> token_handle(int handle) noexcept {
    CRUCIBLE_TRY(token, open_token(handle));
    return token.handle();
}

[[nodiscard]] Result<int, CountedToken> refuse_with(int handle) noexcept {
    if (handle < 0) return err(CountedToken{-handle});
    return handle;
}

[[nodiscard]] Result<int, CountedToken> pass_refusal(int handle) noexcept {
    CRUCIBLE_TRY(value, refuse_with(handle));
    return value + 1;
}

void test_linear_parts() {
    CRUCIBLE_FATAL_INVARIANT(token_handle(9).expect("a token opens") == 9);
    CRUCIBLE_FATAL_INVARIANT(token_handle(-1).is_err());
    CRUCIBLE_FATAL_INVARIANT(live_tokens == 0);
    CRUCIBLE_FATAL_INVARIANT(pass_refusal(4).match([](int value) noexcept { return value; },
                                                   [](CountedToken token) noexcept { return -token.handle(); })
                             == 5);
    {
        CountedToken const given_back = pass_refusal(-6).err().expect("a negative handle is refused");
        CRUCIBLE_FATAL_INVARIANT(given_back.handle() == 6 && live_tokens == 1);
    }
    CRUCIBLE_FATAL_INVARIANT(live_tokens == 0);
}

// A Result of Unit gives no value, and the placeholder discards it.
int doors_closed = 0;

[[nodiscard]] Result<Unit, Refusal> close_door(bool is_open) noexcept {
    if (!is_open) return err(Refusal::odd);
    ++doors_closed;
    return Unit{};
}

[[nodiscard]] Result<Unit, Refusal> close_two(bool first, bool second) noexcept {
    CRUCIBLE_TRY(_, close_door(first));
    CRUCIBLE_TRY(_, close_door(second));
    return Unit{};
}

void test_placeholder() {
    doors_closed = 0;
    close_two(true, true).expect("two open doors close");
    CRUCIBLE_FATAL_INVARIANT(doors_closed == 2);
    CRUCIBLE_FATAL_INVARIANT(close_two(true, false).is_err() && doors_closed == 3);
    CRUCIBLE_FATAL_INVARIANT(close_two(false, true).is_err() && doors_closed == 3);
}

// A chain of four steps stops at the first error.
int steps_done = 0;

[[nodiscard]] Result<int, Refusal> step(int value) noexcept {
    ++steps_done;
    return half_of(value);
}

[[nodiscard]] Result<int, Refusal> four_steps(int value) noexcept {
    CRUCIBLE_TRY(first, step(value));
    CRUCIBLE_TRY(second, step(first));
    CRUCIBLE_TRY(third, step(second));
    CRUCIBLE_TRY(fourth, step(third));
    return fourth;
}

void test_chain() {
    steps_done = 0;
    CRUCIBLE_FATAL_INVARIANT(four_steps(48).expect("48 halves four times") == 3 && steps_done == 4);
    steps_done = 0;
    CRUCIBLE_FATAL_INVARIANT(four_steps(12).err().expect("3 is odd") == Refusal::odd && steps_done == 3);
    steps_done = 0;
    CRUCIBLE_FATAL_INVARIANT(four_steps(5).is_err() && steps_done == 1);
}

// The macro in a constant evaluation.
static_assert(quarter_of(20).expect("twenty has a quarter") == 5);
static_assert(quarter_of(10).is_err());
static_assert(positive_sum(1, 2).expect("one and two are positive") == 3);
static_assert(positive_sum(0, 2).is_none());

}  // namespace

int main() {
    test_temporary_operands();
    test_moved_operands();
    test_error_conversion();
    test_linear_parts();
    test_placeholder();
    test_chain();
    return 0;
}
