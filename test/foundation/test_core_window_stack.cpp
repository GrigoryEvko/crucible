// The window stack of foundation/core/WindowStack.h: loans that open and end
// in order, nested loans with windows that do not overlap, a loan that does
// not fit, and the end of a loan out of order, which ends the process.

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
    ::fixy::check::expect_dies(test, end_out_of_order, ::fixy::TextView{"stack_->top_ == base_ + count_"});
    ::fixy::check::pass(kTest, "test_core_window_stack: PASSED\n");
    return 0;
}
