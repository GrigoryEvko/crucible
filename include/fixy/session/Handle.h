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
// callable they pass.
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
// The guarantee is for acyclic ownership only.  If a program sends an
// endpoint over a channel to a thread that already holds the dual, or
// connects two fork-shaped channels in a cycle, then the forest
// condition does not hold, and the guarantee does not apply.  Priorities
// across sessions (van den Heuvel and Pérez, LMCS 2024; Kokke and
// Dardha, LMCS 2023) remove that limit, and they are not implemented.
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

#include <fixy/session/Payload.h>
#include <fixy/session/Stepping.h>

#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <meta>
#include <optional>
#include <source_location>
#include <string_view>
#include <tuple>
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
        return "select<branch_index>(transport_callable) or select_local<branch_index>()";
    } else if constexpr (is_offer_v<Proto>) {
        return "branch(transport_callable, handler) or pick_local<branch_index>()";
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

    // A sibling handle instantiation can mark another handle consumed,
    // which is how a handle shipped to a peer is retired.
    template <typename OtherProto, typename OtherDerived, AbandonmentPolicy OtherPolicy>
    friend class SessionHandleBase;

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

    // Marks the handle consumed without advancing the protocol.  The
    // destructor check still fires for every handle that does not
    // detach, so accidental abandonment stays caught.
    template <typename Reason>
        requires DetachReason<Reason>
    constexpr void detach(Reason /*reason_tag*/) && noexcept {
        require_live_();
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
template <typename PS, typename T>
inline constexpr bool handle_admits_send_v = SendablePayload<T, PS>;

template <typename PS, typename T>
inline constexpr bool handle_admits_recv_v = ReceivablePayload<T, PS>;

template <typename PS, typename T>
using perm_set_after_send_t = ::fixy::session::perm_set_after_send_t<PS, T>;

template <typename PS, typename T>
using perm_set_after_recv_t = ::fixy::session::perm_set_after_recv_t<PS, T>;

// True when a handle at End can close with this set.  An open loan, a
// LentOut or a BorrowedIn, is a promise that the protocol did not keep,
// so the set must hold none.  An owned tag travelled in a payload, and
// its token is held by the value that carried it.
template <typename PS>
inline constexpr bool perm_set_admits_close_v = !perm_set_has_open_loan_v<PS>;

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
// without permissions has the same loop context as before.
template <typename LoopType, typename EntryPS>
struct PermLoopFrame {
    using loop_type = LoopType;
    using body = typename LoopType::body;
    using entry_perm_set = EntryPS;
};

template <typename Frame>
struct loop_entry_perm_set {
    using type = ::foundation::permissions::EmptyPermSet;
};

template <typename LoopType, typename EntryPS>
struct loop_entry_perm_set<PermLoopFrame<LoopType, EntryPS>> {
    using type = EntryPS;
};

template <typename Frame>
using loop_entry_perm_set_t = typename loop_entry_perm_set<Frame>::type;

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
// The brand wraps the loop context and passes the loop traits through,
// which is the extension point that Protocol.h keeps for a context
// wrapper.
template <typename Brand, typename InnerLoopCtx = void>
struct session_brand {
    using brand_type = Brand;
    using inner_loop_ctx = InnerLoopCtx;
};

template <typename LoopCtx>
struct session_brand_of {
    using type = void;
};

template <typename Brand, typename InnerLoopCtx>
struct session_brand_of<session_brand<Brand, InnerLoopCtx>> {
    using type = Brand;
};

}  // namespace detail

template <typename Brand, typename InnerLoopCtx>
struct session_loop_ctx_traits<detail::session_brand<Brand, InnerLoopCtx>> {
    using inner_loop_ctx = session_loop_ctx_inner_t<InnerLoopCtx>;
};

template <typename Brand, typename InnerLoopCtx, typename NewInnerLoopCtx>
struct session_loop_ctx_rebind_inner<detail::session_brand<Brand, InnerLoopCtx>, NewInnerLoopCtx> {
    using type = detail::session_brand<Brand, session_loop_ctx_rebind_inner_t<InnerLoopCtx, NewInnerLoopCtx>>;
};

namespace detail {

// The sole authorized constructor of a bare handle.  Every handle
// specialization's value constructor is private and befriends only this
// factory, so a direct `SessionHandle<Proto, Res, Ctx, Pol, PS>{res}` at
// a call site is rejected as private and cannot bypass the
// well-formedness gate or the Continue-and-Loop resolution.
//
// It is deliberately unconstrained.  Every caller runs its own
// admission first.
template <typename Proto, typename Resource, typename LoopCtx, AbandonmentPolicy Policy,
          typename PS = ::foundation::permissions::EmptyPermSet>
[[nodiscard]] constexpr auto make_session_handle(
    Resource r,
    std::source_location loc = std::source_location::current()) noexcept(std::is_nothrow_move_constructible_v<Resource>)
    -> SessionHandle<Proto, Resource, LoopCtx, Policy, PS> {
    return SessionHandle<Proto, Resource, LoopCtx, Policy, PS>{std::forward<Resource>(r), loc};
}

template <typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
[[nodiscard]] constexpr auto step_to_next(Resource r,
                                          std::source_location loc = std::source_location::current()) noexcept {
    if constexpr (std::is_same_v<R, Continue>) {
        using ActiveLoopCtx = session_loop_ctx_inner_t<LoopCtx>;
        static_assert(!std::is_void_v<ActiveLoopCtx>, "fixy::session::diagnostic [Continue_Without_Loop]: "
                                                      "Continue appears outside a Loop context.  "
                                                      "Every Continue must have an enclosing Loop<Body>.");
        static_assert(::foundation::permissions::perm_set_equal_v<PS, loop_entry_perm_set_t<ActiveLoopCtx>>,
                      "fixy::session::diagnostic [PermissionImbalance]: one iteration of the loop changes the "
                      "permission set, so a Continue would start the next iteration with a different set.  Each "
                      "permission that the body receives, it must send back before the Continue, and each "
                      "permission that the body sends, it must receive back.");
        using NextBody = typename ActiveLoopCtx::body;
        // The body may itself begin with a Loop or a Continue, so this
        // recurses.  Forwarding `loc` keeps the outermost caller's site
        // rather than replacing it with this frame's.
        return step_to_next<NextBody, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r), loc);
    } else if constexpr (is_loop_v<R>) {
        using InnerBody = typename R::body;
        using InnerCtx = session_loop_ctx_rebind_inner_t<LoopCtx, loop_frame_t<R, PS>>;
        // Entering an inner Loop shadows the enclosing loop context.
        // That shadowing is what binds Continue to the nearest Loop.
        return step_to_next<InnerBody, Resource, InnerCtx, Policy, PS>(std::forward<Resource>(r), loc);
    } else if constexpr (is_vendor_pinned_v<R>) {
        // The vendor is a declaration for the layer above.  The handle
        // steps the protocol that it pins.
        return step_to_next<typename R::protocol, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r), loc);
    } else {
        static_assert(is_head_v<R>, "fixy::session::diagnostic [Protocol_Ill_Formed]: "
                                    "unexpected protocol shape after resolution.  "
                                    "Only Send/Recv/Select/Offer/End/Continue are valid heads.");
        return make_session_handle<R, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r), loc);
    }
}

// Sends the cancellation through the Resource.  The call is unqualified,
// so argument-dependent lookup finds the Resource's own function.
template <typename Resource>
constexpr void cancel_resource(std::remove_reference_t<Resource>& resource) noexcept {
    cancel_session(resource);
}

// The Resource half of every handle, and the half of the abandonment
// policy that needs the Resource: the cancellation.  The destructor of
// this class runs before the destructor of SessionHandleBase, while the
// Resource is still alive, so the cancellation is sent here and the
// handle is marked.  The base destructor then finds nothing to report.
template <typename Proto, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
class handle_core : public SessionHandleBase<Proto, SessionHandle<Proto, Resource, LoopCtx, Policy, PS>, Policy> {
    using base_type = SessionHandleBase<Proto, SessionHandle<Proto, Resource, LoopCtx, Policy, PS>, Policy>;

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
                this->mark_consumed_();
            }
        }
    }

protected:
    constexpr handle_core(Resource r, std::source_location loc) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : base_type{loc}, resource_{std::forward<Resource>(r)} {}

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
        this->mark_consumed_();
    }
};

}  // namespace detail

template <typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
class [[nodiscard]] SessionHandle<End, Resource, LoopCtx, Policy, PS>
    : public detail::handle_core<End, Resource, LoopCtx, Policy, PS> {
    using core_type = detail::handle_core<End, Resource, LoopCtx, Policy, PS>;

    static_assert(detail::perm_set_admits_close_v<PS>,
                  "fixy::session::diagnostic [PermissionImbalance]: the protocol reaches End while the "
                  "permission set holds an open loan, a LentOut or a BorrowedIn.  close() would lose it.  Return "
                  "each loan before End.");

    template <typename FProto, typename FRes, typename FLoop, AbandonmentPolicy FPol, typename FPS>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop, FPol, FPS>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : core_type{std::forward<Resource>(r), loc} {}

public:
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

    template <typename FProto, typename FRes, typename FLoop, AbandonmentPolicy FPol, typename FPS>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop, FPol, FPS>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : core_type{std::forward<Resource>(r), loc} {}

public:
    using message_type = T;
    using continuation = R;

    // The Transport is what physically moves the value to the peer.
    // Its signature is void(Resource&, T&&).  The returned handle sits
    // at the continuation, with Continue and Loop already resolved.
    template <typename Transport>
        requires std::is_invocable_v<Transport, Resource&, T&&>
    [[nodiscard]] constexpr auto
    send(T value, Transport transport) && noexcept(std::is_nothrow_invocable_v<Transport, Resource&, T&&>
                                                   && std::is_nothrow_move_constructible_v<Resource>
                                                   && std::is_nothrow_move_constructible_v<T>) {
        static_assert(detail::handle_admits_send_v<PS, T>,
                      "fixy::session::diagnostic [PermissionImbalance]: the permission set does not hold what the "
                      "message takes, or the payload walk of fixy/session/Payload.h refuses the message.  Hold each "
                      "permission that the payload moves or lends before the send.");
        std::invoke(transport, this->live_resource_(), std::move(value));
        return detail::step_to_next<R, Resource, LoopCtx, Policy, detail::perm_set_after_send_t<PS, T>>(
            this->take_resource_());
    }
};

template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
class [[nodiscard]] SessionHandle<Recv<T, R>, Resource, LoopCtx, Policy, PS>
    : public detail::handle_core<Recv<T, R>, Resource, LoopCtx, Policy, PS> {
    using core_type = detail::handle_core<Recv<T, R>, Resource, LoopCtx, Policy, PS>;

    template <typename FProto, typename FRes, typename FLoop, AbandonmentPolicy FPol, typename FPS>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop, FPol, FPS>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : core_type{std::forward<Resource>(r), loc} {}

public:
    using message_type = T;
    using continuation = R;

    // The Transport signature is T(Resource&).  The pair is the
    // received value and the handle at the continuation.
    template <typename Transport>
        requires std::is_invocable_r_v<T, Transport, Resource&>
    [[nodiscard]] constexpr auto
    recv(Transport transport) && noexcept(std::is_nothrow_invocable_r_v<T, Transport, Resource&>
                                          && std::is_nothrow_move_constructible_v<Resource>
                                          && std::is_nothrow_move_constructible_v<T>) {
        static_assert(detail::handle_admits_recv_v<PS, T>,
                      "fixy::session::diagnostic [PermissionImbalance]: the permission set does not hold what the "
                      "message closes, the message gives a region that the set already holds, or the payload walk "
                      "of fixy/session/Payload.h refuses the message.");
        T value = std::invoke(transport, this->live_resource_());
        auto next = detail::step_to_next<R, Resource, LoopCtx, Policy, detail::perm_set_after_recv_t<PS, T>>(
            this->take_resource_());
        return std::pair{std::move(value), std::move(next)};
    }
};

template <typename... Branches, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
class [[nodiscard]] SessionHandle<Select<Branches...>, Resource, LoopCtx, Policy, PS>
    : public detail::handle_core<Select<Branches...>, Resource, LoopCtx, Policy, PS> {
    using core_type = detail::handle_core<Select<Branches...>, Resource, LoopCtx, Policy, PS>;

    template <typename FProto, typename FRes, typename FLoop, AbandonmentPolicy FPol, typename FPS>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop, FPol, FPS>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : core_type{std::forward<Resource>(r), loc} {}

public:
    static constexpr std::size_t branch_count = sizeof...(Branches);

    // A second empty-choice rejection, so a construction route that
    // reaches this class without passing the factory's own check still
    // fires the same diagnostic.
    static_assert(branch_count > 0, "fixy::session::diagnostic [Empty_Choice_Combinator]: "
                                    "SessionHandle<Select<>>: cannot construct a runnable handle "
                                    "on Select<> with zero branches — there is no branch for "
                                    "select<I>() to choose.  See mint_session_handle for the full "
                                    "diagnostic and remediation.");

    // Picks branch I and signals the choice to the peer through
    // Transport, whose signature is void(Resource&, std::size_t).
    //
    // The index bound is a body static_assert rather than a
    // requires-clause so that an out-of-range index reports the named
    // diagnostic instead of a bare unsatisfied-constraint message.
    // Transport still gates overload resolution.
    template <std::size_t I, typename Transport>
        requires std::is_invocable_v<Transport, Resource&, std::size_t>
    [[nodiscard]] constexpr auto
    select(Transport transport) && noexcept(std::is_nothrow_invocable_v<Transport, Resource&, std::size_t>
                                            && std::is_nothrow_move_constructible_v<Resource>) {
        static_assert(I < sizeof...(Branches), "fixy::session::diagnostic [Branch_Index_Out_Of_Range]: "
                                               "SessionHandle<Select<...>>::select<I>(transport): branch "
                                               "index I is out of range for this Select position.  The "
                                               "protocol has fewer branches than the index requested; "
                                               "verify I < branch_count at the call site (decltype("
                                               "handle)::branch_count is exposed for compile-time queries).");
        std::invoke(transport, this->live_resource_(), I);
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next<Chosen, Resource, LoopCtx, Policy, PS>(this->take_resource_());
    }

    // Advances the local handle WITHOUT telling the peer which branch
    // was picked.  The name carries the omission so it is visible at
    // every call site.  On a wire-based session the peer never learns
    // the choice and the two endpoints drift apart, so this is for an
    // in-memory channel, a mocked transport, or a pipeline whose branch
    // is fixed at compile time on both sides.
    template <std::size_t I>
    [[nodiscard]] constexpr auto select_local() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        static_assert(I < sizeof...(Branches), "fixy::session::diagnostic [Branch_Index_Out_Of_Range]: "
                                               "SessionHandle<Select<...>>::select_local<I>(): branch index "
                                               "I is out of range for this Select position.  The protocol "
                                               "has fewer branches than the index requested; verify I < "
                                               "branch_count at the call site.");
        this->require_live_();
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next<Chosen, Resource, LoopCtx, Policy, PS>(this->take_resource_());
    }

    // Deleting the zero-argument form forces every call site to state
    // whether the peer is told.  A default would have to pick one, and
    // picking the silent one drifts wire-based sessions apart.
    template <std::size_t I>
    void select() && = delete("[Wire_Variant_Required] SessionHandle<Select<...>>::select<I>() "
                              "without arguments is not allowed.  Choose one: "
                              "(a) `select<I>(transport)` to signal the branch choice over "
                              "the wire (the peer sees the I-th branch and stays in sync), "
                              "OR (b) `select_local<I>()` to advance the local handle WITHOUT "
                              "signalling the peer (in-memory channels and unit tests only — "
                              "wire-based sessions where the peer doesn't observe the "
                              "choice will silently drift off-protocol).  The framework "
                              "refuses to guess which one you meant.");
};

template <typename... Branches, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
class [[nodiscard]] SessionHandle<Offer<Branches...>, Resource, LoopCtx, Policy, PS>
    : public detail::handle_core<Offer<Branches...>, Resource, LoopCtx, Policy, PS> {
    using core_type = detail::handle_core<Offer<Branches...>, Resource, LoopCtx, Policy, PS>;

    template <typename FProto, typename FRes, typename FLoop, AbandonmentPolicy FPol, typename FPS>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop, FPol, FPS>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : core_type{std::forward<Resource>(r), loc} {}

public:
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

    // Receives the peer's branch label through Transport, whose
    // signature is std::size_t(Resource&), then calls the handler with
    // the handle for that branch.  The handler is invoked once per
    // branch type and every branch must give the handler the same
    // return type, or all of them void.
    //
    // An out-of-range label aborts.  The peer has sent a label this
    // protocol does not define, so the two endpoints no longer agree
    // and no branch can be entered safely.
    template <typename Transport, typename Handler>
        requires std::is_invocable_r_v<std::size_t, Transport, Resource&>
    constexpr auto branch(Transport transport, Handler handler) && {
        const std::size_t idx = std::invoke(transport, this->live_resource_());
        return dispatch_branch_(idx, this->take_resource_(), std::move(handler),
                                std::make_index_sequence<branch_count>{});
    }

    // Assumes branch I WITHOUT receiving the peer's label.  The name
    // carries the omission so it is visible at every call site.  If the
    // peer signals a different branch the two endpoints diverge, so
    // this is for an in-memory channel, a mocked transport, or a
    // pipeline whose branch is fixed at compile time on both sides.
    template <std::size_t I>
    [[nodiscard]] constexpr auto pick_local() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        static_assert(I < branch_count, "fixy::session::diagnostic [Branch_Index_Out_Of_Range]: "
                                        "SessionHandle<Offer<...>>::pick_local<I>(): branch index "
                                        "I is out of range for this Offer position.  The protocol "
                                        "has fewer branches than the index requested; verify I < "
                                        "branch_count at the call site.  On a sender-annotated "
                                        "Offer<Sender<Role>, B0, ...> the annotation is not a "
                                        "branch, so B0 is index 0.");
        this->require_live_();
        using Chosen = std::tuple_element_t<I, branches>;
        return detail::step_to_next<Chosen, Resource, LoopCtx, Policy, PS>(this->take_resource_());
    }

    // Deleting the zero-argument form forces every call site to state
    // whether the peer's label is read.
    template <std::size_t I>
    void pick() && = delete("[Wire_Variant_Required] SessionHandle<Offer<...>>::pick<I>() "
                            "without arguments is not allowed.  Use "
                            "`pick_local<I>()` to advance the local handle WITHOUT "
                            "receiving a peer label (in-memory channels and unit tests "
                            "only — wire-based sessions where the peer's actual choice "
                            "differs from I will silently drift off-protocol).  The "
                            "framework refuses to guess that the peer-skipping "
                            "variant was what you meant.");

private:
    template <std::size_t I>
    static constexpr auto make_branch_handle_(Resource r) {
        using B = std::tuple_element_t<I, branches>;
        return detail::step_to_next<B, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r));
    }

    template <std::size_t... Is, typename Handler>
    static constexpr auto dispatch_branch_(std::size_t idx, Resource res, Handler handler, std::index_sequence<Is...>) {
        if (idx >= branch_count) [[unlikely]] {
            std::abort();
        }

        // The result type comes from branch 0.  A branch whose handler
        // returns a different type then fails to convert into the
        // single optional below, which is what enforces the
        // same-return-type rule.
        using FirstHandle = decltype(make_branch_handle_<0>(std::declval<Resource>()));
        using Result = std::invoke_result_t<Handler&&, FirstHandle>;

        if constexpr (std::is_void_v<Result>) {
            bool dispatched = false;
            (
                [&]() {
                    if (!dispatched && idx == Is) {
                        std::invoke(std::move(handler), make_branch_handle_<Is>(std::forward<Resource>(res)));
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
                        result.emplace(
                            std::invoke(std::move(handler), make_branch_handle_<Is>(std::forward<Resource>(res))));
                        dispatched = true;
                    }
                }(),
                ...);
            if (!result) [[unlikely]]
                std::abort();
            return std::move(*result);
        }
    }
};

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

// The Resource is what a handle stores at runtime, and the concept
// exists to stop a handle from outliving what it points at.
//
// A value type is safe because the handle owns it and their lifetimes
// coincide.  An lvalue reference to a Pinned object is safe because a
// Pinned object cannot be moved, so its address is stable for its whole
// lifetime and the caller only has to outlive the handles.  An lvalue
// reference to a non-Pinned object is rejected: moving or assigning to
// the referent relocates it and every live handle dangles with no
// diagnostic.  An rvalue reference is rejected because it would bind to
// a temporary that dies before the handle is used.
//
// A raw object pointer is admitted without requiring a Pinned pointee.
// Code that reaches for a raw pointer is already in a manual-lifetime
// regime the type system can only partly support, and the pointer
// itself is a value the handle owns.  A function pointer is admitted
// because a function's address is stable by language rule.

template <typename Resource>
concept SessionResource = !std::is_reference_v<Resource>
                       || (std::is_lvalue_reference_v<Resource>
                           && std::derived_from<std::remove_reference_t<Resource>,
                                                ::foundation::Pinned<std::remove_reference_t<Resource>>>);

// The admission gate for handle construction, expressed as a concept
// rather than as body static_asserts alone.  A body static_assert is
// not visible to SFINAE: overload resolution accepts the signature for
// any Proto and the failure only appears at instantiation, where a
// requires-expression further up the stack cannot observe it.
// Downstream concepts that ask whether a handle can be minted need that
// answer, so the checks live in the signature.

// An empty choice is not well-formed either.  The empty-choice clause
// comes first so that the refusal of such a protocol names that fault
// and not the general one.
template <typename Proto>
concept WellFormedRunnableProtocol = !is_empty_choice_v<Proto> && is_well_formed_v<Proto>;

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

namespace detail {

// Builds the first handle of an admitted protocol with a given
// permission set.  A Proto that starts with a Loop is unrolled one
// iteration, so the returned handle sits at the loop body with the Loop
// (or its permission frame) as its LoopCtx.  Any other head passes
// through unchanged.
//
// It checks only the permission flow of the whole protocol.  Each public
// mint runs its own admission first, and states the flow in its
// constraint.  A mint that consumes Permission tokens calls this with the
// set of the tokens it consumed.  No mint may call it with a set whose
// tokens it did not consume.
//
// LoopCtx is void, or a session brand that an owning entry point sets.
template <typename Proto, typename Resource, AbandonmentPolicy Policy, typename PS, typename LoopCtx = void>
[[nodiscard]] constexpr auto open_session_(Resource r, std::source_location loc) noexcept {
    static_assert(PermissionFlowCloses<Proto, PS>,
                  "fixy::session::diagnostic [PermissionImbalance]: a path of the protocol sends a region that the "
                  "permission set does not hold, receives a second owner of a region, changes the set in one loop "
                  "iteration, or reaches End with an open loan.  The walk visits every branch, so an arm that no "
                  "run selects counts too.");
    return step_to_next<Proto, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r), loc);
}

template <typename Proto, typename Resource, AbandonmentPolicy Policy,
          typename PS = ::foundation::permissions::EmptyPermSet, typename LoopCtx = void>
using first_handle_t = decltype(open_session_<Proto, Resource, Policy, PS, LoopCtx>(std::declval<Resource>(),
                                                                                    std::source_location{}));

template <typename Brand>
using brand_ctx_t = session_brand<Brand, void>;

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

    static_assert(SessionResource<Resource>, "fixy::session::diagnostic [SessionResource_NotPinned]: "
                                             "mint_session_handle<Proto, Resource>: Resource must be either "
                                             "a value type (handle owns it by value) or an lvalue reference "
                                             "to a type derived from foundation::Pinned<T>.  An lvalue "
                                             "reference to a non-Pinned object lets a subsequent move of the "
                                             "channel leave live handles dangling (use-after-free).  Either: "
                                             "(a) make the channel Pinned by deriving it from "
                                             "foundation::Pinned<ChannelType>, or (b) pass the channel by "
                                             "value (copies are fine for value-like channels), or (c) wrap "
                                             "the channel in std::reference_wrapper if the caller's "
                                             "lifetime contract is satisfied by other means.  Rvalue-"
                                             "reference Resource is also rejected — the handle would bind "
                                             "to a temporary and dangle immediately on return.");

    static_assert(!std::is_same_v<Proto, Continue>, "fixy::session::diagnostic [Continue_Without_Loop]: "
                                                    "Continue cannot be the top-level protocol.");

    // Forwarding `loc` keeps the caller's site in the abandonment
    // diagnostic even though the unroll inserts an intermediate handle
    // whose own default location would otherwise win.
    return detail::open_session_<Proto, Resource, Policy, ::foundation::permissions::EmptyPermSet>(
        std::forward<Resource>(r), loc);
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
                         && std::is_same_v<typename detail::session_brand_of<typename H::loop_ctx>::type, Brand>
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
    auto at_end = std::invoke(std::move(body),
                              detail::open_session_<Proto, Resource, Policy, ::foundation::permissions::EmptyPermSet,
                                                    detail::brand_ctx_t<Body>>(std::forward<Resource>(r), loc));
    return std::move(at_end).close();
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

// One side of a forked channel.  It builds the endpoint in the thread
// that runs it, gives the endpoint to the body, and closes the handle
// that the body returns.  A struct and not a lambda, because a lambda
// capture cannot hold a reference Resource by its declared type.
template <typename Proto, AbandonmentPolicy Policy, typename Resource, typename Body>
struct forked_endpoint_ {
    Resource resource;
    Body body;
    std::source_location loc;

    template <typename Perm, typename Ctx>
    void operator()(Perm permission, Ctx const& ctx) noexcept {
        auto head = open_session_<Proto, Resource, Policy, ::foundation::permissions::EmptyPermSet, brand_ctx_t<Body>>(
            std::forward<Resource>(resource), loc);
        auto at_end = std::invoke(std::move(body), std::move(head), std::move(permission), ctx);
        static_cast<void>(std::move(at_end).close());
    }
};

template <typename Body, typename Proto, typename Resource, typename Policy, typename Tag, typename Ctx>
concept ForkedEndpointBody =
    std::is_nothrow_invocable_v<Body, forked_head_t<Proto, Resource, Policy, Body>,
                                ::foundation::permissions::Permission<Tag>, Ctx const&>
    && ClosesInSessionOf<std::invoke_result_t<Body, forked_head_t<Proto, Resource, Policy, Body>,
                                              ::foundation::permissions::Permission<Tag>, Ctx const&>,
                         Resource, Body>;

}  // namespace detail

// The context gate of the fork-shaped mint: two runnable local
// protocols, one the dual of the other, and a fork that the context may
// start.
template <typename Ctx, typename Proto, typename Parent, typename SelfTag, typename PeerTag>
concept CtxFitsForkedChannel = WellFormedRunnableProtocol<Proto> && WellFormedRunnableProtocol<dual_of_t<Proto>>
                            && PermissionFlowCloses<Proto, ::foundation::permissions::EmptyPermSet>
                            && PermissionFlowCloses<dual_of_t<Proto>, ::foundation::permissions::EmptyPermSet>
                            && ::foundation::permissions::CtxFitsPermissionFork<Ctx, Parent, SelfTag, PeerTag>;

// Makes a channel and starts its two sides on two threads through
// foundation's permission_fork.  The parent permission splits into
// SelfTag and PeerTag.  The self body runs Proto over self_resource, and
// the peer body runs the dual over peer_resource.  Each body gets its
// endpoint, its child permission and the context, and must return its
// endpoint at End.  The call returns the parent permission after the two
// threads join.
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
          && SessionResource<ResourcePeer>
          && detail::ForkedEndpointBody<SelfBody, Proto, ResourceSelf, Policy, SelfTag, Ctx>
          && detail::ForkedEndpointBody<PeerBody, dual_of_t<Proto>, ResourcePeer, Policy, PeerTag, Ctx>
// §XXI carve-out: cx=alloc — starting a thread is a kernel side effect.
[[nodiscard]] ::foundation::permissions::Permission<Parent, Brand>
mint_forked_channel(Ctx const& ctx, ::foundation::permissions::Permission<Parent, Brand>&& parent,
                    ResourceSelf self_resource, ResourcePeer peer_resource, SelfBody self_body, PeerBody peer_body,
                    std::source_location loc = std::source_location::current()) noexcept {
    using SelfSide = detail::forked_endpoint_<Proto, Policy, ResourceSelf, SelfBody>;
    using PeerSide = detail::forked_endpoint_<dual_of_t<Proto>, Policy, ResourcePeer, PeerBody>;
    return ::foundation::permissions::mint_permission_fork<SelfTag, PeerTag>(
        ctx, std::move(parent), SelfSide{std::forward<ResourceSelf>(self_resource), std::move(self_body), loc},
        PeerSide{std::forward<ResourcePeer>(peer_resource), std::move(peer_body), loc});
}

// The one form that gives the two endpoints to one caller.  It is a
// hatch for a test that drives the two sides on one thread in a fixed
// order, and the context gate admits only a context that holds the Test
// capability.  A production context has no Test capability, so it
// cannot reach this mint.
template <typename Ctx, typename Proto>
concept CtxFitsTestChannel = ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Test>
                          && WellFormedRunnableProtocol<Proto> && WellFormedRunnableProtocol<dual_of_t<Proto>>
                          && PermissionFlowCloses<Proto, ::foundation::permissions::EmptyPermSet>
                          && PermissionFlowCloses<dual_of_t<Proto>, ::foundation::permissions::EmptyPermSet>;

template <typename Proto, AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Ctx, typename ResourceA,
          typename ResourceB>
    requires CtxFitsTestChannel<Ctx, Proto> && SessionResource<ResourceA> && SessionResource<ResourceB>
[[nodiscard]] constexpr auto mint_test_channel(Ctx const&, ResourceA resource_a, ResourceB resource_b,
                                               std::source_location loc = std::source_location::current()) noexcept {
    return std::pair{
        detail::open_session_<Proto, ResourceA, Policy, ::foundation::permissions::EmptyPermSet>(
            std::forward<ResourceA>(resource_a), loc),
        detail::open_session_<dual_of_t<Proto>, ResourceB, Policy, ::foundation::permissions::EmptyPermSet>(
            std::forward<ResourceB>(resource_b), loc)};
}

}  // namespace fixy::session
