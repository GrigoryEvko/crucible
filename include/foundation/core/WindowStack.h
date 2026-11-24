#pragma once

// The window stack of the Region family: N elements inline, which the stack
// lends as windows that do not overlap, in the order of a stack.
//
//   WindowStack<T, N>   N elements in the object itself, and the top: the
//                       count of the elements that the open windows hold
//   WindowLoan<T, N>    the loan of one window of count elements at the top
//                       of a stack, from its construction to its end
//
// Window doors
//   stack.with_window(count, on_open, on_closed)
//                       takes the count elements at the top of the stack
//                       when they fit, moves the top past them, and calls
//                       on_open with the ViewMut<T, Brand> of the window,
//                       with the scope brand of the call.  When on_open
//                       returns or raises, the top goes back to the start of
//                       the window.  When the window does not fit, on_closed
//                       runs, and the top stays.  The two arms give one type
//   stack.with_window<Count>(on_open, on_closed)
//                       the same door for a window of Count elements: on_open
//                       gets the ViewMut<T, Brand, Count> of the fixed extent
//   stack.with_foreign_window(count, on_open, on_closed)
//   stack.with_foreign_window<Count>(on_open, on_closed)
//                       the two doors above, for arms that run a foreign
//                       runtime (the foreign doors)
// The result of the arms of a window door leaves the frame (FrameResult of
// foundation/core/Choice.h), and the arms store no borrow outside the frame
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h).  So no View, Ref
// or address of the window outlives the door.  An arm can raise.
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
// A window starts at the top, and each window that opens while it is open
// starts past its end.  So the windows of two open doors do not overlap, and
// two ViewMuts of two brands name two runs that do not overlap (the alias
// proof of foundation/core/Frame.h).  The ViewMut of a door is the one writer
// of its window.  A window door names no loan to its arms, so an arm, a
// function that it calls and a Ref of the stack open only new windows past
// the window of the door, and never a second writer of it.  The stack gives
// no View of its whole storage and no element: each read and each write goes
// through a window.  An element of a window holds the value that the last
// window of its slot wrote, or the value of its construction, so a reader
// reads only the elements that its own window wrote.
//
// The windows of the doors end in the reverse order of their opens, because
// each door ends its window before it returns.  A loan does not copy and does
// not move, and it holds a Ref to its stack, so the loans of one stack end in
// the reverse order of their opens when each one lives in a scope.  The end of
// a loan ends the process in each build when the top is not the end of its
// window: a loan that ends before a loan that it holds open ends the process.
// The stack does not copy or move, because each open window points into it.
//
// Foreign doors
//   A foreign door lends the same ViewMut and ends its window in the same
//   order, and it checks neither the result nor the captures of its arms.  It
//   is the hatch for arms that run a foreign runtime, such as an operator of
//   PyTorch: such an arm gives the result of the runtime, which can be a
//   reference, and it captures objects that hold borrows.  An arm can then
//   keep a borrow of the window past the door, and no check sees it.  So each
//   call stands in an opt-out region of the class ABI (foundation/Quarantine.h),
//   and the ledger of the rule quarantine counts each region.
//
// Owner thread
//   A stack has one owner thread, which opens and ends each of its windows.
//   The top is a plain word, with no atomic step, so a window from a second
//   thread is a race on the top.
//
// Complexity: O(1) for each open and each end.

#include <foundation/Brand.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/core/Array.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Index.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

template <class T, std::size_t N>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && std::is_nothrow_default_constructible_v<T>
class WindowLoan;

// The door of a WindowLoan to the door of a Ref (the doors of the owners above
// foundation/core/Ref.h).  Its one member is private, and WindowLoan is its
// one friend.  The invariant: a loan holds the Ref of its stack with the
// private brand of the loan, the loan is the one holder of that Ref, and the
// loan ends before its stack.  A home owner row of utils/rule-table.txt keeps
// this definition in this header.
class WindowLoanRefDoor final : ::foundation::NoObject<WindowLoanRefDoor> {
    template <class T, std::size_t N>
        requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && std::is_nothrow_default_constructible_v<T>
    friend class WindowLoan;

    // The stack of a loan, as a Ref of the brand of the loan.
    template <::foundation::brand::IsBrand Brand, RefTarget T>
    [[nodiscard]] CRUCIBLE_INLINE static constexpr Ref<T, Brand> lend_(T& object) noexcept {
        return detail::RefDoor::lend<Brand>(object);
    }
};

// The door of a WindowLoan to the key of the frame doors (the doors of the
// owners above foundation/core/Frame.h).  Its one member is private, and
// WindowLoan is its one friend.  The invariant: a loan reads the run of the
// FixedArray of its stack only to take the window above the top, which no
// other open window holds.  A home owner row of utils/rule-table.txt keeps
// this definition in this header.
class WindowLoanFrameKeyDoor final : ::foundation::NoObject<WindowLoanFrameKeyDoor> {
    template <class T, std::size_t N>
        requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && std::is_nothrow_default_constructible_v<T>
    friend class WindowLoan;

    // The key of the frame doors, for the run of the slots of a stack.
    [[nodiscard]] CRUCIBLE_INLINE static constexpr detail::FrameKey key_() noexcept { return detail::FrameDoor::key(); }
};

// The count of the elements of a window.
using WindowCount = Quantity<unit::Element, std::uint32_t>;

// Whether a loan holds its window.
enum class WindowState : std::uint8_t {
    closed,
    open,
};

// The result of the open arm of a window door, for the ViewMut of its window.
template <class OnOpen, class T, class Brand, std::size_t Extent>
using window_open_result_t = detail::body_result_t<OnOpen&&, ViewMut<T, Brand, Extent>>;

// The two arms of a window door: the open arm takes the ViewMut of the
// window, the closed arm takes nothing, and the two give one type.  An arm
// can raise.
template <class OnOpen, class OnClosed, class T, class Brand, std::size_t Extent>
concept WindowArmsCallable =
    std::is_invocable_v<OnOpen&&, ViewMut<T, Brand, Extent>> && std::is_invocable_v<OnClosed&&>
    && std::same_as<window_open_result_t<OnOpen, T, Brand, Extent>, std::invoke_result_t<OnClosed&&>>;

// The arms of a window door: their result leaves the frame (FrameResult of
// foundation/core/Choice.h), they hold no linear value by value, and they
// store no borrow outside the frame.
template <class OnOpen, class OnClosed, class T, class Brand, std::size_t Extent>
concept WindowArms = WindowArmsCallable<OnOpen, OnClosed, T, Brand, Extent>
                  && FrameResult<window_open_result_t<OnOpen, T, Brand, Extent>, Brand>
                  && ::foundation::linear::HoldsNoLinearValue<OnOpen>
                  && ::foundation::linear::HoldsNoLinearValue<OnClosed> && ArmsStoreNoBorrowOutside<OnOpen, OnClosed>;

// The arms whose result does not leave the frame: the result twin of a window
// door binds to them, so the refusal gives its reason.
template <class OnOpen, class OnClosed, class T, class Brand, std::size_t Extent>
concept RefusedWindowArms =
    WindowArmsCallable<OnOpen, OnClosed, T, Brand, Extent>
    && (!FrameResult<window_open_result_t<OnOpen, T, Brand, Extent>, Brand>)
    && ::foundation::linear::HoldsNoLinearValue<OnOpen> && ::foundation::linear::HoldsNoLinearValue<OnClosed>
    && ArmsStoreNoBorrowOutside<OnOpen, OnClosed>;

// The arms of which one holds a linear value by value: the linear twin of
// each window door binds to them.
template <class OnOpen, class OnClosed, class T, class Brand, std::size_t Extent>
concept LinearWindowArms =
    std::is_invocable_v<OnOpen&&, ViewMut<T, Brand, Extent>> && std::is_invocable_v<OnClosed&&>
    && (!::foundation::linear::HoldsNoLinearValue<OnOpen> || !::foundation::linear::HoldsNoLinearValue<OnClosed>);

// The arms of which one stores a borrow outside the frame: the store twin of
// a window door binds to them, so the refusal gives its reason.
template <class OnOpen, class OnClosed, class T, class Brand, std::size_t Extent>
concept StoringWindowArms =
    std::is_invocable_v<OnOpen&&, ViewMut<T, Brand, Extent>> && std::is_invocable_v<OnClosed&&>
    && ::foundation::linear::HoldsNoLinearValue<OnOpen> && ::foundation::linear::HoldsNoLinearValue<OnClosed>
    && (!ArmsStoreNoBorrowOutside<OnOpen, OnClosed>);

// The arms of a foreign door: they give one type and hold no linear value by
// value, and the door checks no other fact of them (the foreign doors).
template <class OnOpen, class OnClosed, class T, class Brand, std::size_t Extent>
concept ForeignWindowArms =
    WindowArmsCallable<OnOpen, OnClosed, T, Brand, Extent> && ::foundation::linear::HoldsNoLinearValue<OnOpen>
    && ::foundation::linear::HoldsNoLinearValue<OnClosed>;

// True when the arms of a window door raise nothing.
template <class OnOpen, class OnClosed, class T, class Brand, std::size_t Extent>
inline constexpr bool window_arms_are_nothrow_v =
    std::is_nothrow_invocable_v<OnOpen&&, ViewMut<T, Brand, Extent>> && std::is_nothrow_invocable_v<OnClosed&&>;

// A count of a window of a fixed extent: a View extent of at most the
// elements of the stack.
template <class T, std::size_t Count, std::size_t N>
concept WindowExtent = ViewExtent<T, Count> && (Count != dynamic_extent) && (Count <= N);

template <class T, std::size_t N>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && std::is_nothrow_default_constructible_v<T>
class WindowStack final {
    FixedArray<T, N> slots_{};
    std::size_t top_ = 0;

    friend class WindowLoan<T, N>;

public:
    using element_type = T;

    // Each element value-initialized, and no window open.
    constexpr WindowStack() noexcept = default;

    WindowStack(WindowStack const&) = delete("each open window points into its stack");
    WindowStack& operator=(WindowStack const&) = delete("each open window points into its stack");
    WindowStack(WindowStack&&) = delete("each open window points into its stack");
    WindowStack& operator=(WindowStack&&) = delete("each open window points into its stack");

    // The count of the elements that the open windows hold, at most N.
    [[nodiscard]] constexpr std::size_t held() const noexcept {
        std::size_t const top = top_;
        [[assume(top <= N)]];
        return top;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

    // The window door of count elements (the window doors).  The pack refuses
    // each explicit argument, so no caller names the brand of the frame.
    // Complexity: O(1), and the cost of the arm.
    template <class... Refused, class OnOpen, class OnClosed, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && WindowArms<OnOpen, OnClosed, T, Fresh, dynamic_extent>
    constexpr decltype(auto) with_window(WindowCount count, OnOpen&& on_open, OnClosed&& on_closed) & noexcept(
        window_arms_are_nothrow_v<OnOpen, OnClosed, T, Fresh, dynamic_extent>) {
        return WindowLoan<T, N>::template lend_<Fresh, dynamic_extent>(*this, count, static_cast<OnOpen&&>(on_open),
                                                                       static_cast<OnClosed&&>(on_closed));
    }
    template <class... Refused, class OnOpen, class OnClosed, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && RefusedWindowArms<OnOpen, OnClosed, T, Fresh, dynamic_extent>
    void with_window(WindowCount count, OnOpen&& on_open, OnClosed&& on_closed) & = delete(
        "the result of a frame door leaves the frame, so it holds no reference and no address, and it names no brand of "
        "the frame: give a plain value");
    template <class... Refused, class OnOpen, class OnClosed, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && LinearWindowArms<OnOpen, OnClosed, T, Fresh, dynamic_extent>
    void with_window(WindowCount count, OnOpen&& on_open,
                     OnClosed&& on_closed) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class... Refused, class OnOpen, class OnClosed, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && StoringWindowArms<OnOpen, OnClosed, T, Fresh, dynamic_extent>
    void with_window(WindowCount count, OnOpen&& on_open,
                     OnClosed&& on_closed) & = delete(CRUCIBLE_STORING_BODY_REFUSAL);

    // The window door of Count elements: on_open gets the ViewMut of the
    // fixed extent Count.  Complexity: O(1), and the cost of the arm.
    template <std::size_t Count, class... Refused, class OnOpen, class OnClosed,
              ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && WindowExtent<T, Count, N> && WindowArms<OnOpen, OnClosed, T, Fresh, Count>
    constexpr decltype(auto)
    with_window(OnOpen&& on_open,
                OnClosed&& on_closed) & noexcept(window_arms_are_nothrow_v<OnOpen, OnClosed, T, Fresh, Count>) {
        return WindowLoan<T, N>::template lend_<Fresh, Count>(*this, WindowCount{Count}, static_cast<OnOpen&&>(on_open),
                                                              static_cast<OnClosed&&>(on_closed));
    }
    template <std::size_t Count, class... Refused, class OnOpen, class OnClosed,
              ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0)
                 && WindowExtent<T, Count, N> && RefusedWindowArms<OnOpen, OnClosed, T, Fresh, Count>
    void with_window(OnOpen&& on_open, OnClosed&& on_closed) & = delete(
        "the result of a frame door leaves the frame, so it holds no reference and no address, and it names no brand of "
        "the frame: give a plain value");
    template <std::size_t Count, class... Refused, class OnOpen, class OnClosed,
              ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0)
                 && WindowExtent<T, Count, N> && LinearWindowArms<OnOpen, OnClosed, T, Fresh, Count>
    void with_window(OnOpen&& on_open, OnClosed&& on_closed) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <std::size_t Count, class... Refused, class OnOpen, class OnClosed,
              ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0)
                 && WindowExtent<T, Count, N> && StoringWindowArms<OnOpen, OnClosed, T, Fresh, Count>
    void with_window(OnOpen&& on_open, OnClosed&& on_closed) & = delete(CRUCIBLE_STORING_BODY_REFUSAL);

    // The foreign door of count elements (the foreign doors).  Each call
    // stands in an opt-out region of the class ABI.  Complexity: O(1), and the
    // cost of the arm.
    template <class... Refused, class OnOpen, class OnClosed, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && ForeignWindowArms<OnOpen, OnClosed, T, Fresh, dynamic_extent>
    constexpr decltype(auto) with_foreign_window(WindowCount count, OnOpen&& on_open, OnClosed&& on_closed) & noexcept(
        window_arms_are_nothrow_v<OnOpen, OnClosed, T, Fresh, dynamic_extent>) {
        return WindowLoan<T, N>::template lend_<Fresh, dynamic_extent>(*this, count, static_cast<OnOpen&&>(on_open),
                                                                       static_cast<OnClosed&&>(on_closed));
    }
    template <class... Refused, class OnOpen, class OnClosed, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && LinearWindowArms<OnOpen, OnClosed, T, Fresh, dynamic_extent>
    void with_foreign_window(WindowCount count, OnOpen&& on_open,
                             OnClosed&& on_closed) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

    // The foreign door of Count elements.  Each call stands in an opt-out
    // region of the class ABI.  Complexity: O(1), and the cost of the arm.
    template <std::size_t Count, class... Refused, class OnOpen, class OnClosed,
              ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0)
             && WindowExtent<T, Count, N> && ForeignWindowArms<OnOpen, OnClosed, T, Fresh, Count>
    constexpr decltype(auto)
    with_foreign_window(OnOpen&& on_open,
                        OnClosed&& on_closed) & noexcept(window_arms_are_nothrow_v<OnOpen, OnClosed, T, Fresh, Count>) {
        return WindowLoan<T, N>::template lend_<Fresh, Count>(*this, WindowCount{Count}, static_cast<OnOpen&&>(on_open),
                                                              static_cast<OnClosed&&>(on_closed));
    }
    template <std::size_t Count, class... Refused, class OnOpen, class OnClosed,
              ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0)
                 && WindowExtent<T, Count, N> && LinearWindowArms<OnOpen, OnClosed, T, Fresh, Count>
    void with_foreign_window(OnOpen&& on_open, OnClosed&& on_closed) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

    // A temporary stack ends with the full expression, so a window door of it
    // lends a window that no later code reads.  A window door moves the top,
    // so a const stack has no window door.
    template <class... Arguments>
    void with_window(Arguments&&... arguments) && = delete("a window door of a temporary stack lends a window that "
                                                           "the full expression ends");
    template <std::size_t Count, class... Arguments>
    void with_window(Arguments&&... arguments) && = delete("a window door of a temporary stack lends a window that "
                                                           "the full expression ends");
    template <class... Arguments>
    void with_foreign_window(Arguments&&... arguments) && = delete("a window door of a temporary stack lends a window "
                                                                   "that the full expression ends");
    template <std::size_t Count, class... Arguments>
    void with_foreign_window(Arguments&&... arguments) && = delete("a window door of a temporary stack lends a "
                                                                   "window that the full expression ends");
};

template <class T, std::size_t N>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && std::is_nothrow_default_constructible_v<T>
class [[nodiscard]] WindowLoan final {
    // The brand of the Ref of a loan to its stack: the loan is the one holder
    // of the Ref, and it ends before its stack.
    struct StackBrand final {};

    friend class WindowStack<T, N>;

    Ref<WindowStack<T, N>, StackBrand> stack_;
    std::size_t base_ = 0;
    std::size_t count_ = 0;
    View<T> window_{};
    WindowState state_ = WindowState::closed;

    // The loan of the window of count elements at the top, which the door of
    // the stack found to fit: the top moves past the window.
    constexpr WindowLoan(WindowStack<T, N>& stack, std::size_t count) noexcept
        : stack_{WindowLoanRefDoor::lend_<StackBrand>(stack)},
          base_{stack.top_},
          count_{count},
          state_{WindowState::open} {
        stack.top_ = base_ + count;
    }

    // The window of count elements at the top of the stack, or of Extent
    // elements for a fixed extent, when it fits: on_open gets its ViewMut with
    // the brand Fresh, and the loan holds the window until on_open returns or
    // raises.  When the window does not fit, on_closed runs, and the top
    // stays.  The fit test is the window of the run, and only a loan reaches
    // the run of the storage.  The loop over the Option admits an arm that
    // raises or that gives a reference, as a foreign door asks.
    template <class Fresh, std::size_t Extent, class OnOpen, class OnClosed>
    CRUCIBLE_INLINE static constexpr decltype(auto) lend_(WindowStack<T, N>& stack, WindowCount count, OnOpen&& on_open,
                                                          OnClosed&& on_closed) {
        std::size_t const top = stack.top_;
        // Only a loan moves the top, and only to the end of a window that fits.
        [[assume(top <= N)]];
        View<T, N> const run = frame_run_(WindowLoanFrameKeyDoor::key_(), stack.slots_);
        if constexpr (Extent == dynamic_extent) {
            for (View<T> const window : run.window(top, count.raw())) {
                WindowLoan const loan{stack, window.size()};
                return static_cast<OnOpen&&>(on_open)(ViewMut<T, Fresh>{WindowLoanFrameKeyDoor::key_(), window});
            }
        } else {
            for (View<T, Extent> const window : run.template window<Extent>(top)) {
                WindowLoan const loan{stack, Extent};
                return static_cast<OnOpen&&>(on_open)(
                    ViewMut<T, Fresh, Extent>{WindowLoanFrameKeyDoor::key_(), window});
            }
        }
        return static_cast<OnClosed&&>(on_closed)();
    }

public:
    // The loan of count elements at the top of stack, or a loan of no window
    // when they do not fit.  Complexity: O(1).
    constexpr WindowLoan(WindowStack<T, N>& stack, WindowCount count) noexcept
        : stack_{WindowLoanRefDoor::lend_<StackBrand>(stack)}, base_{stack.top_}, count_{count.raw()} {
        std::size_t const top = stack.top_;
        [[assume(top <= N)]];
        View<T>{frame_run_(WindowLoanFrameKeyDoor::key_(), stack.slots_)}
            .window(top, count.raw())
            .match(
                [this](View<T> window) noexcept {
                    window_ = window;
                    state_ = WindowState::open;
                },
                [] noexcept {});
        if (state_ == WindowState::open) stack.top_ = top + count_;
    }

    // Gives the window back.  Each loan that opened after this one ended
    // before it, so the top is the end of the window of this loan.  The check
    // holds in each build and under each contract semantic.
    constexpr ~WindowLoan() {
        if (state_ == WindowState::closed) return;
        CRUCIBLE_FATAL_INVARIANT(stack_->top_ == base_ + count_);
        stack_->top_ = base_;
    }

    WindowLoan(WindowLoan const&) = delete("a loan ends at the end of its scope, and a copy would end it two times");
    WindowLoan& operator=(WindowLoan const&) = delete("a loan ends at the end of its scope");
    WindowLoan(WindowLoan&&) = delete("a loan ends at the end of its scope, in the reverse order of its open");
    WindowLoan& operator=(WindowLoan&&) = delete("a loan ends at the end of its scope");

    // True when the loan holds its window.
    [[nodiscard]] constexpr bool is_open() const noexcept { return state_ == WindowState::open; }

    // The View of the window of the loan, or no value for a loan that does
    // not fit.  An element holds the value of the last loan of its slot until
    // the caller writes it.
    [[nodiscard]] constexpr Option<View<T>> window() const noexcept {
        if (state_ == WindowState::closed) return none;
        return Option<View<T>>::some(window_);
    }
};

}  // namespace foundation::core
