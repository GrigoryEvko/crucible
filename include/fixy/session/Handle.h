#pragma once

// The runtime half of the binary session core: the handle that carries
// a protocol position, the linearity contract that keeps exactly one
// handle alive per position, and the factories that build a handle
// from a protocol and a resource.
//
// Protocol.h is the type level and has no runtime representation.  This
// header is where a protocol becomes an object: `SessionHandle<Proto,
// Resource, LoopCtx, Policy, PS>` holds the Resource and offers exactly
// the operations Proto's head admits.  Every operation is `&&`-qualified,
// consumes the handle, and returns the handle at the next position, so
// a protocol is walked rather than queried.
//
// ── One handle ──────────────────────────────────────────────────────
//
// The old tree had two handle families: a plain handle and a
// permissioned handle.  They repeated the same five heads, and the
// permissioned one declared its permission set as a member that no code
// read.  This header has one family.  The permission set PS is the last
// template parameter, and it is a type only, so it costs no byte.
// `Handle<Proto, PS, Resource, LoopCtx, Policy>` is the same class with
// the parameters in the order the design names them.
//
// PS changes at each step.  A send removes the permissions the message
// takes, and the matching receive adds them.  A Continue must see the PS
// that the loop saw at its entry.  A handle cannot reach End while PS
// holds an open loan.  The ownership delta of one message comes from
// fixy/session/Payload.h, and this header reads it in one place,
// detail::perm_set_after_send_t and detail::perm_set_after_recv_t below.
//
// ── What the Policy parameter is for ────────────────────────────────
//
// Stepping.h explains the decision; this header is where it lands.  The
// abandonment policy is a TEMPLATE PARAMETER of SessionHandle, not a
// preprocessor branch inside it.  Under the two checking policies, a
// handle destroyed before its protocol ends aborts or sends a
// cancellation, in Release as in Debug.  Every operation also checks
// that the handle is live, so a moved-from handle aborts on its first
// use and cannot step a protocol a second time.
//
// ── What a handle does NOT do ───────────────────────────────────────
//
// It does not move bytes.  Every consumer method takes a Transport
// callable and invokes it against the Resource; the handle's job is to
// decide WHETHER the operation is legal at this protocol position and
// WHAT position follows it.  A session over a socket, over an in-memory
// ring and over a mock all use the same handle and differ only in the
// callable they pass.  A callable has one of the shapes that "The shapes
// of a transport" names below, and no shape waits where the watch of
// fixy/session/Watch.h cannot see the wait.
//
// ── The word of a choice ────────────────────────────────────────────
//
// select<I>() gives the Transport the wire word of branch I, and branch()
// dispatches on the word that its Transport reads.  The word comes from
// foundation/algebra/Transition.h.  In a keyed choice, where each label
// branch names a label key, the word is the label word of that key.  In
// a positional choice, the word is the position of the branch.  A keyed
// Select therefore reaches the branch of the peer with the same label,
// also when the peer holds its branches in another order.
//
// ── The scope of the deadlock-freedom guarantee ─────────────────────
//
// LinearActris (Jacobs, Hinrichsen and Krebbers, POPL 2024) proves
// deadlock freedom and leak freedom from two conditions:
//
//   1. Each endpoint is linear.  The abandonment policy and the
//      liveness check give this at run time, and the callback entry
//      with_session gives it at the type level for its body.
//   2. Threads and channels form a forest.  mint_forked_channel gives
//      this: it makes a channel only as part of a fork, and each
//      endpoint goes to its own thread.
//
// The guarantee of the types is for acyclic ownership only.  If a program
// sends an endpoint over a channel to a thread that already holds the
// dual, or connects two fork-shaped channels in a cycle, then the forest
// condition does not hold.  fixy/session/Watch.h then orders the sessions
// at run time by priority (Dardha and Gay, Prioritised GV; van den Heuvel
// and Pérez, LMCS 2024).  A Resource states its priority as
// session_priority, and a wait that could close a cycle of waits is
// refused before the thread waits.
//
// ── The decorator shape ─────────────────────────────────────────────
//
// A transport that adds behaviour to a session, for example crash
// detection or a persisted log, is a decorator over this handle.  The
// decorator holds the inner handle as a member, forwards each operation
// to it, and wraps the returned handle in a new decorator.
// fixy/session/CrashTransport.h has this shape.
//
// The inner handle keeps its own policy.  A decorator that is dropped
// before End drops its inner handle, and the destructor of the inner
// handle aborts or sends the cancellation.  A decorator that holds only
// the inner handle needs no policy of its own.
//
// A decorator can also derive from SessionHandleBase<Inner::protocol,
// Decorator, Policy> to report the Stepping contract itself.  It then
// marks itself consumed at each operation.  Under check::Cancel it must
// also mark itself consumed in its own destructor when it is still live,
// so that the inner handle sends the cancellation.  If it does not, the
// base destructor aborts, because the base cannot reach a Resource.

#include <fixy/SelfContained.h>
#include <fixy/concurrent/PayloadRow.h>
#include <fixy/session/ContentAddressed.h>
#include <fixy/session/NetworkModel.h>
#include <fixy/session/Payload.h>
#include <fixy/session/Stepping.h>
#include <fixy/session/Subtype.h>

#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <meta>
#include <new>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace fixy::session {

namespace detail {

// Names the wrapper class alone, without its template arguments.
// Several distinct wrappers share one SessionHandleBase specialization
// for a given Proto, so a diagnostic that prints only Proto cannot say
// which wrapper produced it.
//
// `Derived` may be void, which is the SessionHandleBase default, so
// callers guard the call.
template <typename Derived>
consteval std::string_view wrapper_class_name() noexcept {
    constexpr auto info = ^^Derived;
    if constexpr (std::meta::has_template_arguments(info)) {
        return std::meta::identifier_of(std::meta::template_of(info));
    } else {
        return std::meta::identifier_of(info);
    }
}

// Names the consumer method that advances the current protocol head,
// for the destructor's abandonment message.  Combinators defined in
// sibling headers are covered by the fall-through arm rather than by
// their own arm, because specializing per combinator from those headers
// would make the include graph circular.
template <typename Proto>
consteval std::string_view next_method_hint() noexcept {
    if constexpr (is_send_v<Proto>) {
        return "send(value, transport_callable)";
    } else if constexpr (is_recv_v<Proto>) {
        return "recv(transport_callable)";
    } else if constexpr (is_select_v<Proto>) {
        return "select<branch_index>(transport_callable), or select<branch_index>(no_label) on a Local carrier";
    } else if constexpr (is_offer_v<Proto>) {
        return "branch(transport_callable, handler)";
    } else if constexpr (is_loop_v<Proto>) {
        // Unreachable: a Loop is unrolled before a handle is built.
        return "the loop body's appropriate consumer method "
               "(framework should have unrolled Loop<...>)";
    } else {
        return "the protocol head's appropriate consumer method "
               "(close / send / recv / select / branch)";
    }
}

// The destructor's diagnostic for a dropped protocol.  It is outlined
// and cold, so the destructor of a handle holds one branch and one call.
[[noreturn, gnu::cold, gnu::noinline]] inline void
report_dropped_protocol(std::string_view wrapper, std::string_view full_type, std::string_view protocol,
                        std::string_view hint, std::source_location loc, AbandonAction action) noexcept {
    const char* loc_file = loc.file_name();
    // file_name() returns "" for a default-constructed location, which
    // is what a handle minted without an explicit location carries.
    const bool have_loc = loc_file != nullptr && loc_file[0] != '\0';

    std::fprintf(stderr,
                 "\n"
                 "═════════════════════════════════════════════════════════════════════\n"
                 "fixy::session: ABANDONMENT DETECTED (non-terminal handle)\n"
                 "═════════════════════════════════════════════════════════════════════\n"
                 "  Wrapper class:    %.*s\n",
                 static_cast<int>(wrapper.size()), wrapper.data());
    if (!full_type.empty()) {
        std::fprintf(stderr, "  Full handle type: %.*s\n", static_cast<int>(full_type.size()), full_type.data());
    }
    std::fprintf(stderr, "  Protocol head:    %.*s\n", static_cast<int>(protocol.size()), protocol.data());
    if (have_loc) {
        std::fprintf(stderr,
                     "  Construction at:  %s:%u:%u\n"
                     "  In function:      %s\n",
                     loc_file, loc.line(), loc.column(),
                     loc.function_name());
    } else {
        std::fprintf(stderr, "  Construction at:  <unknown — handle minted without "
                             "source_location capture>\n");
    }
    if (action == AbandonAction::Cancel) {
        std::fprintf(stderr, "  Policy:           check::Cancel, but no class sent the cancellation.  A decorator\n"
                             "                    must mark itself consumed so that its inner handle cancels.\n");
    }
    std::fprintf(stderr,
                 "  Expected action:  call .%.*s\n"
                 "\n"
                 "The handle was destroyed via its destructor without being\n"
                 "consumed via a &&-qualified consumer method.  Either:\n"
                 "  1. Consume the handle by calling its appropriate consumer\n"
                 "     method (close / send / recv / select / branch), OR\n"
                 "  2. Advance the protocol to a terminal state (End), OR\n"
                 "  3. Explicitly abandon via std::move(handle).detach(reason),\n"
                 "     where `reason` is one of:\n"
                 "       * detach_reason::InfiniteLoopProtocol\n"
                 "           — Loop<X> with no close branch; transport-level close.\n"
                 "       * detach_reason::TransportClosedOutOfBand\n"
                 "           — peer crash detected (CNTP RETRY_EXC, SWIM dead, fd close).\n"
                 "       * detach_reason::TestInstrumentation\n"
                 "           — test code intentionally drops at a known-safe point.\n"
                 "       * detach_reason::AsyncCancellation\n"
                 "           — std::jthread stop_token fired mid-protocol.\n"
                 "       * detach_reason::OwnerLifetimeBoundEarlyExit\n"
                 "           — bridge/wrapper destructor; last-resort abandonment.\n"
                 "\n"
                 "The framework cannot recover the protocol; aborting.\n"
                 "═════════════════════════════════════════════════════════════════════\n",
                 static_cast<int>(hint.size()), hint.data());
    std::abort();
}

// The liveness check's diagnostic.  A consumed handle holds no protocol
// position, so an operation on it would step the protocol a second time
// or read a Resource that moved away.
[[noreturn, gnu::cold, gnu::noinline]] inline void report_use_after_consume(std::string_view wrapper,
                                                                            std::string_view protocol,
                                                                            std::source_location loc) noexcept {
    const char* loc_file = loc.file_name();
    const bool have_loc = loc_file != nullptr && loc_file[0] != '\0';
    std::fprintf(stderr,
                 "\n"
                 "fixy::session: USE OF A CONSUMED HANDLE\n"
                 "  Wrapper class:    %.*s\n"
                 "  Protocol head:    %.*s\n"
                 "  Construction at:  %s:%u\n"
                 "The handle was moved from, stepped, closed or detached.  It holds no protocol\n"
                 "position.  Use the handle that the move or the step returned.\n",
                 static_cast<int>(wrapper.size()), wrapper.data(), static_cast<int>(protocol.size()), protocol.data(),
                 have_loc ? loc_file : "<unknown>", have_loc ? loc.line() : std::uint_least32_t{0});
    std::abort();
}

}  // namespace detail

// Each tag names one sanctioned reason for marking a handle consumed
// without advancing its protocol.  The tag makes each class of
// abandonment separately greppable, which a bare `.detach()` would not.
// Detection is by inheritance, so a caller adds its own tag by deriving
// from tag_base and the concept admits it.

namespace detach_reason {

struct tag_base {};

// The protocol loops forever with no close branch.  Termination is
// implicit: the transport closes the channel underneath the session
// abstraction.
struct InfiniteLoopProtocol : tag_base {};

// The transport observed the peer's session disappear out of band, so
// no further protocol step can complete.
struct TransportClosedOutOfBand : tag_base {};

// A test walked a partial protocol path and abandoned it deliberately.
struct TestInstrumentation : tag_base {};

// The local side cancelled mid-protocol, for example on a stop token.
// The peer may still be alive, which is what separates this from
// TransportClosedOutOfBand.
struct AsyncCancellation : tag_base {};

// The object that bounded the handle's lifetime is being destroyed.
// This is a last resort and signals that the handle's lifetime did not
// match the protocol's termination point.
struct OwnerLifetimeBoundEarlyExit : tag_base {};

}  // namespace detach_reason

template <typename T>
concept DetachReason = std::is_base_of_v<detach_reason::tag_base, T> && !std::is_same_v<T, detach_reason::tag_base>;

template <typename Proto, typename Resource, typename LoopCtx = void,
          AbandonmentPolicy Policy = DefaultAbandonmentPolicy,
          typename PS = ::foundation::permissions::EmptyPermSet>
class SessionHandle;

// The same class, with the parameters in the order the design names
// them: the protocol, the permission set, the resource, the loop
// context and the policy.
template <typename Proto, typename PS, typename Resource, typename LoopCtx = void,
          AbandonmentPolicy Policy = DefaultAbandonmentPolicy>
using Handle = SessionHandle<Proto, Resource, LoopCtx, Policy, PS>;

// A Resource that can tell the peer that this endpoint stops before the
// protocol ends.  The customization point is a noexcept function
// cancel_session(Resource&) that argument-dependent lookup finds.  For a
// pointer Resource, lookup reads the namespace of the pointee.
//
// check::Cancel requires this concept.  The explicit cancel() operation
// requires it too.
template <typename Resource>
concept CancellableResource = requires(std::remove_reference_t<Resource>& resource) {
    { cancel_session(resource) } noexcept;
};

// Carries the lifetime contract for every handle specialization: a
// handle destroyed at a non-terminal protocol state without having been
// consumed is a dropped protocol, and under the Enforced policy that
// aborts.  Moving marks the SOURCE consumed, so a moved-from handle
// does not fire the check.
//
// Inheritance is public so that `.detach()` is callable on a derived
// handle without a per-class using-declaration.
//
// `Derived` names the wrapper class that inherits this base, so the
// abandonment diagnostic can say which wrapper aborted when several
// wrappers share one Proto.  It defaults to void, and a wrapper that
// leaves it void gets a less precise diagnostic.
//
// This base holds no Resource.  A decorator derives from it directly and
// holds an inner handle, and the handle family below adds the Resource
// in detail::handle_core.
template <typename Proto, typename Derived = void, AbandonmentPolicy Policy = DefaultAbandonmentPolicy>
class SessionHandleBase {
    [[no_unique_address]] Policy tracker_;

protected:
    // Call before returning from a consumer method.  After it, the
    // destructor check sees the tracker marked and skips the abort.
    constexpr void mark_consumed_() noexcept { tracker_.mark(); }

    constexpr bool is_consumed_() const noexcept { return tracker_.was_marked(); }

    // The session's record in fixy/session/Watch.h, or none.
    [[nodiscard]] constexpr watch::session_ref session_() const noexcept { return tracker_.session(); }

    // Ends the session in the watch: it detached or cancelled.  Call
    // before mark_consumed_, which lets go of the record.
    constexpr void release_endpoint_() noexcept {
        if constexpr (Policy::checks_abandonment) {
            if !consteval {
                watch::release(tracker_.session().endpoint);
            }
        }
    }

    // The precondition of every operation: the handle is live.  Under
    // check::Off the tracker has no state, and the check folds away.
    constexpr void require_live_() const noexcept {
        if constexpr (Policy::checks_abandonment) {
            if (tracker_.was_marked()) [[unlikely]] {
                detail::report_use_after_consume(wrapper_name(), type_display_name_v<Proto>,
                                                 tracker_.construction_loc());
            }
        }
    }

    // True when the handle is live at a position that still owes a
    // message.  Under check::Off it is always false, because the policy
    // does not know.
    [[nodiscard]] constexpr bool owes_protocol_() const noexcept {
        if constexpr (Policy::checks_abandonment) {
            return !tracker_.was_marked() && !is_terminal_state_v<Proto>;
        } else {
            return false;
        }
    }

    // The action for a dropped protocol.  The destructor calls it, and so
    // does a move assignment, because an assignment over a live handle
    // drops the protocol that the target held.  Under check::Cancel the
    // derived class sends the cancellation first and marks the handle, so
    // this finds nothing to report.
    constexpr void act_on_dropped_protocol_() noexcept {
        if constexpr (Policy::checks_abandonment) {
            if (owes_protocol_()) [[unlikely]] {
                detail::report_dropped_protocol(wrapper_name(), full_handle_type_name(), type_display_name_v<Proto>,
                                                detail::next_method_hint<Proto>(), tracker_.construction_loc(),
                                                Policy::action);
            }
        }
    }

public:
    // The Stepping contract.  SteppingGraded reads these five names and
    // nothing else; they are declared here so every specialization
    // inherits the same answers and cannot drift per head.
    using protocol_type = Proto;
    using abandonment_policy = Policy;

    static constexpr ModalityKind modality = ModalityKind::Stepping;

    // Returns a view into the program's constant data, safe to store
    // for the program's lifetime.
    [[nodiscard]] static constexpr std::string_view protocol_name() noexcept { return type_display_name_v<Proto>; }

    [[nodiscard]] static constexpr bool is_terminal() noexcept { return is_terminal_state_v<Proto>; }

    constexpr SessionHandleBase() noexcept = default;

    // Derived constructors default their own location parameter to
    // std::source_location::current(), so the site captured here is the
    // caller's, not the framework's.
    constexpr explicit SessionHandleBase(std::source_location loc) noexcept : tracker_{loc} {}

    // The handle family passes the record that fixy/session/Watch.h keeps
    // for the session.  A decorator passes none.
    constexpr SessionHandleBase(std::source_location loc, watch::session_ref session) noexcept
        : tracker_{loc, session} {}

    // A handle is linear, and a handle on the heap can outlive every path
    // that ends its protocol.  The class refuses the new-expression, as
    // Permission and ReadView do.  A container that holds a handle
    // constructs it in place and destroys it, so it stays admitted.
    static void* operator new(std::size_t) =
        delete("[Session_Handle_On_Heap] a session handle lives on the stack or inside an owner that destroys it.  "
               "A new-expression gives it storage that nothing has to free, and the protocol then never ends.");
    static void* operator new[](std::size_t) =
        delete("[Session_Handle_On_Heap] an array of session handles on the heap can outlive every protocol in it.");
    static void* operator new(std::size_t, std::align_val_t) =
        delete("[Session_Handle_On_Heap] a session handle lives on the stack or inside an owner that destroys it.");
    static void* operator new[](std::size_t, std::align_val_t) =
        delete("[Session_Handle_On_Heap] an array of session handles on the heap can outlive every protocol in it.");

    // A wrapper that did not pass itself as Derived is reported as
    // "SessionHandle", so the destructor diagnostic stays stable.
    [[nodiscard]] static constexpr std::string_view wrapper_name() noexcept {
        if constexpr (!std::is_void_v<Derived>) {
            return detail::wrapper_class_name<Derived>();
        } else {
            return "SessionHandle";
        }
    }

    [[nodiscard]] static constexpr std::string_view next_method_hint() noexcept {
        return detail::next_method_hint<Proto>();
    }

    [[nodiscard]] static constexpr std::string_view full_handle_type_name() noexcept {
        if constexpr (!std::is_void_v<Derived>) {
            return type_display_name_v<Derived>;
        } else {
            return std::string_view{};
        }
    }

    // True while the handle holds its protocol position.  Under check::Off
    // the policy keeps no state, so the answer is always true there.
    [[nodiscard]] constexpr bool is_live() const noexcept {
        if constexpr (Policy::checks_abandonment) {
            return !tracker_.was_marked();
        } else {
            return true;
        }
    }

    // The record of the session in fixy/session/Watch.h, or none.  A
    // decorator that waits on its own, as the crash transport does, opens
    // its watch::wait_scope on this endpoint, so the watch sees the wait.
    [[nodiscard]] constexpr watch::endpoint_id watch_endpoint() const noexcept { return tracker_.session().endpoint; }

    // Marks the handle consumed without advancing the protocol.  The
    // destructor check still fires for every handle that does not
    // detach, so accidental abandonment stays caught.
    template <typename Reason>
        requires DetachReason<Reason>
    constexpr void detach(Reason /*reason_tag*/) && noexcept {
        require_live_();
        release_endpoint_();
        tracker_.mark();
    }

    // The deleted overload outranks the templated one for a zero-arg
    // call, so the diagnostic is this string rather than a
    // compiler-version-specific "no matching function" message.
    void detach() && = delete("[DetachReason_Required] SessionHandle::detach() requires a typed "
                              "reason tag from detach_reason::*.  Pass one of "
                              "detach_reason::InfiniteLoopProtocol{} (Loop<X> with no close "
                              "branch), TransportClosedOutOfBand{} (peer crash), "
                              "TestInstrumentation{} (test code only), AsyncCancellation{} "
                              "(jthread stop_token), or OwnerLifetimeBoundEarlyExit{} "
                              "(bridge/wrapper destructor).  The tag names the audit class so "
                              "each class of abandonment stays separately greppable.");

    SessionHandleBase(const SessionHandleBase&) =
        delete("SessionHandle is linear — protocol progress is consumed, not copied.");
    SessionHandleBase& operator=(const SessionHandleBase&) =
        delete("SessionHandle is linear — protocol progress is consumed, not copied.");

    // The move marks the source consumed so its destructor check skips
    // the abort, and the moved-into handle inherits the source's state.
    // The source is then dead: every operation on it fails the liveness
    // check.
    //
    // Only move-assignment can self-alias, since the language forbids
    // naming `h` inside its own initializer.  The standard leaves a
    // self-moved object valid but unspecified.  This contract is
    // stronger: self-move must leave the consumed state untouched, or
    // the abandonment check stops firing for a handle that really was
    // leaked.  The guard is duplicated in the policy's move_from so the
    // invariant survives a caller reaching the tracker directly.
    constexpr SessionHandleBase(SessionHandleBase&& other) noexcept { tracker_.move_from(other.tracker_); }

    constexpr SessionHandleBase& operator=(SessionHandleBase&& other) noexcept {
        if (this == &other) [[unlikely]]
            return *this;
        act_on_dropped_protocol_();
        tracker_.move_from(other.tracker_);
        return *this;
    }

    // Under check::Off the whole body is discarded, so the destructor is
    // trivial in effect: no branch, no format strings in .rodata, no
    // reference to std::abort.
    ~SessionHandleBase() { act_on_dropped_protocol_(); }
};

namespace detail {

// ── The ownership delta of one message ───────────────────────────────
//
// These names are the only place where the handle reads what a message
// does to the permission set.  fixy/session/Payload.h computes the delta
// with a walk over every component of the payload: a send needs what the
// payload takes, and the matching receive gains it.  A payload that the
// walk refuses, for example a token behind a pointer, is refused here.
// Each name is a concept, so no user specialization changes a step.
template <typename PS, typename T>
concept handle_admits_send_v = SendablePayload<T, PS>;

template <typename PS, typename T>
concept handle_admits_recv_v = ReceivablePayload<T, PS>;

template <typename PS, typename T>
using perm_set_after_send_t = ::fixy::session::perm_set_after_send_t<PS, T>;

template <typename PS, typename T>
using perm_set_after_recv_t = ::fixy::session::perm_set_after_recv_t<PS, T>;

// True when a handle at End can close with this set.  An open loan, a
// LentOut or a BorrowedIn, is a promise that the protocol did not keep,
// so the set must hold none.  An owned tag travelled in a payload, and
// its token is held by the value that carried it.
template <typename PS>
concept perm_set_admits_close_v = !perm_set_has_open_loan_v<PS>;

// True when the set is empty, so a loop frame is the Loop itself.
template <typename PS>
inline constexpr bool perm_set_is_empty_v =
    ::foundation::permissions::perm_set_equal_v<PS, ::foundation::permissions::EmptyPermSet>;

// ── The loop frame ────────────────────────────────────────────────────
//
// A Continue must see the permission set that the loop saw at its
// entry.  If one iteration changed the set, the next iteration would
// start with a different set, and a permission would be lost or
// duplicated once per iteration.  The frame records the entry set beside
// the loop.  With an empty set the frame is the Loop itself, so a handle
// without permissions has the same loop context as before.  The frame
// type, PermLoopFrame, stands in fixy/session/Protocol.h beside the other
// forms of a loop context.

// The entry set of a loop context: the second argument of a frame, and
// the empty set for any other form.  The form is checked, so no type of an
// unknown form gives an entry set.
[[nodiscard]] consteval std::meta::info entry_perm_set_of(std::meta::info loop_ctx) {
    const std::meta::info inner = inner_loop_ctx_of(loop_ctx);
    if (is_instance_of(inner, ^^PermLoopFrame)) return std::meta::dealias(std::meta::template_arguments_of(inner)[1]);
    return ^^::foundation::permissions::EmptyPermSet;
}

template <typename Frame>
using loop_entry_perm_set_t = [:entry_perm_set_of(^^Frame):];

template <typename LoopType, typename PS>
using loop_frame_t = std::conditional_t<perm_set_is_empty_v<PS>, LoopType, PermLoopFrame<LoopType, PS>>;

// ── The session brand ────────────────────────────────────────────────
//
// An entry point that owns the handle (with_session, mint_forked_channel)
// takes back the handle that its body returns.  If the body could return
// any End handle over the same Resource, it could mint a new session,
// walk that one to End, and return it in place of the handle it was
// given.  The brand closes this.  The owning entry point puts the type
// of the body in the loop context of every handle of its session, and it
// accepts back only a handle with that brand.  No public mint makes a
// branded handle, so the returned handle is one that the body received.
//
// The brand, session_brand, is a form of loop context, so it stands in
// fixy/session/Protocol.h with the other forms.

// The brand of a loop context, or void for a context with no brand.
[[nodiscard]] consteval std::meta::info brand_of(std::meta::info loop_ctx) {
    const std::meta::info type = std::meta::dealias(loop_ctx);
    if (is_instance_of(type, ^^session_brand)) return std::meta::dealias(std::meta::template_arguments_of(type)[0]);
    return ^^void;
}

// True when the loop context carries the brand Brand.  A context with no
// brand carries the brand void.
template <typename LoopCtx, typename Brand>
concept carries_brand = brand_of(^^LoopCtx) == std::meta::dealias(^^Brand);

}  // namespace detail

// ── Passkeys ─────────────────────────────────────────────────────────
//
// A passkey is the proof that its holder is the one class that the key
// befriends.  A private surface of this layer, a constructor or a
// builder, takes the key as its first parameter, and no code without a
// key reaches the surface.  Each key has this shape:
//
//   - The class is final, and its default constructor is private and
//     user-provided.
//   - It has no copy and no move.  A key exists only as a temporary that
//     its friend makes for one call, and no callee can keep it.
//   - Its destructor is user-provided, so the class is neither trivially
//     copyable nor an implicit-lifetime type.  No std::bit_cast and no
//     std::start_lifetime_as makes a key.
//   - It befriends exactly one class, and that class is not a template
//     and is defined in the same header.  No specialization of a
//     template and no class of another translation unit becomes a
//     friend.  A second definition of the befriended class in a
//     translation unit that includes the header is a redefinition error.
//
// The friend is a door.  Each public member of a door either states the
// whole gate of the mint that calls it, or takes a live object and does
// one complete step on it.  So a direct call of a door member is no
// weaker than the mint or the step.  A class template never receives
// friendship.  A template that must build its successor, as a handle
// does at each step, calls a public step of the door, and the door
// builds the successor with the key.
//
// The language leaves one route that this shape does not close: an
// explicit specialization of a member template of the door is a member
// of the door.  scripts/check-proof-routes.py refuses an explicit
// specialization of a function in the source (function-specialization).

namespace detail {

// True when K has the shape of a passkey that the section above names.
// The walk of the witness roster reads each key by its name as well.
template <typename K>
consteval bool is_sealed_passkey() noexcept {
    return std::is_final_v<K> && !std::is_default_constructible_v<K> && !std::is_copy_constructible_v<K>
        && !std::is_move_constructible_v<K> && !std::is_copy_assignable_v<K> && !std::is_move_assignable_v<K>
        && !std::is_trivially_copyable_v<K> && !std::is_implicit_lifetime_v<K> && !std::is_aggregate_v<K>
        && std::is_empty_v<K>;
}

}  // namespace detail

class HandleFactory;
class SessionMintDoor;

// The key of every handle constructor.  Only the handle factory makes
// one, so every live handle comes from the factory, directly or by a
// move from a handle that the factory built.
class HandleKey final {
    constexpr HandleKey() noexcept {}
    friend class HandleFactory;

public:
    HandleKey(const HandleKey&) = delete("a handle key exists only for the one call that its maker passes it to");
    HandleKey& operator=(const HandleKey&) = delete("a handle key is never stored");
    constexpr ~HandleKey() noexcept {}
};

// The key of the builders that open a session.  Only the door of the
// mints makes one, and each public member of the door states the whole
// gate of its mint first.
class SessionOpenKey final {
    constexpr SessionOpenKey() noexcept {}
    friend class SessionMintDoor;

public:
    SessionOpenKey(const SessionOpenKey&) = delete("a session key exists only for the one call that its maker "
                                                   "passes it to");
    SessionOpenKey& operator=(const SessionOpenKey&) = delete("a session key is never stored");
    constexpr ~SessionOpenKey() noexcept {}
};

static_assert(detail::is_sealed_passkey<HandleKey>() && detail::is_sealed_passkey<SessionOpenKey>(),
              "a passkey is final, has a private user-provided constructor, no copy, no move and a user-provided "
              "destructor, so no route but its one friend makes it");

namespace detail {

// Sends the cancellation through the Resource.  The call is unqualified,
// so argument-dependent lookup finds the Resource's own function.
template <typename Resource>
constexpr void cancel_resource(std::remove_reference_t<Resource>& resource) noexcept {
    cancel_session(resource);
}

}  // namespace detail

// ── The shapes of a transport ────────────────────────────────────────
//
// A transport never waits where the watch of fixy/session/Watch.h cannot
// see the wait.  Each shape either returns at once, or takes the
// wait_scope that the handle opened before the call:
//
//   a trying write     bool(Resource&, T&)     false while it has no room,
//                                              and the value stays as it was
//   a declared write   void(Resource&, T&&, watch::wait_scope&)
//   a polling read     std::optional<T>(Resource&)   no value while nothing
//                                                    is there
//   a declared read    T(Resource&, watch::wait_scope&)
//
// The word of a choice or of a keyed message is std::size_t, with the same
// shapes.  When a trying write finds no room, or a polling read finds
// nothing, the handle waits through the watch and tries again.  A declared
// call waits inside the transport, and it calls poll() on the scope while
// it waits.  A transport of any other shape could wait where the watch does
// not see it, so each operation refuses it with [Transport_Shape].

template <typename F, typename Resource, typename T>
concept TryWrite =
    std::is_invocable_v<F&, Resource&, T&> && std::same_as<std::invoke_result_t<F&, Resource&, T&>, bool>;

template <typename F, typename Resource, typename T>
concept DeclaredWrite = !TryWrite<F, Resource, T> && std::is_invocable_v<F&, Resource&, T&&, watch::wait_scope&>
                     && std::is_void_v<std::invoke_result_t<F&, Resource&, T&&, watch::wait_scope&>>;

template <typename F, typename Resource, typename T>
concept WriteTransport = TryWrite<F, Resource, T> || DeclaredWrite<F, Resource, T>;

template <typename F, typename Resource, typename T>
concept PollRead = std::is_invocable_v<F&, Resource&>
                && std::same_as<std::invoke_result_t<F&, Resource&>, std::optional<T>>;

template <typename F, typename Resource, typename T>
concept DeclaredRead = !PollRead<F, Resource, T> && std::is_invocable_v<F&, Resource&, watch::wait_scope&>
                    && std::same_as<std::invoke_result_t<F&, Resource&, watch::wait_scope&>, T>;

template <typename F, typename Resource, typename T>
concept ReadTransport = PollRead<F, Resource, T> || DeclaredRead<F, Resource, T>;

namespace detail {

template <typename F, typename Resource, typename T>
consteval bool write_is_nothrow() noexcept {
    if constexpr (TryWrite<F, Resource, T>) {
        return std::is_nothrow_invocable_v<F&, Resource&, T&>;
    } else {
        return std::is_nothrow_invocable_v<F&, Resource&, T&&, watch::wait_scope&>;
    }
}

template <typename F, typename Resource, typename T>
consteval bool read_is_nothrow() noexcept {
    if constexpr (PollRead<F, Resource, T>) {
        return std::is_nothrow_invocable_v<F&, Resource&> && std::is_nothrow_move_constructible_v<T>;
    } else {
        return std::is_nothrow_invocable_v<F&, Resource&, watch::wait_scope&>;
    }
}

// Retries a polling read until it gives a value.  It runs only after the
// first read found nothing, so it is outlined and cold.  It waits through
// a watch::wait_scope, which refuses a wait that breaks the priority
// order and aborts on a cycle of waits across sessions.
template <typename Transport, typename Resource>
[[gnu::cold, gnu::noinline]] auto wait_for_arrival(Transport& transport, Resource& resource,
                                                   watch::endpoint_id endpoint) noexcept(
    std::is_nothrow_invocable_v<Transport&, Resource&>) -> std::invoke_result_t<Transport&, Resource&> {
    watch::wait_scope wait{endpoint};
    for (;;) {
        auto arrived = std::invoke(transport, resource);
        if (arrived) return arrived;
        wait.poll();
    }
}

// Retries a trying write until it takes the value, as wait_for_arrival
// retries a read.
template <typename Transport, typename Resource, typename T>
[[gnu::cold, gnu::noinline]] void wait_for_room(Transport& transport, Resource& resource, T& value,
                                                watch::endpoint_id endpoint) noexcept(
    std::is_nothrow_invocable_v<Transport&, Resource&, T&>) {
    watch::wait_scope wait{endpoint};
    while (!std::invoke(transport, resource, value)) wait.poll();
}

// Writes `value` through either write shape.  A trying write that finds
// room costs one call.  A declared write gets the scope opened for it.
template <typename T, typename Transport, typename Resource>
constexpr void write_through(Transport& transport, Resource& resource, T& value,
                             watch::endpoint_id endpoint) noexcept(write_is_nothrow<Transport, Resource, T>()) {
    if constexpr (TryWrite<Transport, Resource, T>) {
        if (!std::invoke(transport, resource, value)) [[unlikely]]
            wait_for_room(transport, resource, value, endpoint);
    } else {
        watch::wait_scope wait{endpoint};
        std::invoke(transport, resource, std::move(value), wait);
    }
}

// Reads a value through either read shape.
template <typename T, typename Transport, typename Resource>
[[nodiscard]] constexpr T read_through(Transport& transport, Resource& resource, watch::endpoint_id endpoint) noexcept(
    read_is_nothrow<Transport, Resource, T>()) {
    if constexpr (PollRead<Transport, Resource, T>) {
        std::optional<T> arrived = std::invoke(transport, resource);
        if (!arrived) [[unlikely]]
            arrived = wait_for_arrival(transport, resource, endpoint);
        return std::move(*arrived);
    } else {
        watch::wait_scope wait{endpoint};
        return std::invoke(transport, resource, wait);
    }
}

// A read of the same shape as `read` that gives each value it reads to
// `seen` first.  A decorator uses it to learn what its inner handle read.
template <typename T, typename Resource, typename Read, typename Seen>
[[nodiscard]] constexpr auto observed_read(Read& read, Seen seen) noexcept {
    if constexpr (PollRead<Read, Resource, T>) {
        return [&read, seen](Resource& resource) mutable noexcept(read_is_nothrow<Read, Resource, T>())
                   -> std::optional<T> {
            std::optional<T> got = std::invoke(read, resource);
            if (got) seen(*got);
            return got;
        };
    } else {
        return [&read, seen](Resource& resource, watch::wait_scope& wait) mutable noexcept(
                   read_is_nothrow<Read, Resource, T>()) -> T {
            T got = std::invoke(read, resource, wait);
            seen(got);
            return got;
        };
    }
}

// A write of the same shape as `write` that calls `taken` once the
// transport took the value.
template <typename T, typename Resource, typename Write, typename Taken>
[[nodiscard]] constexpr auto observed_write(Write& write, Taken taken) noexcept {
    if constexpr (TryWrite<Write, Resource, T>) {
        return [&write, taken](Resource& resource, T& value) mutable noexcept(write_is_nothrow<Write, Resource, T>())
                   -> bool {
            const bool is_taken = std::invoke(write, resource, value);
            if (is_taken) taken();
            return is_taken;
        };
    } else {
        return [&write, taken](Resource& resource, T&& value, watch::wait_scope& wait) mutable noexcept(
                   write_is_nothrow<Write, Resource, T>()) {
            std::invoke(write, resource, std::move(value), wait);
            taken();
        };
    }
}

}  // namespace detail

// ── The value of a keyed message ─────────────────────────────────────
//
// A keyed message, a PeerMsg or a Labelled, is its label word and then
// the value of its payload.  The label step sends or reads the word.  The
// handle then stands at the value step, a plain Send or Recv of the
// payload, which moves the value through a transport of that type.  A
// payload of void has no value.  The label word is then the whole
// message, and the handle stands past the label step.
//
// The label step and the value step are one message.  The crash transport
// of fixy/session/CrashTransport.h counts them as one message, and it
// refuses a crash between them.  The label step does not change the
// permission set.  The value step changes it, as each plain Send or Recv
// of the payload does.  The payload walk of fixy/session/Payload.h reads
// the payload of a keyed message, so the permission flow of the whole
// protocol changes the set at the same message.

// The two metafunctions live in detail, so no program specializes them:
// the handle, the crash transport and the checkpoint read the landing of
// a keyed step here, and a specialization would move one end past a value
// that the peer sends.
namespace detail {

// The payload of a keyed message.  Only PeerMsg and Labelled are keyed,
// because the payload rules of fixy/session/Protocol.h are sealed.  The
// primary has no definition, so a message of another shape stops the
// build.
template <typename Message>
struct keyed_value;
template <typename Peer, typename Label, typename Payload>
struct keyed_value<PeerMsg<Peer, Label, Payload>> {
    using type = Payload;
};
template <typename Label, typename Payload>
struct keyed_value<Labelled<Label, Payload>> {
    using type = Payload;
};

// The position after the label word of a keyed step: the value step, or
// the continuation when the payload is void.  The template parameters of
// the partial specializations have the names that the handle classes use,
// because GCC can print a Send or a Recv with the names of another
// specialization, and the fixtures match the printed names.
template <typename Step>
struct keyed_landing;
template <typename T, typename R>
struct keyed_landing<Send<T, R>> {
    using type = std::conditional_t<std::is_void_v<typename keyed_value<T>::type>, R,
                                    Send<typename keyed_value<T>::type, R>>;
};
template <typename T, typename R>
struct keyed_landing<Recv<T, R>> {
    using type = std::conditional_t<std::is_void_v<typename keyed_value<T>::type>, R,
                                    Recv<typename keyed_value<T>::type, R>>;
};

}  // namespace detail

template <typename Message>
using keyed_value_t = typename detail::keyed_value<Message>::type;

template <typename Step>
using keyed_landing_t = typename detail::keyed_landing<Step>::type;

// True when the message of a keyed step has a value.
template <typename Step>
concept keyed_step_has_value_v = !std::is_void_v<keyed_value_t<typename Step::message_type>>;

// ── Wire words ───────────────────────────────────────────────────────
//
// The Transport carries a word as std::size_t, which holds the 64 bits
// of a label word on each target this tree supports.

static_assert(sizeof(std::size_t) == sizeof(std::uint64_t),
              "a label word has 64 bits, and the Transport carries it as std::size_t");

// True when each label branch of the Select or the Offer names a label
// key, so its wire words are label words.  It is a concept, so no program
// specializes it: branch_of_wire_word reads it to decode a received word.
template <typename Choice>
concept is_keyed_choice_v =
    ::foundation::algebra::transition::is_keyed_choice_type(detail::protocol_registry, ^^Choice);

namespace detail {

// The word of branch I.  A branch that is no label has no word, because
// the peer never picks it: the endpoint enters it on an event.
template <typename Choice, std::size_t I>
consteval std::uint64_t branch_wire_word_of() {
    constexpr ::foundation::algebra::transition::wire_word word =
        ::foundation::algebra::transition::wire_word_of(protocol_registry, ^^Choice, I);
    static_assert(word.is_wired, "fixy::session::diagnostic [Branch_Has_No_Wire_Word]: branch I of the choice is no "
                                 "label, so no endpoint picks it with a word.  The endpoint enters such a branch on "
                                 "an event, for example the detection of a crash.");
    return word.value;
}

// The word of each branch of the choice, and whether the branch has one.
template <typename Choice, std::size_t Count>
consteval std::array<::foundation::algebra::transition::wire_word, Count> wire_words_of() {
    std::array<::foundation::algebra::transition::wire_word, Count> words{};
    for (std::size_t index = 0; index < Count; ++index) {
        words[index] = ::foundation::algebra::transition::wire_word_of(protocol_registry, ^^Choice, index);
    }
    return words;
}

template <typename Choice>
inline constexpr auto wire_words_v = wire_words_of<Choice, Choice::branch_count>();

}  // namespace detail

// The word that select<I>() sends for branch I of Choice.  The handle reads
// the registry and not this spelling, so a specialization of it changes
// only what its author reads.
template <typename Choice, std::size_t I>
inline constexpr std::uint64_t branch_wire_word_v = detail::branch_wire_word_of<Choice, I>();

inline constexpr std::size_t no_branch = static_cast<std::size_t>(-1);

// The branch of Choice that a received word names, or no_branch.  A keyed
// choice compares the word with the label word of each label branch, so
// a word that no label of Choice has names no branch.  A positional
// choice reads the word as a position.  In either kind a branch that is
// no label has no word, so the wire never enters it: the crash transport
// of fixy/session/CrashTransport.h enters a crash branch with
// HandleFactory::recover.
// Complexity: linear in the branches, each step a compare with a
// constant.
template <typename Choice>
[[nodiscard]] constexpr std::size_t branch_of_wire_word(std::uint64_t word) noexcept {
    constexpr std::size_t count = Choice::branch_count;
    if constexpr (!is_keyed_choice_v<Choice>) {
        constexpr auto& words = detail::wire_words_v<Choice>;
        if (word >= count || !words[static_cast<std::size_t>(word)].is_wired) return no_branch;
        return static_cast<std::size_t>(word);
    } else {
        constexpr auto& words = detail::wire_words_v<Choice>;
        for (std::size_t index = 0; index < count; ++index) {
            if (words[index].is_wired && words[index].value == word) return index;
        }
        return no_branch;
    }
}

namespace detail {

template <typename Choice, std::size_t I>
consteval auto branch_landing_of() {
    using Branch = std::tuple_element_t<I, typename Choice::branches_tuple>;
    if constexpr (is_keyed_choice_v<Choice> && wire_words_v<Choice>[I].is_wired) {
        return std::type_identity<keyed_landing_t<Branch>>{};
    } else {
        return std::type_identity<Branch>{};
    }
}

}  // namespace detail

// The protocol at which a choice enters branch I.  A label branch of a
// keyed choice enters past its label word: at the value step, or past the
// message when its payload is void.  Every other branch enters at the
// branch.  A decorator that tracks the protocol of its inner handle reads
// it here.
template <typename Choice, std::size_t I>
using branch_landing_t = typename decltype(detail::branch_landing_of<Choice, I>())::type;

// ── The admission of a protocol ──────────────────────────────────────
//
// The admission gate for handle construction, expressed as a concept
// rather than as body static_asserts alone.  A body static_assert is
// not visible to SFINAE: overload resolution accepts the signature for
// any Proto and the failure only appears at instantiation, where a
// requires-expression further up the stack cannot observe it.
// Downstream concepts that ask whether a handle can be minted need that
// answer, so the checks live in the signature.

namespace detail {

// The number of distinct roles that some local types name as peers,
// together.  The walk is named_peers_of of fixy/session/Payload.h.
// Complexity: quadratic in the number of peer names that the walk finds.
[[nodiscard]] consteval std::size_t distinct_peer_count(std::initializer_list<std::meta::info> protocols) {
    std::vector<std::meta::info> distinct;
    for (const std::meta::info protocol : protocols) {
        for (const std::meta::info peer : named_peers_of(protocol)) {
            if (!holds_type(distinct, peer)) distinct.push_back(peer);
        }
    }
    return distinct.size();
}

}  // namespace detail

// A local type that names two peers or more is the projection of a
// multiparty protocol onto one role.  Whether it runs safely depends on
// the global type and on the network model of the carrier
// (fixy/session/Network.h).  A mint of this header sees neither, so it
// refuses such a local type.  A multiparty binding asks CarrierImplements
// of fixy/session/Network.h with the global type first.
template <typename Proto>
concept NamesAtMostOnePeer = detail::distinct_peer_count({^^Proto}) <= 1;

// An empty choice is not well-formed either.  The empty-choice clause
// comes first so that the refusal of such a protocol names that fault
// and not the general one.  A protocol that hands an endpoint of a
// session to a peer of that session is refused too
// (DelegatesToNoOwnPeer in fixy/session/Payload.h).
template <typename Proto>
concept WellFormedRunnableProtocol =
    !is_empty_choice_v<Proto> && is_well_formed_v<Proto> && DelegatesToNoOwnPeer<Proto> && NamesAtMostOnePeer<Proto>;

namespace detail {

// The permission flow of a whole protocol from a starting set PS.  The
// handle checks each step when the step is compiled, and a branch that no
// run selects is never compiled.  This walk visits every branch, so an arm
// that leaves a loan open, or sends a region that the set does not hold,
// is refused at the mint even when the program never selects that arm.
//
// LoopPS is the set at the entry of the innermost Loop, or void outside
// every Loop.  A head that the walk does not know is refused.
//
// Complexity: one visit for each node of the protocol tree.
template <typename P, typename PS, typename LoopPS>
struct permission_flow_ {
    static consteval bool closes() noexcept { return false; }
};

template <typename T, typename R, typename PS, typename LoopPS>
struct permission_flow_<Send<T, R>, PS, LoopPS> {
    static consteval bool closes() noexcept {
        if constexpr (handle_admits_send_v<PS, T>) {
            return permission_flow_<R, perm_set_after_send_t<PS, T>, LoopPS>::closes();
        } else {
            return false;
        }
    }
};

template <typename T, typename R, typename PS, typename LoopPS>
struct permission_flow_<Recv<T, R>, PS, LoopPS> {
    static consteval bool closes() noexcept {
        if constexpr (handle_admits_recv_v<PS, T>) {
            return permission_flow_<R, perm_set_after_recv_t<PS, T>, LoopPS>::closes();
        } else {
            return false;
        }
    }
};

template <typename... Branches, typename PS, typename LoopPS>
struct permission_flow_<Select<Branches...>, PS, LoopPS> {
    static consteval bool closes() noexcept { return (permission_flow_<Branches, PS, LoopPS>::closes() && ...); }
};

template <typename... Branches, typename PS, typename LoopPS>
struct permission_flow_<Offer<Branches...>, PS, LoopPS> {
    static consteval bool closes() noexcept { return (permission_flow_<Branches, PS, LoopPS>::closes() && ...); }
};

template <typename Role, typename... Branches, typename PS, typename LoopPS>
struct permission_flow_<Offer<Sender<Role>, Branches...>, PS, LoopPS> {
    static consteval bool closes() noexcept { return (permission_flow_<Branches, PS, LoopPS>::closes() && ...); }
};

template <typename Body, typename PS, typename LoopPS>
struct permission_flow_<Loop<Body>, PS, LoopPS> {
    static consteval bool closes() noexcept { return permission_flow_<Body, PS, PS>::closes(); }
};

template <typename PS, typename LoopPS>
struct permission_flow_<Continue, PS, LoopPS> {
    static consteval bool closes() noexcept {
        if constexpr (std::is_void_v<LoopPS>) {
            return false;
        } else {
            return ::foundation::permissions::perm_set_equal_v<PS, LoopPS>;
        }
    }
};

template <typename PS, typename LoopPS>
struct permission_flow_<End, PS, LoopPS> {
    static consteval bool closes() noexcept { return perm_set_admits_close_v<PS>; }
};

template <VendorBackend V, typename P, typename PS, typename LoopPS>
struct permission_flow_<VendorPinned<V, P>, PS, LoopPS> {
    static consteval bool closes() noexcept { return permission_flow_<P, PS, LoopPS>::closes(); }
};

}  // namespace detail

// True when every path of Proto, from the set PS, sends only what the set
// holds, receives no second owner of a region, returns each loop to the
// set of its entry, and reaches End with no open loan.
template <typename Proto, typename PS>
concept PermissionFlowCloses = detail::permission_flow_<Proto, PS, void>::closes();

// ── Rewinding a session ──────────────────────────────────────────────
//
// A rewind opens a new session at a position inside a protocol, for
// example at the checkpoint that a rollback returns to.  The gate asks
// the same of that session as the context-free mint asks of a session
// from its start, so a rewind gives nothing that closing the End handle
// and minting a new session does not give:
//
//   - The loop context is none, or a plain Loop that is a runnable
//     protocol by itself.  A session brand is refused, so no rewind makes
//     a handle that a callback entry accepts back.
//   - R, under one Loop binder, is a runnable protocol.  Each Continue in
//     R binds to that binder, and the handle binds it to the loop context.
//   - R and the loop context together name one peer at most, as the
//     local type of a binary session does.
//   - Every path moves no permission, because the set is empty.
//   - The handle at End belongs to no session that a callback entry owns,
//     because that entry takes its handle back.

namespace detail {

template <typename R, typename LoopCtx>
consteval bool is_resumable_position() noexcept {
    using Empty = ::foundation::permissions::EmptyPermSet;
    if constexpr (std::is_void_v<LoopCtx>) {
        return !std::is_same_v<R, Continue> && WellFormedRunnableProtocol<R> && PermissionFlowCloses<R, Empty>;
    } else if constexpr (!is_loop_v<LoopCtx>) {
        return false;
    } else if constexpr (!(WellFormedRunnableProtocol<LoopCtx> && PermissionFlowCloses<LoopCtx, Empty>)) {
        return false;
    } else if constexpr (std::is_same_v<R, Continue>) {
        return true;
    } else {
        return WellFormedRunnableProtocol<Loop<R>> && PermissionFlowCloses<Loop<R>, Empty>
            && distinct_peer_count({^^R, ^^LoopCtx}) <= 1;
    }
}

}  // namespace detail

// True when a session can start at position R in the loop context LoopCtx.
template <typename R, typename LoopCtx>
concept ResumablePosition = detail::is_resumable_position<R, LoopCtx>();

// The gate of HandleFactory::rewind.
template <typename R, typename LoopCtx, typename EndLoopCtx>
concept RewindableTo =
    ResumablePosition<R, LoopCtx> && detail::carries_brand<EndLoopCtx, void>;

// ── Local choices ────────────────────────────────────────────────────
//
// A choice that puts no label on a wire is sound only where no peer
// reads a label.  Two forms exist, and each is refused elsewhere:
//
//   - select<I>(no_label) on a session whose Resource states the Local
//     network of fixy/session/NetworkModel.h and whose protocol names no
//     peer.  A log or an atomic cell has no peer, so its choice is its
//     own.  A Resource that states no network, or another one, is
//     refused, because on a channel the peer would read the next payload
//     as a label.
//   - select_local<I>(ctx) and pick_local<I>(ctx), with a context that
//     holds the Test capability, as mint_test_channel asks.  A test that
//     drives the two ends of a channel on one thread picks each branch on
//     both ends.  A production context has no Test capability.

// The trying write of a local choice.  It takes the wire word and writes
// nothing.
struct no_label_t {
    template <typename Resource>
    constexpr bool operator()(Resource&, std::size_t&) const noexcept {
        return true;
    }
};

inline constexpr no_label_t no_label{};

// True when a choice of Proto in the loop context LoopCtx may put no label
// on a wire: the Resource states the Local network, and the protocol names
// no peer.
template <typename Resource, typename Proto, typename LoopCtx>
concept AdmitsLocalChoice = LocalCarrier<Resource> && detail::distinct_peer_count({^^Proto, ^^LoopCtx}) == 0;

// True when the Transport of a select may write the word of the branch:
// every write except no_label, and no_label where a local choice is
// admitted.
template <typename Transport, typename Resource, typename Proto, typename LoopCtx>
concept WritesTheLabel = !std::is_same_v<Transport, no_label_t> || AdmitsLocalChoice<Resource, Proto, LoopCtx>;

// The context of select_local and pick_local.
template <typename Ctx>
concept CtxPicksLocally = ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Test>;

namespace detail {
// The gate of HandleFactory::recover, defined after CtxAdmitsProtocolRow.
template <typename Ctx, typename Choice, std::size_t I, typename LoopCtx, typename PS>
struct recover_gate;
}  // namespace detail

namespace detail {
template <typename Proto, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
class handle_core;
}  // namespace detail

// ── The handle factory ───────────────────────────────────────────────
//
// The one class that builds a handle.  It is the only friend of
// HandleKey, and every handle constructor takes that key, so every live
// handle comes from this class, directly or by a move from a handle that
// it built.
//
// Its builders are private.  They build a handle at a protocol position,
// with a loop context, a permission set and a session record that the
// caller names, so a builder in reach of other code would give a handle
// that no mint admitted.  Its public members are of three kinds, and
// none of them gives more than the mints and the steps give:
//
//   - open_ takes a SessionOpenKey, which only the door of the mints
//     makes, and the door states the whole gate of each mint first.
//   - The steps take a live handle and do one complete operation on it:
//     they move the message through the transport and build the handle
//     at the next position.  The handle classes forward their consumer
//     methods here, so a direct call is the same operation as the method.
//   - rewind takes a handle at End and opens a new session on its
//     Resource at a position that the gate of rewind admits.
//
// No object of the class exists.  Every constructor is deleted and the
// destructor is user-provided.  The class is neither trivially copyable
// nor an implicit-lifetime type, and no byte route makes one.
class HandleFactory final {
    HandleFactory() = delete("the handle factory holds static members only; no object of it exists");
    HandleFactory(const HandleFactory&) = delete("the handle factory holds static members only");
    HandleFactory& operator=(const HandleFactory&) = delete("the handle factory holds static members only");
    HandleFactory(HandleFactory&&) = delete("the handle factory holds static members only");
    HandleFactory& operator=(HandleFactory&&) = delete("the handle factory holds static members only");
    constexpr ~HandleFactory() noexcept {}

    // Builds the handle at Proto as it stands.
    template <typename Proto, typename Resource, typename LoopCtx, AbandonmentPolicy Policy,
              typename PS = ::foundation::permissions::EmptyPermSet>
    [[nodiscard]] static constexpr auto make_(Resource r, watch::session_ref session = {},
                                              std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>) -> SessionHandle<Proto, Resource, LoopCtx, Policy, PS>;

    // Builds the handle at the head that R reaches after Continue, Loop
    // and a vendor pin resolve.
    template <typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
    [[nodiscard]] static constexpr auto step_(Resource r, watch::session_ref session = {},
                                              std::source_location loc = std::source_location::current()) noexcept;

    // Builds the handle that a choice enters for branch I.
    template <typename Choice, std::size_t I, typename Resource, typename LoopCtx, AbandonmentPolicy Policy,
              typename PS>
    [[nodiscard]] static constexpr auto enter_(Resource r, watch::session_ref session) noexcept(
        std::is_nothrow_move_constructible_v<Resource>);

    // Builds the first handle of a protocol on a session record.
    template <typename Proto, typename Resource, AbandonmentPolicy Policy, typename PS, typename LoopCtx>
    [[nodiscard]] static constexpr auto start_(Resource r, std::source_location loc,
                                               watch::session_ref session) noexcept;

    // Claims a new record for a session, with the priority of its
    // Resource, and names the calling thread as its holder.
    template <typename Proto, typename Resource, AbandonmentPolicy Policy>
    [[nodiscard]] static constexpr watch::session_ref claim_(std::source_location loc) noexcept;

    // The core of a handle, where its Resource and its tracker are.
    template <typename Proto, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
    [[nodiscard]] static constexpr auto& core_(SessionHandle<Proto, Resource, LoopCtx, Policy, PS>& handle) noexcept {
        detail::handle_core<Proto, Resource, LoopCtx, Policy, PS>& core = handle;
        return core;
    }

    // Calls the handler with the handle that the choice enters for branch
    // idx, and first with the index as a constant when PassesIndex holds.
    // The handler is invoked once per branch type, and every branch must
    // give the handler the same return type, or all of them void.
    template <bool PassesIndex, typename Choice, typename Resource, typename LoopCtx, AbandonmentPolicy Policy,
              typename PS, typename Handler, std::size_t... Is>
    static constexpr auto dispatch_(std::size_t idx, Resource res, watch::session_ref session, Handler handler,
                                    std::index_sequence<Is...>);

    // Calls the handler for branch I, with the index first when PassesIndex
    // holds.
    template <bool PassesIndex, std::size_t I, typename Handler, typename BranchHandle>
    static constexpr decltype(auto) call_branch_(Handler&& handler, BranchHandle&& branch_handle) {
        if constexpr (PassesIndex) {
            return std::invoke(std::forward<Handler>(handler), std::integral_constant<std::size_t, I>{},
                               std::forward<BranchHandle>(branch_handle));
        } else {
            return std::invoke(std::forward<Handler>(handler), std::forward<BranchHandle>(branch_handle));
        }
    }

public:
    // Opens a session with a new record, or on a record that a channel
    // mint claimed.
    template <typename Proto, typename Resource, AbandonmentPolicy Policy, typename PS, typename LoopCtx = void>
    [[nodiscard]] static constexpr auto open_(SessionOpenKey const&, Resource r, std::source_location loc) noexcept;
    template <typename Proto, typename Resource, AbandonmentPolicy Policy, typename PS, typename LoopCtx = void>
    [[nodiscard]] static constexpr auto open_(SessionOpenKey const&, Resource r, std::source_location loc,
                                              watch::endpoint_id endpoint) noexcept;

    // The type of the first handle of a session, for the gates that read
    // it.  It builds nothing.
    template <typename Proto, typename Resource, AbandonmentPolicy Policy, typename PS, typename LoopCtx>
    using first_handle = decltype(open_<Proto, Resource, Policy, PS, LoopCtx>(
        std::declval<SessionOpenKey const&>(), std::declval<Resource>(), std::source_location{}));

    // The type of the handle at position R in the loop context LoopCtx,
    // after Continue, Loop and a vendor pin resolve.  It builds nothing.
    template <typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy,
              typename PS = ::foundation::permissions::EmptyPermSet>
    using handle_at = decltype(step_<R, Resource, LoopCtx, Policy, PS>(std::declval<Resource>()));

    // Rewinds a session.  It takes a handle at End and opens a new session
    // on its Resource at position R in the loop context LoopCtx, with a new
    // record.  fixy/session/Checkpoint.h rolls back to a checkpoint with
    // it.  The gate is RewindableTo, below the admission concepts, so the
    // new session is one that a mint admits from its start, and the handle
    // at End belongs to no session that a callback entry owns.
    template <typename R, typename LoopCtx, typename Resource, typename EndLoopCtx, AbandonmentPolicy Policy>
        requires RewindableTo<R, LoopCtx, EndLoopCtx>
    [[nodiscard]] static constexpr auto
    rewind(SessionHandle<End, Resource, EndLoopCtx, Policy, ::foundation::permissions::EmptyPermSet>&& at_end,
           std::source_location loc = std::source_location::current()) noexcept;

    // Enters branch I of an Offer, a branch that is no label, with no word
    // read.  The crash transport of fixy/session/CrashTransport.h enters a
    // crash branch with it.  It gives nothing that a detach of the handle
    // and a mint of a session at branch I do not give: branch I is no
    // label, so no peer picks it; the permission set is empty; RewindableTo
    // admits the branch in the loop context, as a mint admits a protocol
    // at its start; and the context admits the effect row of the branch, as
    // the context of mint_session admits the row of its protocol.  The
    // handle keeps the record of the session.
    template <std::size_t I, typename Ctx, typename... Branches, typename Resource, typename LoopCtx,
              AbandonmentPolicy Policy, typename PS>
        requires detail::recover_gate<Ctx, Offer<Branches...>, I, LoopCtx, PS>::value
    [[nodiscard]] static constexpr auto
    recover(Ctx const&, SessionHandle<Offer<Branches...>, Resource, LoopCtx, Policy, PS>&& handle) noexcept(
        std::is_nothrow_move_constructible_v<Resource>) {
        auto& core = core_(handle);
        core.require_live_();
        const watch::session_ref session = core.session_();
        return enter_<Offer<Branches...>, I, Resource, LoopCtx, Policy, PS>(core.take_resource_(), session);
    }

    // ── The steps ─────────────────────────────────────────────────────

    // Sends a value.  The transport has one of the two write shapes above.
    // The returned handle sits at the continuation, with Continue and Loop
    // already resolved.
    template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Transport>
        requires(!is_keyed_step_v<Send<T, R>>) && WriteTransport<Transport, Resource, T>
    [[nodiscard]] static constexpr auto
    send(SessionHandle<Send<T, R>, Resource, LoopCtx, Policy, PS>&& handle, T value,
         Transport transport) noexcept(detail::write_is_nothrow<Transport, Resource, T>()
                                       && std::is_nothrow_move_constructible_v<Resource>
                                       && std::is_nothrow_move_constructible_v<T>) {
        static_assert(detail::handle_admits_send_v<PS, T>,
                      "fixy::session::diagnostic [PermissionImbalance]: the permission set does not hold what the "
                      "message takes, or the payload walk of fixy/session/Payload.h refuses the message.  Hold each "
                      "permission that the payload moves or lends before the send.");
        auto& core = core_(handle);
        detail::write_through<T>(transport, core.live_resource_(), value, core.session_().endpoint);
        const watch::session_ref session = core.session_();
        return step_<R, Resource, LoopCtx, Policy, detail::perm_set_after_send_t<PS, T>>(core.take_resource_(),
                                                                                        session);
    }

    // Sends the label word of a keyed message.  The returned handle stands
    // at the value step, or past the message when its payload is void.
    template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Transport>
        requires is_keyed_step_v<Send<T, R>> && WriteTransport<Transport, Resource, std::size_t>
    [[nodiscard]] static constexpr auto
    send(SessionHandle<Send<T, R>, Resource, LoopCtx, Policy, PS>&& handle, Transport transport) noexcept(
        detail::write_is_nothrow<Transport, Resource, std::size_t>() && std::is_nothrow_move_constructible_v<Resource>) {
        static_assert(detail::handle_admits_send_v<PS, T>,
                      "fixy::session::diagnostic [PermissionImbalance]: the permission set does not hold what the "
                      "message takes, or the payload walk of fixy/session/Payload.h refuses the message.");
        auto& core = core_(handle);
        std::size_t word = detail::step_wire_word_of(^^Send<T, R>);
        detail::write_through<std::size_t>(transport, core.live_resource_(), word, core.session_().endpoint);
        const watch::session_ref session = core.session_();
        return step_<keyed_landing_t<Send<T, R>>, Resource, LoopCtx, Policy, PS>(core.take_resource_(), session);
    }

    // Receives a value.  The pair is the value and the handle at the
    // continuation.  A polling read that finds the message costs one call.
    template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Transport>
        requires(!is_keyed_step_v<Recv<T, R>>) && ReadTransport<Transport, Resource, T>
    [[nodiscard]] static constexpr auto
    recv(SessionHandle<Recv<T, R>, Resource, LoopCtx, Policy, PS>&& handle,
         Transport transport) noexcept(detail::read_is_nothrow<Transport, Resource, T>()
                                       && std::is_nothrow_move_constructible_v<Resource>
                                       && std::is_nothrow_move_constructible_v<T>) {
        static_assert(detail::handle_admits_recv_v<PS, T>,
                      "fixy::session::diagnostic [PermissionImbalance]: the permission set does not hold what the "
                      "message closes, the message gives a region that the set already holds, or the payload walk "
                      "of fixy/session/Payload.h refuses the message.");
        auto& core = core_(handle);
        T value = detail::read_through<T>(transport, core.live_resource_(), core.session_().endpoint);
        const watch::session_ref session = core.session_();
        auto next = step_<R, Resource, LoopCtx, Policy, detail::perm_set_after_recv_t<PS, T>>(core.take_resource_(),
                                                                                             session);
        return std::pair{std::move(value), std::move(next)};
    }

    // Reads the label word of a keyed message.  It returns the handle at
    // the value step, or past the message when its payload is void.  A
    // word other than the label word of this message aborts: the peer sent
    // a label that this position does not accept, as a word that names no
    // branch of an Offer aborts.
    template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Transport>
        requires is_keyed_step_v<Recv<T, R>> && ReadTransport<Transport, Resource, std::size_t>
    [[nodiscard]] static constexpr auto recv(SessionHandle<Recv<T, R>, Resource, LoopCtx, Policy, PS>&& handle,
                                             Transport transport) {
        static_assert(detail::handle_admits_recv_v<PS, T>,
                      "fixy::session::diagnostic [PermissionImbalance]: the permission set does not hold what the "
                      "message closes, or the payload walk of fixy/session/Payload.h refuses the message.");
        auto& core = core_(handle);
        const std::size_t word =
            detail::read_through<std::size_t>(transport, core.live_resource_(), core.session_().endpoint);
        if (word != detail::step_wire_word_of(^^Recv<T, R>)) [[unlikely]] {
            std::abort();
        }
        const watch::session_ref session = core.session_();
        return step_<keyed_landing_t<Recv<T, R>>, Resource, LoopCtx, Policy, PS>(core.take_resource_(), session);
    }

    // Picks branch I and signals the choice to the peer through Transport,
    // a write of a std::size_t word.  The Transport receives the wire word
    // of branch I: its label word in a keyed choice, and I in a positional
    // one.  In a keyed choice the returned handle stands at the value step
    // of the message of branch I, or past the message when its payload is
    // void.  In a positional choice it stands at the branch.
    //
    // The index bound is a body static_assert rather than a
    // requires-clause so that an out-of-range index reports the named
    // diagnostic instead of a bare unsatisfied-constraint message.
    template <std::size_t I, typename... Branches, typename Resource, typename LoopCtx, AbandonmentPolicy Policy,
              typename PS, typename Transport>
        requires WriteTransport<Transport, Resource, std::size_t>
              && WritesTheLabel<Transport, Resource, Select<Branches...>, LoopCtx>
    [[nodiscard]] static constexpr auto select(SessionHandle<Select<Branches...>, Resource, LoopCtx, Policy, PS>&& handle,
                                               Transport transport) noexcept(detail::write_is_nothrow<Transport, Resource,
                                                                                                      std::size_t>()
                                                                             && std::is_nothrow_move_constructible_v<Resource>) {
        using Choice = Select<Branches...>;
        static_assert(I < Choice::branch_count, "fixy::session::diagnostic [Branch_Index_Out_Of_Range]: "
                                                "SessionHandle<Select<...>>::select<I>(transport): branch "
                                                "index I is out of range for this Select position.  The "
                                                "protocol has fewer branches than the index requested; "
                                                "verify I < branch_count at the call site (decltype("
                                                "handle)::branch_count is exposed for compile-time queries).");
        auto& core = core_(handle);
        // The word comes from the registry and not from branch_wire_word_v,
        // so a specialization of the public spelling never reaches the wire.
        std::size_t word = detail::branch_wire_word_of<Choice, I>();
        detail::write_through<std::size_t>(transport, core.live_resource_(), word, core.session_().endpoint);
        const watch::session_ref session = core.session_();
        return enter_<Choice, I, Resource, LoopCtx, Policy, PS>(core.take_resource_(), session);
    }

    // Enters branch I of a Select WITHOUT telling the peer which branch was
    // picked.  The context holds the Test capability.
    template <std::size_t I, typename Ctx, typename... Branches, typename Resource, typename LoopCtx,
              AbandonmentPolicy Policy, typename PS>
        requires CtxPicksLocally<Ctx>
    [[nodiscard]] static constexpr auto
    select_local(Ctx const&, SessionHandle<Select<Branches...>, Resource, LoopCtx, Policy, PS>&& handle) noexcept(
        std::is_nothrow_move_constructible_v<Resource>) {
        using Choice = Select<Branches...>;
        static_assert(I < Choice::branch_count, "fixy::session::diagnostic [Branch_Index_Out_Of_Range]: "
                                                "SessionHandle<Select<...>>::select_local<I>(): branch index "
                                                "I is out of range for this Select position.  The protocol "
                                                "has fewer branches than the index requested; verify I < "
                                                "branch_count at the call site.");
        auto& core = core_(handle);
        core.require_live_();
        const watch::session_ref session = core.session_();
        return enter_<Choice, I, Resource, LoopCtx, Policy, PS>(core.take_resource_(), session);
    }

    // Receives the peer's wire word through Transport, a read of a
    // std::size_t word, then calls the handler with the handle for the
    // branch that the word names (branch_of_wire_word).  In a keyed choice
    // the handle of a label branch stands at the value step of its message,
    // or past the message when its payload is void.  A word that names no
    // branch aborts: the peer has sent a label this protocol does not
    // define, so the two endpoints no longer agree.
    template <typename... Branches, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Transport, typename Handler>
        requires ReadTransport<Transport, Resource, std::size_t>
    static constexpr auto branch(SessionHandle<Offer<Branches...>, Resource, LoopCtx, Policy, PS>&& handle,
                                 Transport transport, Handler handler) {
        using Choice = Offer<Branches...>;
        auto& core = core_(handle);
        const std::size_t word =
            detail::read_through<std::size_t>(transport, core.live_resource_(), core.session_().endpoint);
        const watch::session_ref session = core.session_();
        return dispatch_<false, Choice, Resource, LoopCtx, Policy, PS>(
            branch_of_wire_word<Choice>(word), core.take_resource_(), session, std::move(handler),
            std::make_index_sequence<Choice::branch_count>{});
    }

    // The step of branch, with the index of the branch given to the
    // handler first, as std::integral_constant<std::size_t, I>.
    template <typename... Branches, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Transport, typename Handler>
        requires ReadTransport<Transport, Resource, std::size_t>
    static constexpr auto branch_indexed(SessionHandle<Offer<Branches...>, Resource, LoopCtx, Policy, PS>&& handle,
                                         Transport transport, Handler handler) {
        using Choice = Offer<Branches...>;
        auto& core = core_(handle);
        const std::size_t word =
            detail::read_through<std::size_t>(transport, core.live_resource_(), core.session_().endpoint);
        const watch::session_ref session = core.session_();
        return dispatch_<true, Choice, Resource, LoopCtx, Policy, PS>(
            branch_of_wire_word<Choice>(word), core.take_resource_(), session, std::move(handler),
            std::make_index_sequence<Choice::branch_count>{});
    }

    // Enters branch I of an Offer WITHOUT receiving the peer's label.  The
    // context holds the Test capability.
    template <std::size_t I, typename Ctx, typename... Branches, typename Resource, typename LoopCtx,
              AbandonmentPolicy Policy, typename PS>
        requires CtxPicksLocally<Ctx>
    [[nodiscard]] static constexpr auto
    pick_local(Ctx const&, SessionHandle<Offer<Branches...>, Resource, LoopCtx, Policy, PS>&& handle) noexcept(
        std::is_nothrow_move_constructible_v<Resource>) {
        using Choice = Offer<Branches...>;
        static_assert(I < Choice::branch_count, "fixy::session::diagnostic [Branch_Index_Out_Of_Range]: "
                                                "SessionHandle<Offer<...>>::pick_local<I>(): branch index "
                                                "I is out of range for this Offer position.  The protocol "
                                                "has fewer branches than the index requested; verify I < "
                                                "branch_count at the call site.  On a sender-annotated "
                                                "Offer<Sender<Role>, B0, ...> the annotation is not a "
                                                "branch, so B0 is index 0.");
        auto& core = core_(handle);
        core.require_live_();
        const watch::session_ref session = core.session_();
        return enter_<Choice, I, Resource, LoopCtx, Policy, PS>(core.take_resource_(), session);
    }
};

namespace detail {

// The Resource half of every handle, and the half of the abandonment
// policy that needs the Resource: the cancellation.  The destructor of
// this class runs before the destructor of SessionHandleBase, while the
// Resource is still alive, so the cancellation is sent here and the
// handle is marked.  The base destructor then finds nothing to report.
//
// The handle factory is the one friend.  Its steps take the Resource out
// of the core and build the next handle.
template <typename Proto, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
class handle_core : public SessionHandleBase<Proto, SessionHandle<Proto, Resource, LoopCtx, Policy, PS>, Policy> {
    using base_type = SessionHandleBase<Proto, SessionHandle<Proto, Resource, LoopCtx, Policy, PS>, Policy>;
    friend class ::fixy::session::HandleFactory;

    static_assert(Policy::action != AbandonAction::Cancel || CancellableResource<Resource>,
                  "fixy::session::diagnostic [Cancel_Needs_A_Channel]: check::Cancel sends a cancellation to the "
                  "peer from the destructor, and this Resource has no way to send one.  Give the Resource a "
                  "noexcept function cancel_session(Resource&) that argument-dependent lookup finds, or use "
                  "check::Enforced.");

    Resource resource_;

    constexpr void cancel_if_owed_() noexcept {
        if constexpr (Policy::action == AbandonAction::Cancel) {
            if (this->owes_protocol_()) [[unlikely]] {
                cancel_resource<Resource>(resource_);
                this->release_endpoint_();
                this->mark_consumed_();
            }
        }
    }

protected:
    // Hands the Resource to the next handle, and marks this one consumed.
    // Each consumer method calls it exactly once, after the transport.
    [[nodiscard]] constexpr Resource take_resource_() noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        this->mark_consumed_();
        return std::forward<Resource>(resource_);
    }

    [[nodiscard]] constexpr Resource& live_resource_() noexcept {
        this->require_live_();
        return resource_;
    }

public:
    using protocol = Proto;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using perm_set = PS;

    // The constructor takes the handle key, which only the handle factory
    // makes.  The handle over this core passes the key on, so a core is
    // built only when the factory builds its handle.
    constexpr handle_core(HandleKey const&, Resource r, watch::session_ref session,
                          std::source_location loc) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : base_type{loc, session}, resource_{std::forward<Resource>(r)} {}

    constexpr handle_core(handle_core&& other) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : base_type{std::move(other)}, resource_{std::forward<Resource>(other.resource_)} {}

    // An assignment over a live handle drops the protocol that the target
    // held.  The target's policy acts on that drop first, and only then
    // does it take the source's position.  A reference Resource cannot be
    // rebound, so a handle over one cannot be assigned.
    constexpr handle_core& operator=(handle_core&& other) noexcept
        requires(!std::is_reference_v<Resource> && std::is_nothrow_move_assignable_v<Resource>)
    {
        if (this == &other) [[unlikely]]
            return *this;
        cancel_if_owed_();
        base_type::operator=(std::move(other));
        resource_ = std::move(other.resource_);
        return *this;
    }

    ~handle_core() { cancel_if_owed_(); }

    [[nodiscard]] constexpr Resource& resource() & noexcept {
        this->require_live_();
        return resource_;
    }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept {
        this->require_live_();
        return resource_;
    }

    // Stops the session before its end and tells the peer.  The peer
    // sees the cancellation on its next operation, through the transport.
    constexpr void cancel() && noexcept
        requires CancellableResource<Resource>
    {
        this->require_live_();
        cancel_resource<Resource>(resource_);
        this->release_endpoint_();
        this->mark_consumed_();
    }
};

}  // namespace detail

// ── The handle family ────────────────────────────────────────────────
//
// Each specialization offers the operations of its protocol head.  Its
// constructor takes the handle key, so only the handle factory builds
// one.  Each consumer method forwards to the step of the factory with
// the same name, which moves the message and builds the next handle.

template <typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
class [[nodiscard]] SessionHandle<End, Resource, LoopCtx, Policy, PS>
    : public detail::handle_core<End, Resource, LoopCtx, Policy, PS> {
    using core_type = detail::handle_core<End, Resource, LoopCtx, Policy, PS>;

    static_assert(detail::perm_set_admits_close_v<PS>,
                  "fixy::session::diagnostic [PermissionImbalance]: the protocol reaches End while the "
                  "permission set holds an open loan, a LentOut or a BorrowedIn.  close() would lose it.  Return "
                  "each loan before End.");

public:
    constexpr explicit SessionHandle(HandleKey const& key, Resource r, watch::session_ref session,
                                     std::source_location loc) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : core_type{key, std::forward<Resource>(r), session, loc} {}

    // Closing is what hands the Resource back.  It is available only at
    // End, so a caller can recover the Resource only after the protocol
    // has run to completion.
    [[nodiscard]] constexpr Resource close() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        this->require_live_();
        return this->take_resource_();
    }
};

template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
class [[nodiscard]] SessionHandle<Send<T, R>, Resource, LoopCtx, Policy, PS>
    : public detail::handle_core<Send<T, R>, Resource, LoopCtx, Policy, PS> {
    using core_type = detail::handle_core<Send<T, R>, Resource, LoopCtx, Policy, PS>;

public:
    constexpr explicit SessionHandle(HandleKey const& key, Resource r, watch::session_ref session,
                                     std::source_location loc) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : core_type{key, std::forward<Resource>(r), session, loc} {}

    using message_type = T;
    using continuation = R;

    // A keyed message (a PeerMsg or a Labelled) is its label word and then
    // the value of its payload.  The step is a Select of one branch, and it
    // sends the word as the Select does (fixy/session/Protocol.h).  The
    // value step then sends the value (the section on the value of a keyed
    // message above).
    static constexpr bool is_keyed = is_keyed_step_v<Send<T, R>>;

    // The Transport is what physically moves the value to the peer.  It
    // has one of the two write shapes above the handle family.  The
    // returned handle sits at the continuation, with Continue and Loop
    // already resolved.
    template <typename Transport>
        requires(!is_keyed) && WriteTransport<Transport, Resource, T>
    [[nodiscard]] constexpr auto
    send(T value, Transport transport) && noexcept(detail::write_is_nothrow<Transport, Resource, T>()
                                                   && std::is_nothrow_move_constructible_v<Resource>
                                                   && std::is_nothrow_move_constructible_v<T>) {
        return HandleFactory::send(std::move(*this), std::move(value), std::move(transport));
    }

    template <typename Transport>
        requires(!is_keyed) && (!WriteTransport<Transport, Resource, T>)
    void send(T, Transport) && = delete("[Transport_Shape] a write either tries, as bool(Resource&, T&), and returns "
                                        "false with the value unchanged while it has no room, or declares its wait, "
                                        "as void(Resource&, T&&, fixy::session::watch::wait_scope&).  A write of "
                                        "another shape can wait where the watch of fixy/session/Watch.h does not see "
                                        "it.");

    // Sends the label word of the message through Transport, a write of a
    // std::size_t word.  The peer can hold an Offer with more labels, and
    // it enters the branch of this word.  The returned handle stands at the
    // value step, or past the message when its payload is void.
    template <typename Transport>
        requires is_keyed && WriteTransport<Transport, Resource, std::size_t>
    [[nodiscard]] constexpr auto send(Transport transport) && noexcept(
        detail::write_is_nothrow<Transport, Resource, std::size_t>() && std::is_nothrow_move_constructible_v<Resource>) {
        return HandleFactory::send(std::move(*this), std::move(transport));
    }

    template <typename Transport>
        requires is_keyed && (!WriteTransport<Transport, Resource, std::size_t>)
    void send(Transport) && = delete("[Transport_Shape] a write of a word either tries, as "
                                     "bool(Resource&, std::size_t), or declares its wait, as "
                                     "void(Resource&, std::size_t, fixy::session::watch::wait_scope&).  A write of "
                                     "another shape can wait where the watch of fixy/session/Watch.h does not see it.");

    template <typename U, typename Transport>
        requires is_keyed
    void send(U&&, Transport) && = delete("[Keyed_Label_Takes_No_Value] a Send of a PeerMsg or a Labelled is keyed: "
                                          "this step sends only its label word.  Call send(transport) with a write "
                                          "of a std::size_t word.  The handle then stands at the value step, which "
                                          "sends the value of the payload.");
};

template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
class [[nodiscard]] SessionHandle<Recv<T, R>, Resource, LoopCtx, Policy, PS>
    : public detail::handle_core<Recv<T, R>, Resource, LoopCtx, Policy, PS> {
    using core_type = detail::handle_core<Recv<T, R>, Resource, LoopCtx, Policy, PS>;

public:
    constexpr explicit SessionHandle(HandleKey const& key, Resource r, watch::session_ref session,
                                     std::source_location loc) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : core_type{key, std::forward<Resource>(r), session, loc} {}

    using message_type = T;
    using continuation = R;

    // A keyed message is its label word and then the value of its payload.
    // The step is an Offer of one branch, and it reads the label word as the
    // Offer does.  The value step then reads the value.
    static constexpr bool is_keyed = is_keyed_step_v<Recv<T, R>>;

    // The Transport has one of the two read shapes above the handle
    // family.  The pair is the received value and the handle at the
    // continuation.  A polling read that finds the message costs one call.
    template <typename Transport>
        requires(!is_keyed) && ReadTransport<Transport, Resource, T>
    [[nodiscard]] constexpr auto
    recv(Transport transport) && noexcept(detail::read_is_nothrow<Transport, Resource, T>()
                                          && std::is_nothrow_move_constructible_v<Resource>
                                          && std::is_nothrow_move_constructible_v<T>) {
        return HandleFactory::recv(std::move(*this), std::move(transport));
    }

    template <typename Transport>
        requires(!is_keyed) && (!ReadTransport<Transport, Resource, T>)
    void recv(Transport) && = delete("[Transport_Shape] a read either polls, as std::optional<T>(Resource&), and "
                                     "returns no value while nothing is there, or declares its wait, as "
                                     "T(Resource&, fixy::session::watch::wait_scope&).  A read of another shape can "
                                     "wait where the watch of fixy/session/Watch.h does not see it.");

    // Reads the label word of the message through Transport, a read of a
    // std::size_t word.  It returns the handle at the value step, or past
    // the message when its payload is void.  A word other than the label
    // word of this message aborts: the peer sent a label that this position
    // does not accept, as a word that names no branch of an Offer aborts.
    template <typename Transport>
        requires is_keyed && ReadTransport<Transport, Resource, std::size_t>
    [[nodiscard]] constexpr auto recv(Transport transport) && {
        return HandleFactory::recv(std::move(*this), std::move(transport));
    }

    template <typename Transport>
        requires is_keyed && (!ReadTransport<Transport, Resource, std::size_t>)
    void recv(Transport) && = delete("[Transport_Shape] a read of a word either polls, as "
                                     "std::optional<std::size_t>(Resource&), or declares its wait, as "
                                     "std::size_t(Resource&, fixy::session::watch::wait_scope&).  A read of another "
                                     "shape can wait where the watch of fixy/session/Watch.h does not see it.");
};

template <typename... Branches, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
class [[nodiscard]] SessionHandle<Select<Branches...>, Resource, LoopCtx, Policy, PS>
    : public detail::handle_core<Select<Branches...>, Resource, LoopCtx, Policy, PS> {
    using core_type = detail::handle_core<Select<Branches...>, Resource, LoopCtx, Policy, PS>;

public:
    constexpr explicit SessionHandle(HandleKey const& key, Resource r, watch::session_ref session,
                                     std::source_location loc) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : core_type{key, std::forward<Resource>(r), session, loc} {}

    using protocol = Select<Branches...>;

    // Read from the protocol, as the Offer handle does, so that a note in
    // the pack, such as a Sender, never counts as a branch.
    using branches = typename protocol::branches_tuple;

    static constexpr std::size_t branch_count = protocol::branch_count;

    // A second empty-choice rejection, so a construction route that
    // reaches this class without passing the factory's own check still
    // fires the same diagnostic.
    static_assert(branch_count > 0, "fixy::session::diagnostic [Empty_Choice_Combinator]: "
                                    "SessionHandle<Select<>>: cannot construct a runnable handle "
                                    "on Select<> with zero branches — there is no branch for "
                                    "select<I>() to choose.  See mint_session_handle for the full "
                                    "diagnostic and remediation.");

    // Picks branch I and signals the choice to the peer through
    // Transport, a write of a std::size_t word.  The Transport receives the
    // wire word of branch I: its label word in a keyed choice, and I in a
    // positional one.  In a keyed choice the returned handle stands at the
    // value step of the message of branch I, which sends the value next, or
    // past the message when its payload is void.  In a positional choice it
    // stands at the branch, and the branch sends its payload next.
    //
    // The index bound is a body static_assert rather than a
    // requires-clause so that an out-of-range index reports the named
    // diagnostic instead of a bare unsatisfied-constraint message.
    // Transport still gates overload resolution.
    // A carrier with no peer passes no_label as the Transport, so the
    // choice puts no word on a wire (Local choices, above).
    template <std::size_t I, typename Transport>
        requires WriteTransport<Transport, Resource, std::size_t>
              && WritesTheLabel<Transport, Resource, Select<Branches...>, LoopCtx>
    [[nodiscard]] constexpr auto
    select(Transport transport) && noexcept(detail::write_is_nothrow<Transport, Resource, std::size_t>()
                                            && std::is_nothrow_move_constructible_v<Resource>) {
        return HandleFactory::template select<I>(std::move(*this), std::move(transport));
    }

    template <std::size_t I, typename Transport>
        requires(!WriteTransport<Transport, Resource, std::size_t>)
    void select(Transport) && = delete("[Transport_Shape] a write of a word either tries, as "
                                       "bool(Resource&, std::size_t), or declares its wait, as "
                                       "void(Resource&, std::size_t, fixy::session::watch::wait_scope&).  A write of "
                                       "another shape can wait where the watch of fixy/session/Watch.h does not see "
                                       "it.");

    template <std::size_t I, typename Transport>
        requires WriteTransport<Transport, Resource, std::size_t>
              && (!WritesTheLabel<Transport, Resource, Select<Branches...>, LoopCtx>)
    void select(Transport) && = delete("[Local_Choice_Needs_A_Local_Carrier] select<I>(no_label) puts no word on "
                                       "the wire, and a peer then reads the next payload as a label.  Only a "
                                       "Resource that states session_network = Network::Local, over a protocol that "
                                       "names no peer, takes no_label.  Give the peer the word with a write, or use "
                                       "select_local<I>(ctx) with a test context.");

    // Advances the local handle WITHOUT telling the peer which branch
    // was picked.  The context holds the Test capability: a test that
    // drives the two ends of a channel on one thread picks the branch on
    // each end.
    template <std::size_t I, typename Ctx>
        requires CtxPicksLocally<Ctx>
    [[nodiscard]] constexpr auto select_local(Ctx const& ctx) && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        return HandleFactory::template select_local<I>(ctx, std::move(*this));
    }

    // Deleting the zero-argument form forces every call site to state
    // whether the peer is told.  A default would have to pick one, and
    // picking the silent one drifts wire-based sessions apart.
    template <std::size_t I>
    void select() && = delete("[Wire_Variant_Required] SessionHandle<Select<...>>::select<I>() "
                              "without arguments is not allowed.  Choose one: "
                              "(a) `select<I>(transport)` to signal the branch choice over "
                              "the wire (the peer sees the I-th branch and stays in sync), "
                              "(b) `select<I>(no_label)` on a carrier that states the Local network, "
                              "OR (c) `select_local<I>(ctx)` with a test context.  The framework "
                              "refuses to guess which one you meant.");
};

template <typename... Branches, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
class [[nodiscard]] SessionHandle<Offer<Branches...>, Resource, LoopCtx, Policy, PS>
    : public detail::handle_core<Offer<Branches...>, Resource, LoopCtx, Policy, PS> {
    using core_type = detail::handle_core<Offer<Branches...>, Resource, LoopCtx, Policy, PS>;

public:
    constexpr explicit SessionHandle(HandleKey const& key, Resource r, watch::session_ref session,
                                     std::source_location loc) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : core_type{key, std::forward<Resource>(r), session, loc} {}

    using protocol = Offer<Branches...>;

    // Read from the protocol rather than re-derived from this class's
    // own pack.  `Offer<Sender<Role>, Bs...>` matches this
    // specialization with `Branches... = {Sender<Role>, Bs...}`, so
    // `sizeof...(Branches)` counts the sender annotation as a branch
    // while Protocol.h's own partial specialization does not.  The two
    // then disagree about one type: `Offer<Sender<R>, B0, B1>` reports
    // 2 through the protocol and 3 through the handle, `pick_local<0>`
    // selects the annotation instead of the first branch, and the
    // dispatch bounds check admits a peer label one past the last real
    // branch.  Deriving both the count and the branch list from
    // `protocol` leaves exactly one place that decides what a branch
    // is, and that place is the combinator that owns the annotation.
    using branches = typename protocol::branches_tuple;

    static constexpr std::size_t branch_count = protocol::branch_count;

    // Mirror of the Select guard.  No peer label decodes to a valid
    // branch here.  An `Offer<Sender<Role>>` carrying an annotation and
    // no branch reaches this too, because the count excludes the tag.
    static_assert(branch_count > 0, "fixy::session::diagnostic [Empty_Choice_Combinator]: "
                                    "SessionHandle<Offer<>>: cannot construct a runnable handle "
                                    "on Offer<> with zero branches — there is no label the peer "
                                    "can send.  A sender-annotated Offer<Sender<Role>> reaches "
                                    "this as well: the annotation names the signalling role and "
                                    "is not a branch.  See mint_session_handle for the full "
                                    "diagnostic and remediation.");

    // Receives the peer's wire word through Transport, a read of a
    // std::size_t word, then calls the handler with the handle for the
    // branch that the word names (branch_of_wire_word).  The
    // handler is invoked once per branch type and every branch must give
    // the handler the same return type, or all of them void.  In a keyed
    // choice the handle of a label branch stands at the value step of its
    // message, which reads the value next, or past the message when its
    // payload is void.
    //
    // A word that names no branch aborts.  The peer has sent a label this
    // protocol does not define, so the two endpoints no longer agree
    // and no branch can be entered safely.
    template <typename Transport, typename Handler>
        requires ReadTransport<Transport, Resource, std::size_t>
    constexpr auto branch(Transport transport, Handler handler) && {
        return HandleFactory::branch(std::move(*this), std::move(transport), std::move(handler));
    }

    // The same step, and the handler also takes the index of the branch as
    // std::integral_constant<std::size_t, I>, first.  In a keyed choice two
    // branches can enter at the same handle type, and the index tells them
    // apart.
    template <typename Transport, typename Handler>
        requires ReadTransport<Transport, Resource, std::size_t>
    constexpr auto branch_indexed(Transport transport, Handler handler) && {
        return HandleFactory::branch_indexed(std::move(*this), std::move(transport), std::move(handler));
    }

    template <typename Transport, typename Handler>
        requires(!ReadTransport<Transport, Resource, std::size_t>)
    void branch(Transport, Handler) && = delete("[Transport_Shape] a read of a word either polls, as "
                                                "std::optional<std::size_t>(Resource&), or declares its wait, as "
                                                "std::size_t(Resource&, fixy::session::watch::wait_scope&).  A read "
                                                "of another shape can wait where the watch of fixy/session/Watch.h "
                                                "does not see it.");

    // Assumes branch I WITHOUT receiving the peer's label.  The context
    // holds the Test capability: a test that drives the two ends of a
    // channel on one thread picks the branch on each end.
    template <std::size_t I, typename Ctx>
        requires CtxPicksLocally<Ctx>
    [[nodiscard]] constexpr auto pick_local(Ctx const& ctx) && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        return HandleFactory::template pick_local<I>(ctx, std::move(*this));
    }

    // Deleting the zero-argument form forces every call site to state
    // whether the peer's label is read.
    template <std::size_t I>
    void pick() && = delete("[Wire_Variant_Required] SessionHandle<Offer<...>>::pick<I>() "
                            "without arguments is not allowed.  Use branch(transport, handler) to read "
                            "the label of the peer, or `pick_local<I>(ctx)` with a test context.");
};

// ── The builders of the handle factory ───────────────────────────────

// Builds the handle at R, and carries the session's reference in the
// watch to it.  A step that reaches a terminal state owes no message, so
// it ends the session in the watch and the terminal handle carries no
// record.  A caller that builds a handle without a record passes none,
// and that session is not tracked.
template <typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
constexpr auto HandleFactory::step_(Resource r, watch::session_ref session, std::source_location loc) noexcept {
    if constexpr (std::is_same_v<R, Continue>) {
        using ActiveLoopCtx = session_loop_ctx_inner_t<LoopCtx>;
        static_assert(!std::is_void_v<ActiveLoopCtx>, "fixy::session::diagnostic [Continue_Without_Loop]: "
                                                      "Continue appears outside a Loop context.  "
                                                      "Every Continue must have an enclosing Loop<Body>.");
        static_assert(::foundation::permissions::perm_set_equal_v<PS, detail::loop_entry_perm_set_t<ActiveLoopCtx>>,
                      "fixy::session::diagnostic [PermissionImbalance]: one iteration of the loop changes the "
                      "permission set, so a Continue would start the next iteration with a different set.  Each "
                      "permission that the body receives, it must send back before the Continue, and each "
                      "permission that the body sends, it must receive back.");
        using NextBody = detail::loop_body_t<ActiveLoopCtx>;
        // The body may itself begin with a Loop or a Continue, so this
        // recurses.  Forwarding `loc` keeps the outermost caller's site
        // rather than replacing it with this frame's.
        return step_<NextBody, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r), session, loc);
    } else if constexpr (is_loop_v<R>) {
        using InnerBody = detail::loop_body_t<R>;
        using InnerCtx = session_loop_ctx_rebind_inner_t<LoopCtx, detail::loop_frame_t<R, PS>>;
        // Entering an inner Loop shadows the enclosing loop context.
        // That shadowing is what binds Continue to the nearest Loop.
        return step_<InnerBody, Resource, InnerCtx, Policy, PS>(std::forward<Resource>(r), session, loc);
    } else if constexpr (is_vendor_pinned_v<R>) {
        // The vendor is a declaration for the layer above.  The handle
        // steps the protocol that it pins.
        return step_<typename R::protocol, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r), session, loc);
    } else {
        static_assert(is_head_v<R>, "fixy::session::diagnostic [Protocol_Ill_Formed]: "
                                    "unexpected protocol shape after resolution.  "
                                    "Only Send/Recv/Select/Offer/End/Continue are valid heads.");
        if constexpr (Policy::checks_abandonment && is_terminal_state_v<R>) {
            if !consteval {
                watch::release(session.endpoint);
            }
            return make_<R, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r), watch::session_ref{}, loc);
        } else {
            return make_<R, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r), session, loc);
        }
    }
}

// The handle that a choice enters for branch I.  A label branch of a
// keyed choice is its label step (rule 6 of the section on branches and
// labels of foundation/algebra/Transition.h), and the word of the branch
// is the label word of that step.  The handle enters at the value step of
// the message, or past the message when its payload is void.  Every other
// branch, a positional one or one that is no label, enters at its head.
template <typename Choice, std::size_t I, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
constexpr auto HandleFactory::enter_(Resource r, watch::session_ref session) noexcept(
    std::is_nothrow_move_constructible_v<Resource>) {
    using Branch = std::tuple_element_t<I, typename Choice::branches_tuple>;
    if constexpr (is_keyed_choice_v<Choice> && detail::wire_words_v<Choice>[I].is_wired) {
        using Message = typename Branch::message_type;
        if constexpr (is_select_v<Choice>) {
            static_assert(detail::handle_admits_send_v<PS, Message>,
                          "fixy::session::diagnostic [PermissionImbalance]: the permission set does not hold what "
                          "the message of the branch takes, or the payload walk of fixy/session/Payload.h refuses "
                          "the message.");
        } else {
            static_assert(detail::handle_admits_recv_v<PS, Message>,
                          "fixy::session::diagnostic [PermissionImbalance]: the permission set does not hold what "
                          "the message of the branch closes, or the payload walk of fixy/session/Payload.h refuses "
                          "the message.");
        }
        return step_<keyed_landing_t<Branch>, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r), session);
    } else {
        return step_<Branch, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r), session);
    }
}

// A word that names no branch aborts.  The result type comes from branch
// 0.  A branch whose handler returns a different type then fails to
// convert into the single optional below, which is what enforces the
// same-return-type rule.
template <bool PassesIndex, typename Choice, typename Resource, typename LoopCtx, AbandonmentPolicy Policy,
          typename PS, typename Handler, std::size_t... Is>
constexpr auto HandleFactory::dispatch_(std::size_t idx, Resource res, watch::session_ref session, Handler handler,
                                        std::index_sequence<Is...>) {
    if (idx >= Choice::branch_count) [[unlikely]] {
        std::abort();
    }
    using FirstHandle = decltype(enter_<Choice, 0, Resource, LoopCtx, Policy, PS>(std::declval<Resource>(), session));
    using Result = decltype(call_branch_<PassesIndex, 0>(std::declval<Handler&&>(), std::declval<FirstHandle>()));

    if constexpr (std::is_void_v<Result>) {
        bool dispatched = false;
        (
            [&]() {
                if (!dispatched && idx == Is) {
                    call_branch_<PassesIndex, Is>(
                        std::move(handler),
                        enter_<Choice, Is, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(res), session));
                    dispatched = true;
                }
            }(),
            ...);
    } else {
        std::optional<Result> result;
        bool dispatched = false;
        (
            [&]() {
                if (!dispatched && idx == Is) {
                    result.emplace(call_branch_<PassesIndex, Is>(
                        std::move(handler),
                        enter_<Choice, Is, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(res), session)));
                    dispatched = true;
                }
            }(),
            ...);
        if (!result) [[unlikely]]
            std::abort();
        return std::move(*result);
    }
}

// ── Viewing a position ───────────────────────────────────────────────
//
// A caller that wants to look at where a handle is, without a step,
// takes a view: fixy::mint_view<Tag>(handle) from fixy/ScopedView.h.
// The tag names a kind of position.  The view gate asks view_ok below,
// which argument-dependent lookup finds for a SessionHandle.
//
//   - A tag that does not name the handle's position is refused when the
//     program compiles, because view_ok then does not exist for it.
//   - A view of a handle that is not live fails the precondition of
//     mint_view when the program runs, because a consumed handle holds no
//     position to look at.
//
// A view is a borrow.  It does not survive a step of the handle it looks
// at, and fixy/ScopedView.h says what that costs.

namespace position {

struct AtSend {};
struct AtRecv {};
struct AtSelect {};
struct AtOffer {};
struct AtEnd {};

}  // namespace position

// True when Tag names the position of Proto.  A tag outside the family
// above names no position, so the answer for it is false.
template <typename Proto, typename Tag>
inline constexpr bool protocol_is_at_v = (std::is_same_v<Tag, position::AtSend> && is_send_v<Proto>)
                                      || (std::is_same_v<Tag, position::AtRecv> && is_recv_v<Proto>)
                                      || (std::is_same_v<Tag, position::AtSelect> && is_select_v<Proto>)
                                      || (std::is_same_v<Tag, position::AtOffer> && is_offer_v<Proto>)
                                      || (std::is_same_v<Tag, position::AtEnd> && is_terminal_state_v<Proto>);

template <typename Proto, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS, typename Tag>
    requires protocol_is_at_v<Proto, Tag>
[[nodiscard]] constexpr bool view_ok(SessionHandle<Proto, Resource, LoopCtx, Policy, PS> const& handle,
                                     std::type_identity<Tag>) noexcept {
    return handle.is_live();
}

// ── The Resource ─────────────────────────────────────────────────────
//
// The Resource is what a handle stores at runtime.  The concept stops a
// handle from outliving what it points at, and it stops a second holder
// from reaching the channel of a live session.
//
// A value type is safe when the handle owns it: their lifetimes coincide.
// An lvalue reference to a Pinned object is safe because a Pinned object
// cannot be moved, so its address is stable for its whole lifetime and
// the caller only has to outlive the handles.  An lvalue reference to a
// non-Pinned object is refused: a move of the referent relocates it and
// every live handle dangles.  An rvalue reference is refused because it
// binds to a temporary that dies before the handle is used.
//
// A value Resource that reaches state outside itself has one holder.  A
// copy of it is a second channel to the peer: the holder could send the
// copy in a message, or build a second handle on it, and write outside
// the protocol.  This is the one-holder rule of per-message resources in
// Actris: a channel end is an exclusive resource, never a duplicable one.
// So a Resource that can be copied must be fixy::SelfContained, which
// reads every base and member, private ones too, and names the part that
// reaches out.  A raw pointer, a function pointer and a
// std::reference_wrapper are copyable and reach out, so they are refused.
// A Resource that reaches a channel holds a MoveOnlyResource member,
// which deletes its copy and keeps it an aggregate.
//
// The reach is conservative in the way SelfContained is: a member that
// the walk cannot account for counts as a reach.  Complexity: linear in
// the distinct types reached from the Resource.

// A member that makes the Resource that holds it move-only.  It is empty,
// and [[no_unique_address]] gives it no byte.
struct MoveOnlyResource {
    constexpr MoveOnlyResource() noexcept = default;
    constexpr MoveOnlyResource(MoveOnlyResource&&) noexcept = default;
    constexpr MoveOnlyResource& operator=(MoveOnlyResource&&) noexcept = default;
    MoveOnlyResource(const MoveOnlyResource&) =
        delete("[Resource_Copy] a Resource that reaches a channel has one holder.  A copy is a second channel");
    MoveOnlyResource& operator=(const MoveOnlyResource&) =
        delete("[Resource_Copy] a Resource that reaches a channel has one holder.  A copy is a second channel");
    ~MoveOnlyResource() = default;
};

// True when a value of the type can be duplicated: by construction from
// a copy, or by assignment from one.
template <typename Resource>
inline constexpr bool resource_is_copyable_v =
    std::is_copy_constructible_v<Resource> || std::is_copy_assignable_v<Resource>;

// A value Resource: owned by the handle, and either self-contained or
// impossible to copy.
template <typename Resource>
concept OwnedSessionResource =
    !std::is_reference_v<Resource> && (!resource_is_copyable_v<Resource> || ::fixy::SelfContained<Resource>);

// A reference Resource: an lvalue reference to a Pinned object.
template <typename Resource>
concept PinnedSessionResource =
    std::is_lvalue_reference_v<Resource>
    && std::derived_from<std::remove_reference_t<Resource>, ::foundation::Pinned<std::remove_reference_t<Resource>>>;

template <typename Resource>
concept SessionResource = OwnedSessionResource<Resource> || PinnedSessionResource<Resource>;

// ── The priority of a session ────────────────────────────────────────
//
// A Resource states the priority of its session in the order of
// fixy/session/Watch.h with a static constexpr member session_priority of
// the type watch::priority.  A Resource that states none has the lowest
// priority.  A member of that name with another type is refused: a plain
// integer with that name could be a count, and the order would read it.

namespace detail {

template <typename Resource>
consteval watch::priority session_priority_of() noexcept {
    using Bare = std::remove_cvref_t<Resource>;
    constexpr bool states_a_priority = requires { Bare::session_priority; };
    if constexpr (states_a_priority) {
        constexpr bool is_typed = std::same_as<std::remove_cv_t<decltype(Bare::session_priority)>, watch::priority>;
        static_assert(is_typed,
                      "fixy::session::diagnostic [Session_Priority_Type]: the Resource declares session_priority "
                      "with a type other than fixy::session::watch::priority.  Declare it as "
                      "static constexpr fixy::session::watch::priority session_priority{N};");
        if constexpr (is_typed) return Bare::session_priority;
    }
    return watch::priority::lowest;
}

}  // namespace detail

template <typename Resource>
inline constexpr watch::priority session_priority_v = detail::session_priority_of<Resource>();

// The two ends of a channel are one session, so their Resources state one
// priority.
template <typename ResourceA, typename ResourceB>
concept ChannelEndsShareAPriority = session_priority_v<ResourceA> == session_priority_v<ResourceB>;

// The member definitions of the factory that read the admission concepts
// stand here, after those concepts.

template <typename Proto, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
constexpr auto HandleFactory::make_(Resource r, watch::session_ref session, std::source_location loc) noexcept(
    std::is_nothrow_move_constructible_v<Resource>) -> SessionHandle<Proto, Resource, LoopCtx, Policy, PS> {
    static_assert(SessionResource<Resource>, "fixy::session::diagnostic [SessionResource_Refused]: the handle "
                                             "factory builds a handle only over a Resource that a mint admits.");
    return SessionHandle<Proto, Resource, LoopCtx, Policy, PS>{HandleKey{}, std::forward<Resource>(r), session, loc};
}

// HandleFactory::start_ builds the first handle of an admitted protocol
// with a given permission set.  A Proto that starts with a Loop is
// unrolled one iteration.  The returned handle then sits at the loop body
// with the Loop (or its permission frame) as its LoopCtx.  Any other head
// passes through unchanged.
//
// Each public mint runs its own admission first.  This member states the
// part of the admission that every mint shares again: a runnable
// protocol, a Resource that a mint admits, a permission flow that closes
// from PS, and a loop context that is void or a session brand.  A mint
// that consumes Permission tokens calls it with the set of the tokens it
// consumed.  No mint may call it with a set whose tokens it did not
// consume.
//
// Under a checking policy the session has a record in
// fixy/session/Watch.h, and the first handle names the calling thread as
// its holder.  The two open_ overloads below differ only in how they get
// the record.
template <typename Proto, typename Resource, AbandonmentPolicy Policy, typename PS, typename LoopCtx>
constexpr auto HandleFactory::start_(Resource r, std::source_location loc, watch::session_ref session) noexcept {
    static_assert(WellFormedRunnableProtocol<Proto>,
                  "fixy::session::diagnostic [Protocol_Ill_Formed]: the handle factory opens a session only on a "
                  "protocol that a mint admits.");
    static_assert(SessionResource<Resource>, "fixy::session::diagnostic [SessionResource_Refused]: the handle "
                                             "factory opens a session only over a Resource that a mint admits.");
    static_assert(std::is_void_v<LoopCtx>
                      || std::is_same_v<LoopCtx, detail::session_brand<typename[:detail::brand_of(^^LoopCtx):], void>>,
                  "fixy::session::diagnostic [Protocol_Ill_Formed]: the first handle of a session has no loop "
                  "context, or the brand of the body that owns the session.");
    static_assert(PermissionFlowCloses<Proto, PS>,
                  "fixy::session::diagnostic [PermissionImbalance]: a path of the protocol sends a region that the "
                  "permission set does not hold, receives a second owner of a region, changes the set in one loop "
                  "iteration, or reaches End with an open loan.  The walk visits every branch, so an arm that no "
                  "run selects counts too.");
    return step_<Proto, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r), session, loc);
}

template <typename Proto, typename Resource, AbandonmentPolicy Policy>
constexpr watch::session_ref HandleFactory::claim_(std::source_location loc) noexcept {
    watch::session_ref session{};
    if constexpr (Policy::checks_abandonment) {
        if !consteval {
            const watch::endpoint_id endpoint = watch::claim(type_display_name_v<Proto>, loc,
                                                             session_priority_v<Resource>,
                                                             watch::holder_on_claim::calling_thread);
            session = {endpoint, watch::current_thread_slot()};
        }
    }
    return session;
}

// Opens a session that has no record yet, and claims one for it with the
// priority of its Resource.  The calling thread holds the first handle.
template <typename Proto, typename Resource, AbandonmentPolicy Policy, typename PS, typename LoopCtx>
constexpr auto HandleFactory::open_(SessionOpenKey const&, Resource r, std::source_location loc) noexcept {
    return start_<Proto, Resource, Policy, PS, LoopCtx>(std::forward<Resource>(r), loc,
                                                        claim_<Proto, Resource, Policy>(loc));
}

// Opens a session on a record that a channel mint claimed and linked to
// its peer.  The mint claimed it with no holder, and can run on a
// different thread, and the calling thread records itself as the holder.
template <typename Proto, typename Resource, AbandonmentPolicy Policy, typename PS, typename LoopCtx>
constexpr auto HandleFactory::open_(SessionOpenKey const&, Resource r, std::source_location loc,
                                    watch::endpoint_id endpoint) noexcept {
    watch::session_ref session{};
    if constexpr (Policy::checks_abandonment) {
        if !consteval {
            session = {endpoint, watch::note_holder(endpoint)};
        }
    }
    return start_<Proto, Resource, Policy, PS, LoopCtx>(std::forward<Resource>(r), loc, session);
}

template <typename R, typename LoopCtx, typename Resource, typename EndLoopCtx, AbandonmentPolicy Policy>
    requires RewindableTo<R, LoopCtx, EndLoopCtx>
constexpr auto
HandleFactory::rewind(SessionHandle<End, Resource, EndLoopCtx, Policy, ::foundation::permissions::EmptyPermSet>&& at_end,
                      std::source_location loc) noexcept {
    Resource resource = std::move(at_end).close();
    return step_<R, Resource, LoopCtx, Policy, ::foundation::permissions::EmptyPermSet>(
        std::forward<Resource>(resource), claim_<R, Resource, Policy>(loc), loc);
}

namespace detail {

// Claims the two linked records of a channel, one for each end, with the
// one priority of the channel.  The thread that claims them does not hold
// the ends: each end's holder is the thread that opens it.  Under
// check::Off it claims nothing.
template <typename SelfProto, typename PeerProto, AbandonmentPolicy Policy, watch::priority Order>
[[nodiscard]] inline std::pair<watch::endpoint_id, watch::endpoint_id> claim_channel_(std::source_location loc) noexcept {
    if constexpr (Policy::checks_abandonment) {
        const watch::endpoint_id self =
            watch::claim(type_display_name_v<SelfProto>, loc, Order, watch::holder_on_claim::none_yet);
        const watch::endpoint_id peer =
            watch::claim(type_display_name_v<PeerProto>, loc, Order, watch::holder_on_claim::none_yet);
        watch::link(self, peer);
        return {self, peer};
    } else {
        return {watch::endpoint_id::none, watch::endpoint_id::none};
    }
}

template <typename Proto, typename Resource, AbandonmentPolicy Policy,
          typename PS = ::foundation::permissions::EmptyPermSet, typename LoopCtx = void>
using first_handle_t = HandleFactory::first_handle<Proto, Resource, Policy, PS, LoopCtx>;

template <typename Brand>
using brand_ctx_t = session_brand<Brand, void>;

// Names the class Door through a type that depends on Dep.  A mint body
// that names a door defined after the mint uses this alias.  The lookup
// into the door then waits for the instantiation, where the door is
// complete.
template <typename Door, typename Dep>
struct late_door_ {
    using type = Door;
};
template <typename Door, typename Dep>
using late_door_t = typename late_door_<Door, Dep>::type;

}  // namespace detail

// The body static_asserts repeat the concept's checks.  They fire only
// if some route reaches the body without the concept having run, and
// their prose is what explains a rejection the concept states only as a
// boolean.

template <typename Proto, typename Resource, AbandonmentPolicy Policy = DefaultAbandonmentPolicy>
    requires WellFormedRunnableProtocol<Proto> && SessionResource<Resource>
          && PermissionFlowCloses<Proto, ::foundation::permissions::EmptyPermSet>
[[nodiscard]] constexpr auto mint_session_handle(Resource r,
                                                 std::source_location loc = std::source_location::current()) noexcept {
    static_assert(is_well_formed_v<Proto>, "fixy::session::diagnostic [Protocol_Ill_Formed]: "
                                           "protocol is ill-formed.  Most likely cause: a Continue "
                                           "appears outside any enclosing Loop<Body>.  Every Continue must "
                                           "have a Loop above it in the protocol tree.");

    // is_well_formed refuses an empty choice too.  This assertion names
    // the fault, and the assertion above gives the general refusal.
    static_assert(!is_empty_choice_v<Proto>, "fixy::session::diagnostic [Empty_Choice_Combinator]: "
                                             "mint_session_handle<Proto> — Proto contains a "
                                             "reachable empty Select<> / Offer<> / Offer<Sender<R>> "
                                             "(top-level or nested under Send/Recv/Loop/branch).  "
                                             "Cannot construct a runnable handle: Select<> has "
                                             "no branch for select<I>() to choose; Offer<> has no label "
                                             "the peer can signal.  The trait walks recursively so "
                                             "nested empties are caught at mint time, not at the "
                                             "eventual select<I>() / recv() that hits the dead-end.  "
                                             "Subtyping refuses an empty choice as well, because a "
                                             "substitute of that type never sends.  Add one branch or "
                                             "more at every reachable choice position, for example "
                                             "Select<Send<Stop, End>>.");

    static_assert(SessionResource<Resource>, "fixy::session::diagnostic [SessionResource_Refused]: "
                                             "mint_session_handle<Proto, Resource>: the Resource must be a "
                                             "value the handle owns, or an lvalue reference to a type "
                                             "derived from foundation::Pinned<T>.  A value that can be "
                                             "copied must be fixy::SelfContained: a copy of a Resource that "
                                             "reaches a channel is a second channel to the peer.  Either: "
                                             "(a) give the Resource a [[no_unique_address]] "
                                             "fixy::session::MoveOnlyResource member, or (b) make the "
                                             "channel Pinned and pass it by lvalue reference.  A raw "
                                             "pointer, a function pointer and a std::reference_wrapper are "
                                             "refused.  An rvalue-reference Resource binds to a temporary "
                                             "and is refused too.");

    static_assert(!std::is_same_v<Proto, Continue>, "fixy::session::diagnostic [Continue_Without_Loop]: "
                                                    "Continue cannot be the top-level protocol.");

    // Forwarding `loc` keeps the caller's site in the abandonment
    // diagnostic even though the unroll inserts an intermediate handle
    // whose own default location would otherwise win.
    return detail::late_door_t<SessionMintDoor, Proto>::template open<Proto, Resource, Policy>(
        std::forward<Resource>(r), loc);
}

// ── A session that starts with permissions ───────────────────────────
//
// The permission set of a handle names the regions whose tokens its
// messages can move.  mint_session_handle starts with the empty set.
// mint_permissioned_session consumes one token for each tag, and starts
// the session with the set of those tags.  It returns the handle and a
// PermHold of the same set.  The handle carries the set as a type, and
// the hold carries the tokens.  The caller takes a token from the hold
// for each message that moves one.  This is the CSL frame rule at the
// start of a session: the set of the handle is backed by tokens that no
// other holder has, and each region has one owner (Actris per-message
// resources).
//
// The gate is one concept, CtxFitsSessionFrom, for every set of tags.  The
// context is an execution context, and it admits the permission row of
// each tag (foundation/permissions/Permission.h).  The tags are distinct.
// The protocol is runnable, the Resource is a SessionResource, and the
// permission flow of the whole protocol closes from the set of the
// tokens, on every branch.  The context holds each effect that a payload
// of the protocol carries, and the row of each permission that the
// protocol delivers to the endpoint (CtxAdmitsProtocolRow).  This mint
// asks for one tag or more.  mint_session of fixy/session/Entry.h reads
// the same concept with no tag.
//
// The tokens are the last parameters, so the mint cannot take a
// source_location after them.  The session records the site of the mint.
// Complexity: linear in the size of the protocol, at compile time.

// ── The effect row of a protocol ────────────────────────────────────
//
// A context that starts a session must hold each effect that a message
// of the session carries.  The row of a protocol is the union of the
// rows of its payloads, over each Send and each Recv, on each branch, and
// in each protocol that a DelegatedSession carries.  A received payload
// counts as a sent one does, because the receiver then holds what the
// payload carries.  The old tree checked the two directions the same way.
//
// The walk is the payload walk of fixy/concurrent/PayloadRow.h, with the
// rules of this layer for the families that its rosters do not name:
//
//   - a marker, DeclassifyOnSend and CTPayload carry their value,
//     argument 0.  The token or the loan of a marker is no payload.
//   - PeerMsg and Labelled carry their payload, the last argument.  The
//     peer and the label are names.
//   - a DelegatedSession carries the protocol of its endpoint, argument
//     0.  Its Resource, its policy and its permission set are not
//     messages of that protocol.
//   - a SharedReader, a crash record and a bare Permission carry no row.
//   - each combinator of the registry of fixy/session/Protocol.h carries
//     each of its type arguments, and the note of a choice carries none,
//     because its role is a name.
//
// A payload that the walk cannot classify stops the build, and the text
// names it.  The walk stands here and not in fixy/session/Payload.h, so
// a header that reads only the payload rules does not include the payload
// walk.  Complexity: linear in the distinct types that the protocol and
// its payloads reach.
//
// A receive can also give the endpoint a permission: a token, a read loan
// or a returned token of a region.  A touch of that region incurs the row
// of its tag, so the row of the protocol also holds the permission row of
// each region that the protocol delivers to the endpoint
// (protocol_delivered_regions of fixy/session/Payload.h).  A mint checks
// the row of each token that it consumes.  This check covers each token
// that arrives after the mint, in a message or in a delegated endpoint.

namespace detail {

[[nodiscard]] consteval std::vector<::fixy::concurrent::payload_family_rule> session_payload_rules() {
    using Rule = ::fixy::concurrent::payload_family_rule;
    constexpr std::uint64_t carries_first = std::uint64_t{1};
    constexpr std::uint64_t carries_nothing = std::uint64_t{0};
    constexpr std::uint64_t carries_every = ~std::uint64_t{0};
    std::vector<Rule> rules{
        Rule{^^Transferable, carries_first},
        Rule{^^Returned, carries_first},
        Rule{^^Borrowed, carries_first},
        Rule{^^Released, carries_first},
        Rule{^^DeclassifyOnSend, carries_first},
        Rule{^^CTPayload, carries_first},
        Rule{^^PeerMsg, std::uint64_t{1} << 2},
        Rule{^^Labelled, std::uint64_t{1} << 1},
        Rule{^^ContentAddressed, carries_first},
        Rule{^^DelegatedSession, carries_first},
        Rule{^^SharedReader, carries_nothing},
        Rule{^^Crash, carries_nothing},
        Rule{^^::foundation::permissions::Permission, carries_nothing},
    };
    const auto has_rule = [&rules](std::meta::info family) consteval {
        for (const Rule& rule : rules) {
            if (rule.family == family) return true;
        }
        return false;
    };
    for (const std::meta::info member : std::meta::members_of(protocol_registry, std::meta::access_context::current())) {
        if (!std::meta::is_variable(member)) continue;
        if (std::meta::remove_cvref(std::meta::type_of(member)) != ^^::foundation::algebra::transition::combinator)
            continue;
        const auto entry = std::meta::extract<::foundation::algebra::transition::combinator>(member);
        if (std::meta::is_class_template(entry.shape) && !has_rule(entry.shape)) {
            rules.push_back(Rule{entry.shape, carries_every});
        }
        if (entry.annotation != std::meta::info{} && !has_rule(entry.annotation)) {
            rules.push_back(Rule{entry.annotation, carries_nothing});
        }
    }
    return rules;
}

}  // namespace detail

// The rules of this layer for the payload walk.
inline constexpr std::span<const ::fixy::concurrent::payload_family_rule> session_payload_families =
    std::define_static_array(detail::session_payload_rules());

// The union of the effect rows of the payloads of Proto.
template <class Proto>
using protocol_payload_row_t = ::fixy::concurrent::payload_row_under_t<Proto, session_payload_families>;

namespace detail {

[[nodiscard]] consteval std::string_view delivered_region_without_row_text(std::meta::info region) {
    std::string text{"fixy::session::diagnostic [Delivered_Region_Without_Row]: a receive of the protocol delivers a "
                     "permission of the region "};
    text += std::meta::display_string_of(region);
    text += ", and its tag declares no permission row.  No program can mint a token for that tag, and the gate "
            "cannot name the effects that a touch of the region incurs.  Declare the row of the tag, as "
            "foundation/permissions/Permission.h states.";
    return std::define_static_string(text);
}

// The row of a delivered region, or the empty row when its tag declares
// none.  The primary does not name permission_row_t, so a tag with no row
// gives one diagnostic, from union_of_permission_rows below.
template <class Region, bool = ::foundation::permissions::has_permission_row_v<Region>>
struct declared_permission_row {
    using type = ::foundation::effects::Row<>;
};

template <class Region>
struct declared_permission_row<Region, true> {
    using type = ::foundation::permissions::permission_row_t<Region>;
};

template <class Row, class... Regions>
struct union_of_permission_rows {
    using type = Row;
};

template <class Row, class First, class... Rest>
struct union_of_permission_rows<Row, First, Rest...> {
    static_assert(::foundation::permissions::has_permission_row_v<First>, delivered_region_without_row_text(^^First));
    using type = typename union_of_permission_rows<
        ::foundation::effects::row_union_t<Row, typename declared_permission_row<First>::type>, Rest...>::type;
};

template <class Regions>
struct permission_rows_of_set;

template <class... Regions>
struct permission_rows_of_set<::foundation::permissions::PermSet<Regions...>>
    : union_of_permission_rows<::foundation::effects::Row<>, Regions...> {};

}  // namespace detail

// The union of the permission rows of the regions that Proto can deliver
// to the endpoint that runs it (protocol_delivered_regions of
// fixy/session/Payload.h).  A delivered tag that declares no row stops
// the build, because no program can mint a token for it.
template <class Proto>
using protocol_delivered_permission_row_t =
    typename detail::permission_rows_of_set<protocol_delivered_regions_t<Proto>>::type;

// The context admits the effect row of Proto: the row of each payload,
// and the permission row of each region that Proto delivers to the
// endpoint.  A payload that the walk cannot classify stops the build.
template <typename Ctx, typename Proto>
concept CtxAdmitsProtocolRow =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::is_subrow_v<protocol_payload_row_t<Proto>, typename Ctx::row_type>
    && ::foundation::effects::is_subrow_v<protocol_delivered_permission_row_t<Proto>, typename Ctx::row_type>;

namespace detail {

// The gate of HandleFactory::recover: branch I of the Offer exists and is
// no label, the permission set is empty, a session can start at the branch
// in the loop context, and the context admits the effect row of the branch.
template <typename Ctx, typename Choice, std::size_t I, typename LoopCtx, typename PS>
struct recover_gate : std::false_type {};

template <typename Ctx, typename... Branches, std::size_t I, typename LoopCtx, typename PS>
    requires(I < Offer<Branches...>::branch_count)
struct recover_gate<Ctx, Offer<Branches...>, I, LoopCtx, PS>
    : std::bool_constant<!wire_words_v<Offer<Branches...>>[I].is_wired && perm_set_is_empty_v<PS>
                         && RewindableTo<std::tuple_element_t<I, typename Offer<Branches...>::branches_tuple>,
                                         LoopCtx, LoopCtx>
                         && CtxAdmitsProtocolRow<
                             Ctx, std::tuple_element_t<I, typename Offer<Branches...>::branches_tuple>>> {};

}  // namespace detail

template <typename Ctx, typename Proto, typename Resource, typename... Tags>
concept CtxFitsSessionFrom =
    ::foundation::effects::IsExecCtx<Ctx> && ::foundation::permissions::detail::perm_tags_unique_v<Tags...>
    && (::foundation::permissions::CtxAdmitsPermission<Tags, Ctx> && ...) && WellFormedRunnableProtocol<Proto>
    && SessionResource<Resource> && PermissionFlowCloses<Proto, ::foundation::permissions::PermSet<Tags...>>
    && CtxAdmitsProtocolRow<Ctx, Proto>;

template <typename Ctx, typename Proto, typename Resource, typename... Tags>
concept CtxFitsPermissionedSession = sizeof...(Tags) != 0 && CtxFitsSessionFrom<Ctx, Proto, Resource, Tags...>;

template <typename Proto, AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Ctx, typename Resource,
          typename... Tags, typename... Brands>
    requires CtxFitsPermissionedSession<Ctx, Proto, Resource, Tags...>
[[nodiscard]] constexpr auto mint_permissioned_session(Ctx const& ctx, Resource resource,
                                                       ::foundation::permissions::Permission<Tags, Brands>... tokens) noexcept {
    return detail::late_door_t<SessionMintDoor, Proto>::template open_permissioned<Proto, Policy>(
        ctx, std::forward<Resource>(resource), std::move(tokens)...);
}

// ── The callback entry point ─────────────────────────────────────────
//
// The library owns the handle.  It builds the first handle, gives it to
// the body, takes back the handle that the body returns, and closes it.
// The body must return a handle at End, so a body that drops the handle
// or stops early does not compile.  This is the design of Thiemann
// (ICFP 2023): the shape of the API makes the handle linear.
//
// Every handle of the session carries the type of the body as its brand,
// and the entry point accepts back only a handle with that brand.  A body
// cannot mint a new session, walk it to End and return it in its place,
// because no public mint makes a branded handle.

// True when H is a handle at End of the session that Brand owns, and its
// close() gives back the Resource.
template <typename H, typename Resource, typename Brand>
concept ClosesInSessionOf = std::is_same_v<std::remove_cvref_t<H>, H> && requires { typename H::loop_ctx; }
                         && detail::carries_brand<typename H::loop_ctx, Brand>
                         && requires(H handle) {
                                { std::move(handle).close() } -> std::same_as<Resource>;
                            };

template <typename Body, typename Proto, typename Resource, typename Policy>
concept SessionBody =
    std::is_invocable_v<Body, detail::first_handle_t<Proto, Resource, Policy, ::foundation::permissions::EmptyPermSet,
                                                     detail::brand_ctx_t<Body>>>
    && ClosesInSessionOf<std::invoke_result_t<Body, detail::first_handle_t<Proto, Resource, Policy,
                                                                           ::foundation::permissions::EmptyPermSet,
                                                                           detail::brand_ctx_t<Body>>>,
                         Resource, Body>;

template <typename Proto, typename Resource, AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Body>
    requires WellFormedRunnableProtocol<Proto> && SessionResource<Resource>
          && PermissionFlowCloses<Proto, ::foundation::permissions::EmptyPermSet>
          && SessionBody<Body, Proto, Resource, Policy>
[[nodiscard]] constexpr Resource
with_session(Resource r, Body body, std::source_location loc = std::source_location::current()) noexcept(
    std::is_nothrow_invocable_v<Body, detail::first_handle_t<Proto, Resource, Policy,
                                                             ::foundation::permissions::EmptyPermSet,
                                                             detail::brand_ctx_t<Body>>>) {
    return detail::late_door_t<SessionMintDoor, Proto>::template run<Proto, Resource, Policy>(std::forward<Resource>(r),
                                                                                             std::move(body), loc);
}

// ── Channels ─────────────────────────────────────────────────────────
//
// A channel has two endpoints.  If one thread holds the two endpoints, it
// can wait on one for a message that only the other can send, and it
// waits forever.  LinearActris excludes this by the forest condition:
// the program makes a channel only when it starts a thread, and gives
// one endpoint to each side.  mint_forked_channel is that shape.  No
// public mint in this header gives the two endpoints to one caller.

template <typename Proto, typename ResourceA, typename ResourceB>
void mint_channel(ResourceA, ResourceB) noexcept =
    delete("[Channel_Needs_A_Fork] mint_channel<Proto>(resource_a, resource_b) is removed.  It gives the two "
           "endpoints of one channel to one caller, and a caller that holds the two endpoints can wait on one of "
           "them for a message that only the other can send.  Use mint_forked_channel<Proto, SelfTag, "
           "PeerTag>(ctx, parent, resource_self, resource_peer, self_body, peer_body), which gives each endpoint "
           "to its own thread.  A one-thread test that needs the two endpoints uses mint_test_channel with a "
           "test context.");

namespace detail {

// The first handle of one side of a forked channel, branded with the
// body of that side.
template <typename Proto, typename Resource, typename Policy, typename Body>
using forked_head_t =
    first_handle_t<Proto, Resource, Policy, ::foundation::permissions::EmptyPermSet, brand_ctx_t<Body>>;

// The body borrows its side of the parent through the fork's view, of the
// parent's brand.  It never holds the token, so it cannot keep the side
// past the join.
template <typename Body, typename Proto, typename Resource, typename Policy, typename Tag, typename Brand, typename Ctx>
concept ForkedEndpointBody =
    std::is_nothrow_invocable_v<Body, forked_head_t<Proto, Resource, Policy, Body>,
                                ::foundation::permissions::WriteView<Tag, Brand> const&, Ctx const&>
    && ClosesInSessionOf<std::invoke_result_t<Body, forked_head_t<Proto, Resource, Policy, Body>,
                                              ::foundation::permissions::WriteView<Tag, Brand> const&, Ctx const&>,
                         Resource, Body>;

}  // namespace detail

// The row gate of a channel.  Each channel mint gives one context to the
// two sides of the channel.  So the context must admit the row of each
// side, as CtxFitsSessionFrom asks for the one side of a session.  Each
// permission that one side sends arrives at the other side, and the row
// of the other side holds the row of that permission.  Every
// channel mint reads this concept, so the channel mints and the session
// mints cannot drift apart.
template <typename Ctx, typename SelfProto, typename PeerProto>
concept CtxAdmitsChannelRow = CtxAdmitsProtocolRow<Ctx, SelfProto> && CtxAdmitsProtocolRow<Ctx, PeerProto>;

// The context gate of the fork-shaped mint: two runnable local
// protocols, one the dual of the other, a context that admits the row of
// each side, and a fork that the context may start.
template <typename Ctx, typename Proto, typename Parent, typename SelfTag, typename PeerTag>
concept CtxFitsForkedChannel = WellFormedRunnableProtocol<Proto> && WellFormedRunnableProtocol<dual_of_t<Proto>>
                            && PermissionFlowCloses<Proto, ::foundation::permissions::EmptyPermSet>
                            && PermissionFlowCloses<dual_of_t<Proto>, ::foundation::permissions::EmptyPermSet>
                            && CtxAdmitsChannelRow<Ctx, Proto, dual_of_t<Proto>>
                            && ::foundation::permissions::CtxFitsPermissionFork<Ctx, Parent, SelfTag, PeerTag>;

// Makes a channel and starts its two sides on two threads through
// foundation's permission_fork.  The parent permission splits into
// SelfTag and PeerTag.  The self body runs Proto over self_resource, and
// the peer body runs the dual over peer_resource.  Each body gets its
// endpoint, the fork's view of its child permission and the context, and
// must return its endpoint at End.  The call returns the parent
// permission after the two threads join.
//
// No thread holds the two endpoints when the mint makes them.  Each
// endpoint exists in the thread that runs its body, so ownership is a
// tree: the caller owns the two threads, and each thread owns one
// endpoint.  This is the shape the forest condition of LinearActris asks
// for.  A body can still move its handle to another thread through
// shared memory, and then the forest condition does not hold.  The
// brand makes the body get its own handle back before it can return.
template <typename Proto, typename SelfTag, typename PeerTag, AbandonmentPolicy Policy = DefaultAbandonmentPolicy,
          typename Ctx, typename Parent, typename Brand, typename ResourceSelf, typename ResourcePeer,
          typename SelfBody, typename PeerBody>
    requires CtxFitsForkedChannel<Ctx, Proto, Parent, SelfTag, PeerTag> && SessionResource<ResourceSelf>
          && SessionResource<ResourcePeer> && ChannelEndsShareAPriority<ResourceSelf, ResourcePeer>
          && detail::ForkedEndpointBody<SelfBody, Proto, ResourceSelf, Policy, SelfTag, Brand, Ctx>
          && detail::ForkedEndpointBody<PeerBody, dual_of_t<Proto>, ResourcePeer, Policy, PeerTag, Brand, Ctx>
// §XXI carve-out: cx=alloc — starting a thread is a kernel side effect.
[[nodiscard]] ::foundation::permissions::Permission<Parent, Brand>
mint_forked_channel(Ctx const& ctx, ::foundation::permissions::Permission<Parent, Brand>&& parent,
                    ResourceSelf self_resource, ResourcePeer peer_resource, SelfBody self_body, PeerBody peer_body,
                    std::source_location loc = std::source_location::current()) noexcept {
    return detail::late_door_t<SessionMintDoor, Proto>::template fork_channel<Proto, dual_of_t<Proto>, SelfTag, PeerTag,
                                                                              Policy>(
        ctx, std::move(parent), std::forward<ResourceSelf>(self_resource), std::forward<ResourcePeer>(peer_resource),
        std::move(self_body), std::move(peer_body), loc);
}

// The one form that gives the two endpoints to one caller.  It is a
// hatch for a test that drives the two sides on one thread in a fixed
// order, and the context gate admits only a context that holds the Test
// capability.  A production context has no Test capability, so it
// cannot reach this mint.  The context must also admit the row of each
// side, as the context of a forked channel must.
template <typename Ctx, typename Proto>
concept CtxFitsTestChannel = ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Test>
                          && WellFormedRunnableProtocol<Proto> && WellFormedRunnableProtocol<dual_of_t<Proto>>
                          && PermissionFlowCloses<Proto, ::foundation::permissions::EmptyPermSet>
                          && PermissionFlowCloses<dual_of_t<Proto>, ::foundation::permissions::EmptyPermSet>
                          && CtxAdmitsChannelRow<Ctx, Proto, dual_of_t<Proto>>;

template <typename Proto, AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Ctx, typename ResourceA,
          typename ResourceB>
    requires CtxFitsTestChannel<Ctx, Proto> && SessionResource<ResourceA> && SessionResource<ResourceB>
          && ChannelEndsShareAPriority<ResourceA, ResourceB>
[[nodiscard]] constexpr auto mint_test_channel(Ctx const& ctx, ResourceA resource_a, ResourceB resource_b,
                                               std::source_location loc = std::source_location::current()) noexcept {
    return detail::late_door_t<SessionMintDoor, Proto>::template open_test_channel<Proto, Policy>(
        ctx, std::forward<ResourceA>(resource_a), std::forward<ResourceB>(resource_b), loc);
}

// The relation between the two sides of a forked channel.  The sides are
// exact duals, or each side refines the dual of the other side at the
// capacity that the two Resources state (fixy/session/Subtype.h).  The
// relation asks for both directions because refinement keeps each exit,
// and duality does not keep that property.  mint_forked_async_channel of
// fixy/session/AsyncChannel.h states the second form with a diagnostic
// for each clause.
template <typename SelfProto, typename PeerProto, typename ResourceSelf, typename ResourcePeer>
concept ForkedSidesAgree =
    std::is_same_v<PeerProto, dual_of_t<SelfProto>>
    || (StatesChannelCapacity<ResourceSelf> && StatesChannelCapacity<ResourcePeer>
        && (channel_capacity_v<ResourceSelf> == channel_capacity_v<ResourcePeer>)
        && is_subtype_async_v<SelfProto, dual_of_t<PeerProto>, ResourceSelf>
        && is_subtype_async_v<PeerProto, dual_of_t<SelfProto>, ResourcePeer>);

// The whole gate of a fork-shaped channel: two runnable sides whose
// permission flow closes, a context that admits the row of each side and
// may start the fork, two admitted Resources of one priority, a relation
// between the sides, and a body for each side that returns its endpoint
// at End.
template <typename Ctx, typename SelfProto, typename PeerProto, typename Parent, typename Brand, typename SelfTag,
          typename PeerTag, typename Policy, typename ResourceSelf, typename ResourcePeer, typename SelfBody,
          typename PeerBody>
concept CtxFitsForkedSides =
    WellFormedRunnableProtocol<SelfProto> && WellFormedRunnableProtocol<PeerProto>
    && PermissionFlowCloses<SelfProto, ::foundation::permissions::EmptyPermSet>
    && PermissionFlowCloses<PeerProto, ::foundation::permissions::EmptyPermSet>
    && CtxAdmitsChannelRow<Ctx, SelfProto, PeerProto>
    && ::foundation::permissions::CtxFitsPermissionFork<Ctx, Parent, SelfTag, PeerTag>
    && SessionResource<ResourceSelf> && SessionResource<ResourcePeer>
    && ChannelEndsShareAPriority<ResourceSelf, ResourcePeer>
    && ForkedSidesAgree<SelfProto, PeerProto, ResourceSelf, ResourcePeer>
    && detail::ForkedEndpointBody<SelfBody, SelfProto, ResourceSelf, Policy, SelfTag, Brand, Ctx>
    && detail::ForkedEndpointBody<PeerBody, PeerProto, ResourcePeer, Policy, PeerTag, Brand, Ctx>;

// ── The door of the mints ────────────────────────────────────────────
//
// The one friend of SessionOpenKey, so the one class that opens a session
// on the handle factory.  Each public member is one mint of this layer
// and states the whole gate of that mint in its requires clause, so a
// direct call of a member is no weaker than the mint.  The mints above
// forward here for their names and their default arguments, through
// detail::late_door_t, because the class stands after them.  No object of
// the class exists.
class SessionMintDoor final {
    SessionMintDoor() = delete("the mint door holds static members only; no object of it exists");
    SessionMintDoor(const SessionMintDoor&) = delete("the mint door holds static members only");
    SessionMintDoor& operator=(const SessionMintDoor&) = delete("the mint door holds static members only");
    SessionMintDoor(SessionMintDoor&&) = delete("the mint door holds static members only");
    SessionMintDoor& operator=(SessionMintDoor&&) = delete("the mint door holds static members only");
    constexpr ~SessionMintDoor() noexcept {}

    // One side of a forked channel.  It builds the endpoint in the thread
    // that runs it, gives the endpoint and the fork's view of its child
    // permission to the body, and closes the handle that the body returns.
    // A struct and not a lambda, because a lambda capture cannot hold a
    // reference Resource by its declared type.  It is a private member, and
    // only fork_channel builds one.
    template <typename Proto, AbandonmentPolicy Policy, typename Resource, typename Body>
    struct forked_endpoint_ {
        Resource resource;
        Body body;
        std::source_location loc;
        // The record the channel mint claimed for this end, linked to the
        // record of the other end.
        watch::endpoint_id endpoint = watch::endpoint_id::none;

        template <typename View, typename Ctx>
        void operator()(View const& view, Ctx const& ctx) noexcept {
            auto head = HandleFactory::open_<Proto, Resource, Policy, ::foundation::permissions::EmptyPermSet,
                                             detail::brand_ctx_t<Body>>(
                SessionOpenKey{}, std::forward<Resource>(resource), loc, endpoint);
            auto at_end = std::invoke(std::move(body), std::move(head), view, ctx);
            static_cast<void>(std::move(at_end).close());
        }
    };

public:
    // mint_session_handle: a session with the empty permission set.
    template <typename Proto, typename Resource, AbandonmentPolicy Policy>
        requires WellFormedRunnableProtocol<Proto> && SessionResource<Resource>
              && PermissionFlowCloses<Proto, ::foundation::permissions::EmptyPermSet>
    [[nodiscard]] static constexpr auto open(Resource r, std::source_location loc) noexcept {
        return HandleFactory::open_<Proto, Resource, Policy, ::foundation::permissions::EmptyPermSet>(
            SessionOpenKey{}, std::forward<Resource>(r), loc);
    }

    // mint_permissioned_session: consumes one token for each tag, and
    // opens the session with the set of those tags.
    template <typename Proto, AbandonmentPolicy Policy, typename Ctx, typename Resource, typename... Tags,
              typename... Brands>
        requires CtxFitsPermissionedSession<Ctx, Proto, Resource, Tags...>
    [[nodiscard]] static constexpr auto
    open_permissioned(Ctx const&, Resource resource,
                      ::foundation::permissions::Permission<Tags, Brands>... tokens) noexcept {
        using Set = ::foundation::permissions::PermSet<Tags...>;
        auto hold = mint_permission_hold(std::move(tokens)...);
        auto head = HandleFactory::open_<Proto, Resource, Policy, Set>(SessionOpenKey{}, std::forward<Resource>(resource),
                                                                      std::source_location::current());
        return std::pair{std::move(head), std::move(hold)};
    }

    // with_session: runs the body on the first handle of a session that
    // carries the brand of the body, and closes the End handle that the
    // body returns.
    template <typename Proto, typename Resource, AbandonmentPolicy Policy, typename Body>
        requires WellFormedRunnableProtocol<Proto> && SessionResource<Resource>
              && PermissionFlowCloses<Proto, ::foundation::permissions::EmptyPermSet>
              && SessionBody<Body, Proto, Resource, Policy>
    [[nodiscard]] static constexpr Resource run(Resource r, Body body, std::source_location loc) noexcept(
        std::is_nothrow_invocable_v<Body, detail::first_handle_t<Proto, Resource, Policy,
                                                                 ::foundation::permissions::EmptyPermSet,
                                                                 detail::brand_ctx_t<Body>>>) {
        auto at_end = std::invoke(
            std::move(body),
            HandleFactory::open_<Proto, Resource, Policy, ::foundation::permissions::EmptyPermSet, detail::brand_ctx_t<Body>>(
                SessionOpenKey{}, std::forward<Resource>(r), loc));
        return std::move(at_end).close();
    }

    // mint_forked_channel and mint_forked_async_channel: claims the two
    // linked records of a channel and starts its two sides on two threads.
    // No thread holds the two endpoints.
    template <typename SelfProto, typename PeerProto, typename SelfTag, typename PeerTag, AbandonmentPolicy Policy,
              typename Ctx, typename Parent, typename Brand, typename ResourceSelf, typename ResourcePeer,
              typename SelfBody, typename PeerBody>
        requires CtxFitsForkedSides<Ctx, SelfProto, PeerProto, Parent, Brand, SelfTag, PeerTag, Policy, ResourceSelf,
                                    ResourcePeer, SelfBody, PeerBody>
    [[nodiscard]] static ::foundation::permissions::Permission<Parent, Brand>
    fork_channel(Ctx const& ctx, ::foundation::permissions::Permission<Parent, Brand>&& parent,
                 ResourceSelf self_resource, ResourcePeer peer_resource, SelfBody self_body, PeerBody peer_body,
                 std::source_location loc) noexcept {
        using SelfSide = forked_endpoint_<SelfProto, Policy, ResourceSelf, SelfBody>;
        using PeerSide = forked_endpoint_<PeerProto, Policy, ResourcePeer, PeerBody>;
        const auto [self_endpoint, peer_endpoint] =
            detail::claim_channel_<SelfProto, PeerProto, Policy, session_priority_v<ResourceSelf>>(loc);
        return ::foundation::permissions::mint_permission_fork<SelfTag, PeerTag>(
            ctx, std::move(parent),
            SelfSide{std::forward<ResourceSelf>(self_resource), std::move(self_body), loc, self_endpoint},
            PeerSide{std::forward<ResourcePeer>(peer_resource), std::move(peer_body), loc, peer_endpoint});
    }

    // mint_test_channel: the two endpoints of one channel to one caller, for
    // a context that holds the Test capability.
    template <typename Proto, AbandonmentPolicy Policy, typename Ctx, typename ResourceA, typename ResourceB>
        requires CtxFitsTestChannel<Ctx, Proto> && SessionResource<ResourceA> && SessionResource<ResourceB>
              && ChannelEndsShareAPriority<ResourceA, ResourceB>
    [[nodiscard]] static constexpr auto open_test_channel(Ctx const&, ResourceA resource_a, ResourceB resource_b,
                                                          std::source_location loc) noexcept {
        std::pair<watch::endpoint_id, watch::endpoint_id> endpoints{watch::endpoint_id::none,
                                                                    watch::endpoint_id::none};
        if !consteval {
            endpoints = detail::claim_channel_<Proto, dual_of_t<Proto>, Policy, session_priority_v<ResourceA>>(loc);
        }
        return std::pair{HandleFactory::open_<Proto, ResourceA, Policy, ::foundation::permissions::EmptyPermSet>(
                             SessionOpenKey{}, std::forward<ResourceA>(resource_a), loc, endpoints.first),
                         HandleFactory::open_<dual_of_t<Proto>, ResourceB, Policy,
                                              ::foundation::permissions::EmptyPermSet>(
                             SessionOpenKey{}, std::forward<ResourceB>(resource_b), loc, endpoints.second)};
    }
};

}  // namespace fixy::session
