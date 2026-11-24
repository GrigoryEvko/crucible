// The window stack of foundation/core/WindowStack.h: window doors that open
// and end in order, nested windows that do not overlap, also when a closure
// that the arm calls opens them, a window that does not fit, a window of a
// fixed extent, and a foreign door whose arm raises or gives a reference.

#include <foundation/Quarantine.h>
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

[[nodiscard]] constexpr core::WindowCount count_of(std::uint32_t count) noexcept { return core::WindowCount{count}; }

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
    CRUCIBLE_TEST_ASSERT(kTest, stack.held() == 0 && Stack::capacity() == 8);
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
// a closure that the arm calls opens it.  So the inner window is a second run
// with a brand of its own, and no second writer of the outer window exists.
void test_nested_window_doors_do_not_overlap() {
    Stack stack{};
    auto const open_inner = [&stack](core::View<std::uint32_t const> outer, std::uint32_t value) noexcept {
        return stack.with_window(
            count_of(5),
            [outer, value](auto inner) noexcept {
                bool const is_disjoint =
                    core::offset_of(outer, inner.view()).is_none() && core::offset_of(inner.view(), outer).is_none();
                fill_run(std::move(inner), value);
                return is_disjoint;
            },
            [] noexcept { return false; });
    };
    bool const is_disjoint = stack.with_window(
        count_of(3),
        [&open_inner, &stack](auto outer) noexcept {
            for (std::uint32_t& element : outer) {
                element = 1;
            }
            bool const is_inner_disjoint = open_inner(outer.view(), 10);
            return is_inner_disjoint && sum_run(outer) == 3 && stack.held() == 3;
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

}  // namespace

int main() {
    test_a_window_door_opens_and_ends_in_order();
    test_nested_window_doors_do_not_overlap();
    test_a_full_stack_runs_the_closed_arm();
    test_a_foreign_door_ends_its_window_on_a_raise();
    ::fixy::check::pass(kTest, "test_core_window_stack: PASSED\n");
    return 0;
}
