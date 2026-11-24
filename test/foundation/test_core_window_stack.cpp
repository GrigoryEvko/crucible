// The window stack of foundation/core/WindowStack.h: loans that open and end
// in order, nested loans with windows that do not overlap, a loan that does
// not fit, and the end of a loan out of order, which ends the process.

#include <foundation/Quarantine.h>
#include <foundation/core/Ref.h>
#include <foundation/core/WindowStack.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

namespace {

namespace core = ::foundation::core;

constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

using Stack = core::WindowStack<std::uint32_t, 8>;
using Loan = core::WindowLoan<std::uint32_t, 8>;

[[nodiscard]] constexpr core::WindowCount count_of(std::uint32_t count) noexcept { return core::WindowCount{count}; }

// Writes value into each element of the window of a loan.
void fill_window(Loan const& loan, std::uint32_t value) noexcept {
    for (core::View<std::uint32_t> const window : loan.window()) {
        for (std::uint32_t& element : window) {
            element = value;
        }
    }
}

// The sum of the elements of the window of a loan.
[[nodiscard]] std::uint32_t sum_of(Loan const& loan) noexcept {
    std::uint32_t sum = 0;
    for (core::View<std::uint32_t> const window : loan.window()) {
        for (std::uint32_t const element : window) {
            sum += element;
        }
    }
    return sum;
}

// A loan takes its window at the top, and its end gives the top back.
void test_a_loan_opens_and_ends_in_order() {
    Stack stack{};
    CRUCIBLE_TEST_ASSERT(kTest, stack.held() == 0 && Stack::capacity() == 8);
    {
        Loan const loan{stack, count_of(3)};
        CRUCIBLE_TEST_ASSERT(kTest, loan.is_open() && stack.held() == 3);
        fill_window(loan, 7);
        CRUCIBLE_TEST_ASSERT(kTest, sum_of(loan) == 21);
    }
    CRUCIBLE_TEST_ASSERT(kTest, stack.held() == 0);
}

// A loan that opens while another is open starts past its window, so the
// writes of the inner loan leave the outer window as it was.
void test_nested_loans_have_windows_that_do_not_overlap() {
    Stack stack{};
    Loan const outer{stack, count_of(3)};
    fill_window(outer, 1);
    {
        Loan const inner{stack, count_of(5)};
        CRUCIBLE_TEST_ASSERT(kTest, inner.is_open() && stack.held() == 8);
        fill_window(inner, 10);
        CRUCIBLE_TEST_ASSERT(kTest, sum_of(outer) == 3 && sum_of(inner) == 50);
        bool is_disjoint = false;
        for (core::View<std::uint32_t> const first : outer.window()) {
            for (core::View<std::uint32_t> const second : inner.window()) {
                is_disjoint = core::offset_of(first, second).is_none() && core::offset_of(second, first).is_none();
            }
        }
        CRUCIBLE_TEST_ASSERT(kTest, is_disjoint);
    }
    CRUCIBLE_TEST_ASSERT(kTest, stack.held() == 3 && sum_of(outer) == 3);
}

// A loan past the capacity holds no window and leaves the top, and its end
// gives nothing back.
void test_a_full_stack_gives_no_window() {
    Stack stack{};
    Loan const most{stack, count_of(6)};
    {
        Loan const refused{stack, count_of(3)};
        CRUCIBLE_TEST_ASSERT(kTest, !refused.is_open() && refused.window().is_none() && stack.held() == 6);
    }
    CRUCIBLE_TEST_ASSERT(kTest, stack.held() == 6);
    Loan const rest{stack, count_of(2)};
    CRUCIBLE_TEST_ASSERT(kTest, rest.is_open() && stack.held() == 8);
}

// Writes value into each element of the window of a door.
template <class Window>
void fill_run(Window window, std::uint32_t value) noexcept {
    core::fill(std::move(window), value);
}

// The sum of the elements of the window of a door.
template <class Window>
[[nodiscard]] std::uint32_t sum_run(Window const& window) noexcept {
    std::uint32_t sum = 0;
    for (std::uint32_t const element : window.view()) {
        sum += element;
    }
    return sum;
}

// A window door takes its window at the top, and the top goes back when the
// arm returns.
void test_a_window_door_opens_and_ends_in_order() {
    Stack stack{};
    std::uint32_t const sum = stack.with_window(
        count_of(3),
        [&stack](auto window) noexcept {
            CRUCIBLE_TEST_ASSERT(kTest, window.size() == 3 && stack.held() == 3);
            fill_run(std::move(window), 7);
            return std::uint32_t{1};
        },
        [] noexcept { return std::uint32_t{0}; });
    CRUCIBLE_TEST_ASSERT(kTest, sum == 1 && stack.held() == 0);
    std::uint32_t const read = stack.with_window(
        count_of(3), [](auto window) noexcept { return sum_run(window); }, [] noexcept { return 0u; });
    CRUCIBLE_TEST_ASSERT(kTest, read == 21 && stack.held() == 0);
}

// A window that opens in the arm of another window starts past it, also when
// a function that the arm calls opens it.  So the inner window is a second
// run, and no second writer of the outer window exists.
void test_nested_window_doors_do_not_overlap() {
    Stack stack{};
    auto const open_inner = [&stack](std::uint32_t value) noexcept {
        return stack.with_window(
            count_of(5),
            [value](auto inner) noexcept {
                fill_run(std::move(inner), value);
                return true;
            },
            [] noexcept { return false; });
    };
    bool const is_disjoint = stack.with_window(
        count_of(3),
        [&open_inner, &stack](auto outer) noexcept {
            for (std::uint32_t& element : outer) {
                element = 1;
            }
            bool const is_inner_open = open_inner(10);
            return is_inner_open && sum_run(outer) == 3 && stack.held() == 3;
        },
        [] noexcept { return false; });
    CRUCIBLE_TEST_ASSERT(kTest, is_disjoint && stack.held() == 0);
    std::uint32_t const inner_sum =
        stack.with_window(count_of(8), [](auto all) noexcept { return sum_run(all); }, [] noexcept { return 0u; });
    CRUCIBLE_TEST_ASSERT(kTest, inner_sum == 3 + 50);
}

// A window past the free elements runs the closed arm and leaves the top.  A
// window of a fixed extent gives a ViewMut of that extent.
void test_a_full_stack_runs_the_closed_arm() {
    Stack stack{};
    int const code = stack.with_window<6>(
        [&stack](auto most) noexcept {
            static_assert(decltype(most)::extent == 6);
            int const refused =
                stack.with_window(count_of(3), [](auto) noexcept { return 1; }, [] noexcept { return 2; });
            int const rest = stack.with_window<2>([](auto) noexcept { return 10; }, [] noexcept { return 20; });
            return refused + rest + static_cast<int>(stack.held()) * 100;
        },
        [] noexcept { return -1; });
    CRUCIBLE_TEST_ASSERT(kTest, code == 2 + 10 + 600 && stack.held() == 0);
}

// An arm of a foreign door that raises ends its window on the unwind, and a
// foreign door gives the reference that its arm gives.
void test_a_foreign_door_ends_its_window_on_a_raise() {
    Stack stack{};
    std::uint32_t caught = 0;
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: an arm raises through a foreign door, as an operator of PyTorch raises")
    try {
        static_cast<void>(stack.with_foreign_window(
            count_of(4),
            [](auto window) -> std::uint32_t {
                if (window.size() == 4) throw std::uint32_t{7};
                return 0;
            },
            [] noexcept { return std::uint32_t{0}; }));
    } catch (std::uint32_t const& thrown) {
        caught = thrown;
    }
    std::uint32_t held = 9;
    std::uint32_t& given = stack.with_foreign_window<2>([&held](auto) noexcept -> std::uint32_t& { return held; },
                                                        [&held]() noexcept -> std::uint32_t& { return held; });
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    CRUCIBLE_TEST_ASSERT(kTest, caught == 7 && stack.held() == 0 && &given == &held);
}

// The outer loan ends while the inner loan holds the top past it.
void end_out_of_order() noexcept {
    Stack stack{};
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    core::Box<Loan> outer = core::mint_box<Loan>(alloc, stack, count_of(2));
    core::Box<Loan> const inner = core::mint_box<Loan>(alloc, stack, count_of(2));
    { core::Box<Loan> const ended = std::move(outer); }
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_a_loan_opens_and_ends_in_order();
    test_nested_loans_have_windows_that_do_not_overlap();
    test_a_full_stack_gives_no_window();
    test_a_window_door_opens_and_ends_in_order();
    test_nested_window_doors_do_not_overlap();
    test_a_full_stack_runs_the_closed_arm();
    test_a_foreign_door_ends_its_window_on_a_raise();
    ::fixy::check::expect_dies(test, end_out_of_order, ::fixy::TextView{"stack_->top_ == base_ + count_"});
    ::fixy::check::pass(kTest, "test_core_window_stack: PASSED\n");
    return 0;
}
