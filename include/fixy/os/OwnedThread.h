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
//     The owner keeps its fields in a state of the owner core of
//     foundation/core/Owner.h, so a move reads the source and then empties
//     it.  The end of the owner and a join empty the owner before the join,
//     as the reset of the core does, so the end of the frame sees an empty
//     owner.  The state is in the home of the owner states of fixy, so the
//     roster of the owner probe holds it.
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
//
// THE THREAD OF AN OWNER
//     mint_owner_thread<&Owner::thread_, &Owner::request_stop_>(ctx,
//     options, owner, body) starts one thread that runs body(owner), and
//     keeps the owner of that thread in owner.thread_, an OwnerThread<Owner>.
//     The owner names the two members, so the door needs no access of its
//     own to them.
//     The body borrows the owner, and the door proves in the type that the
//     owner outlives the thread:
//       - thread_ is the last non-static data member of Owner.  The members
//         end in the reverse order of their declarations, so thread_ ends
//         first, and each other member is alive while the thread ends.
//       - The destructor of Owner is defaulted, so no destructor body runs
//         before the members end.  A destructor that is defaulted outside
//         the class counts when its definition comes before the call of the
//         mint.
//       - Owner has no copy and no move, so the pointer to the owner in the
//         OwnerThread stays valid while the owner lives.
//     The end of an OwnerThread that holds a thread calls the stop hook of
//     the owner through that pointer, and then joins.  The hook tells the body
//     to end and wakes it, and it does not join.  stop_and_join() does the
//     same at an earlier line, and leaves the OwnerThread empty, so its end
//     then does nothing.  The body takes the owner as its one parameter and
//     gives nothing.  The owner is its only borrow: the door checks its own
//     captures under the policy of a thread that escapes (foundation/
//     Closure.h).  The body writes the owner while the thread that starts it
//     runs on, so the door asks that two threads can write the owner at the
//     same time (Synchronized of foundation/Transfer.h).  Such an owner keeps
//     thread_ and each plain part private and states synchronized{}, and a
//     static assertion of the door names the first part that refuses.  A
//     mint while the OwnerThread holds a thread ends the
//     process.  quiescence(owner) gives the proof that no thread of the owner
//     runs, while the OwnerThread holds no thread (THE QUIESCENCE OF AN
//     OWNER, at QuiescenceProof).

#include <fixy/os/ThreadTasks.h>
#include <foundation/ByteSeal.h>
#include <foundation/Closure.h>
#include <foundation/Transfer.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Report.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Errno.h>
#include <foundation/reflect/TypeComponents.h>

#include <cstdint>
#include <meta>
#include <new>

namespace fixy::thread {

class OwnedThread;

namespace detail {

struct owned_thread_door;
struct owner_thread_door;

}  // namespace detail

template <typename Owner>
class OwnerThread;

// The proof that no thread of the owner runs (THE QUIESCENCE OF AN OWNER).
// It proves that its holder is the one thread that reaches the parts of the
// owner.  Only the OwnerThread of the owner gives one: its door
// quiescence(owner) gives it while the OwnerThread holds no thread, such as
// after stop_and_join.  The proof names its owner object, and a reset ends
// the process when the object that it resets does not lie inside that owner,
// as the reset of fixy/handle/OneShotFlag.h does.  So the proof of one
// pipeline resets no part of another.  The proof does not copy or move, but a
// named proof stays alive after a later mint of the thread.  So the proof
// keeps its OwnerThread, and a use of the proof while that OwnerThread holds a
// thread ends the process (is_quiescent).
template <class Owner>
class [[nodiscard]] QuiescenceProof final {
    friend struct detail::owner_thread_door;

    QuiescenceProof(Owner const& owner, OwnerThread<Owner> const& slot) noexcept
        : owner_{__builtin_addressof(owner)}, slot_{__builtin_addressof(slot)} {}

    Owner const* owner_ = nullptr;
    OwnerThread<Owner> const* slot_ = nullptr;
    // The seal refuses each byte route, such as std::bit_cast, so no bytes
    // become a proof.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    QuiescenceProof(QuiescenceProof const&) = delete("a proof of quiescence names one owner, and a copy names it too");
    QuiescenceProof&
    operator=(QuiescenceProof const&) = delete("a proof of quiescence names one owner, and a copy names it too");
    ~QuiescenceProof() = default;

    // True while the OwnerThread of the proof holds no thread.  A reset reads
    // it, so a proof that outlives a later mint of the thread refuses.  The
    // definition follows OwnerThread.
    [[nodiscard]] bool is_quiescent() const noexcept;

    // True when each byte of the part lies inside the owner of the proof.
    // The compare reads the two addresses as words, as offset_of of
    // foundation/core/Region.h does, so a part of a different object gives
    // false with no compare of pointers of two objects.  It reads no byte of
    // the owner.
    template <class Part>
    [[nodiscard]] bool holds_part(Part const& part) const noexcept {
        if constexpr (sizeof(Part) > sizeof(Owner)) {
            return false;
        } else {
            auto const first = __builtin_bit_cast(__UINTPTR_TYPE__, static_cast<void const*>(owner_));
            auto const address =
                __builtin_bit_cast(__UINTPTR_TYPE__, static_cast<void const*>(__builtin_addressof(part)));
            return address >= first && address - first <= sizeof(Owner) - sizeof(Part);
        }
    }
};

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

}  // namespace fixy::thread

namespace fixy::detail::owner {

// The fields of an owned thread: the handle of the thread, the frame that the
// thread reads, and the function that ends the frame.  An emptied state holds
// no thread, and its release does nothing.
struct OwnedThreadState {
    using owner_state_mark = ::foundation::core::OwnerStateMark;

    std::uint64_t handle = 0;
    void* frame = nullptr;
    void (*end_frame)(void*) noexcept = nullptr;

    [[nodiscard]] static constexpr OwnedThreadState emptied(OwnedThreadState const& /*state*/) noexcept {
        return OwnedThreadState{};
    }

    // Waits for the end of the body, and then ends the frame and the body on
    // the thread that joins.
    static void release(OwnedThreadState const& state) noexcept {
        if (state.frame == nullptr) return;
        ::fixy::thread::detail::join_thread_task(state.handle);
        state.end_frame(state.frame);
    }
};

}  // namespace fixy::detail::owner

namespace fixy::thread {

// The owner of one thread that the base started (THE OWNER).  An owner can
// move to another thread, which then joins the thread and ends the frame and
// its body (foundation/Transfer.h).  The body runs on the owned thread, so it
// is on a thread other than its mint in any case.  The frame fields are
// pointers that only the holder of the owner reads.
class [[nodiscard]][[= ::foundation::transfer::sendable{}]] OwnedThread final
    : public ::foundation::core::OwnerCore<::fixy::detail::owner::OwnedThreadState> {
    using State = ::fixy::detail::owner::OwnedThreadState;
    using Core = ::foundation::core::OwnerCore<State>;

public:
    OwnedThread(OwnedThread const&) = delete("one thread has one owner of its join.  A copy would join it two times");
    OwnedThread&
    operator=(OwnedThread const&) = delete("one thread has one owner of its join.  A copy would join it two times");

    // The move of the core takes the source first, so the source holds no
    // thread when the target holds it.
    OwnedThread(OwnedThread&&) noexcept = default;

    OwnedThread& operator=(OwnedThread&&) = delete(
        "an assignment over a live owner would join its thread at a line that shows no join.  Join the old "
        "owner with join() first");

    // The reset empties the owner before the join, so the release of the
    // core at its end finds no thread.
    ~OwnedThread() { this->reset_(); }

    // Waits for the end of the body, and ends the frame and the owner.
    void join() && noexcept {
        if (this->state_.frame == nullptr) [[unlikely]] {
            ::foundation::core::fatal("a join of an OwnedThread that holds no thread: a move or a join emptied it");
        }
        this->reset_();
    }

    // True when the owner holds a thread that it has not joined.
    [[nodiscard]] bool holds_thread() const noexcept { return this->state_.frame != nullptr; }

private:
    // The door of the mint.  A Result of an OwnedThread needs the complete
    // class, so no declaration in this class can name the mint.
    friend struct detail::owned_thread_door;
    friend struct detail::owner_thread_door;

    OwnedThread(std::uint64_t handle, void* frame, void (*end_frame)(void*) noexcept) noexcept
        : Core{State{handle, frame, end_frame}} {}

    // Takes the thread of the source into this empty owner, and empties the
    // source.  The OwnerThread of an owner keeps its thread with it.
    void adopt_(OwnedThread&& other) noexcept {
        if (holds_thread()) [[unlikely]] {
            ::foundation::core::fatal("an OwnedThread took a second thread while it held one");
        }
        Core::operator=(static_cast<Core&&>(other));
    }
};

namespace detail {

// The door of the two mints to the private constructor of an OwnedThread.
// Only the mints call it, with a body that their gates admitted.
struct owned_thread_door {
    // An owner that holds no thread, as the source of a move is.  An
    // OwnerThread starts empty with it.
    [[nodiscard]] static OwnedThread empty() noexcept { return OwnedThread{0, nullptr, nullptr}; }

    // Moves the body into a frame and starts the thread of the frame (THE
    // FRAME).  Complexity: one allocation and one start.
    template <typename Body>
    [[nodiscard]] static ::foundation::core::Result<OwnedThread, ::foundation::os::Errno>
    start(ThreadOptions const& options, Body&& body) noexcept {
        using Frame = owned_frame_<Body>;
        void* const storage = ::foundation::core::detail::heap_allocate_(sizeof(Frame), alignof(Frame));
        Frame* const frame = ::new(storage) Frame{static_cast<Body&&>(body), options.name};
        // The owner holds the address of the frame, so the match reads a
        // named Result, and each arm builds the result of the mint.
        ::foundation::core::Result<std::uint64_t, ::foundation::os::Errno> const started =
            Frame::start_thread(*frame, options);
        return started.match(
            [frame](std::uint64_t const& handle) noexcept
                -> ::foundation::core::Result<OwnedThread, ::foundation::os::Errno> {
                return OwnedThread{handle, frame, &Frame::end};
            },
            [frame](::foundation::os::Errno const& error) noexcept
                -> ::foundation::core::Result<OwnedThread, ::foundation::os::Errno> {
                Frame::end(frame);
                return ::foundation::core::err(error);
            });
    }
};

}  // namespace detail

// The policy of a body of an owned thread, and of the own captures of a body
// of mint_owner_thread (foundation/Closure.h): the body runs past the end of
// the frame of the call, so it holds no borrow.
inline constexpr ::foundation::closure::DoorPolicy escaping_body_policy{::foundation::closure::Crossing::Escapes,
                                                                        ::foundation::closure::QttGrade::One, false};

// Starts one thread with the options, which runs the body, and gives its
// owner (THE GATE, THE ERRORS).  Complexity: one allocation and one start.
template <typename Ctx, typename Body>
    requires CanMintOwnedThread<Ctx, Body>
[[nodiscard]] ::foundation::core::Result<OwnedThread, ::foundation::os::Errno>
mint_owned_thread(Ctx const& /*ctx*/, ThreadOptions const& options, Body body) noexcept {
    constexpr ::foundation::closure::ClosureVerdict verdict =
        ::foundation::closure::check_closure(^^Body, escaping_body_policy, "mint_owned_thread");
    static_assert(verdict.is_admitted(), verdict);
    return detail::owned_thread_door::start(options, static_cast<Body&&>(body));
}

namespace detail {

// The last data member of the class, or a null reflection for a class with
// no data member.  Complexity: linear in the bases and the members.
[[nodiscard]] consteval std::meta::info last_data_member_(std::meta::info owner) {
    std::meta::info last{};
    for (::foundation::reflect::TypeNode const& part :
         ::foundation::reflect::member_components_of(::foundation::reflect::TypeNode{owner, true})) {
        if (part.member != std::meta::info{}) last = part.member;
    }
    return last;
}

// The count of the data members of the type in the class and in its bases.
// Complexity: linear in the bases and the members of the walk.
[[nodiscard]] consteval std::size_t count_members_of_type_(std::meta::info owner, std::meta::info member_type) {
    std::size_t count = 0;
    for (::foundation::reflect::TypeNode const& part :
         ::foundation::reflect::member_components_of(::foundation::reflect::TypeNode{owner, true})) {
        if (part.member == std::meta::info{}) {
            count += count_members_of_type_(part.type, member_type);
        } else if (std::meta::dealias(std::meta::type_of(part.member)) == member_type) {
            ++count;
        }
    }
    return count;
}

// True when Member points at the last data member of Owner, an
// OwnerThread<Owner> (THE THREAD OF AN OWNER).
//
// A pointer to a data member of the type OwnerThread<Owner> names a member
// of Owner or of one of its bases.  So when the class and its bases hold one
// such member, and it is the last data member of Owner, Member points at it,
// and the check needs no splice of the member.
template <typename Owner, auto Member>
[[nodiscard]] consteval bool is_last_owner_thread_member_() {
    std::meta::info const thread_type = ^^OwnerThread<Owner>;
    std::meta::info const last = last_data_member_(^^Owner);
    return __is_same(decltype(Member), OwnerThread<Owner> Owner::*) && last != std::meta::info{}
        && std::meta::dealias(std::meta::type_of(last)) == thread_type
        && count_members_of_type_(^^Owner, thread_type) == 1;
}

// True when no destructor body of Owner runs before its members end: the
// destructor is implicit or defaulted (THE THREAD OF AN OWNER).
template <typename Owner>
[[nodiscard]] consteval bool ends_with_its_members_() {
    constexpr std::meta::info destructor = ^^Owner::~Owner;
    return !std::meta::is_user_provided(destructor) || std::meta::is_defaulted(destructor);
}

// The first data member of the owner, in the order of the declarations, that
// two threads cannot write at the same time: a reference, a const member that
// is not Shareable, or a member that is not Synchronized (THE SHARED WRITE of
// foundation/Transfer.h).  The owner itself when no member refuses, as when a
// base refuses.  Complexity: linear in the members, and the walk of each one.
[[nodiscard]] consteval std::meta::info first_unsynchronized_member_(std::meta::info owner) {
    for (::foundation::reflect::TypeNode const& part :
         ::foundation::reflect::member_components_of(::foundation::reflect::TypeNode{owner, true})) {
        // A node of a base names no member.
        if (part.member == std::meta::info{}) continue;
        if (!part.is_held_by_value) return part.member;
        const bool holds = part.was_const
                             ? ::foundation::transfer::holds(part.type, ::foundation::transfer::Property::Share)
                             : ::foundation::transfer::is_synchronized(part.type);
        if (!holds) return part.member;
    }
    return owner;
}

}  // namespace detail

// An owner that stays in place: it has no copy and no move, so a pointer to
// it stays valid while it lives (THE THREAD OF AN OWNER).
template <typename Owner>
concept StaysInPlace = !__is_constructible(Owner, Owner const&) && !__is_constructible(Owner, Owner&&)
                    && !__is_assignable(Owner&, Owner const&) && !__is_assignable(Owner&, Owner&&);

// A body of the thread of an owner: it takes the owner, gives nothing and
// does not throw.
template <typename Body, typename Owner>
concept OwnerThreadBody = __is_object(Body) && !__is_const(Body) && __is_nothrow_constructible(Body, Body&&)
                       && __is_nothrow_destructible(Body) && __is_nothrow_invocable(Body&, Owner&)
                       && requires(Body& body, Owner& owner) { requires __is_same(decltype(body(owner)), void); };

// The fit of mint_owner_thread (THE THREAD OF AN OWNER): a context that
// starts threads and allocates, the last member of the owner for its
// thread, a stop hook of the owner that takes no argument, gives nothing and
// does not throw, an owner whose destructor is defaulted and that stays in
// place, and a body that takes the owner.
template <auto Member, auto Hook, typename Ctx, typename Owner, typename Body>
concept CanMintOwnerThread =
    CtxStartsThreads<Ctx> && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Alloc>
    && detail::is_last_owner_thread_member_<Owner, Member>() && __is_same(decltype(Hook), void (Owner::*)() noexcept)
    && detail::ends_with_its_members_<Owner>() && StaysInPlace<Owner> && OwnerThreadBody<Body, Owner>;

// The owner of the thread of an owner (THE THREAD OF AN OWNER).  It is the
// last data member of Owner, and only mint_owner_thread gives it a thread.
// Its end calls the stop hook of the owner and joins, so the thread ends
// while each other member of the owner is alive.
//
// It reaches the owner through a plain pointer that only the mint sets.  The
// pointer is valid only because the mint asks StaysInPlace<Owner>: an owner
// with no copy and no move keeps its address for its whole life, and the
// OwnerThread ends with it.  A gate that drops StaysInPlace would let the
// pointer name the source of a move.
//
// The pointer names the object that holds the OwnerThread, so the
// OwnerThread moves and is read exactly as its owner is, and it states
// by_arguments{} (foundation/Transfer.h).
template <typename Owner>
class[[= ::foundation::transfer::by_arguments{}]] OwnerThread final {
public:
    OwnerThread() noexcept = default;
    OwnerThread(OwnerThread const&) = delete("the thread borrows its owner at one address");
    OwnerThread& operator=(OwnerThread const&) = delete("the thread borrows its owner at one address");
    OwnerThread(OwnerThread&&) = delete("the thread borrows its owner at one address");
    OwnerThread& operator=(OwnerThread&&) = delete("the thread borrows its owner at one address");

    ~OwnerThread() { stop_and_join(); }

    // Calls the stop hook of the owner and joins the thread, when the
    // OwnerThread holds one.  It is empty after the call.
    void stop_and_join() noexcept {
        if (!thread_.holds_thread()) return;
        (owner_->*stop_)();
        OwnedThread taken{std::move(thread_)};
        std::move(taken).join();
    }

    // True when the OwnerThread holds a thread that it has not joined.
    [[nodiscard]] bool holds_thread() const noexcept { return thread_.holds_thread(); }

    // The proof that no thread of the owner runs (THE QUIESCENCE OF AN
    // OWNER), for a reset of a part of the owner that the
    // thread reads.  Take it before the first mint or after stop_and_join.  A
    // call while the OwnerThread holds a thread ends the process, and so does
    // an owner that does not hold this OwnerThread.  The definition follows
    // the door.
    [[nodiscard]] QuiescenceProof<Owner> quiescence(Owner const& owner) const noexcept;

private:
    friend struct detail::owner_thread_door;

    // The owner and its stop hook.  The mint sets the two before it keeps
    // a thread, so each is set while thread_ holds one.  The owner stays in
    // place, so the pointer stays valid while the owner lives.
    Owner* owner_ = nullptr;
    void (Owner::*stop_)() noexcept = nullptr;
    // The last member, so it joins first.  An empty owner holds no thread.
    OwnedThread thread_{detail::owned_thread_door::empty()};
};

namespace detail {

// The door of mint_owner_thread to the private members of an OwnerThread, and
// the one door to a proof of quiescence (THE QUIESCENCE OF AN OWNER).
struct owner_thread_door {
    template <typename Owner>
    static void keep(OwnerThread<Owner>& slot, Owner& owner, void (Owner::*stop)() noexcept,
                     OwnedThread thread) noexcept {
        slot.owner_ = __builtin_addressof(owner);
        slot.stop_ = stop;
        slot.thread_.adopt_(std::move(thread));
    }

    // The proof of the owner of the slot.  The OwnerThread calls it while it
    // holds no thread, and the slot lies inside the owner.
    template <typename Owner>
    [[nodiscard]] static QuiescenceProof<Owner> prove_quiescence(Owner const& owner,
                                                                 OwnerThread<Owner> const& slot) noexcept {
        // The proof does not move, so the check reads a proof of its own,
        // and the return builds the proof in the place of the caller.
        if (!QuiescenceProof<Owner>{owner, slot}.holds_part(slot)) [[unlikely]] {
            ::foundation::core::fatal("OwnerThread: a proof of quiescence of an owner that does not hold the "
                                      "OwnerThread.  Give the owner whose member the OwnerThread is");
        }
        return QuiescenceProof<Owner>{owner, slot};
    }
};

}  // namespace detail

template <class Owner>
bool QuiescenceProof<Owner>::is_quiescent() const noexcept {
    return !slot_->holds_thread();
}

template <typename Owner>
QuiescenceProof<Owner> OwnerThread<Owner>::quiescence(Owner const& owner) const noexcept {
    if (thread_.holds_thread()) [[unlikely]] {
        ::foundation::core::fatal("OwnerThread: a proof of quiescence while the thread of the owner runs.  Call "
                                  "stop_and_join first");
    }
    return detail::owner_thread_door::prove_quiescence(owner, *this);
}

// Starts one thread with the options, which runs body(owner), and keeps its
// owner in the member of the owner (THE THREAD OF AN OWNER).  A start that
// fails is the error, and then the member stays empty.  Complexity: one
// allocation and one start.
template <auto Member, auto Hook, typename Ctx, typename Owner, typename Body>
    requires CanMintOwnerThread<Member, Hook, Ctx, Owner, Body>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
mint_owner_thread(Ctx const& /*ctx*/, ThreadOptions const& options, Owner& owner, Body body) noexcept {
    constexpr ::foundation::closure::ClosureVerdict verdict =
        ::foundation::closure::check_closure(^^Body, escaping_body_policy, "mint_owner_thread");
    static_assert(verdict.is_admitted(), verdict);
    // The body writes the owner while the thread that starts it runs on (THE
    // THREAD OF AN OWNER).  The three messages give the rule, the owner and
    // its first member that two threads cannot write.
    if constexpr (!::foundation::transfer::Synchronized<Owner>) {
        static_assert(::foundation::transfer::Synchronized<Owner>,
                      "mint_owner_thread: the thread writes its owner while the thread that starts it runs on, so two "
                      "threads write the owner at the same time, and the owner is not Synchronized "
                      "(foundation/Transfer.h).  Keep thread_ and each plain part of the owner private, make each "
                      "part that the two threads write an Atomic or a channel, and state synchronized{} with its "
                      "reason.  The next two messages name the owner and its first member that two threads cannot "
                      "write");
        static_assert(::foundation::transfer::Synchronized<Owner>, std::meta::display_string_of(^^Owner));
        static_assert(::foundation::transfer::Synchronized<Owner>,
                      std::meta::display_string_of(detail::first_unsynchronized_member_(^^Owner)));
    }
    OwnerThread<Owner>& slot = owner.*Member;
    CRUCIBLE_PRE(!slot.holds_thread());
    Owner* const held = __builtin_addressof(owner);
    auto run = [held, moved = static_cast<Body&&>(body)]() mutable noexcept { moved(*held); };
    ::foundation::core::Result<OwnedThread, ::foundation::os::Errno> started =
        detail::owned_thread_door::start(options, std::move(run));
    if (started.is_err()) {
        return ::foundation::core::err(std::move(started).err().expect("a refused start holds its error"));
    }
    detail::owner_thread_door::keep(slot, owner, Hook,
                                    std::move(started).expect("a start that is no error holds its owner"));
    return ::foundation::core::Unit{};
}
}  // namespace fixy::thread
