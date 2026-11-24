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
// PS is the last template parameter.  The handle keeps the token or the
// loan of each element of PS in its slots (HoldSlots of
// fixy/session/Flow.h), so the set and the tokens are one ledger.  A token
// type is empty, so the slots cost no byte.
// `Handle<Proto, PS, Resource, LoopCtx, Policy>` is the same class with
// the parameters in the order the design names them.
//
// PS changes at each step.  A send removes the permissions the message
// takes, and the matching receive adds them.  The step that moves a token
// takes it out of the slots of the sender and puts it into the slots of the
// receiver, so the caller gives the value of a marker and never its token.
// A Continue must see the PS that the loop saw at its entry.  A handle
// cannot reach End while PS holds an open loan, and close() at End gives
// back the token of each element of PS with the Resource.  The ownership
// delta of one message comes from fixy/session/Flow.h, and
// fixy/session/Admission.h reads it in one place,
// detail::perm_set_after_send_t and detail::perm_set_after_recv_t.
//
// ── What the Policy parameter is for ────────────────────────────────
//
// Stepping.h explains the decision; this header is where it lands.  The
// abandonment policy is a template parameter of SessionHandle, not a
// preprocessor branch inside it.  Under the two checking policies, a
// handle destroyed before its protocol ends aborts or sends a
// cancellation, in Release as in Debug.  Every operation also checks
// that the handle is live, so a moved-from handle aborts on its first
// use and cannot step a protocol a second time.
//
// ── What a handle does not do ───────────────────────────────────────
//
// It does not move bytes.  Every consumer method takes a Transport
// callable and invokes it against the Resource; the handle's job is to
// decide whether the operation is legal at this protocol position and
// What position follows it.  A session over a socket, over an in-memory
// ring and over a mock all use the same handle and differ only in the
// callable they pass.  A callable has one of the shapes of
// fixy/session/Transport.h, and no shape waits where the watch of
// fixy/session/Watch.h cannot see the wait.  A session over a pole of a
// channel takes no callable: its send and its receive take the context of
// the step, and they call the write and the read of the pole table of the
// channel (fixy/session/Admission.h).
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
//      with_session of fixy/session/Entry.h gives it at the type level for
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

#include <foundation/Closure.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Record.h>
#include <foundation/core/Report.h>
#include <foundation/core/Text.h>
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
#include <optional>
#include <source_location>
#include <type_traits>
#include <utility>

namespace fixy::session {

// The site of a mint: the location that the handle keeps for its reports,
// and the Site of foundation/core/Fatal.h that the record of the watch keeps
// (fixy/session/Watch.h).  The constructor is consteval, so a location of
// the run time cannot make a site, and the file of the Site is a text of
// static storage.  Each public mint takes the site as its last parameter,
// `MintSite site = {}`, so the default reads the place of the call of the
// mint.  A caller can give a constant location in its place.
//
// The two parts name strings of static storage that no thread writes, so the
// site moves to the thread of each side of a forked channel
// (foundation/Transfer.h).  The fork asks only that fact.  The record of the
// watch reads no function, so the Site names none, and the constructor
// walks no long name of a function of a template.
class[[= ::foundation::transfer::sendable{}]] MintSite {
    std::source_location loc_{};
    ::foundation::core::Site site_{};

public:
    consteval MintSite(std::source_location location = std::source_location::current()) noexcept
        : loc_{location}, site_{location.file_name(), static_cast<int>(location.line()), ""} {}

    // The location of the call of the mint, for the reports of the handle.
    [[nodiscard]] constexpr std::source_location location() const noexcept { return loc_; }

    // The Site of the call of the mint, for the record of the watch.
    [[nodiscard]] constexpr ::foundation::core::Site watch_site() const noexcept { return site_; }
};

namespace detail {

// What the factory gives to the handle that it builds: the record of the
// session in the watch, a site, and the count of the steps of a handle in a
// loop.  start_ and rewind give the site of the mint to the first handle of a
// session.  A step gives no site (handle_core::step_origin_), so it stores the
// same empty site on each step.  A handle that keeps no count of its steps
// (handle_core::keeps_step_count) does not read the count.
struct step_origin {
    watch::session_ref session{};
    std::source_location site{};
    std::uint64_t steps = 0;
};

// The count of the steps that led to a handle in a loop.  A loop gives a
// handle its type again at each iteration, so only the count tells two such
// handles of one session apart.  A handle outside each loop has a position
// that no step reaches again, so its type tells its time, and it keeps no
// count: the empty form takes no byte, and a step adds nothing to it.
template <bool KeepsCount>
class step_tally {
    std::uint64_t steps_ = 0;

public:
    constexpr step_tally() noexcept = default;
    constexpr explicit step_tally(step_origin const& origin) noexcept : steps_{origin.steps} {}

    // The copy reads and writes the field, not the class.  GCC 16 then keeps
    // the count of a local handle in a register across a loop of steps.  A
    // copy of the class makes an aggregate copy, and the loop then loads the
    // count from the stack in each iteration (test/shape/session_send.cpp).
    constexpr step_tally(step_tally const& other) noexcept : steps_{other.steps_} {}
    constexpr step_tally& operator=(step_tally const& other) noexcept {
        steps_ = other.steps_;
        return *this;
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return steps_; }
};

template <>
class step_tally<false> {
public:
    constexpr step_tally() noexcept = default;
    constexpr explicit step_tally(step_origin const& /*origin*/) noexcept {}

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return 0; }
};

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

// Writes the site of the mint of the session of `endpoint`, as the record of
// the watch keeps it, in the place of the site of a handle that names none.
// A handle that a step built names no site (SessionHandleBase::step_origin_),
// and a handle of a session that the watch does not track has no record.
//
// One write gives the two forms: a precision of zero prints no digit of the
// line when no site is known.
[[gnu::cold, gnu::noinline]] inline void report_site_of_the_mint(watch::endpoint_id endpoint) noexcept {
    const watch::record_site minted = watch::site_of(endpoint);
    const ::foundation::core::TextView file =
        minted.file.match([](::foundation::core::StaticText text) noexcept { return text.view(); },
                          []() noexcept { return ::foundation::core::TextView{}; });
    const bool is_known = file.size() != 0;
    std::fprintf(stderr, "  Construction at:  %.*s%s%.*u%s\n", static_cast<int>(file.size()),
                 is_known ? ::foundation::core::detail::text_first_(file) : "",
                 is_known ? ":"
                          : "no site is known: the handle names none, and the watch keeps no record of its session",
                 is_known ? 1 : 0, is_known ? static_cast<unsigned>(minted.line) : 0U,
                 is_known ? ", the mint of the session" : "");
}

// The destructor's diagnostic for a dropped protocol.  It is outlined
// and cold, so the destructor of a handle holds one branch and one call.
[[noreturn, gnu::cold, gnu::noinline]] inline void
report_dropped_protocol(std::string_view wrapper, std::string_view full_type, std::string_view protocol,
                        std::string_view hint, std::source_location loc, watch::endpoint_id endpoint,
                        AbandonAction action) noexcept {
    const char* loc_file = loc.file_name();
    // file_name() returns "" for a default-constructed location, which
    // is what a handle that a step built carries.
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
        report_site_of_the_mint(endpoint);
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
    // A consumed handle holds no record, so the site of the mint of its
    // session is not known when the handle names no site, as a handle that
    // a step built does.  A precision of zero prints no digit of the line.
    std::fprintf(stderr,
                 "\n"
                 "fixy::session: USE OF A CONSUMED HANDLE\n"
                 "  Wrapper class:    %.*s\n"
                 "  Protocol head:    %.*s\n"
                 "  Construction at:  %s%s%.*u\n"
                 "The handle was moved from, stepped, closed or detached.  It holds no protocol\n"
                 "position.  Use the handle that the move or the step returned.\n",
                 static_cast<int>(wrapper.size()), wrapper.data(), static_cast<int>(protocol.size()), protocol.data(),
                 have_loc ? loc_file
                          : "no site is known: the handle names none, and a consumed handle holds no record of its "
                            "session",
                 have_loc ? ":" : "", have_loc ? 1 : 0, have_loc ? loc.line() : std::uint_least32_t{0});
    std::abort();
}

// The value that a send of the payload takes: the value of a marker of the
// slot form Move, Lend or Release (fixy/session/Flow.h), and the payload
// itself for each other form.
[[nodiscard]] consteval std::meta::info sent_value_of(std::meta::info payload) {
    const SlotForm form = std::meta::extract<SlotForm>(std::meta::substitute(^^slot_form_v, {payload}));
    if (form == SlotForm::Move || form == SlotForm::Lend || form == SlotForm::Release) {
        return std::meta::dealias(::fixy::session::transition::template_arguments_run(std::meta::dealias(payload))[0]);
    }
    return payload;
}

template <typename T>
using sent_value_t = [:sent_value_of(^^T):];

// The tag of a bare Permission.
template <typename T>
using token_tag_t = [:std::meta::dealias(std::meta::template_arguments_of(std::meta::dealias(^^T))[0]):];

// The refusal of a keyed reception whose read gave a word other than the
// label word of its message.  The peer sent a label that this position does
// not accept, so the two endpoints do not agree on the protocol.
[[noreturn, gnu::cold, gnu::noinline]] inline void
report_foreign_label_word(std::uint64_t word, std::uint64_t expected, ::foundation::core::TextView protocol) noexcept {
    ::foundation::core::fatal("fixy::session: diagnostic [Keyed_Label_Word_Mismatch]: a keyed reception read the "
                              "word {}, and its message has the label word {}.  The peer sent a label that this "
                              "position does not accept, so the two endpoints do not agree on the protocol {}.",
                              word, expected, protocol);
}

// The refusal of an Offer whose read gave a word that names none of its
// branches.  The peer sent a label that this protocol does not define.
[[noreturn, gnu::cold, gnu::noinline]] inline void
report_word_names_no_branch(std::uint64_t word, std::size_t branch_count,
                            ::foundation::core::TextView protocol) noexcept {
    ::foundation::core::fatal("fixy::session: diagnostic [Word_Names_No_Branch]: an Offer read the word {}, which "
                              "names none of its {} branches.  The peer sent a label that this protocol does not "
                              "define, so the two endpoints do not agree on the protocol {}.",
                              word, branch_count, protocol);
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
// aborts.  Moving marks the source consumed, so a moved-from handle
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

    // The site of the handle, which the turn of a pin keeps.
    [[nodiscard]] constexpr std::source_location construction_loc_() const noexcept {
        return tracker_.construction_loc();
    }

    // The one holder check of a pin (HandleFactory::pin), under a tracked
    // policy.
    constexpr void hold_on_this_thread_() noexcept { tracker_.hold_on_this_thread(); }

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
                                                tracker_.session().endpoint, Policy::action);
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
    //
    // The kind self-move of the rule must-consume refuses each self-move
    // that one function names: a local, a local through a reference to it, a
    // member, and an object behind one reference or one pointer.  The guard
    // stays as the run-time backstop of an alias through two names, such as
    // two reference parameters or two indices of one array, which the rule
    // cannot see.  A step builds the next handle by move construction, and a
    // caller assigns it to a different object, so the compiler folds the
    // compare in each steady loop of a send (test/shape/session_send.cpp).
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
// on a wire: the Resource states the Local network, and neither the
// protocol nor the body of the loop around it names a peer.
template <typename Resource, typename Proto, typename LoopCtx>
concept AdmitsLocalChoice = LocalCarrier<Resource> && detail::distinct_peer_count(^^Proto, ^^LoopCtx) == 0;

// True when the Transport of a select may write the word of the branch:
// every write except no_label, and no_label where a local choice is
// admitted.
template <typename Transport, typename Resource, typename Proto, typename LoopCtx>
concept WritesTheLabel = !std::is_same_v<Transport, no_label_t> || AdmitsLocalChoice<Resource, Proto, LoopCtx>;

// The context of select_local and pick_local.
template <typename Ctx>
concept CtxPicksLocally = ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Test>;

namespace detail {

// True when a type names a session handle in one hop: a SessionHandle, or a
// Ref of one.  The facts of a capture give the target with the reference or
// the pointer removed (foundation/Closure.h), so a reference and a pointer to
// a handle name it too.
[[nodiscard]] consteval bool names_a_session_handle(std::meta::info type) {
    std::meta::info bare = std::meta::dealias(std::meta::remove_cv(type));
    if (std::meta::has_template_arguments(bare) && std::meta::template_of(bare) == ^^::foundation::core::Ref) {
        bare = std::meta::dealias(std::meta::remove_cv(transition::template_arguments_run(bare)[0]));
    }
    return std::meta::has_template_arguments(bare) && std::meta::template_of(bare) == ^^SessionHandle;
}

// True when a body of a pin reaches a session handle through a direct
// capture (HandleFactory::pin).  The check reads only the direct captures,
// and it does not follow this or another pointer to a class.  A body whose
// captures the walk cannot read reaches a handle as far as the pin knows.
// Complexity: linear in the captures.
[[nodiscard]] consteval bool pin_body_reaches_a_handle(std::meta::info body) {
    const ::foundation::closure::ClosureFacts facts = ::foundation::closure::closure_facts(body);
    if (!facts.is_readable()) return true;
    for (const ::foundation::closure::CaptureFact& fact : facts) {
        if (names_a_session_handle(fact.target)) return true;
    }
    return false;
}

// True when the result of a body of a pin is a handle over the Resource of the
// pin, under the pinned policy of the pin.
[[nodiscard]] consteval bool is_a_handle_of_the_pin(std::meta::info result, std::meta::info pinned) {
    const std::meta::info given = std::meta::dealias(result);
    if (!std::meta::has_template_arguments(given) || std::meta::template_of(given) != ^^SessionHandle) return false;
    const transition::static_run<std::meta::info> got = transition::template_arguments_run(given);
    const transition::static_run<std::meta::info> want = transition::template_arguments_run(std::meta::dealias(pinned));
    return std::meta::dealias(got[1]) == std::meta::dealias(want[1])
        && std::meta::dealias(got[3]) == std::meta::dealias(want[3]);
}

// The body of a pin takes the pinned handle, and it gives back a pinned handle
// over the same Resource, at the position where the body stops.
template <typename Body, typename Pinned>
concept PinBodyGivesAPinnedHandle =
    std::is_invocable_v<Body, Pinned> && is_a_handle_of_the_pin(^^std::invoke_result_t<Body, Pinned>, ^^Pinned);

}  // namespace detail

// A body of a pin: a callable whose direct captures name no session handle
// (HandleFactory::pin).
template <typename Body>
concept PinBody = !detail::pin_body_reaches_a_handle(^^std::remove_cvref_t<Body>);

namespace detail {
template <typename Proto, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
class handle_core;
}  // namespace detail

// The result of a send with a stop test (HandleFactory::send): the
// successor, and the value when the stop test reported before the
// transport took it.
template <typename Next, typename T>
struct [[nodiscard]] StoppedSend {
    Next next;
    ::fixy::Option<T> kept;
};

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
//   - rewind takes a handle at End of a session that is no channel, and
//     opens a new session on its Resource at a position that the gate of
//     rewind admits.  rewind_channel does the same for the two ends of a
//     channel, and links the two new records in the watch.
//
// No object of the class exists, and foundation/NoObject.h tells why no
// constructor and no byte route makes one.
class HandleFactory final : ::foundation::NoObject<HandleFactory> {
    // Builds the handle at Proto as it stands.  Each caller is a step, which
    // gives the record and the site.
    template <typename Proto, typename Resource, typename LoopCtx, AbandonmentPolicy Policy,
              typename PS = ::foundation::permissions::EmptyPermSet>
    [[nodiscard]] static constexpr auto make_(Resource r, HoldSlots<PS> slots, detail::step_origin origin) noexcept(
        std::is_nothrow_move_constructible_v<Resource>) -> SessionHandle<Proto, Resource, LoopCtx, Policy, PS>;

    // Turns the pinned handle that the body of a pin gives back to the
    // tracked policy of the same action (pin).  A handle at End keeps the mark
    // of its record.
    template <typename Proto, typename Resource, typename LoopCtx, AbandonAction Action, typename PS>
    [[nodiscard]] static constexpr auto
    turn_back_(SessionHandle<Proto, Resource, LoopCtx, check::detail::pinned_policy<Action>, PS>&& pinned) noexcept(
        std::is_nothrow_move_constructible_v<Resource>) {
        using Tracked = check::detail::tracked_policy<Action>;
        auto& core = core_(pinned);
        core.require_live_();
        const detail::step_origin origin = core.turn_origin_();
        auto [resource, slots] = std::move(core).release_parts_();
        if constexpr (std::is_same_v<Proto, End>) {
            auto back = make_<Proto, Resource, LoopCtx, Tracked, PS>(std::forward<Resource>(resource), std::move(slots),
                                                                     origin);
            back.mark_ = pinned.mark_;
            return back;
        } else {
            return make_<Proto, Resource, LoopCtx, Tracked, PS>(std::forward<Resource>(resource), std::move(slots),
                                                                origin);
        }
    }

    // Builds the handle at the head that R reaches after Continue, Loop, a
    // named state and a vendor pin resolve.  The slots go to the handle.
    //
    // The origin holds the site of the mint of the session.  start_ gives
    // it to the first handle, and each step gives the origin of the handle
    // that it consumes (detail::step_origin).
    template <typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
    [[nodiscard]] static constexpr auto step_(Resource r, HoldSlots<PS> slots, detail::step_origin origin) noexcept;

    // Builds the handle that a choice enters for branch I.
    template <typename Choice, std::size_t I, typename Resource, typename LoopCtx, AbandonmentPolicy Policy,
              typename PS>
    [[nodiscard]] static constexpr auto
    enter_(Resource r, HoldSlots<PS> slots,
           detail::step_origin origin) noexcept(std::is_nothrow_move_constructible_v<Resource>);

    // Builds the first handle of a protocol on a session record.
    template <typename Proto, typename Resource, AbandonmentPolicy Policy, typename PS, typename LoopCtx>
    [[nodiscard]] static constexpr auto start_(Resource r, HoldSlots<PS> slots, std::source_location loc,
                                               watch::session_ref session) noexcept;

    // Claims a new record for a session, with the priority of its
    // Resource, and names the calling thread as its holder.  The record
    // keeps the Site of the mint.
    template <typename Proto, typename Resource, AbandonmentPolicy Policy>
    [[nodiscard]] static constexpr watch::session_ref claim_(MintSite site) noexcept;

    // The core of a handle, where its Resource and its tracker are.  A storage
    // door: the core is the base of the handle that the step of the factory takes.
    template <typename Proto, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
    [[= ::foundation::linear::moves_own_storage{}]] [[nodiscard]] static constexpr auto&
        core_(SessionHandle<Proto, Resource, LoopCtx, Policy, PS>& handle) noexcept {
        detail::handle_core<Proto, Resource, LoopCtx, Policy, PS>& core = handle;
        return core;
    }

    // Calls the handler with the handle that the choice enters for the
    // branch that the wire word names, and first with the index as a
    // constant when PassesIndex holds.  The handler is invoked once per
    // branch type, and every branch must give the handler the same return
    // type, or all of them void.
    template <bool PassesIndex, typename Choice, typename Resource, typename LoopCtx, AbandonmentPolicy Policy,
              typename PS, typename Handler, std::size_t... Is>
    static constexpr auto dispatch_(std::size_t word, Resource res, HoldSlots<PS> slots, detail::step_origin origin,
                                    Handler handler, std::index_sequence<Is...>);

    // Calls the handler for branch I, with the index first when PassesIndex
    // holds.  The branch owns its handler and calls it one time through the
    // once door call_once of foundation/Linear.h, so the handler can hold a
    // linear value by move and consume it in the arm that runs.
    template <bool PassesIndex, std::size_t I, typename Handler, typename BranchHandle>
    static constexpr decltype(auto) call_branch_(Handler&& handler, BranchHandle&& branch_handle) {
        if constexpr (PassesIndex) {
            return ::foundation::linear::call_once(static_cast<Handler&&>(handler),
                                                   std::integral_constant<std::size_t, I>{},
                                                   std::forward<BranchHandle>(branch_handle));
        } else {
            return ::foundation::linear::call_once(static_cast<Handler&&>(handler),
                                                   std::forward<BranchHandle>(branch_handle));
        }
    }

    // The body of each send of a value (the public send below).  The
    // transport form of the send gives the transport of its caller, and the
    // pole form gives the write of the pole table.  One body holds the form
    // with a stop test and the form without one, so the value moves to the
    // transport through one reference in each form.
    template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Transport, typename Stop>
    [[nodiscard]] static constexpr auto
    send_value_(SessionHandle<Send<T, R>, Resource, LoopCtx, Policy, PS>&& handle, detail::sent_value_t<T> value,
                Transport transport,
                [[maybe_unused]] Stop stop) noexcept(detail::write_is_nothrow<Transport, Resource, T>()
                                                     && std::is_nothrow_invocable_v<Stop&>
                                                     && std::is_nothrow_move_constructible_v<Resource>
                                                     && std::is_nothrow_move_constructible_v<T>) {
        static_assert(PayloadTransfers<T>,
                      "fixy::session::diagnostic [Payload_Not_Owned]: the payload cannot move to another thread, or it "
                      "holds a borrow (Sendable and SendableOwned of foundation/Transfer.h).  Send a value that owns "
                      "each part that it holds.");
        static_assert(detail::handle_admits_send_v<PS, T>,
                      "fixy::session::diagnostic [PermissionImbalance]: the permission set does not hold what the "
                      "message takes, or the payload walk of fixy/session/Classification.h refuses the message.  "
                      "Hold each permission that the payload moves or lends before the send.");
        using Next = detail::perm_set_after_send_t<PS, T>;
        auto& core = core_(handle);
        core.require_live_();
        const watch::endpoint_id endpoint = core.session_().endpoint;
        const detail::step_origin origin = core.step_origin_();
        auto [resource, held_slots] = std::move(core).release_parts_();
        if constexpr (slot_form_v<T> != SlotForm::Plain) {
            auto [message, next_slots] = HoldFactory::message_from_slots<T>(std::move(held_slots), std::move(value));
            if constexpr (std::is_same_v<Stop, detail::no_write_stop>) {
                detail::write_through<T>(transport, resource, message, endpoint);
                return step_<R, Resource, LoopCtx, Policy, Next>(std::forward<Resource>(resource),
                                                                 std::move(next_slots), origin);
            } else {
                ::fixy::Option<T> kept = detail::write_unless<T>(transport, stop, resource, message, endpoint);
                auto next = step_<R, Resource, LoopCtx, Policy, Next>(std::forward<Resource>(resource),
                                                                      std::move(next_slots), origin);
                return StoppedSend<decltype(next), T>{std::move(next), std::move(kept)};
            }
        } else if constexpr (std::is_same_v<Stop, detail::no_write_stop>) {
            detail::write_through<T>(transport, resource, value, endpoint);
            HoldSlots<Next> slots = HoldFactory::as_set<Next>(std::move(held_slots));
            return step_<R, Resource, LoopCtx, Policy, Next>(std::forward<Resource>(resource), std::move(slots),
                                                             origin);
        } else {
            ::fixy::Option<T> kept = detail::write_unless<T>(transport, stop, resource, value, endpoint);
            HoldSlots<Next> slots = HoldFactory::as_set<Next>(std::move(held_slots));
            auto next =
                step_<R, Resource, LoopCtx, Policy, Next>(std::forward<Resource>(resource), std::move(slots), origin);
            return StoppedSend<decltype(next), T>{std::move(next), std::move(kept)};
        }
    }

    // The read of the value of a receive.  For a pole of a channel, `source`
    // is the context of the step, and the read of the pole table reads in it.
    // For each other Resource, `source` is the transport of the caller, and
    // the read moves it in, as a parameter by value does, so a function
    // decays to a pointer to it.
    template <typename T, typename Resource, typename Source>
    [[nodiscard]] static constexpr T read_value_(
        Resource& resource, Source&& source,
        watch::endpoint_id endpoint) noexcept(ChannelPole<Resource>
                                              || (detail::read_is_nothrow<std::decay_t<Source>, Resource, T>()
                                                  && std::is_nothrow_constructible_v<std::decay_t<Source>, Source>)) {
        if constexpr (ChannelPole<Resource>) {
            auto table_read = [&source](Resource& pole) noexcept -> ::fixy::Option<T> {
                return pole_table_t<Resource>::read(source, pole);
            };
            return detail::read_through<T>(table_read, resource, endpoint);
        } else {
            std::decay_t<Source> transport{std::forward<Source>(source)};
            return detail::read_through<T>(transport, resource, endpoint);
        }
    }

public:
    // Opens a session with a new record, or on a record that a channel
    // mint claimed.  The slots hold the tokens of PS that the mint consumed.
    template <typename Proto, typename Resource, AbandonmentPolicy Policy, typename PS, typename LoopCtx = void>
    [[nodiscard]] static constexpr auto open_(SessionOpenKey const&, Resource r, HoldSlots<PS> slots,
                                              MintSite site) noexcept;
    template <typename Proto, typename Resource, AbandonmentPolicy Policy, typename PS, typename LoopCtx = void>
    [[nodiscard]] static constexpr auto open_(SessionOpenKey const&, Resource r, HoldSlots<PS> slots, MintSite site,
                                              watch::endpoint_id endpoint) noexcept;

    // The type of the first handle of a session, for the gates that read
    // it.  It builds nothing.
    template <typename Proto, typename Resource, AbandonmentPolicy Policy, typename PS, typename LoopCtx>
    using first_handle = decltype(open_<Proto, Resource, Policy, PS, LoopCtx>(
        std::declval<SessionOpenKey const&>(), std::declval<Resource>(), std::declval<HoldSlots<PS>>(), MintSite{}));

    // The type of the handle at position R in the loop context LoopCtx,
    // after Continue, Loop, a named state and a vendor pin resolve.  It
    // builds nothing.
    template <typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy,
              typename PS = ::foundation::permissions::EmptyPermSet>
    using handle_at = decltype(step_<R, Resource, LoopCtx, Policy, PS>(
        std::declval<Resource>(), std::declval<HoldSlots<PS>>(), detail::step_origin{}));

    // Rewinds a session.  It takes a handle at End and opens a new session
    // on its Resource at position R in the loop context LoopCtx, with a new
    // record.  fixy/session/Checkpoint.h rolls back to a checkpoint with
    // it.  The gate is RewindableTo of fixy/session/Admission.h, so the
    // new session is one that a mint admits from its start, and the handle
    // at End belongs to no session that a callback entry owns.
    //
    // The handle at End must be the end of no channel.  The new record would
    // have no peer, and the watch would no longer see the channel, so an end
    // of a channel rewinds with its peer, through rewind_channel.  The gate
    // refuses each end of a forked channel when the program compiles, because
    // the body of each side brands it.  The precondition refuses an end whose
    // mark names a peer, as an end of a test channel or of a rewound channel.
    // The gate refuses a Resource that reaches a pole of a channel, because
    // the protocol of a pole comes from the pole table of its channel, and a
    // rewind names a protocol of its own.
    template <typename R, typename LoopCtx, typename Resource, typename EndLoopCtx, AbandonmentPolicy Policy>
        requires RewindableTo<R, LoopCtx, EndLoopCtx> && (!ReachesAChannelPole<Resource>)
    [[nodiscard]] static constexpr auto
    rewind(SessionHandle<End, Resource, EndLoopCtx, Policy, ::foundation::permissions::EmptyPermSet>&& at_end,
           MintSite site = {}) noexcept;

    // Rewinds the two ends of a channel together.  It takes the handle at
    // End of each end and opens a new session on each Resource: one at
    // position R in the loop context LoopCtx, and one at the dual of R.  The
    // step into End released the record of each end, so a rewind of one end
    // alone would claim a record with no peer, and rewind refuses such an
    // end.  This rewind links the two new records, as a channel mint does.
    // The gate is RewindableChannelTo of fixy/session/Admission.h.
    // The result holds the two handles, in the order of the arguments, and a
    // structured binding gives them out.
    //
    // The two handles must be the two ends of one channel.  The gate refuses
    // two ends whose types show two channels, and the precondition refuses
    // two ends whose marks do not name each other.  The gate refuses a
    // Resource that reaches a pole of a channel, as rewind does.
    template <typename Rewound, typename PeerRewound>
    struct RewoundChannel {
        Rewound rewound;
        PeerRewound peer_rewound;
    };

    template <typename R, typename LoopCtx, typename Resource, typename PeerResource, typename EndLoopCtx,
              typename PeerEndLoopCtx, AbandonmentPolicy Policy>
        requires RewindableChannelTo<R, LoopCtx, EndLoopCtx, PeerEndLoopCtx, Resource, PeerResource>
              && (!ReachesAChannelPole<Resource>) && (!ReachesAChannelPole<PeerResource>)
    [[nodiscard]] static constexpr auto rewind_channel(
        SessionHandle<End, Resource, EndLoopCtx, Policy, ::foundation::permissions::EmptyPermSet>&& at_end,
        SessionHandle<End, PeerResource, PeerEndLoopCtx, Policy, ::foundation::permissions::EmptyPermSet>&& peer_at_end,
        MintSite site = {}) noexcept;

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
        const detail::step_origin origin = core.step_origin_();
        auto [resource, slots] = std::move(core).release_parts_();
        return enter_<Offer<Branches...>, I, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(resource),
                                                                            std::move(slots), origin);
    }

    // ── The pin ───────────────────────────────────────────────────────
    //
    // Pins a handle to the calling thread for the run of a body.  The pin
    // makes the one holder check of the handle, and the one note of the
    // holder when the handle comes from another thread.  It builds the
    // handle at the same position under the pinned policy of the same action
    // (check::detail::pinned_policy of fixy/session/Stepping.h), calls the
    // body with it, and turns the pinned handle that the body returns back to
    // the tracked policy.  The pinned policy is thread_bound, so the handle
    // stays on the thread of the pin, and a step through it reads no slot of
    // the thread and calls no note.  The turn back builds the tracked handle
    // through its constructor, whose holder check finds the thread of the pin
    // and notes nothing.  That is one compare for each pin, outside the loop
    // of the body.  A turn with no check needs a turn tag through the five
    // constructors of SessionHandle and through its bases, and the compare
    // costs less.  A turn is no step, so the count of the steps and the site
    // stay.
    //
    // The pin takes the handle by rvalue, from a local or from an Option
    // member that it empties.  The body must not reach the handle while the
    // pin holds it.  PinBody refuses a body with a direct capture whose type,
    // with cv removed, is a SessionHandle, a reference or a pointer to one,
    // or a Ref of one.  The check does not follow this, or another pointer to
    // a class: a deep walk would refuse each stage whose class holds a handle,
    // and such a stage is the primary user of the pin.  So a use of the handle
    // through this during the pin is a use of a consumed handle when the
    // program runs, which the liveness check ends, or a none of the emptied
    // Option.
    //
    // A pin is a bind, the body and a release in one scope.  The pin calls
    // the body of its caller and does not own it, so the body holds no
    // linear value by value (HoldsNoLinearValue of foundation/Linear.h).
    template <typename Ctx, typename Proto, typename Resource, typename LoopCtx, AbandonAction Action, typename PS,
              typename Body>
        requires ::foundation::effects::IsExecCtx<Ctx> && (!is_terminal_state_v<Proto>)
              && PinBody<Body> && ::foundation::linear::HoldsNoLinearValue<Body>
              && detail::PinBodyGivesAPinnedHandle<
                     Body, SessionHandle<Proto, Resource, LoopCtx, check::detail::pinned_policy<Action>, PS>>
    [[nodiscard]] static constexpr auto
    pin(Ctx const& ctx, SessionHandle<Proto, Resource, LoopCtx, check::detail::tracked_policy<Action>, PS>&& handle,
        Body&& body) noexcept(std::is_nothrow_move_constructible_v<Resource>
                              && std::is_nothrow_invocable_v<
                                  Body,
                                  SessionHandle<Proto, Resource, LoopCtx, check::detail::pinned_policy<Action>, PS>>) {
        return release(std::forward<Body>(body)(bind(ctx, std::move(handle))));
    }

    template <typename Ctx, typename Proto, typename Resource, typename LoopCtx, AbandonAction Action, typename PS,
              typename Body>
        requires(!PinBody<Body>)
    static void pin(Ctx const&, SessionHandle<Proto, Resource, LoopCtx, check::detail::tracked_policy<Action>, PS>&&,
                    Body&&) = delete("[Pin_Body_Holds_A_Handle] the body of a pin captures a session handle, a "
                                     "reference or a pointer to one, or a Ref of one.  The pin holds the handle for "
                                     "the run of the body, so the body must reach it only through the pinned handle "
                                     "that it gets.");

    template <typename Ctx, typename Proto, typename Resource, typename LoopCtx, AbandonAction Action, typename PS,
              typename Body>
        requires PinBody<Body> && (!::foundation::linear::HoldsNoLinearValue<Body>)
    static void pin(Ctx const&, SessionHandle<Proto, Resource, LoopCtx, check::detail::tracked_policy<Action>, PS>&&,
                    Body&&) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

    // Binds a tracked handle to the calling thread for the rest of its life:
    // the one holder check of a pin, with no body.  The bound handle holds the
    // pinned policy of the same action, which is thread_bound and not
    // Shareable (fixy/session/Stepping.h).  So no door of foundation/Transfer.h
    // moves the handle to another thread, and no step or move of it reads the
    // slot of the thread.  A bind is no step, so the count of the steps and the
    // site stay.  release gives the tracked handle back, for a handle that must
    // cross a thread.
    template <typename Ctx, typename Proto, typename Resource, typename LoopCtx, AbandonAction Action, typename PS>
        requires ::foundation::effects::IsExecCtx<Ctx> && (!is_terminal_state_v<Proto>)
    [[nodiscard]] static constexpr auto
    bind(Ctx const&, SessionHandle<Proto, Resource, LoopCtx, check::detail::tracked_policy<Action>, PS>&&
                         handle) noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        auto& core = core_(handle);
        core.require_live_();
        core.hold_on_this_thread_();
        const detail::step_origin origin = core.turn_origin_();
        auto [resource, slots] = std::move(core).release_parts_();
        return make_<Proto, Resource, LoopCtx, check::detail::pinned_policy<Action>, PS>(
            std::forward<Resource>(resource), std::move(slots), origin);
    }

    // Gives a bound or a pinned handle back to the tracked policy of the same
    // action, at the same position.  The session goes on: the release ends no
    // record of the watch, and the tracked handle notes its holder at its first
    // step on another thread.
    template <typename Proto, typename Resource, typename LoopCtx, AbandonAction Action, typename PS>
    [[nodiscard]] static constexpr auto
    release(SessionHandle<Proto, Resource, LoopCtx, check::detail::pinned_policy<Action>, PS>&& pinned) noexcept(
        std::is_nothrow_move_constructible_v<Resource>) {
        return turn_back_(std::move(pinned));
    }

    // ── The steps ─────────────────────────────────────────────────────
    //
    // A step that moves a token or a loan builds the message from the slots
    // of the handle, or puts the message into them (the slot form of
    // fixy/session/Flow.h).  So a send of a marker takes the value of the
    // marker, a send of a bare Permission takes no value, and a receive of
    // one of them gives the value of the marker, or no value.

    // Sends a value.  The transport has one of the two write shapes of
    // fixy/session/Transport.h.
    // The returned handle sits at the continuation, with Continue and Loop
    // already resolved.
    //
    // A send with a stop test takes a trying write, and it tests `stop`
    // before each try.  The handle steps on in each case.  The result is a
    // StoppedSend: the successor, and the value when `stop` reported before
    // the transport took it.  The crash transport of
    // fixy/session/CrashTransport.h gives a payload to a crashed peer back
    // with it.
    //
    // A send of a marker builds the marker from the slots.  The form with a
    // stop test gives the message back when the stop test reported first: a
    // marker that it gives back holds the token or the loan that the slots
    // gave up, so the token leaves the session to the caller, as it leaves
    // through close() at End.  No form takes a token into the slots.
    //
    // A pole of a channel takes no transport of the caller.  Its send takes
    // the context of the step, and the step writes through the write of the
    // pole table of its channel (fixy/session/Admission.h).
    template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Transport, typename Stop = detail::no_write_stop>
        requires(!is_keyed_step_v<Send<T, R>>) && (slot_form_v<T> != SlotForm::Token) && (!ChannelPole<Resource>)
             && WriteTransport<Transport, Resource, T> && ::foundation::linear::HoldsNoLinearValue<Stop>
             && (std::is_same_v<Stop, detail::no_write_stop>
                 || (CallableTryWrite<Transport, Resource, T> && WriteStop<Stop>))
    [[nodiscard]] static constexpr auto
    send(SessionHandle<Send<T, R>, Resource, LoopCtx, Policy, PS>&& handle, detail::sent_value_t<T> value,
         Transport transport, Stop stop = Stop{}) noexcept(detail::write_is_nothrow<Transport, Resource, T>()
                                                           && std::is_nothrow_invocable_v<Stop&>
                                                           && std::is_nothrow_move_constructible_v<Resource>
                                                           && std::is_nothrow_move_constructible_v<T>) {
        return send_value_<T, R, Resource, LoopCtx, Policy, PS>(std::move(handle), std::move(value),
                                                                std::move(transport), std::move(stop));
    }

    // Sends a value over a pole of a channel, through the write of its pole
    // table, in the context of the step.  The write takes a reference to the
    // context for the step only.
    template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Ctx>
        requires(!is_keyed_step_v<Send<T, R>>) && (slot_form_v<T> == SlotForm::Plain) && PoleWritesIn<Resource, Ctx, T>
    [[nodiscard]] static constexpr auto
    send(SessionHandle<Send<T, R>, Resource, LoopCtx, Policy, PS>&& handle, Ctx const& ctx,
         detail::sent_value_t<T> value) noexcept(std::is_nothrow_move_constructible_v<Resource>
                                                 && std::is_nothrow_move_constructible_v<T>) {
        return send_value_<T, R, Resource, LoopCtx, Policy, PS>(
            std::move(handle), std::move(value),
            [&ctx](Resource& pole, T& sent) noexcept -> bool { return pole_table_t<Resource>::write(ctx, pole, sent); },
            detail::no_write_stop{});
    }

    template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename First, typename Second, typename... Rest>
        requires(!is_keyed_step_v<Send<T, R>>) && ChannelPole<Resource>
                 && (sizeof...(Rest) != 0 || !PoleWritesIn<Resource, std::remove_cvref_t<First>, T>)
    static void send(SessionHandle<Send<T, R>, Resource, LoopCtx, Policy, PS>&&, First&&, Second&&, Rest&&...) = delete(
        "[Pole_Owns_Its_Transport] a session over a pole of a channel sends through the write that the pole table of "
        "its channel names (fixy/session/Admission.h).  Call send(handle, ctx, value) with a context that the write of "
        "the table admits.  A transport of the caller would move the values of one channel in two ways.");

    // Sends a bare Permission.  The token comes from the slots of the
    // handle, so the caller gives no value.  No pole table moves a token, so
    // a pole of a channel refuses the send of a token.
    template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Transport>
        requires(!is_keyed_step_v<Send<T, R>>) && (slot_form_v<T> == SlotForm::Token) && (!ChannelPole<Resource>)
             && WriteTransport<Transport, Resource, T>
    [[nodiscard]] static constexpr auto
    send(SessionHandle<Send<T, R>, Resource, LoopCtx, Policy, PS>&& handle,
         Transport transport) noexcept(detail::write_is_nothrow<Transport, Resource, T>()
                                       && std::is_nothrow_move_constructible_v<Resource>) {
        static_assert(PayloadTransfers<T>,
                      "fixy::session::diagnostic [Payload_Not_Owned]: the payload cannot move to another thread, or it "
                      "holds a borrow (Sendable and SendableOwned of foundation/Transfer.h).  Send a value that owns "
                      "each part that it holds.");
        static_assert(detail::handle_admits_send_v<PS, T>,
                      "fixy::session::diagnostic [PermissionImbalance]: the permission set does not hold the token "
                      "that the message moves.");
        using Next = detail::perm_set_after_send_t<PS, T>;
        auto& core = core_(handle);
        core.require_live_();
        const watch::endpoint_id endpoint = core.session_().endpoint;
        const detail::step_origin origin = core.step_origin_();
        auto [resource, held_slots] = std::move(core).release_parts_();
        auto [token, rest] = HoldFactory::take_slot<detail::token_tag_t<T>>(std::move(held_slots));
        detail::write_through<T>(transport, resource, token, endpoint);
        return step_<R, Resource, LoopCtx, Policy, Next>(std::forward<Resource>(resource),
                                                         HoldFactory::as_set<Next>(std::move(rest)), origin);
    }

    template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Arg>
        requires(!is_keyed_step_v<Send<T, R>>) && (slot_form_v<T> == SlotForm::Token) && ChannelPole<Resource>
    static void send(SessionHandle<Send<T, R>, Resource, LoopCtx, Policy, PS>&&, Arg&&) = delete(
        "[Pole_Owns_Its_Transport] a token send over a pole of a channel writes a token, and the pole table of its "
        "channel (fixy/session/Admission.h) moves no token.  The send needs a pole table that moves the token.  A "
        "write of the caller would move the values of one channel in two ways.");

    // Sends the label word of a keyed message.  The returned handle stands
    // at the value step, or past the message when its payload is void.  No
    // pole table moves a label word, so a pole of a channel refuses a keyed
    // send.
    template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Transport>
        requires is_keyed_step_v<Send<T, R>> && (!ChannelPole<Resource>)
              && WriteTransport<Transport, Resource, std::size_t>
    [[nodiscard]] static constexpr auto
    send(SessionHandle<Send<T, R>, Resource, LoopCtx, Policy, PS>&& handle,
         Transport transport) noexcept(detail::write_is_nothrow<Transport, Resource, std::size_t>()
                                       && std::is_nothrow_move_constructible_v<Resource>) {
        static_assert(detail::handle_admits_send_v<PS, T>,
                      "fixy::session::diagnostic [PermissionImbalance]: the permission set does not hold what the "
                      "message takes, or the payload walk of fixy/session/Classification.h refuses the message.");
        auto& core = core_(handle);
        // A constant variable, so a label key that has no word stops the
        // build here with the refusal of fixy/session/Fold.h, which names
        // its rule.  A call in the body would make this step an immediate
        // function, and the compiler would print no reason.
        constexpr std::uint64_t step_word = detail::step_wire_word_of(^^Send<T, R>);
        std::size_t word = step_word;
        detail::write_through<std::size_t>(transport, core.live_resource_(), word, core.session_().endpoint);
        const detail::step_origin origin = core.step_origin_();
        auto [resource, slots] = std::move(core).release_parts_();
        return step_<keyed_landing_t<Send<T, R>>, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(resource),
                                                                                 std::move(slots), origin);
    }

    template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Arg>
        requires is_keyed_step_v<Send<T, R>> && ChannelPole<Resource>
    static void send(SessionHandle<Send<T, R>, Resource, LoopCtx, Policy, PS>&&, Arg&&) = delete(
        "[Pole_Owns_Its_Transport] a keyed send over a pole of a channel writes a label word, and the pole table of "
        "its channel (fixy/session/Admission.h) moves no label word.  The send needs a pole table that moves the "
        "label word.  A write of the caller would move the words of one channel in two ways.");

    // Receives a value.  The Record of foundation/core/Record.h holds the
    // value and the handle at the continuation, and a structured binding
    // gives the two out.  A polling read that finds the message costs one
    // call.  The token or the loan of a marker goes into the slots, and the
    // Record holds the value of the marker.  A bare Permission goes into the
    // slots, and the result is the handle alone.
    //
    // The source of the read is the transport of the caller, with one of the
    // two read shapes of fixy/session/Transport.h.  A pole of a channel takes
    // no transport of the caller: its source is the context of the step, and
    // the step reads through the read of the pole table of its channel
    // (fixy/session/Admission.h).  One step holds the two forms, so the Record
    // of each receive of a value has one source.
    template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Source>
        requires(!is_keyed_step_v<Recv<T, R>>)
             && ((!ChannelPole<Resource> && ReadTransport<std::remove_cvref_t<Source>, Resource, T>)
                 || PoleReadsIn<Resource, std::remove_cvref_t<Source>, T>)
    [[nodiscard]] static constexpr auto
    recv(SessionHandle<Recv<T, R>, Resource, LoopCtx, Policy, PS>&& handle,
         Source&& source) noexcept(std::is_nothrow_move_constructible_v<Resource>
                                   && std::is_nothrow_move_constructible_v<T>
                                   && (ChannelPole<Resource>
                                       || (detail::read_is_nothrow<std::decay_t<Source>, Resource, T>()
                                           && std::is_nothrow_constructible_v<std::decay_t<Source>, Source>))) {
        static_assert(
            PayloadTransfers<T>,
            "fixy::session::diagnostic [Payload_Not_Owned]: the payload cannot move to another thread, or it "
            "holds a borrow (Sendable and SendableOwned of foundation/Transfer.h).  Receive a value that owns "
            "each part that it holds.");
        static_assert(detail::handle_admits_recv_v<PS, T>,
                      "fixy::session::diagnostic [PermissionImbalance]: the permission set does not hold what the "
                      "message closes, the message gives a region that the set already holds, or the payload walk "
                      "of fixy/session/Classification.h refuses the message.");
        using Next = detail::perm_set_after_recv_t<PS, T>;
        auto& core = core_(handle);
        core.require_live_();
        const watch::endpoint_id endpoint = core.session_().endpoint;
        const detail::step_origin origin = core.step_origin_();
        auto [resource, held_slots] = std::move(core).release_parts_();
        T value = read_value_<T, Resource>(resource, std::forward<Source>(source), endpoint);
        if constexpr (slot_form_v<T> == SlotForm::Token) {
            HoldSlots<Next> slots =
                HoldFactory::as_set<Next>(HoldFactory::put_slot(std::move(held_slots), std::move(value)));
            return step_<R, Resource, LoopCtx, Policy, Next>(std::forward<Resource>(resource), std::move(slots),
                                                             origin);
        } else if constexpr (slot_form_v<T> != SlotForm::Plain) {
            auto [payload, next_slots] = HoldFactory::value_into_slots(std::move(held_slots), std::move(value));
            auto next = step_<R, Resource, LoopCtx, Policy, Next>(std::forward<Resource>(resource),
                                                                  std::move(next_slots), origin);
            return ::foundation::core::Record{std::move(payload), std::move(next)};
        } else {
            HoldSlots<Next> slots = HoldFactory::as_set<Next>(std::move(held_slots));
            auto next =
                step_<R, Resource, LoopCtx, Policy, Next>(std::forward<Resource>(resource), std::move(slots), origin);
            return ::foundation::core::Record{std::move(value), std::move(next)};
        }
    }

    template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Arg>
        requires(!is_keyed_step_v<Recv<T, R>>)
                 && ChannelPole<Resource> && (!PoleReadsIn<Resource, std::remove_cvref_t<Arg>, T>)
    static void recv(SessionHandle<Recv<T, R>, Resource, LoopCtx, Policy, PS>&&, Arg&&) = delete(
        "[Pole_Owns_Its_Transport] a session over a pole of a channel receives through the read that the pole table "
        "of its channel names (fixy/session/Admission.h).  Call recv(handle, ctx) with a context that the read of the "
        "table admits.  A transport of the caller would move the values of one channel in two ways.");

    // Reads the label word of a keyed message.  It returns the handle at
    // the value step, or past the message when its payload is void.  A
    // word other than the label word of this message ends the process with
    // Keyed_Label_Word_Mismatch: the peer sent a label that this position
    // does not accept, as a word that names no branch of an Offer does.  No
    // pole table moves a label word, so a pole of a channel refuses a keyed
    // receive.
    template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Transport>
        requires is_keyed_step_v<Recv<T, R>> && (!ChannelPole<Resource>)
              && ReadTransport<Transport, Resource, std::size_t>
    [[nodiscard]] static constexpr auto recv(SessionHandle<Recv<T, R>, Resource, LoopCtx, Policy, PS>&& handle,
                                             Transport transport) {
        static_assert(detail::handle_admits_recv_v<PS, T>,
                      "fixy::session::diagnostic [PermissionImbalance]: the permission set does not hold what the "
                      "message closes, or the payload walk of fixy/session/Classification.h refuses the message.");
        auto& core = core_(handle);
        // A constant variable, for the reason of the keyed send above.
        constexpr std::uint64_t step_word = detail::step_wire_word_of(^^Recv<T, R>);
        const std::size_t word =
            detail::read_through<std::size_t>(transport, core.live_resource_(), core.session_().endpoint);
        if (word != step_word) [[unlikely]] {
            detail::report_foreign_label_word(word, step_word, type_display_text_v<Recv<T, R>>);
        }
        const detail::step_origin origin = core.step_origin_();
        auto [resource, slots] = std::move(core).release_parts_();
        return step_<keyed_landing_t<Recv<T, R>>, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(resource),
                                                                                 std::move(slots), origin);
    }

    template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Arg>
        requires is_keyed_step_v<Recv<T, R>> && ChannelPole<Resource>
    static void recv(SessionHandle<Recv<T, R>, Resource, LoopCtx, Policy, PS>&&, Arg&&) = delete(
        "[Pole_Owns_Its_Transport] a keyed receive over a pole of a channel reads a label word, and the pole table of "
        "its channel (fixy/session/Admission.h) moves no label word.  The receive needs a pole table that moves the "
        "label word.  A read of the caller would move the words of one channel in two ways.");

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
    //
    // No pole table moves a label word, so a select over a pole of a
    // channel takes only no_label, and the choice puts no word on the wire.
    template <std::size_t I, typename... Branches, typename Resource, typename LoopCtx, AbandonmentPolicy Policy,
              typename PS, typename Transport>
        requires WriteTransport<Transport, Resource, std::size_t>
              && WritesTheLabel<Transport, Resource, Select<Branches...>, LoopCtx>
              && (!ChannelPole<Resource> || std::is_same_v<Transport, no_label_t>)
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
        const detail::step_origin origin = core.step_origin_();
        auto [resource, slots] = std::move(core).release_parts_();
        return enter_<Choice, I, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(resource), std::move(slots),
                                                                origin);
    }

    template <std::size_t I, typename... Branches, typename Resource, typename LoopCtx, AbandonmentPolicy Policy,
              typename PS, typename Transport>
        requires ChannelPole<Resource> && (!std::is_same_v<Transport, no_label_t>)
    static void select(SessionHandle<Select<Branches...>, Resource, LoopCtx, Policy, PS>&&, Transport) = delete(
        "[Pole_Owns_Its_Transport] a select over a pole of a channel puts no label on the wire, because the pole "
        "table of its channel (fixy/session/Admission.h) moves no label word.  Call select<I>(no_label) on a pole that "
        "states the Local network.  A select with a label needs a pole table that moves the label word.");

    // Enters branch I of a Select without telling the peer which branch was
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
        const detail::step_origin origin = core.step_origin_();
        auto [resource, slots] = std::move(core).release_parts_();
        return enter_<Choice, I, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(resource), std::move(slots),
                                                                origin);
    }

    // Receives the peer's wire word through Transport, a read of a
    // std::size_t word, then calls the handler with the handle for the
    // branch that the word names (branch_of_wire_word).  In a keyed choice
    // the handle of a label branch stands at the value step of its message,
    // or past the message when its payload is void.  A word that names no
    // branch ends the process with Word_Names_No_Branch: the peer has sent a
    // label this protocol does not define, so the two endpoints no longer
    // agree.  A select over a pole of a channel puts no label on the wire, so
    // a pole refuses a branch.
    template <typename... Branches, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Transport, typename Handler>
        requires(!ChannelPole<Resource>) && ReadTransport<Transport, Resource, std::size_t>
    static constexpr auto branch(SessionHandle<Offer<Branches...>, Resource, LoopCtx, Policy, PS>&& handle,
                                 Transport transport, Handler handler) {
        using Choice = Offer<Branches...>;
        auto& core = core_(handle);
        const std::size_t word =
            detail::read_through<std::size_t>(transport, core.live_resource_(), core.session_().endpoint);
        const detail::step_origin origin = core.step_origin_();
        auto [resource, slots] = std::move(core).release_parts_();
        return dispatch_<false, Choice, Resource, LoopCtx, Policy, PS>(
            word, std::forward<Resource>(resource), std::move(slots), origin, std::move(handler),
            std::make_index_sequence<Choice::branch_count>{});
    }

    template <typename... Branches, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Transport, typename Handler>
        requires ChannelPole<Resource>
    static void branch(SessionHandle<Offer<Branches...>, Resource, LoopCtx, Policy, PS>&&, Transport, Handler) = delete(
        "[Pole_Owns_Its_Transport] a branch over a pole of a channel reads a label word, and the pole table of its "
        "channel (fixy/session/Admission.h) moves no label word: a select over a pole puts no label on the wire.  The "
        "branch needs a pole table that moves the label word.  A test picks a branch with pick_local<I>(ctx).");

    // The step of branch, with the index of the branch given to the
    // handler first, as std::integral_constant<std::size_t, I>.
    template <typename... Branches, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Transport, typename Handler>
        requires(!ChannelPole<Resource>) && ReadTransport<Transport, Resource, std::size_t>
    static constexpr auto branch_indexed(SessionHandle<Offer<Branches...>, Resource, LoopCtx, Policy, PS>&& handle,
                                         Transport transport, Handler handler) {
        using Choice = Offer<Branches...>;
        auto& core = core_(handle);
        const std::size_t word =
            detail::read_through<std::size_t>(transport, core.live_resource_(), core.session_().endpoint);
        const detail::step_origin origin = core.step_origin_();
        auto [resource, slots] = std::move(core).release_parts_();
        return dispatch_<true, Choice, Resource, LoopCtx, Policy, PS>(word, std::forward<Resource>(resource),
                                                                      std::move(slots), origin, std::move(handler),
                                                                      std::make_index_sequence<Choice::branch_count>{});
    }

    template <typename... Branches, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Transport, typename Handler>
        requires ChannelPole<Resource>
    static void
    branch_indexed(SessionHandle<Offer<Branches...>, Resource, LoopCtx, Policy, PS>&&, Transport, Handler) = delete(
        "[Pole_Owns_Its_Transport] a branch over a pole of a channel reads a label word, and the pole table of its "
        "channel (fixy/session/Admission.h) moves no label word: a select over a pole puts no label on the wire.  The "
        "branch needs a pole table that moves the label word.  A test picks a branch with pick_local<I>(ctx).");

    // Enters branch I of an Offer without receiving the peer's label.  The
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
        const detail::step_origin origin = core.step_origin_();
        auto [resource, slots] = std::move(core).release_parts_();
        return enter_<Choice, I, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(resource), std::move(slots),
                                                                origin);
    }

    // Runs the body with a read view of a region that the handle borrows,
    // and hands the handle back at the same position.  The handle moves into
    // the call, so a body that names the handle through a capture finds it
    // consumed.  A body that returns nothing gives the handle back, and a
    // body that returns a value gives a Record of that value and the handle.
    template <typename Tag, typename Proto, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Body>
        requires(detail::holds_loan_of(^^PS, ^^Tag))
             && ::foundation::permissions::ReadViewBody<Body, Tag, detail::element_brand_t<PS, Tag>>
             && ::foundation::linear::HoldsNoLinearValue<Body>
    [[nodiscard]] static constexpr auto read(SessionHandle<Proto, Resource, LoopCtx, Policy, PS>&& handle,
                                             Body&& body) {
        core_(handle).require_live_();
        SessionHandle<Proto, Resource, LoopCtx, Policy, PS> held{std::move(handle)};
        auto& slots = core_(held).slots_;
        using Result = decltype(HoldFactory::read_slot<Tag>(slots, std::forward<Body>(body)));
        if constexpr (std::is_void_v<Result>) {
            HoldFactory::read_slot<Tag>(slots, std::forward<Body>(body));
            return held;
        } else {
            Result value = HoldFactory::read_slot<Tag>(slots, std::forward<Body>(body));
            return ::foundation::core::Record{std::move(value), std::move(held)};
        }
    }

    // A body that holds a linear value by value binds to this overload, so
    // the refusal gives its reason (HoldsNoLinearValue of foundation/Linear.h).
    template <typename Tag, typename Proto, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
              typename Body>
        requires(!::foundation::linear::HoldsNoLinearValue<Body>) && (detail::holds_loan_of(^^PS, ^^Tag))
                 && ::foundation::permissions::ReadViewBody<Body, Tag, detail::element_brand_t<PS, Tag>>
    static void read(SessionHandle<Proto, Resource, LoopCtx, Policy, PS>&& handle,
                     Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
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

public:
    // True when the handle keeps the count of its steps: a checking policy,
    // a position other than End, and an enclosing loop.  Only a Continue
    // gives a handle its type again, and a Continue stands only inside the
    // body of a loop, where the inner loop context is not void.  So a handle
    // outside each loop has a position that no step of its session reaches
    // again, and its type tells two handles of one session apart.  The test
    // compares types and walks no registry of the combinators, so a unit that
    // instantiates a handle walks no family of that namespace for it.
    static constexpr bool keeps_step_count =
        Policy::checks_abandonment && !std::is_same_v<Proto, End> && !std::is_void_v<session_loop_ctx_inner_t<LoopCtx>>;

private:
    // The count sits after the policy and before the Resource, so a handle in
    // a loop has the layout of a policy with a count in it.
    [[no_unique_address]] step_tally<keeps_step_count> tally_;

    Resource resource_;

    // The token or the loan of each element of PS (HoldSlots of
    // fixy/session/Flow.h).  Each step that moves a token takes it out of
    // these slots or puts it into them, so the set of the handle and its
    // tokens are one ledger.  A token type is empty, so the slots take no
    // byte.  They carry no flag of state: the handle is linear, and its policy
    // knows whether it is live.
    [[no_unique_address]] HoldSlots<PS> slots_;

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
    // The two parts of a core that its end gives: the Resource and the slots.
    // A Resource can be a reference, so the parts are a plain class and not a
    // Record.  The end gives each member, so no initializer runs.
    struct released_parts {
        Resource resource{};
        [[no_unique_address]] HoldSlots<PS> slots{};
    };

    // Ends the core.  It marks the core consumed and gives the Resource and
    // the slots together to the next handle, or to the step that moves a
    // token out of the slots.  A consumer reads the record, the endpoint and
    // the count of the core first, because the mark clears the endpoint.
    // Then it calls this door one time.
    [[nodiscard]] constexpr released_parts
    release_parts_() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        this->mark_consumed_();
        return released_parts{std::forward<Resource>(resource_), std::move(slots_)};
    }

    [[nodiscard]] constexpr Resource& live_resource_() noexcept {
        this->require_live_();
        return resource_;
    }

    // What a step gives to the handle that it builds: the record of the
    // session, no site, and the count of this handle.  A copy of the site
    // would cost a load and a store on each step, so a stepped handle names no
    // site, and the report of a dropped protocol reads the site of the mint
    // from the record of the session in the watch
    // (detail::report_dropped_protocol).  The step adds one to the count for a
    // handle that keeps one (HandleFactory::step_).
    [[nodiscard]] constexpr step_origin step_origin_() const noexcept { return {this->session_(), {}, tally_.value()}; }

    // What a turn of a pin gives to the handle that it builds: the record,
    // the site and the count of this handle (HandleFactory::pin).  A turn is
    // no step, so the count and the site stay.
    [[nodiscard]] constexpr step_origin turn_origin_() const noexcept {
        return {this->session_(), this->construction_loc_(), tally_.value()};
    }

public:
    using protocol = Proto;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using perm_set = PS;

    // The constructor takes the handle key, which only the handle factory
    // makes.  The handle over this core passes the key on, so a core is
    // built only when the factory builds its handle.
    constexpr handle_core(HandleKey const&, Resource r, HoldSlots<PS> slots,
                          step_origin origin) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : base_type{origin.site, origin.session},
          tally_{origin},
          resource_{std::forward<Resource>(r)},
          slots_{std::move(slots)} {}

    constexpr handle_core(handle_core&& other) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : base_type{std::move(other)},
          tally_{other.tally_},
          resource_{std::forward<Resource>(other.resource_)},
          slots_{std::move(other.slots_)} {}

    // The count of the steps that led to this handle in its session, for a
    // handle in a loop (keeps_step_count).  Two handles of one session at two
    // times have two counts.  A handle outside each loop has no count: its
    // type tells its time, so the call does not compile.
    [[nodiscard]] constexpr std::uint64_t step_count() const noexcept
        requires keeps_step_count
    {
        return tally_.value();
    }

    // An assignment over a live handle drops the protocol that the target
    // held.  The target's policy acts on that drop first, and only then
    // does it take the source's position and its slots.  A reference
    // Resource cannot be rebound, so a handle over one cannot be assigned.
    // A parked token cannot be assigned over, so a handle whose set holds a
    // LentOut cannot be assigned either.  A self-move keeps the handle as it
    // is.  Without the guard, the cancellation of a Cancel policy would reach
    // the peer, and the Resource would be moved into itself.  The guard is
    // the run-time backstop of an alias through two names, as the guard of
    // SessionHandleBase is.
    constexpr handle_core& operator=(handle_core&& other) noexcept
        requires(!std::is_reference_v<Resource> && std::is_nothrow_move_assignable_v<Resource>
                 && std::is_nothrow_move_assignable_v<HoldSlots<PS>>)
    {
        if (this == &other) [[unlikely]]
            return *this;
        cancel_if_owed_();
        base_type::operator=(std::move(other));
        tally_ = other.tally_;
        resource_ = std::move(other.resource_);
        slots_ = std::move(other.slots_);
        return *this;
    }

    ~handle_core() { cancel_if_owed_(); }

    // The named detach door of a handle (foundation/Linear.h).  It marks the
    // handle consumed without advancing the protocol, with a typed reason,
    // and the Resource ends with the handle.  The destructor check still
    // fires for each handle that does not detach, so an accidental drop stays
    // caught.
    //
    // The handle is the explicit object parameter, an rvalue reference to the
    // handle itself, so the rule must-consume sees the call consume the
    // handle and its linear Resource, such as a pole of a channel.  The
    // explicit object parameter states the rule of && in the constraint of
    // the door: an lvalue handle gives no Self that the constraint admits.
    template <typename Self, typename Reason>
        requires std::derived_from<Self, handle_core> && DetachReason<Reason>
    [[= ::foundation::linear::detach_door{}]] constexpr void detach(this Self&& self, Reason /*reason_tag*/) noexcept {
        self.require_live_();
        self.release_endpoint_();
        self.mark_consumed_();
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

    // Runs the body with a read view of a region that the handle borrows
    // (HandleFactory::read), and hands the handle back.  The explicit
    // object parameter names the handle class, so the read needs no cast
    // from the core to the handle.
    template <typename Tag, typename Self, typename Body>
        requires(!std::is_reference_v<Self>) && (detail::holds_loan_of(^^PS, ^^Tag))
             && ::foundation::permissions::ReadViewBody<Body, Tag, detail::element_brand_t<PS, Tag>>
             && ::foundation::linear::HoldsNoLinearValue<Body>
    [[nodiscard]] constexpr auto read(this Self&& self, Body&& body) {
        return HandleFactory::template read<Tag>(std::move(self), std::forward<Body>(body));
    }

    // A body that holds a linear value by value binds to this overload, so
    // the refusal gives its reason (HoldsNoLinearValue of foundation/Linear.h).
    template <typename Tag, typename Self, typename Body>
        requires(!::foundation::linear::HoldsNoLinearValue<Body>) && (!std::is_reference_v<Self>)
                 && (detail::holds_loan_of(^^PS, ^^Tag))
                 && ::foundation::permissions::ReadViewBody<Body, Tag, detail::element_brand_t<PS, Tag>>
    void read(this Self&& self, Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
};

}  // namespace detail

// ── The handle family ────────────────────────────────────────────────
//
// Each specialization offers the operations of its protocol head.  Its
// constructor takes the handle key, so only the handle factory builds
// one.  Each consumer method forwards to the step of the factory with
// the same name, which moves the message and builds the next handle.

namespace detail {

// The mark of the record that a step into End released (watch::channel_mark
// of fixy/session/Watch.h).  A policy that keeps no record keeps no mark, so
// a handle at End under check::Off stays the size of its Resource.
template <bool KeepsRecord>
struct end_mark {
    watch::channel_mark mark{};

    [[nodiscard]] constexpr watch::channel_mark value() const noexcept { return mark; }
};

template <>
struct end_mark<false> {
    [[nodiscard]] constexpr watch::channel_mark value() const noexcept { return {}; }
};

// What close() gives back: the Resource alone when the set is empty, and
// otherwise the Resource and the token of each tag, in one Record with the
// Resource first.  A Record holds no reference, so a Resource that is a
// reference with a set that is not empty gives the Resource, and the close
// of that handle is refused.
template <typename Resource, typename PS, bool = detail::perm_set_is_empty(^^PS) || std::is_reference_v<Resource>>
struct closed_of {
    using type = Resource;
};

template <typename Resource, typename... Elems>
struct closed_of<Resource, ::foundation::permissions::PermSet<Elems...>, false> {
    using type = ::foundation::core::Record<Resource, hold_slot_t<Elems>...>;
};

}  // namespace detail

template <typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
class [[nodiscard]]
SessionHandle<End, Resource, LoopCtx, Policy, PS> : public detail::handle_core<End, Resource, LoopCtx, Policy, PS> {
    using core_type = detail::handle_core<End, Resource, LoopCtx, Policy, PS>;
    friend class HandleFactory;

    static_assert(detail::perm_set_admits_close_v<PS>,
                  "fixy::session::diagnostic [PermissionImbalance]: the protocol reaches End while the "
                  "permission set holds an open loan, a LentOut or a BorrowedIn.  close() would lose it.  Return "
                  "each loan before End.");

    // The mark of the record that the step into End released.  The rewind
    // of a channel reads it to know that two handles at End are the two ends
    // of one channel.
    [[no_unique_address]] detail::end_mark<Policy::checks_abandonment> mark_{};

public:
    constexpr explicit SessionHandle(
        HandleKey const& key, Resource r, HoldSlots<PS> slots,
        detail::step_origin origin) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : core_type{key, std::forward<Resource>(r), std::move(slots), origin} {}

    // Closing is what hands the Resource back.  It is available only at
    // End, so a caller can recover the Resource only after the protocol
    // has run to completion.  A handle whose set holds tags gives back the
    // token of each tag from its slots too, in a Record after the Resource,
    // in the order of the set.  The slots of an empty set hold nothing, so the
    // close of an empty set drops them.
    [[nodiscard]] constexpr Resource close() && noexcept(std::is_nothrow_move_constructible_v<Resource>)
        requires(detail::perm_set_is_empty(^^PS))
    {
        this->require_live_();
        auto [resource, slots] = std::move(*this).release_parts_();
        return std::forward<Resource>(resource);
    }

    [[nodiscard]] constexpr typename detail::closed_of<Resource, PS>::type
    close() && noexcept(std::is_nothrow_move_constructible_v<Resource>)
        requires(!detail::perm_set_is_empty(^^PS) && !std::is_reference_v<Resource>)
    {
        this->require_live_();
        auto [resource, slots] = std::move(*this).release_parts_();
        return HoldFactory::close_slots(std::forward<Resource>(resource), std::move(slots));
    }

    void close() &&
        requires(!detail::perm_set_is_empty(^^PS) && std::is_reference_v<Resource>)
    = delete("fixy::session::diagnostic [Close_Tokens_With_A_Reference]: close() gives back the Resource and the "
             "tokens of the set in one Record, and a Record holds no reference.  Give the session a Resource that "
             "it owns, or send each token before End.");
};

template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
class [[nodiscard]] SessionHandle<Send<T, R>, Resource, LoopCtx, Policy, PS>
    : public detail::handle_core<Send<T, R>, Resource, LoopCtx, Policy, PS> {
    using core_type = detail::handle_core<Send<T, R>, Resource, LoopCtx, Policy, PS>;

public:
    constexpr explicit SessionHandle(
        HandleKey const& key, Resource r, HoldSlots<PS> slots,
        detail::step_origin origin) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : core_type{key, std::forward<Resource>(r), std::move(slots), origin} {}

    using message_type = T;
    using continuation = R;

    // A keyed message (a PeerMsg or a Labelled) is its label word and then
    // the value of its payload.  The step is a Select of one branch, and it
    // sends the word as the Select does (fixy/session/Protocol.h).  The
    // value step then sends the value (the section on the value of a keyed
    // message in fixy/session/Transport.h).
    static constexpr bool is_keyed = is_keyed_step_v<Send<T, R>>;

    // The slot form of the message (fixy/session/Flow.h).  The value of a
    // marker is the value that the sender gives, and the step takes the
    // token or the loan from the slots.  A bare Permission takes no value.
    static constexpr SlotForm slot_form = slot_form_v<T>;

    // The Transport is what physically moves the value to the peer.  It
    // has one of the two write shapes of fixy/session/Transport.h.  The
    // returned handle sits at the continuation, with Continue and Loop
    // already resolved.  For a marker, the value is the value of the
    // marker, and the Transport gets the marker that the step builds.  A
    // pole of a channel takes no Transport: its table names the write.
    template <typename Transport>
        requires(!is_keyed) && (slot_form != SlotForm::Token) && (!ChannelPole<Resource>)
             && WriteTransport<Transport, Resource, T>
    [[nodiscard]] constexpr auto send(detail::sent_value_t<T> value, Transport transport) && noexcept(
        detail::write_is_nothrow<Transport, Resource, T>()
        && std::is_nothrow_move_constructible_v<Resource> && std::is_nothrow_move_constructible_v<T>) {
        return HandleFactory::send(std::move(*this), std::move(value), std::move(transport));
    }

    template <typename Transport>
        requires(!is_keyed) && (slot_form != SlotForm::Token) && (!ChannelPole<Resource>)
                 && (!WriteShape<Transport, Resource, T>)
    void send(detail::sent_value_t<T>, Transport) && = delete(
        "[Transport_Shape] a write either tries, as bool(Resource&, T&), and returns false with the value unchanged "
        "while it has no room, or declares its wait, as void(Resource&, T&&, fixy::session::watch::wait_scope&).  A "
        "write of another shape can wait where the watch of fixy/session/Watch.h does not see it.");

    // A session over a pole of a channel sends through the write of the pole
    // table of its channel (fixy/session/Admission.h), in the context of the
    // step.  The write takes a reference to the context for the step only.
    template <typename Ctx>
        requires(!is_keyed) && (slot_form == SlotForm::Plain) && PoleWritesIn<Resource, Ctx, T>
    [[nodiscard]] constexpr auto send(Ctx const& ctx,
                                      T value) && noexcept(std::is_nothrow_move_constructible_v<Resource>
                                                           && std::is_nothrow_move_constructible_v<T>) {
        return HandleFactory::send(std::move(*this), ctx, std::move(value));
    }

    template <typename First, typename Second>
        requires(!is_keyed) && ChannelPole<Resource> && (!PoleWritesIn<Resource, std::remove_cvref_t<First>, T>)
    void send(First&&, Second&&) && = delete(
        "[Pole_Owns_Its_Transport] a session over a pole of a channel sends through the write that the pole table of "
        "its channel names (fixy/session/Admission.h).  Call send(ctx, value) with a context that the write of the "
        "table admits.  A transport of the caller would move the values of one channel in two ways.");

    // A marker that the caller built carries a token from outside the slots
    // of the handle.
    template <typename Transport>
        requires(!is_keyed) && detail::IsSlotMarker<T>
    void send(T, Transport) && = delete(
        "[Marker_From_The_Slots] the step builds the marker of this message from the slots of the handle.  Give the "
        "value of the marker, send(value, transport).  A marker that the caller builds carries a token from outside "
        "the slots, and the set of the handle would claim a token that it does not hold.");

    // Sends a bare Permission.  The token comes from the slots of the
    // handle, so the caller gives no value.  No pole table moves a token, so
    // a pole of a channel refuses the send of a token.
    template <typename Transport>
        requires(!is_keyed) && (slot_form == SlotForm::Token) && (!ChannelPole<Resource>)
             && WriteTransport<Transport, Resource, T>
    [[nodiscard]] constexpr auto
    send(Transport transport) && noexcept(detail::write_is_nothrow<Transport, Resource, T>()
                                          && std::is_nothrow_move_constructible_v<Resource>) {
        return HandleFactory::send(std::move(*this), std::move(transport));
    }

    template <typename Arg>
        requires(!is_keyed) && (slot_form == SlotForm::Token) && ChannelPole<Resource>
    void send(Arg&&) && = delete(
        "[Pole_Owns_Its_Transport] a token send over a pole of a channel writes a token, and the pole table of its "
        "channel (fixy/session/Admission.h) moves no token.  The send needs a pole table that moves the token.  A "
        "write of the caller would move the values of one channel in two ways.");

    template <typename U, typename Transport>
        requires(!is_keyed) && (slot_form == SlotForm::Token)
    void send(U&&, Transport) && = delete(
        "[Token_From_The_Slots] the step takes the token of this message from the slots of the handle.  Call "
        "send(transport).  A token that the caller gives comes from outside the slots, and the set of the handle "
        "would claim a token that it does not hold.");

    // Sends the label word of the message through Transport, a write of a
    // std::size_t word.  The peer can hold an Offer with more labels, and
    // it enters the branch of this word.  The returned handle stands at the
    // value step, or past the message when its payload is void.  No pole
    // table moves a label word, so a pole of a channel refuses a keyed send.
    template <typename Transport>
        requires is_keyed && (!ChannelPole<Resource>) && WriteTransport<Transport, Resource, std::size_t>
    [[nodiscard]] constexpr auto
    send(Transport transport) && noexcept(detail::write_is_nothrow<Transport, Resource, std::size_t>()
                                          && std::is_nothrow_move_constructible_v<Resource>) {
        return HandleFactory::send(std::move(*this), std::move(transport));
    }

    template <typename Arg>
        requires is_keyed && ChannelPole<Resource>
    void send(Arg&&) && = delete(
        "[Pole_Owns_Its_Transport] a keyed send over a pole of a channel writes a label word, and the pole table of "
        "its channel (fixy/session/Admission.h) moves no label word.  The send needs a pole table that moves the "
        "label word.  A write of the caller would move the words of one channel in two ways.");

    template <typename Transport>
        requires is_keyed && (!ChannelPole<Resource>) && (!WriteShape<Transport, Resource, std::size_t>)
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
    constexpr explicit SessionHandle(
        HandleKey const& key, Resource r, HoldSlots<PS> slots,
        detail::step_origin origin) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : core_type{key, std::forward<Resource>(r), std::move(slots), origin} {}

    using message_type = T;
    using continuation = R;

    // A keyed message is its label word and then the value of its payload.
    // The step is an Offer of one branch, and it reads the label word as the
    // Offer does.  The value step then reads the value.
    static constexpr bool is_keyed = is_keyed_step_v<Recv<T, R>>;

    // The source of the read is a Transport with one of the two read shapes
    // of fixy/session/Transport.h.  The Record holds the received value and
    // the handle at the continuation.  A polling read that finds the message
    // costs one call.  A session over a pole of a channel takes the context
    // of the step in place of a Transport, and it receives through the read
    // of the pole table of its channel (fixy/session/Admission.h).  The read
    // takes a reference to the context for the step only.  One member holds
    // the two forms, so the Record of each receive of a value has one source.
    template <typename Source>
        requires(!is_keyed)
             && ((!ChannelPole<Resource> && ReadTransport<std::remove_cvref_t<Source>, Resource, T>)
                 || PoleReadsIn<Resource, std::remove_cvref_t<Source>, T>)
    [[nodiscard]] constexpr auto recv(Source&& source) && noexcept(
        std::is_nothrow_move_constructible_v<Resource> && std::is_nothrow_move_constructible_v<T>
        && (ChannelPole<Resource>
            || (detail::read_is_nothrow<std::remove_cvref_t<Source>, Resource, T>()
                && std::is_nothrow_constructible_v<std::remove_cvref_t<Source>, Source>))) {
        return HandleFactory::recv(std::move(*this), std::forward<Source>(source));
    }

    template <typename Transport>
        requires(!is_keyed) && (!ChannelPole<Resource>) && (!ReadShape<Transport, Resource, T>)
    void recv(Transport) && = delete("[Transport_Shape] a read either polls, as fixy::Option<T>(Resource&), and "
                                     "returns no value while nothing is there, or declares its wait, as "
                                     "T(Resource&, fixy::session::watch::wait_scope&).  A read of another shape can "
                                     "wait where the watch of fixy/session/Watch.h does not see it.");

    template <typename Arg>
        requires(!is_keyed) && ChannelPole<Resource> && (!PoleReadsIn<Resource, std::remove_cvref_t<Arg>, T>)
    void recv(Arg&&) && = delete(
        "[Pole_Owns_Its_Transport] a session over a pole of a channel receives through the read that the pole table "
        "of its channel names (fixy/session/Admission.h).  Call recv(ctx) with a context that the read of the table "
        "admits.  A transport of the caller would move the values of one channel in two ways.");

    // Reads the label word of the message through Transport, a read of a
    // std::size_t word.  It returns the handle at the value step, or past
    // the message when its payload is void.  A word other than the label
    // word of this message ends the process with Keyed_Label_Word_Mismatch:
    // the peer sent a label that this position does not accept, as a word
    // that names no branch of an Offer does.  No pole table moves a label
    // word, so a pole of a channel refuses a keyed receive.
    template <typename Transport>
        requires is_keyed && (!ChannelPole<Resource>) && ReadTransport<Transport, Resource, std::size_t>
    [[nodiscard]] constexpr auto recv(Transport transport) && {
        return HandleFactory::recv(std::move(*this), std::move(transport));
    }

    template <typename Arg>
        requires is_keyed && ChannelPole<Resource>
    void recv(Arg&&) && = delete(
        "[Pole_Owns_Its_Transport] a keyed receive over a pole of a channel reads a label word, and the pole table of "
        "its channel (fixy/session/Admission.h) moves no label word.  The receive needs a pole table that moves the "
        "label word.  A read of the caller would move the words of one channel in two ways.");

    template <typename Transport>
        requires is_keyed && (!ChannelPole<Resource>) && (!ReadShape<Transport, Resource, std::size_t>)
    void recv(Transport) && = delete("[Transport_Shape] a read of a word either polls, as "
                                     "fixy::Option<std::size_t>(Resource&), or declares its wait, as "
                                     "std::size_t(Resource&, fixy::session::watch::wait_scope&).  A read of another "
                                     "shape can wait where the watch of fixy/session/Watch.h does not see it.");
};

template <typename... Branches, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS>
class [[nodiscard]] SessionHandle<Select<Branches...>, Resource, LoopCtx, Policy, PS>
    : public detail::handle_core<Select<Branches...>, Resource, LoopCtx, Policy, PS> {
    using core_type = detail::handle_core<Select<Branches...>, Resource, LoopCtx, Policy, PS>;

public:
    constexpr explicit SessionHandle(
        HandleKey const& key, Resource r, HoldSlots<PS> slots,
        detail::step_origin origin) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : core_type{key, std::forward<Resource>(r), std::move(slots), origin} {}

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
                                    "select<I>() to choose.  See mint_session for the full "
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
    // choice puts no word on a wire (Local choices, above).  No pole table
    // moves a label word, so a select over a pole of a channel takes only
    // no_label.
    template <std::size_t I, typename Transport>
        requires WriteTransport<Transport, Resource, std::size_t>
              && WritesTheLabel<Transport, Resource, Select<Branches...>, LoopCtx>
              && (!ChannelPole<Resource> || std::is_same_v<Transport, no_label_t>)
    [[nodiscard]] constexpr auto
    select(Transport transport) && noexcept(detail::write_is_nothrow<Transport, Resource, std::size_t>()
                                            && std::is_nothrow_move_constructible_v<Resource>) {
        return HandleFactory::template select<I>(std::move(*this), std::move(transport));
    }

    template <std::size_t I, typename Transport>
        requires(!WriteShape<Transport, Resource, std::size_t>)
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

    template <std::size_t I, typename Transport>
        requires WriteTransport<Transport, Resource, std::size_t> && ChannelPole<Resource>
                  && (!std::is_same_v<Transport, no_label_t>)
    void select(Transport) && = delete(
        "[Pole_Owns_Its_Transport] a select over a pole of a channel puts no label on the wire, because the pole "
        "table of its channel (fixy/session/Admission.h) moves no label word.  Call select<I>(no_label) on a pole that "
        "states the Local network.  A select with a label needs a pole table that moves the label word.");

    // Advances the local handle without telling the peer which branch
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
    constexpr explicit SessionHandle(
        HandleKey const& key, Resource r, HoldSlots<PS> slots,
        detail::step_origin origin) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : core_type{key, std::forward<Resource>(r), std::move(slots), origin} {}

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
                                    "is not a branch.  See mint_session for the full "
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
    //
    // A select over a pole of a channel puts no label on the wire, so a
    // pole refuses a branch.
    template <typename Transport, typename Handler>
        requires(!ChannelPole<Resource>) && ReadTransport<Transport, Resource, std::size_t>
    constexpr auto branch(Transport transport, Handler handler) && {
        return HandleFactory::branch(std::move(*this), std::move(transport), std::move(handler));
    }

    // The same step, and the handler also takes the index of the branch as
    // std::integral_constant<std::size_t, I>, first.  In a keyed choice two
    // branches can enter at the same handle type, and the index tells them
    // apart.
    template <typename Transport, typename Handler>
        requires(!ChannelPole<Resource>) && ReadTransport<Transport, Resource, std::size_t>
    constexpr auto branch_indexed(Transport transport, Handler handler) && {
        return HandleFactory::branch_indexed(std::move(*this), std::move(transport), std::move(handler));
    }

    template <typename Transport, typename Handler>
        requires ChannelPole<Resource>
    void branch(Transport, Handler) && = delete(
        "[Pole_Owns_Its_Transport] a branch over a pole of a channel reads a label word, and the pole table of its "
        "channel (fixy/session/Admission.h) moves no label word: a select over a pole puts no label on the wire.  The "
        "branch needs a pole table that moves the label word.  A test picks a branch with pick_local<I>(ctx).");

    template <typename Transport, typename Handler>
        requires ChannelPole<Resource>
    void branch_indexed(Transport, Handler) && = delete(
        "[Pole_Owns_Its_Transport] a branch over a pole of a channel reads a label word, and the pole table of its "
        "channel (fixy/session/Admission.h) moves no label word: a select over a pole puts no label on the wire.  The "
        "branch needs a pole table that moves the label word.  A test picks a branch with pick_local<I>(ctx).");

    template <typename Transport, typename Handler>
        requires(!ChannelPole<Resource>) && (!ReadShape<Transport, Resource, std::size_t>)
    void branch(Transport, Handler) && = delete("[Transport_Shape] a read of a word either polls, as "
                                                "fixy::Option<std::size_t>(Resource&), or declares its wait, as "
                                                "std::size_t(Resource&, fixy::session::watch::wait_scope&).  A read "
                                                "of another shape can wait where the watch of fixy/session/Watch.h "
                                                "does not see it.");

    // Assumes branch I without receiving the peer's label.  The context
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
constexpr auto HandleFactory::step_(Resource r, HoldSlots<PS> slots, detail::step_origin origin) noexcept {
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
        // recurses with the same origin.
        return step_<NextBody, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r), std::move(slots), origin);
    } else if constexpr (is_vendor_pinned_v<R>) {
        // The vendor is a declaration for the layer above.  The handle
        // steps the protocol that it pins.  This arm comes before the Loop
        // arm, because is_loop_v reads under a pin, and the body of a Loop
        // is read from the Loop itself.
        return step_<typename R::protocol, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r), std::move(slots),
                                                                          origin);
    } else if constexpr (is_named_state_v<R>) {
        // A name stands for its protocol, so the handle steps that protocol.
        // A name of a Loop resolves here, before the test for a Loop below.
        return step_<named_state_protocol_t<R>, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r),
                                                                               std::move(slots), origin);
    } else if constexpr (is_loop_v<R>) {
        using InnerBody = detail::loop_body_t<R>;
        using InnerCtx = session_loop_ctx_rebind_inner_t<LoopCtx, detail::loop_frame_t<R, PS>>;
        // Entering an inner Loop shadows the enclosing loop context.
        // That shadowing is what binds Continue to the nearest Loop.
        return step_<InnerBody, Resource, InnerCtx, Policy, PS>(std::forward<Resource>(r), std::move(slots), origin);
    } else {
        static_assert(is_head_v<R>, "fixy::session::diagnostic [Protocol_Ill_Formed]: "
                                    "unexpected protocol shape after resolution.  "
                                    "Only Send/Recv/Select/Offer/End/Continue are valid heads.");
        // A terminal handle holds no record and keeps no count.
        constexpr detail::step_origin terminal{};
        if constexpr (Policy::checks_abandonment && std::is_same_v<R, End>) {
            // The handle at End holds no record, and it keeps the mark of the
            // record.  The release clears the peer of the record, so the mark
            // is read first.
            auto at_end = make_<R, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r), std::move(slots),
                                                                  detail::step_origin{terminal.session, origin.site});
            if !consteval {
                at_end.mark_ = detail::end_mark<true>{watch::mark_of(origin.session.endpoint)};
                watch::release(origin.session.endpoint);
            }
            return at_end;
        } else if constexpr (Policy::checks_abandonment && is_terminal_state_v<R>) {
            if !consteval {
                watch::release(origin.session.endpoint);
            }
            return make_<R, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r), std::move(slots),
                                                           detail::step_origin{terminal.session, origin.site});
        } else if constexpr (SessionHandle<R, Resource, LoopCtx, Policy, PS>::keeps_step_count) {
            // A step counts one.  The turn of a pin is no step, and it keeps
            // the count (turn_origin_).
            return make_<R, Resource, LoopCtx, Policy, PS>(
                std::forward<Resource>(r), std::move(slots),
                detail::step_origin{origin.session, origin.site, origin.steps + 1});
        } else {
            // A handle outside each loop keeps no count, so the step adds none.
            return make_<R, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r), std::move(slots),
                                                           detail::step_origin{origin.session, origin.site});
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
HandleFactory::enter_(Resource r, HoldSlots<PS> slots,
                      detail::step_origin origin) noexcept(std::is_nothrow_move_constructible_v<Resource>) {
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
        return step_<keyed_landing_t<Branch>, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r),
                                                                             std::move(slots), origin);
    } else {
        return step_<Branch, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r), std::move(slots), origin);
    }
}

// A word that names no branch ends the process with Word_Names_No_Branch.
// The result type comes from branch 0.  A branch whose handler returns a
// different type then fails to convert into the single optional below,
// which is what enforces the same-return-type rule.
template <bool PassesIndex, typename Choice, typename Resource, typename LoopCtx, AbandonmentPolicy Policy, typename PS,
          typename Handler, std::size_t... Is>
constexpr auto HandleFactory::dispatch_(std::size_t word, Resource res, HoldSlots<PS> slots, detail::step_origin origin,
                                        Handler handler, std::index_sequence<Is...>) {
    const std::size_t idx = branch_of_wire_word<Choice>(word);
    if (idx >= Choice::branch_count) [[unlikely]] {
        detail::report_word_names_no_branch(word, Choice::branch_count, type_display_text_v<Choice>);
    }
    using FirstHandle = decltype(enter_<Choice, 0, Resource, LoopCtx, Policy, PS>(
        std::declval<Resource>(), std::declval<HoldSlots<PS>>(), origin));
    using Result = decltype(call_branch_<PassesIndex, 0>(std::declval<Handler&&>(), std::declval<FirstHandle>()));

    if constexpr (std::is_void_v<Result>) {
        bool dispatched = false;
        (
            [&]() {
                if (!dispatched && idx == Is) {
                    call_branch_<PassesIndex, Is>(std::move(handler),
                                                  enter_<Choice, Is, Resource, LoopCtx, Policy, PS>(
                                                      std::forward<Resource>(res), std::move(slots), origin));
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
                        std::move(handler), enter_<Choice, Is, Resource, LoopCtx, Policy, PS>(
                                                std::forward<Resource>(res), std::move(slots), origin)));
                    dispatched = true;
                }
            }(),
            ...);
        // An index below the branch count names one of the indices of the
        // fold above, so a branch ran.
        CRUCIBLE_FATAL_INVARIANT(result.has_value());
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
constexpr auto HandleFactory::make_(Resource r, HoldSlots<PS> slots,
                                    detail::step_origin origin) noexcept(std::is_nothrow_move_constructible_v<Resource>)
    -> SessionHandle<Proto, Resource, LoopCtx, Policy, PS> {
    static_assert(SessionResource<Resource>, "fixy::session::diagnostic [SessionResource_Refused]: the handle "
                                             "factory builds a handle only over a Resource that a mint admits.");
    return SessionHandle<Proto, Resource, LoopCtx, Policy, PS>{HandleKey{}, std::forward<Resource>(r), std::move(slots),
                                                               origin};
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
constexpr auto HandleFactory::start_(Resource r, HoldSlots<PS> slots, std::source_location loc,
                                     watch::session_ref session) noexcept {
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
    return step_<Proto, Resource, LoopCtx, Policy, PS>(std::forward<Resource>(r), std::move(slots),
                                                       detail::step_origin{session, loc});
}

template <typename Proto, typename Resource, AbandonmentPolicy Policy>
constexpr watch::session_ref HandleFactory::claim_(MintSite site) noexcept {
    // A constexpr variable is evaluated where it stands, so a Resource that
    // states its priority with the wrong type is refused here, with the
    // message of the refusal.
    constexpr watch::priority resource_priority = session_priority(^^Resource);
    watch::session_ref session{};
    if constexpr (Policy::checks_abandonment) {
        if !consteval {
            const watch::endpoint_id endpoint = watch::claim<type_display_text_v<Proto>>(
                site.watch_site(), resource_priority, watch::holder_on_claim::calling_thread);
            session = {endpoint, watch::current_thread_slot()};
        }
    }
    return session;
}

// Opens a session that has no record yet, and claims one for it with the
// priority of its Resource.  The calling thread holds the first handle.
template <typename Proto, typename Resource, AbandonmentPolicy Policy, typename PS, typename LoopCtx>
constexpr auto HandleFactory::open_(SessionOpenKey const&, Resource r, HoldSlots<PS> slots, MintSite site) noexcept {
    return start_<Proto, Resource, Policy, PS, LoopCtx>(std::forward<Resource>(r), std::move(slots), site.location(),
                                                        claim_<Proto, Resource, Policy>(site));
}

// Opens a session on a record that a channel mint claimed and linked to
// its peer.  The mint claimed it with no holder, and can run on a
// different thread, and the calling thread records itself as the holder.
template <typename Proto, typename Resource, AbandonmentPolicy Policy, typename PS, typename LoopCtx>
constexpr auto HandleFactory::open_(SessionOpenKey const&, Resource r, HoldSlots<PS> slots, MintSite site,
                                    watch::endpoint_id endpoint) noexcept {
    watch::session_ref session{};
    if constexpr (Policy::checks_abandonment) {
        if !consteval {
            session = {endpoint, watch::note_holder(endpoint)};
        }
    }
    return start_<Proto, Resource, Policy, PS, LoopCtx>(std::forward<Resource>(r), std::move(slots), site.location(),
                                                        session);
}

// The precondition reads the mark of the handle at End.  An end of a channel
// names its peer, and an end of a session that is no channel names none.  A
// policy that keeps no record has no mark to read, and a constant evaluation
// claims no record.
template <typename R, typename LoopCtx, typename Resource, typename EndLoopCtx, AbandonmentPolicy Policy>
    requires RewindableTo<R, LoopCtx, EndLoopCtx> && (!ReachesAChannelPole<Resource>)
constexpr auto HandleFactory::rewind(
    SessionHandle<End, Resource, EndLoopCtx, Policy, ::foundation::permissions::EmptyPermSet>&& at_end,
    MintSite site) noexcept {
    if constexpr (Policy::checks_abandonment) {
        if !consteval {
            CRUCIBLE_PRE(watch::is_end_of_no_channel(at_end.mark_.value()));
        }
    }
    Resource resource = std::move(at_end).close();
    return step_<R, Resource, LoopCtx, Policy, ::foundation::permissions::EmptyPermSet>(
        std::forward<Resource>(resource), HoldSlots<::foundation::permissions::EmptyPermSet>{},
        detail::step_origin{claim_<R, Resource, Policy>(site), site.location()});
}

// The precondition reads the marks of the two handles at End.  The two ends
// of one channel name each other (watch::are_ends_of_one_channel), and two
// ends of two channels, or of no channel, do not.  An end that the watch
// did not track names no record, so the rewind refuses it too.  A policy that
// keeps no record links no record, and it has no mark to read.  A constant
// evaluation claims no record, so it has no mark either.
//
// Each end rewinds to its position on a record of its own, which names the
// calling thread as its holder.  The gate gives the two Resources one
// priority, so the two records have the priority of one channel.  The link
// makes them its two ends before either end steps, so a position at End
// keeps the mark of the channel too.
template <typename R, typename LoopCtx, typename Resource, typename PeerResource, typename EndLoopCtx,
          typename PeerEndLoopCtx, AbandonmentPolicy Policy>
    requires RewindableChannelTo<R, LoopCtx, EndLoopCtx, PeerEndLoopCtx, Resource, PeerResource>
          && (!ReachesAChannelPole<Resource>) && (!ReachesAChannelPole<PeerResource>)
constexpr auto HandleFactory::rewind_channel(
    SessionHandle<End, Resource, EndLoopCtx, Policy, ::foundation::permissions::EmptyPermSet>&& at_end,
    SessionHandle<End, PeerResource, PeerEndLoopCtx, Policy, ::foundation::permissions::EmptyPermSet>&& peer_at_end,
    MintSite site) noexcept {
    if constexpr (Policy::checks_abandonment) {
        if !consteval {
            CRUCIBLE_PRE(watch::are_ends_of_one_channel(at_end.mark_.value(), peer_at_end.mark_.value()));
        }
    }
    using PeerR = dual_of_t<R>;
    using PeerLoopCtx = detail::dual_loop_ctx_t<LoopCtx>;
    Resource resource = std::move(at_end).close();
    PeerResource peer_resource = std::move(peer_at_end).close();
    const watch::session_ref session = claim_<R, Resource, Policy>(site);
    const watch::session_ref peer_session = claim_<PeerR, PeerResource, Policy>(site);
    if !consteval {
        watch::link(session.endpoint, peer_session.endpoint);
    }
    using NoSlots = HoldSlots<::foundation::permissions::EmptyPermSet>;
    auto rewound = step_<R, Resource, LoopCtx, Policy, ::foundation::permissions::EmptyPermSet>(
        std::forward<Resource>(resource), NoSlots{}, detail::step_origin{session, site.location()});
    auto peer_rewound = step_<PeerR, PeerResource, PeerLoopCtx, Policy, ::foundation::permissions::EmptyPermSet>(
        std::forward<PeerResource>(peer_resource), NoSlots{}, detail::step_origin{peer_session, site.location()});
    return RewoundChannel<decltype(rewound), decltype(peer_rewound)>{std::move(rewound), std::move(peer_rewound)};
}

namespace detail {

// The two linked records of a channel, one for each end, or none.
struct channel_records {
    watch::endpoint_id self = watch::endpoint_id::none;
    watch::endpoint_id peer = watch::endpoint_id::none;
};

// Claims the two linked records of a channel, one for each end, with the
// one priority of the channel.  The thread that claims them does not hold
// the ends: each end's holder is the thread that opens it.  Under
// check::Off it claims nothing.
template <typename SelfProto, typename PeerProto, AbandonmentPolicy Policy, watch::priority Order>
[[nodiscard]] inline channel_records claim_channel_(MintSite site) noexcept {
    if constexpr (Policy::checks_abandonment) {
        const watch::endpoint_id self =
            watch::claim<type_display_text_v<SelfProto>>(site.watch_site(), Order, watch::holder_on_claim::none_yet);
        const watch::endpoint_id peer =
            watch::claim<type_display_text_v<PeerProto>>(site.watch_site(), Order, watch::holder_on_claim::none_yet);
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

// The peer of a session of one end.  A mint of one end names one protocol,
// and its peer runs the dual of that protocol by the definition of a
// session, so the door asks no relation for it.  The gate of each such mint
// refuses a Resource that reaches a pole of a channel
// (fixy/session/Admission.h), so the two ends of one channel never pass the
// door as two sessions of one end.
struct one_end_peer {};

}  // namespace detail

// True when H is a handle at End of the session that Brand owns, and its
// close() gives back the Resource.
template <typename H, typename Resource, typename Brand>
concept ClosesInSessionOf = std::is_same_v<std::remove_cvref_t<H>, H> && requires { typename H::loop_ctx; }
                         && detail::carries_brand<typename H::loop_ctx, Brand> && requires(H handle) {
                                { std::move(handle).close() } -> std::same_as<Resource>;
                            };

namespace detail {

// The first handle of a session that an owning entry point opens: the
// session carries the brand of the body, under the owned form of the policy
// (check::detail::owned_policy_t of fixy/session/Stepping.h).
template <typename Proto, typename Resource, typename Policy, typename Body>
using owned_head_t = first_handle_t<Proto, Resource, check::detail::owned_policy_t<Policy>,
                                    ::foundation::permissions::EmptyPermSet, brand_ctx_t<Body>>;

}  // namespace detail

template <typename Body, typename Proto, typename Resource, typename Policy>
concept SessionBody = std::is_invocable_v<Body, detail::owned_head_t<Proto, Resource, Policy, Body>>
                   && ClosesInSessionOf<std::invoke_result_t<Body, detail::owned_head_t<Proto, Resource, Policy, Body>>,
                                        Resource, Body>;

namespace detail {

// The first handle of one side of a forked channel, branded with the
// body of that side.
template <typename Proto, typename Resource, typename Policy, typename Body>
using forked_head_t = owned_head_t<Proto, Resource, Policy, Body>;

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
    && ::fixy::concurrent::CtxFitsPermissionFork<Ctx, Parent, SelfTag, PeerTag> && PlainSessionResource<ResourceSelf>
    && PlainSessionResource<ResourcePeer> && ChannelEndsShareAPriority<ResourceSelf, ResourcePeer>
    && ChannelSidesAgree<SelfProto, PeerProto, ResourceSelf, ResourcePeer>
    && detail::ForkedEndpointBody<SelfBody, SelfProto, ResourceSelf, Policy, SelfTag, Brand, Ctx>
    && detail::ForkedEndpointBody<PeerBody, PeerProto, ResourcePeer, Policy, PeerTag, Brand, Ctx>;

// The two ends of a test channel of mint_test_channel: the end at the
// protocol over the first Resource, and the end at its dual over the second
// Resource.  A structured binding gives them out in that order.
template <typename SelfHandle, typename PeerHandle>
struct [[nodiscard]] TestChannelEnds {
    SelfHandle self;
    PeerHandle peer;
};

// ── The door of the mints ────────────────────────────────────────────
//
// The one friend of SessionOpenKey, so the one class that opens a session
// on the handle factory.  Each public member is one mint of this layer
// and states the whole gate of that mint in its requires clause, so a
// direct call of a member is no weaker than the mint.  Each member takes
// the execution context of the mint, and its gate admits the effect row
// of the protocol in that context.  The mints of fixy/session/Mint.h and
// fixy/session/Entry.h forward here for their names and their default
// arguments, through detail::late_door_t.  No object of the class exists.
class SessionMintDoor final : ::foundation::NoObject<SessionMintDoor> {
    // Opens the first handle of one end of a channel.  The end runs SelfProto
    // over ResourceSelf, and each end of the other side runs PeerProto over
    // ResourcePeer.  Each member of this class opens each first handle here,
    // so the relation between the two sides of a channel (ChannelSidesAgree
    // of fixy/session/Admission.h) stands in this one place for each form:
    // the test channel, the two forked forms and the pole of a channel name
    // the two protocols.  A session of one end names detail::one_end_peer as
    // its peer.  The crash and the checkpoint sessions open their inner
    // session through open, so they pass here too.  A delegated endpoint
    // already passed here at its first mint, and a hand-off opens no end.
    // Record is empty for an end that claims its own record, or the one
    // record that a channel mint claimed for the end.
    template <typename SelfProto, typename PeerProto, typename ResourceSelf, typename ResourcePeer,
              AbandonmentPolicy Policy, typename PS, typename LoopCtx = void, typename... Record>
        requires(sizeof...(Record) <= 1)
             && (std::is_same_v<PeerProto, detail::one_end_peer>
                 || ChannelSidesAgree<SelfProto, PeerProto, ResourceSelf, ResourcePeer>)
    [[nodiscard]] static constexpr auto open_end_(ResourceSelf resource, HoldSlots<PS> slots, MintSite site,
                                                  Record... record) noexcept {
        return HandleFactory::open_<SelfProto, ResourceSelf, Policy, PS, LoopCtx>(
            SessionOpenKey{}, std::forward<ResourceSelf>(resource), std::move(slots), site, record...);
    }

    // One side of a forked channel.  It builds the endpoint in the thread
    // that runs it, gives the endpoint and the fork's view of its child
    // permission to the body, and closes the handle that the body returns.
    // A struct and not a lambda, because a lambda capture cannot hold a
    // reference Resource by its declared type.  It is a private member, and
    // only fork_channel builds one.  The thread of the side opens the
    // endpoint, runs the body on it and closes the End handle that the body
    // returns.  So each handle of the side lives on that thread, and the side
    // opens its endpoint under the owned form of the policy.
    template <typename Proto, typename PeerProto, AbandonmentPolicy Policy, typename Resource, typename PeerResource,
              typename Body>
    struct forked_endpoint_ {
        Resource resource;
        Body body;
        MintSite site;
        // The record the channel mint claimed for this end, linked to the
        // record of the other end.
        watch::endpoint_id endpoint = watch::endpoint_id::none;

        template <typename View, typename Ctx>
        void operator()(View const& view, Ctx const& ctx) noexcept {
            auto head = open_end_<Proto, PeerProto, Resource, PeerResource, check::detail::owned_policy_t<Policy>,
                                  ::foundation::permissions::EmptyPermSet, detail::brand_ctx_t<Body>>(
                std::forward<Resource>(resource), no_slots_{}, site, endpoint);
            auto at_end = std::invoke(std::move(body), std::move(head), view, ctx);
            static_cast<void>(std::move(at_end).close());
        }
    };

    // The slots of the empty set, which hold nothing.
    using no_slots_ = HoldSlots<::foundation::permissions::EmptyPermSet>;

public:
    // mint_session: a session with the empty permission set.
    template <typename Proto, AbandonmentPolicy Policy, typename Ctx, typename Resource>
        requires CtxFitsSessionFrom<Ctx, Proto, Resource>
    [[nodiscard]] static constexpr auto open(Ctx const&, Resource r, MintSite site) noexcept {
        return open_end_<Proto, detail::one_end_peer, Resource, Resource, Policy,
                         ::foundation::permissions::EmptyPermSet>(std::forward<Resource>(r), no_slots_{}, site);
    }

    // mint_permissioned_session: consumes one token for each tag, and
    // opens the session with the set of those tokens, each with its brand.
    // The first handle keeps the tokens in its slots, so the set of the handle
    // and its tokens are one ledger from the first step on.
    template <typename Proto, AbandonmentPolicy Policy, typename Ctx, typename Resource, typename... Tags,
              typename... Brands>
        requires CtxFitsPermissionedSession<Ctx, Proto, Resource,
                                            ::foundation::permissions::Permission<Tags, Brands>...>
    [[nodiscard]] static constexpr auto
    open_permissioned(Ctx const&, Resource resource,
                      ::foundation::permissions::Permission<Tags, Brands>... tokens) noexcept {
        using Set = ::foundation::permissions::PermSet<::foundation::permissions::Permission<Tags, Brands>...>;
        return open_end_<Proto, detail::one_end_peer, Resource, Resource, Policy, Set>(
            std::forward<Resource>(resource), HoldFactory::slots_of(std::move(tokens)...), MintSite{});
    }

    // mint_substrate_session of fixy/channels/SubstrateSessionBridge.h and
    // the session mints of fixy/channels/SwmrSession.h: opens a pole of a
    // channel on the protocol that the pole table of the channel names
    // (fixy/session/Admission.h).  The caller names no protocol, so the poles
    // of one channel run the two protocols of one table, and the relation of
    // the two sides holds for them.  The session owns the pole.
    template <AbandonmentPolicy Policy, typename Ctx, typename Pole>
        requires CtxFitsPoleSession<Ctx, Pole>
    [[nodiscard]] static constexpr auto open_pole(Ctx const&, Pole pole, MintSite site = {}) noexcept {
        return open_end_<pole_protocol_t<Pole>, pole_peer_protocol_t<Pole>, Pole, Pole, Policy,
                         ::foundation::permissions::EmptyPermSet>(std::move(pole), no_slots_{}, site);
    }

    // with_session: runs the body on the first handle of a session that
    // carries the brand of the body, and closes the End handle that the
    // body returns.  The calling thread opens the session, runs the body and
    // closes the End handle, so each handle of the session lives on that
    // thread.  Thus the session opens under the owned form of the policy.
    // The door owns the body and calls it one time through the once door
    // call_once of foundation/Linear.h, so the body can hold a linear value
    // by move and consume it in its one call.
    template <typename Proto, AbandonmentPolicy Policy, typename Ctx, typename Resource, typename Body>
        requires CtxFitsSessionFrom<Ctx, Proto, Resource> && SessionBody<Body, Proto, Resource, Policy>
    [[nodiscard]] static constexpr Resource run(Ctx const&, Resource r, Body body, MintSite site) noexcept(
        std::is_nothrow_invocable_v<Body, detail::owned_head_t<Proto, Resource, Policy, Body>>) {
        auto at_end = ::foundation::linear::call_once(
            std::move(body),
            open_end_<Proto, detail::one_end_peer, Resource, Resource, check::detail::owned_policy_t<Policy>,
                      ::foundation::permissions::EmptyPermSet, detail::brand_ctx_t<Body>>(std::forward<Resource>(r),
                                                                                          no_slots_{}, site));
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
                 MintSite site = {}) noexcept {
        using SelfSide = forked_endpoint_<SelfProto, PeerProto, Policy, ResourceSelf, ResourcePeer, SelfBody>;
        using PeerSide = forked_endpoint_<PeerProto, SelfProto, Policy, ResourcePeer, ResourceSelf, PeerBody>;
        const auto [self_endpoint, peer_endpoint] =
            detail::claim_channel_<SelfProto, PeerProto, Policy, session_priority(^^ResourceSelf)>(site);
        return ::fixy::concurrent::mint_permission_fork<SelfTag, PeerTag>(
            ctx, std::move(parent),
            SelfSide{std::forward<ResourceSelf>(self_resource), std::move(self_body), site, self_endpoint},
            PeerSide{std::forward<ResourcePeer>(peer_resource), std::move(peer_body), site, peer_endpoint});
    }

    // mint_test_channel: the two endpoints of one channel to one caller, for
    // a context that holds the Test capability.
    template <typename Proto, AbandonmentPolicy Policy, typename Ctx, typename ResourceA, typename ResourceB>
        requires CtxFitsTestChannel<Ctx, Proto> && PlainSessionResource<ResourceA> && PlainSessionResource<ResourceB>
              && ChannelEndsShareAPriority<ResourceA, ResourceB>
    [[nodiscard]] static constexpr auto open_test_channel(Ctx const&, ResourceA resource_a, ResourceB resource_b,
                                                          MintSite site) noexcept {
        detail::channel_records records{};
        if !consteval {
            records = detail::claim_channel_<Proto, dual_of_t<Proto>, Policy, session_priority(^^ResourceA)>(site);
        }
        auto self =
            open_end_<Proto, dual_of_t<Proto>, ResourceA, ResourceB, Policy, ::foundation::permissions::EmptyPermSet>(
                std::forward<ResourceA>(resource_a), no_slots_{}, site, records.self);
        auto peer =
            open_end_<dual_of_t<Proto>, Proto, ResourceB, ResourceA, Policy, ::foundation::permissions::EmptyPermSet>(
                std::forward<ResourceB>(resource_b), no_slots_{}, site, records.peer);
        return TestChannelEnds<decltype(self), decltype(peer)>{std::move(self), std::move(peer)};
    }
};

}  // namespace fixy::session
