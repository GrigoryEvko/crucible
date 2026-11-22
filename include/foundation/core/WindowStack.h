#pragma once

// The window stack of the Region family: N elements inline, which the stack
// lends as windows that do not overlap, in the order of a stack.
//
//   WindowStack<T, N>   N elements in the object itself, and the top: the
//                       count of the elements that the open loans hold
//   WindowLoan<T, N>    the loan of one window of count elements at the top
//                       of a stack, from its construction to its end
//
// Loans
//   WindowLoan loan{stack, count}   takes the count elements at the top of
//                       the stack when they fit, and moves the top past them.
//                       A loan that does not fit holds no window, and the top
//                       stays
//   loan.window()       the View<T> of the window of the loan, or no value for
//                       a loan that does not fit
//   the end of a loan   gives the window back: the top goes back to the start
//                       of the window
//
// A window starts at the top, and each loan that opens while it is open
// starts past its end.  So the windows of two open loans do not overlap, and
// the View of each loan is the only View that writes its elements.  The stack
// gives no View of its whole storage, no element and no frame door: each read
// and each write goes through a loan.  An element of a window holds the value
// that the last loan of its slot wrote, or the value of its construction, so
// a reader reads only the elements that its own loan wrote.
//
// A loan does not copy and does not move, and it holds a Ref to its stack, so
// the loans of one stack end in the reverse order of their opens when each one
// lives in a scope.  The end of a loan ends the process in each build when the
// top is not the end of its window: a loan that ends before a loan that it
// holds open ends the process.  The stack does not copy or move, because each
// open loan holds a Ref to it.
//
// Owner thread
//   A stack has one owner thread, which opens and ends each of its loans.  The
//   top is a plain word, with no atomic step, so a loan from a second thread
//   is a race on the top.
//
// Complexity: O(1) for each open and each end.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Index.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

template <class T, std::size_t N>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && std::is_nothrow_default_constructible_v<T>
class WindowLoan;

// The count of the elements of a window.
using WindowCount = Quantity<unit::Element, std::uint32_t>;

template <class T, std::size_t N>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && std::is_nothrow_default_constructible_v<T>
class WindowStack final {
    FixedArray<T, N> slots_{};
    std::size_t top_ = 0;

    friend class WindowLoan<T, N>;

public:
    using element_type = T;

    // Each element value-initialized, and no loan open.
    constexpr WindowStack() noexcept = default;

    WindowStack(WindowStack const&) = delete("each open loan holds a Ref to its stack");
    WindowStack& operator=(WindowStack const&) = delete("each open loan holds a Ref to its stack");
    WindowStack(WindowStack&&) = delete("each open loan holds a Ref to its stack");
    WindowStack& operator=(WindowStack&&) = delete("each open loan holds a Ref to its stack");

    // The count of the elements that the open loans hold, at most N.
    [[nodiscard]] constexpr std::size_t held() const noexcept {
        std::size_t const top = top_;
        [[assume(top <= N)]];
        return top;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }
};

template <class T, std::size_t N>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && std::is_nothrow_default_constructible_v<T>
class [[nodiscard]] WindowLoan final {
    // The brand of the Ref of a loan to its stack: the loan is the one holder
    // of the Ref, and the order check at its end proves that its stack lives.
    struct StackBrand final {};

    Ref<WindowStack<T, N>, StackBrand> stack_;
    std::size_t base_ = 0;
    std::size_t count_ = 0;
    Option<View<T>> window_{none};

    // The count elements at the top of the stack, which the top then passes,
    // or no value and no change of the top when they do not fit.  The window
    // of the run is the one fit test of the open, and only a loan reaches the
    // run of the storage.
    [[nodiscard]] static constexpr Option<View<T>> take_window_(WindowStack<T, N>& stack, std::size_t count) noexcept {
        std::size_t const top = stack.top_;
        // Only a loan moves the top, and only to the end of a window that fits.
        [[assume(top <= N)]];
        Option<View<T>> window = View<T>{frame_run_(detail::FrameDoor::key(), stack.slots_)}.window(top, count);
        if (window.is_some()) stack.top_ = top + count;
        return window;
    }

public:
    // The loan of count elements at the top of stack, or a loan of no window
    // when they do not fit.  Complexity: O(1).
    constexpr WindowLoan(WindowStack<T, N>& stack, WindowCount count) noexcept
        : stack_{detail::RefDoor::lend<StackBrand>(stack)},
          base_{stack.top_},
          count_{count.raw()},
          window_{take_window_(stack, count.raw())} {}

    // Gives the window back.  Each loan that opened after this one ended
    // before it, so the top is the end of the window of this loan.  The check
    // holds in each build and under each contract semantic.
    constexpr ~WindowLoan() {
        if (window_.is_none()) return;
        CRUCIBLE_FATAL_INVARIANT(stack_->top_ == base_ + count_);
        stack_->top_ = base_;
    }

    WindowLoan(WindowLoan const&) = delete("a loan ends at the end of its scope, and a copy would end it two times");
    WindowLoan& operator=(WindowLoan const&) = delete("a loan ends at the end of its scope");
    WindowLoan(WindowLoan&&) = delete("a loan ends at the end of its scope, in the reverse order of its open");
    WindowLoan& operator=(WindowLoan&&) = delete("a loan ends at the end of its scope");

    // True when the loan holds its window.
    [[nodiscard]] constexpr bool is_open() const noexcept { return window_.is_some(); }

    // The View of the window of the loan, or no value for a loan that does
    // not fit.  An element holds the value of the last loan of its slot until
    // the caller writes it.
    [[nodiscard]] constexpr Option<View<T>> window() const noexcept { return window_; }
};

}  // namespace foundation::core
