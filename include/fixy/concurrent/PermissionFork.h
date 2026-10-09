#pragma once

// The arm of the parallel composition rule that runs each body on a thread
// of its own.
//
// foundation/permissions/PermissionFork.h holds the view, the write borrow
// door with_write_view, the checks of a fork and the arm that runs the
// bodies one after another on the calling thread.  This header holds the
// arm that starts one thread for each child through the thread door of
// fixy/os/ThreadTasks.h, and joins each thread before it rebuilds the
// parent.  The context must own the background effect, the authority to
// start threads.
//
// One thread for each child, started and joined inside the call.  That
// suits a few children with long bodies, and bodies that wait on each
// other, such as the two sides of a channel.  It is the wrong shape for
// many short tasks.
//
// The children stay in the frame of the fork.  The fork builds a copy of
// each body on the calling thread, in child order.  The thread of a child
// moves its body out of the fork, borrows its child through
// with_write_view, and puts the child back, so the body lives on its own
// thread and ends there.  A body never holds a token
// (foundation/permissions/PermissionFork.h).  When a thread of the fork
// cannot start, the process ends.  The bodies can wait on each other, so
// the fork cannot run them one after another in place of the threads.

#include <fixy/os/ThreadTasks.h>
#include <foundation/NoObject.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace fixy::concurrent {

// The fit of the arm that starts threads: the fit of the inline arm, and a
// context that owns the background effect.
template <typename Ctx, typename Parent, typename... Children>
concept CtxFitsPermissionFork = ::foundation::permissions::CtxFitsPermissionForkInline<Ctx, Parent, Children...>
                             && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Bg>;

// The clause names the gate of the context, the count of the bodies and
// the body of each child.  The last two are the test of
// each_body_takes_its_child of foundation, written as a fold of the
// concept that that test substitutes, so the clause names no tuple.  A
// concept cannot be specialized, so no translation unit can admit a body
// that cannot take its child.  The fold reads the bodies only when the
// counts agree.
template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
    requires CtxFitsPermissionFork<Ctx, Parent, Children...> && (sizeof...(Children) == sizeof...(Callables))
          && (::foundation::permissions::detail::PermissionForkBody<Callables, Children, Brand, Ctx> && ...)
[[nodiscard]] ::foundation::permissions::Permission<Parent, Brand>
mint_permission_fork(Ctx const& ctx, ::foundation::permissions::Permission<Parent, Brand>&& parent,
                     Callables&&... callables) noexcept;

// The threads of a fork.  Starting threads is the work of the mint, so only
// the mint may reach it.  Each member is private and static, the mint is
// the only friend, and the class is final and cannot be built, so no other
// scope can call a member, derive to reach one, or take one's address.
//
// The friend declaration repeats the constraint of the mint exactly,
// because the language requires it.
class PermissionForkThreads final : ::foundation::NoObject<PermissionForkThreads> {
    template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
        requires CtxFitsPermissionFork<Ctx, Parent, Children...> && (sizeof...(Children) == sizeof...(Callables))
              && (::foundation::permissions::detail::PermissionForkBody<Callables, Children, Brand, Ctx> && ...)
    friend ::foundation::permissions::Permission<Parent, Brand>
    mint_permission_fork(Ctx const& ctx, ::foundation::permissions::Permission<Parent, Brand>&& parent,
                         Callables&&... callables) noexcept;

    // What the thread of one child reads: its token in the frame of the
    // fork, the context of the fork and the copy of its body.  The fork waits
    // in the join, so it does not touch them while the thread reads them.
    template <typename Child, typename Brand, typename Ctx, typename Body>
    struct child_frame_ {
        ::foundation::permissions::Permission<Child, Brand>* child = nullptr;
        Ctx const* ctx = nullptr;
        Body* body = nullptr;

        // Runs on the thread of the child.  The thread moves the body out
        // of the fork and copies the context, so each lives on the thread
        // and ends there.
        static void run(void* frame) noexcept {
            child_frame_ const& source = *static_cast<child_frame_ const*>(frame);
            Body moved_body = std::move(*source.body);
            Ctx const child_ctx = *source.ctx;
            *source.child = ::foundation::permissions::with_write_view(child_ctx, std::move(*source.child), moved_body);
        }
    };

    // Starts one thread for each frame and joins each thread.  The frames
    // are parameters of this function, so they live until each thread has
    // joined.
    template <typename... Frames>
    static void start_and_join_(Frames... frames) noexcept {
        ::fixy::thread::detail::thread_task tasks[sizeof...(Frames)]{
            ::fixy::thread::detail::thread_task{&frames, &Frames::run}...};
        ::fixy::thread::detail::run_thread_tasks(::foundation::core::detail::view_over_(tasks, sizeof...(Frames)),
                                                 ::fixy::thread::ThreadOptions{})
            .expect("mint_permission_fork: no thread of the fork started.  The bodies can wait on each other, so the "
                    "fork cannot run them on the calling thread");
    }

    template <typename... Types>
    struct pack_ {};

    template <typename Brand, typename Ctx, typename ChildPack, typename BodyPack>
    struct fork_bodies_;

    // The copy of each body is a parameter of this constructor, so it lives
    // until each thread has joined.  The caller gives the bodies in a braced
    // list, which builds them left to right, so the fork copies the bodies on
    // the calling thread in child order, as the inline arm does.
    template <typename Brand, typename Ctx, typename... Children, typename... Bodies>
    struct fork_bodies_<Brand, Ctx, pack_<Children...>, pack_<Bodies...>> {
        fork_bodies_(Ctx const& ctx, ::foundation::permissions::Permission<Children, Brand>&... children,
                     Bodies... bodies) noexcept {
            start_and_join_(child_frame_<Children, Brand, Ctx, Bodies>{&children, &ctx, &bodies}...);
        }
    };

    // The children carry the parent's brand, the views carry it into the
    // bodies, and the combine carries it back out, so the region that
    // returns is the one that went in.
    template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
    static ::foundation::permissions::Permission<Parent, Brand>
    run_(Ctx const& ctx, ::foundation::permissions::Permission<Parent, Brand>&& parent,
         Callables&&... callables) noexcept {
        ::foundation::permissions::detail::permission_fork_check_<true, Children...>();
        auto&& [... child] = ::foundation::permissions::mint_permission_split_n<Children...>(ctx, std::move(parent));
        static_cast<void>(fork_bodies_<Brand, Ctx, pack_<Children...>, pack_<std::decay_t<Callables>...>>{
            ctx, child..., std::forward<Callables>(callables)...});
        // The children never left this frame.  The combine consumes each
        // of them, and it checks the manifest and the brand again.
        return ::foundation::permissions::mint_permission_combine_n<Parent>(ctx, std::move(child)...);
    }
};

template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
    requires CtxFitsPermissionFork<Ctx, Parent, Children...> && (sizeof...(Children) == sizeof...(Callables))
          && (::foundation::permissions::detail::PermissionForkBody<Callables, Children, Brand, Ctx> && ...)
[[nodiscard]] ::foundation::permissions::Permission<Parent, Brand>
mint_permission_fork(Ctx const& ctx, ::foundation::permissions::Permission<Parent, Brand>&& parent,
                     Callables&&... callables) noexcept {
    return PermissionForkThreads::run_<Children...>(ctx, std::move(parent), std::forward<Callables>(callables)...);
}

}  // namespace fixy::concurrent
