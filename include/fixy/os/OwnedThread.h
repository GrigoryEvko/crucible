#pragma once

// An owned thread: one thread that starts now and joins later.
//
//   mint_owned_thread(ctx, options, body)
//                       starts one thread with the options, which runs the
//                       body, and gives the OwnedThread of that thread, or
//                       the error number of the start
//   std::move(thread).join()
//                       waits for the end of the body, and ends the owner
//
// THE FRAME
//     The mint moves the body into a frame on the heap, beside the task of the
//     thread door and a copy of the name of the thread.  The thread reads the
//     frame, and the owner can move, so the frame has an address of its own.
//     The frame ends after the join, on the thread that joins, so the body
//     ends there too.
//
// THE OWNER
//     An OwnedThread is the one owner of the join of its thread.  It has no
//     copy and no detach.  join() consumes the owner.  The destructor of an
//     owner that holds a thread joins the thread, so no thread outlives its
//     owner.  A move takes the source first: after the move the target holds
//     the thread, and the source holds nothing.  The move assignment is
//     deleted, because an assignment over a live owner would join its thread
//     at a line that shows no join.  An Option of an OwnedThread builds a new
//     payload when it takes a value, so it needs no move assignment.  A class
//     that holds an OwnedThread puts it last, so it joins first.
//
//     The owner keeps its fields in the order of foundation/core/Owner.h: a
//     move reads the source and then empties it, and an end empties the owner
//     before it joins.  The owner does not derive from OwnerCore, so the
//     roster of the owner probe, which reads only the states of the core,
//     does not hold it.
//
// THE GATE
//     The mint takes a context that owns Bg or Init, the authority to start
//     threads (CtxStartsThreads of fixy/os/ThreadTasks.h), and that owns
//     Alloc, because the frame is heap storage.  The body is called with no
//     argument, as the bodies of run_joined and the stages of the pipeline
//     are, and it does not throw.  A background body takes its context from
//     the door of the background context on its own thread.
//
// THE ERRORS
//     A start that the C library refuses is the error of the Result, and no
//     thread runs then.  A join that fails ends the process.  A join of an
//     owner that holds no thread, such as the source of a move, ends the
//     process.

#include <fixy/os/ThreadTasks.h>
#include <foundation/Transfer.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Report.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Errno.h>

#include <cstdint>
#include <new>

namespace fixy::thread {

class OwnedThread;

namespace detail {

struct owned_thread_door;

}  // namespace detail

// The fit of the mint: a context that starts threads and allocates, and a
// body that moves and runs with no throw and takes no argument.  The tests
// of the body are the builtins of the compiler.
template <typename Ctx, typename Body>
concept CanMintOwnedThread =
    CtxStartsThreads<Ctx> && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Alloc>
    && __is_object(Body) && !__is_const(Body) && !__is_volatile(Body) && __is_nothrow_constructible(Body, Body&&)
    && __is_nothrow_destructible(Body) && __is_nothrow_invocable(Body&);

namespace detail {

// The frame of one owned thread (THE FRAME).  The task names the frame, its
// function and the copy of the name, so the thread reads no object of the
// caller of the mint.
template <typename Body>
class owned_frame_ {
public:
    owned_frame_(Body&& moved, ThreadName const& thread_name) noexcept
        : task_{static_cast<void*>(this), &owned_frame_::run, &name_, 0},
          name_{thread_name},
          body_{static_cast<Body&&>(moved)} {}

    owned_frame_(owned_frame_ const&) = delete("the thread reads the frame at its one address");
    owned_frame_& operator=(owned_frame_ const&) = delete("the thread reads the frame at its one address");

    // Starts the thread of the task of the frame, which names the frame,
    // and gives the handle of the thread or the error of the door.
    [[nodiscard]] static ::foundation::core::Result<std::uint64_t, ::foundation::os::Errno>
    start_thread(owned_frame_& frame, ThreadOptions const& options) noexcept {
        return start_thread_task(frame.task_, options);
    }

    // Runs on the owned thread.
    static void run(void* frame) noexcept { static_cast<owned_frame_*>(frame)->body_(); }

    // Ends the body and gives the storage back.  Runs after the join.
    static void end(void* frame) noexcept {
        static_cast<owned_frame_*>(frame)->~owned_frame_();
        ::foundation::core::detail::heap_release_(frame, sizeof(owned_frame_), alignof(owned_frame_));
    }

private:
    thread_task task_{};
    ThreadName name_{};
    Body body_;
};

}  // namespace detail

// The owner of one thread that the base started (THE OWNER).  An owner can
// move to another thread, which then joins the thread and ends the frame and
// its body (foundation/Transfer.h).  The body runs on the owned thread, so it
// is on a thread other than its mint in any case.  The frame fields are
// pointers that only the holder of the owner reads.
class [[nodiscard]][[= ::foundation::transfer::sendable{}]] OwnedThread final {
public:
    OwnedThread(OwnedThread const&) = delete("one thread has one owner of its join.  A copy would join it two times");
    OwnedThread&
    operator=(OwnedThread const&) = delete("one thread has one owner of its join.  A copy would join it two times");

    // The move takes the source first, so the source holds no thread when
    // the target holds it.
    OwnedThread(OwnedThread&& other) noexcept
        : handle_{other.handle_}, frame_{other.frame_}, end_frame_{other.end_frame_} {
        other.handle_ = 0;
        other.frame_ = nullptr;
        other.end_frame_ = nullptr;
    }

    OwnedThread& operator=(OwnedThread&&) = delete(
        "an assignment over a live owner would join its thread at a line that shows no join.  Join the old "
        "owner with join() first");

    ~OwnedThread() { join_and_end_(); }

    // Waits for the end of the body, and ends the frame and the owner.
    void join() && noexcept {
        if (frame_ == nullptr) [[unlikely]] {
            ::foundation::core::fatal("a join of an OwnedThread that holds no thread: a move or a join emptied it");
        }
        join_and_end_();
    }

    // True when the owner holds a thread that it has not joined.
    [[nodiscard]] bool holds_thread() const noexcept { return frame_ != nullptr; }

private:
    // The door of the mint.  A Result of an OwnedThread needs the complete
    // class, so no declaration in this class can name the mint.
    friend struct detail::owned_thread_door;

    OwnedThread(std::uint64_t handle, void* frame, void (*end_frame)(void*) noexcept) noexcept
        : handle_{handle}, frame_{frame}, end_frame_{end_frame} {}

    // The owner is empty before the join starts, as an owner of
    // foundation/core/Owner.h is before its release.
    void join_and_end_() noexcept {
        if (frame_ == nullptr) return;
        std::uint64_t const handle = handle_;
        void* const frame = frame_;
        void (*const end_frame)(void*) noexcept = end_frame_;
        handle_ = 0;
        frame_ = nullptr;
        end_frame_ = nullptr;
        detail::join_thread_task(handle);
        end_frame(frame);
    }

    std::uint64_t handle_ = 0;
    void* frame_ = nullptr;
    void (*end_frame_)(void*) noexcept = nullptr;
};

namespace detail {

// The door of mint_owned_thread to the private constructor of an
// OwnedThread.  Only the mint calls it, with a thread that the thread door
// started and the frame that the thread reads.
struct owned_thread_door {
    [[nodiscard]] static OwnedThread open(std::uint64_t handle, void* frame,
                                          void (*end_frame)(void*) noexcept) noexcept {
        return OwnedThread{handle, frame, end_frame};
    }
};

}  // namespace detail

// Starts one thread with the options, which runs the body, and gives its
// owner (THE GATE, THE ERRORS).  Complexity: one allocation and one start.
template <typename Ctx, typename Body>
    requires CanMintOwnedThread<Ctx, Body>
[[nodiscard]] ::foundation::core::Result<OwnedThread, ::foundation::os::Errno>
mint_owned_thread(Ctx const& /*ctx*/, ThreadOptions const& options, Body body) noexcept {
    using Frame = detail::owned_frame_<Body>;
    void* const storage = ::foundation::core::detail::heap_allocate_(sizeof(Frame), alignof(Frame));
    Frame* const frame = ::new(storage) Frame{static_cast<Body&&>(body), options.name};
    // The owner holds the address of the frame, so the match reads a named
    // Result, and each arm builds the result of the mint.
    ::foundation::core::Result<std::uint64_t, ::foundation::os::Errno> const started =
        Frame::start_thread(*frame, options);
    return started.match(
        [frame](
            std::uint64_t const& handle) noexcept -> ::foundation::core::Result<OwnedThread, ::foundation::os::Errno> {
            return detail::owned_thread_door::open(handle, frame, &Frame::end);
        },
        [frame](::foundation::os::Errno const& error) noexcept
            -> ::foundation::core::Result<OwnedThread, ::foundation::os::Errno> {
            Frame::end(frame);
            return ::foundation::core::err(error);
        });
}

}  // namespace fixy::thread
