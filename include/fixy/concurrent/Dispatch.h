#pragma once

// The dispatch door: the root door at the boundary of an outside dispatcher,
// such as the dispatcher of PyTorch, that mints the keys of one dispatch.
//
//   run_dispatch(ctx, body)   opens a dispatch window on the calling thread,
//                             calls body(ctx, keys...) one time with one fresh
//                             key of each brand that it names, closes the
//                             window, and gives the value of the body
//
// Window
//     The body of a dispatch reaches its owners through outside data, such as
//     the Vigil that the thread-local pointer of PyTorch names, so no walk of
//     its type sees them.  A flag of the calling thread carries the proof
//     instead: the door sets it when it opens the window and clears it when the
//     window closes.  A call of the door that finds the flag set is a nested
//     dispatch.  It ends the process, because two windows on one thread would
//     hold two keys of one brand at the same time.  The flag is the one
//     run-time check of the owner key, and it sits at an outside entry.
//
// Keys
//     The body takes the context first, and then one key LoanKey<B>& of each
//     brand that it names.  Each brand states dispatch_brand{} (the dispatch
//     brand of foundation/core/Frame.h).  The doors that start a thread mint
//     no key of such a brand, so a key of it lives on a thread only inside the
//     window, and the window holds one key of each brand.  Such a key does not
//     move, and no Ref, Box or View holds one, so the body keeps no key past
//     the window.  So the door needs no walk of the type of the body.  The
//     door takes the body by move into its own frame, and the body runs one
//     time as an rvalue.  The entry of foundation/core/RootKeys.h mints the
//     keys, and it takes the key of the frame doors, which DispatchFrameKeyDoor
//     gives only to this door after it opens the window.  So no code mints a
//     key of a dispatch brand outside a window.
//
// Gate
//     The context of the call has a branded foreground source, which only a
//     producer claim and the test witness build.  Such a source moves to no
//     other thread, so no body of a door that starts a thread holds one, and a
//     dispatch opens only on the thread of a claim.
//
// Redispatch
//     The record kernel of the vessel runs the operation through a redispatch
//     to PyTorch first, and each operation of that redispatch enters the
//     record kernel again on the same thread (call_recording of
//     vessel/torch/record_kernel.h, the nest depth of VigilOpNest).  The
//     dispatch to the Vigil runs after the redispatch returns.  So the window
//     holds only the dispatch to the Vigil, and never the redispatch: a nested
//     record during the redispatch opens and closes its own window before the
//     outer window opens.
//
// Flag
//     The flag is one bool of the static TLS of each thread.  It is the static
//     local of a private member function of the window, so only the
//     constructor and the destructor of a window write it.  It has the
//     initial-exec model, so an open and a close cost one load and two stores
//     relative to the thread pointer and no call.  CRUCIBLE_PROCESS_WIDE makes
//     one flag of each thread for the process, also when two shared libraries
//     open windows.  The refusal of a nested dispatch is a cold function of
//     src/fixy/concurrent/Dispatch.cpp.

#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/core/RootKeys.h>
#include <foundation/effects/Ctx.h>

#include <type_traits>

namespace fixy::dispatch {

namespace detail {

// Ends the process for a dispatch that opens inside a dispatch window of the
// same thread (the window).
[[noreturn]] CRUCIBLE_COLD void refuse_nested_dispatch_() noexcept;

// The window of one dispatch on the calling thread: the constructor opens it,
// and the destructor closes it.  It has no copy and no move, so it stays in
// the frame of the door.  A private member function holds the flag as its
// static local, so only a window changes it, and no body closes the window of
// its door.
class [[nodiscard]] DispatchWindow_ final {
public:
    DispatchWindow_() noexcept {
        bool& is_in_dispatch_window = flag_of_thread_();
        // The check stands for fact 2 of foundation/Brand.h, because no type tells two runs of one site apart.
        if (is_in_dispatch_window) [[unlikely]] {
            refuse_nested_dispatch_();
        }
        is_in_dispatch_window = true;
    }

    DispatchWindow_(DispatchWindow_ const&) = delete("a dispatch window belongs to the frame of its door");
    DispatchWindow_& operator=(DispatchWindow_ const&) = delete("a dispatch window belongs to the frame of its door");
    ~DispatchWindow_() noexcept { flag_of_thread_() = false; }

private:
    // The flag of the calling thread: true while the thread is inside a
    // dispatch window (the flag).
    [[nodiscard]] CRUCIBLE_PROCESS_WIDE static bool& flag_of_thread_() noexcept {
        [[gnu::tls_model("initial-exec")]] constinit thread_local bool is_in_dispatch_window = false;
        return is_in_dispatch_window;
    }
};

}  // namespace detail

// A context that opens a dispatch: an execution context whose source is a
// branded foreground source (the gate).
template <typename Ctx>
concept CtxOpensDispatch =
    ::foundation::effects::IsExecCtx<Ctx> && ::foundation::effects::IsBrandedForeground<typename Ctx::cap_type>;

// A body of the dispatch door for the context Ctx: an object that is not a
// reference and not const, that runs as an rvalue with the context and then
// one key LoanKey<B>& of each brand that it names (the keys).
template <typename Body, typename Ctx>
concept DispatchBody = !std::is_reference_v<Body> && !std::is_const_v<Body> && requires {
    typename ::foundation::core::detail::root_body_result_<Body, ::foundation::core::root_key_brands_t<Body, 1>,
                                                           Ctx const&>::type;
};

// Opens a dispatch window on the calling thread, calls the body one time with
// the context and one fresh key of each brand that it names, closes the
// window, and gives the value of the body (the window).  A call inside a
// window of the same thread ends the process.  DispatchFrameKeyDoor names it
// as its friend, so this declaration stands before the door.
template <typename Ctx, typename Body>
    requires(!std::is_lvalue_reference_v<Body>) && CtxOpensDispatch<Ctx> && DispatchBody<Body, Ctx>
auto run_dispatch(Ctx const& ctx, Body&& body) noexcept;

}  // namespace fixy::dispatch

namespace foundation::core {

// The door of the dispatch door to the key of the frame doors (the doors of
// the owners above foundation/core/Frame.h).  Its one member is private, and
// run_dispatch of fixy/concurrent/Dispatch.h is its one friend.  The
// invariant: run_dispatch opens the window of the calling thread first, and
// then it gives the key to the entry of the dispatch door
// (foundation/core/RootKeys.h), which mints the keys of one call of the body.
// So no key of a dispatch brand comes from outside a window.  A home owner row
// of utils/rule-table.txt keeps this definition in this header.
class DispatchFrameKeyDoor final : ::foundation::NoObject<DispatchFrameKeyDoor> {
    template <typename Ctx, typename Body>
        requires(!std::is_lvalue_reference_v<Body>)
             && ::fixy::dispatch::CtxOpensDispatch<Ctx> && ::fixy::dispatch::DispatchBody<Body, Ctx>
    friend auto ::fixy::dispatch::run_dispatch(Ctx const& ctx, Body&& body) noexcept;

    // The key of the frame doors, for the keys of one dispatch.
    [[nodiscard]] CRUCIBLE_INLINE static constexpr detail::FrameKey key_() noexcept { return detail::FrameDoor::key(); }
};

}  // namespace foundation::core

namespace fixy::dispatch {

template <typename Ctx, typename Body>
    requires(!std::is_lvalue_reference_v<Body>) && CtxOpensDispatch<Ctx> && DispatchBody<Body, Ctx>
auto run_dispatch(Ctx const& ctx, Body&& body) noexcept {
    detail::DispatchWindow_ const window{};
    return ::foundation::core::detail::run_dispatch_body_<1>(::foundation::core::DispatchFrameKeyDoor::key_(),
                                                             static_cast<Body&&>(body), ctx);
}

// A body that the caller names as an lvalue stays an object of the caller,
// which could call it again past the window.
template <typename Ctx, typename Body>
    requires std::is_lvalue_reference_v<Body>
void run_dispatch(Ctx const& /*ctx*/, Body&& /*body*/) =
    delete("run_dispatch takes its body by move: write the lambda in the call, or pass std::move(body)");

}  // namespace fixy::dispatch
