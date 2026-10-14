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
// This header has one handle family, for a handle that holds no
// permission and for a handle that holds permissions.  The permission set
// PS is the last template parameter, and it is a type only, so it costs no
// byte.
// `Handle<Proto, PS, Resource, LoopCtx, Policy>` is the same class with
// the parameters in the order the design names them.
//
// PS changes at each step.  A send removes the permissions the message
// takes, and the matching receive adds them.  A Continue must see the PS
// that the loop saw at its entry.  A handle cannot reach End while PS
// holds an open loan.  The ownership delta of one message comes from
// fixy/session/Flow.h, and fixy/session/Admission.h reads it in one
// place, detail::perm_set_after_send_t and detail::perm_set_after_recv_t.
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
// callable they pass.  A callable has one of the shapes of
// fixy/session/Transport.h, and no shape waits where the watch of
// fixy/session/Watch.h cannot see the wait.
//
// ── The word of a choice ────────────────────────────────────────────
//
// select<I>() gives the Transport the wire word of branch I, and branch()
// dispatches on the word that its Transport reads.  The word comes from
// fixy/session/Wire.h.  In a keyed choice, where each label
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
//      with_session of fixy/session/Mint.h gives it at the type level for
//      its body.
//   2. Threads and channels form a forest.  mint_forked_channel of
//      fixy/session/Mint.h gives this: it makes a channel only as part of
//      a fork, and each endpoint goes to its own thread.
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

#include <fixy/concurrent/PermissionFork.h>
#include <fixy/session/Admission.h>
#include <fixy/session/Flow.h>
#include <fixy/session/NetworkModel.h>
#include <fixy/session/Protocol.h>
#include <fixy/session/Registry.h>
#include <fixy/session/Stepping.h>
#include <fixy/session/Transport.h>
#include <fixy/session/Watch.h>
#include <fixy/session/Wire.h>

#include <foundation/NoObject.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <meta>
#include <new>
#include <source_location>
#include <type_traits>
#include <utility>

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
                     loc_file, loc.line(), loc.column(), loc.function_name());
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
[[noreturn, gnu::cold, gnu::noinline]] inline void
report_use_after_consume(std::string_view wrapper, std::string_view protocol, std::source_location loc) noexcept {
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
          AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename PS = ::foundation::permissions::EmptyPermSet>
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
    static void* operator new(std::size_t) = delete(
        "[Session_Handle_On_Heap] a session handle lives on the stack or inside an owner that destroys it.  "
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
// of the door.  The rule proof-routes of the quarantine plugin refuses an
// explicit specialization of a function template (function-specialization).

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

namespace detail {

// Sends the cancellation through the Resource.  The call is unqualified,
// so argument-dependent lookup finds the Resource's own function.
template <typename Resource>
constexpr void cancel_resource(std::remove_reference_t<Resource>& resource) noexcept {
    cancel_session(resource);
}

}  // namespace detail

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
// No object of the class exists, and foundation/NoObject.h tells why no
// constructor and no byte route makes one.
class HandleFactory final : ::foundation::NoObject<HandleFactory> {
    // Builds the handle at Proto as it stands.
    template <typename Proto, typename Resource, typename LoopCtx, AbandonmentPolicy Policy,
              typename PS = ::foundation::permissions::EmptyPermSet>
    [[nodiscard]] static constexpr auto
    make_(Resource r, watch::session_ref session = {},
          std::source_location loc =
              std::source_location::current()) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        -> SessionHandle<Proto, Resource, LoopCtx, Policy, PS>;

    // Builds the handle at the head that R reaches after Continue, Loop
    // and a vendor pin resolve.
    template <typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
    [[nodiscard]] static constexpr auto step_(Resource r, watch::session_ref session = {},
                                              std::source_location loc = std::source_location::current()) noexcept;

    // Builds the handle that a choice enters for branch I.
    template <typename Choice, std::size_t I, typename Resource, typename LoopCtx, AbandonmentPolicy Policy,
              typename PS>
    [[nodiscard]] static constexpr auto
    enter_(Resource r, watch::session_ref session) noexcept(std::is_nothrow_move_constructible_v<Resource>);

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
    // it.  The gate is RewindableTo of fixy/session/Admission.h, so the
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
        requires(detail::recover_admits(^^Ctx, ^^Offer<Branches...>, I, ^^LoopCtx, ^^PS))
    [[nodiscard]] static constexpr auto
    recover(Ctx const&, SessionHandle<Offer<Branches...>, Resource, LoopCtx, Policy, PS>&& handle) noexcept(
        std::is_nothrow_move_constructible_v<Resource>) {
        auto& core = core_(handle);
        core.require_live_();
        const watch::session_ref session = core.session_();
        return enter_<Offer<Branches...>, I, Resource, LoopCtx, Policy, PS>(core.take_resource_(), session);
    }

    // ── The steps ─────────────────────────────────────────────────────

    // Sends a value.  The transport has one of the two write shapes of
    // fixy/session/Transport.h.
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
                      "message takes, or the payload walk of fixy/session/Classification.h refuses the message.  "
                      "Hold each permission that the payload moves or lends before the send.");
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
    send(SessionHandle<Send<T, R>, Resource, LoopCtx, Policy, PS>&& handle,
         Transport transport) noexcept(detail::write_is_nothrow<Transport, Resource, std::size_t>()
                                       && std::is_nothrow_move_constructible_v<Resource>) {
        static_assert(detail::handle_admits_send_v<PS, T>,
                      "fixy::session::diagnostic [PermissionImbalance]: the permission set does not hold what the "
                      "message takes, or the payload walk of fixy/session/Classification.h refuses the message.");
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
                      "of fixy/session/Classification.h refuses the message.");
        auto& core = core_(handle);
        T value = detail::read_through<T>(transport, core.live_resource_(), core.session_().endpoint);
        const watch::session_ref session = core.session_();
        auto next =
            step_<R, Resource, LoopCtx, Policy, detail::perm_set_after_recv_t<PS, T>>(core.take_resource_(), session);
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
                      "message closes, or the payload walk of fixy/session/Classification.h refuses the message.");
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
    [[nodiscard]] static constexpr auto
    select(SessionHandle<Select<Branches...>, Resource, LoopCtx, Policy, PS>&& handle,
           Transport transport) noexcept(detail::write_is_nothrow<Transport, Resource, std::size_t>()
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
class [[nodiscard]]
SessionHandle<End, Resource, LoopCtx, Policy, PS> : public detail::handle_core<End, Resource, LoopCtx, Policy, PS> {
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
    // message in fixy/session/Transport.h).
    static constexpr bool is_keyed = is_keyed_step_v<Send<T, R>>;

    // The Transport is what physically moves the value to the peer.  It
    // has one of the two write shapes of fixy/session/Transport.h.  The
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
    [[nodiscard]] constexpr auto
    send(Transport transport) && noexcept(detail::write_is_nothrow<Transport, Resource, std::size_t>()
                                          && std::is_nothrow_move_constructible_v<Resource>) {
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

    // The Transport has one of the two read shapes of
    // fixy/session/Transport.h.  The pair is the received value and the
    // handle at the continuation.  A polling read that finds the message
    // costs one call.
    template <typename Transport>
        requires(!is_keyed) && ReadTransport<Transport, Resource, T>
    [[nodiscard]] constexpr auto recv(Transport transport) && noexcept(detail::read_is_nothrow<Transport, Resource, T>()
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
    [[nodiscard]] constexpr auto
    select_local(Ctx const& ctx) && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
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
    [[nodiscard]] constexpr auto
    pick_local(Ctx const& ctx) && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
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
        static_assert(::foundation::permissions::perm_set_equal(^^PS, ^^detail::loop_entry_perm_set_t<ActiveLoopCtx>),
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
// labels of fixy/session/Fold.h), and the word of the branch
// is the label word of that step.  The handle enters at the value step of
// the message, or past the message when its payload is void.  Every other
// branch, a positional one or one that is no label, enters at its head.
template <typename Choice, std::size_t I, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
constexpr auto
HandleFactory::enter_(Resource r, watch::session_ref session) noexcept(std::is_nothrow_move_constructible_v<Resource>) {
    using Branch = std::tuple_element_t<I, typename Choice::branches_tuple>;
    if constexpr (is_keyed_choice_v<Choice> && detail::wire_words_v<Choice>[I].is_wired) {
        using Message = typename Branch::message_type;
        if constexpr (is_select_v<Choice>) {
            static_assert(detail::handle_admits_send_v<PS, Message>,
                          "fixy::session::diagnostic [PermissionImbalance]: the permission set does not hold what "
                          "the message of the branch takes, or the payload walk of fixy/session/Classification.h "
                          "refuses the message.");
        } else {
            static_assert(detail::handle_admits_recv_v<PS, Message>,
                          "fixy::session::diagnostic [PermissionImbalance]: the permission set does not hold what "
                          "the message of the branch closes, or the payload walk of fixy/session/Classification.h "
                          "refuses the message.");
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
template <bool PassesIndex, typename Choice, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
          typename Handler, std::size_t... Is>
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
                    call_branch_<PassesIndex, Is>(std::move(handler), enter_<Choice, Is, Resource, LoopCtx, Policy, PS>(
                                                                          std::forward<Resource>(res), session));
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

// The member definitions of the factory that read the admission concepts
// stand here, after those concepts.

template <typename Proto, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
constexpr auto HandleFactory::make_(Resource r, watch::session_ref session,
                                    std::source_location loc) noexcept(std::is_nothrow_move_constructible_v<Resource>)
    -> SessionHandle<Proto, Resource, LoopCtx, Policy, PS> {
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
    // A constexpr variable is evaluated where it stands, so a Resource that
    // states its priority with the wrong type is refused here, with the
    // message of the refusal.
    constexpr watch::priority resource_priority = session_priority(^^Resource);
    watch::session_ref session{};
    if constexpr (Policy::checks_abandonment) {
        if !consteval {
            const watch::endpoint_id endpoint = watch::claim(type_display_name_v<Proto>, loc, resource_priority,
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
constexpr auto HandleFactory::rewind(
    SessionHandle<End, Resource, EndLoopCtx, Policy, ::foundation::permissions::EmptyPermSet>&& at_end,
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
[[nodiscard]] inline std::pair<watch::endpoint_id, watch::endpoint_id>
claim_channel_(std::source_location loc) noexcept {
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

// True when H is a handle at End of the session that Brand owns, and its
// close() gives back the Resource.
template <typename H, typename Resource, typename Brand>
concept ClosesInSessionOf = std::is_same_v<std::remove_cvref_t<H>, H> && requires { typename H::loop_ctx; }
                         && detail::carries_brand<typename H::loop_ctx, Brand> && requires(H handle) {
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
    && ::fixy::concurrent::CtxFitsPermissionFork<Ctx, Parent, SelfTag, PeerTag> && SessionResource<ResourceSelf>
    && SessionResource<ResourcePeer> && ChannelEndsShareAPriority<ResourceSelf, ResourcePeer>
    && ForkedSidesAgree<SelfProto, PeerProto, ResourceSelf, ResourcePeer>
    && detail::ForkedEndpointBody<SelfBody, SelfProto, ResourceSelf, Policy, SelfTag, Brand, Ctx>
    && detail::ForkedEndpointBody<PeerBody, PeerProto, ResourcePeer, Policy, PeerTag, Brand, Ctx>;

// ── The door of the mints ────────────────────────────────────────────
//
// The one friend of SessionOpenKey, so the one class that opens a session
// on the handle factory.  Each public member is one mint of this layer
// and states the whole gate of that mint in its requires clause, so a
// direct call of a member is no weaker than the mint.  The mints of
// fixy/session/Mint.h forward here for their names and their default
// arguments, through detail::late_door_t.  No object of the class exists.
class SessionMintDoor final : ::foundation::NoObject<SessionMintDoor> {
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
        auto head = HandleFactory::open_<Proto, Resource, Policy, Set>(
            SessionOpenKey{}, std::forward<Resource>(resource), std::source_location::current());
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
        std::is_nothrow_invocable_v<
            Body, detail::first_handle_t<Proto, Resource, Policy, ::foundation::permissions::EmptyPermSet,
                                         detail::brand_ctx_t<Body>>>) {
        auto at_end = std::invoke(
            std::move(body),
            HandleFactory::open_<Proto, Resource, Policy, ::foundation::permissions::EmptyPermSet,
                                 detail::brand_ctx_t<Body>>(SessionOpenKey{}, std::forward<Resource>(r), loc));
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
            detail::claim_channel_<SelfProto, PeerProto, Policy, session_priority(^^ResourceSelf)>(loc);
        return ::fixy::concurrent::mint_permission_fork<SelfTag, PeerTag>(
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
        std::pair<watch::endpoint_id, watch::endpoint_id> endpoints{watch::endpoint_id::none, watch::endpoint_id::none};
        if !consteval {
            endpoints = detail::claim_channel_<Proto, dual_of_t<Proto>, Policy, session_priority(^^ResourceA)>(loc);
        }
        return std::pair{
            HandleFactory::open_<Proto, ResourceA, Policy, ::foundation::permissions::EmptyPermSet>(
                SessionOpenKey{}, std::forward<ResourceA>(resource_a), loc, endpoints.first),
            HandleFactory::open_<dual_of_t<Proto>, ResourceB, Policy, ::foundation::permissions::EmptyPermSet>(
                SessionOpenKey{}, std::forward<ResourceB>(resource_b), loc, endpoints.second)};
    }
};

}  // namespace fixy::session
