#pragma once

// The runtime half of the binary session core: the handle that carries
// a protocol position, the linearity contract that keeps exactly one
// handle alive per position, and the one factory that builds a handle
// from a protocol and a resource.
//
// Protocol.h is the type level and has no runtime representation.  This
// header is where a protocol becomes an object: `SessionHandle<Proto,
// Resource, LoopCtx, Policy>` holds the Resource and offers exactly the
// operations Proto's head admits.  Every operation is `&&`-qualified,
// consumes the handle, and returns the handle at the next position, so
// a protocol is walked rather than queried.
//
// ── What the Policy parameter is for ────────────────────────────────
//
// Stepping.h explains the decision; this header is where it lands.  The
// abandonment policy is a TEMPLATE PARAMETER of SessionHandle, not a
// preprocessor branch inside it, so a Debug translation unit and a
// Release one name different types instead of giving one name two
// layouts.  Everything downstream defaults the parameter and never
// spells it, so the cost of naming it is one word in one place.
//
// ── What a handle does NOT do ───────────────────────────────────────
//
// It does not move bytes.  Every consumer method takes a Transport
// callable and invokes it against the Resource; the handle's job is to
// decide WHETHER the operation is legal at this protocol position and
// WHAT position follows it.  A session over a socket, over an in-memory
// ring and over a mock all use the same handle and differ only in the
// callable they pass.

#include <fixy/session/Stepping.h>

#include <foundation/Pinned.h>

#include <concepts>
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
          AbandonmentPolicy Policy = DefaultAbandonmentPolicy>
class SessionHandle;

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
template <typename Proto, typename Derived = void, AbandonmentPolicy Policy = DefaultAbandonmentPolicy>
class SessionHandleBase {
    [[no_unique_address]] Policy tracker_;

protected:
    // Call before returning from a consumer method.  After it, the
    // destructor check sees the tracker marked and skips the abort.
    constexpr void mark_consumed_() noexcept { tracker_.mark(); }

    constexpr bool is_consumed_() const noexcept { return tracker_.was_marked(); }

    // A sibling handle instantiation can mark another handle consumed,
    // which is how a handle shipped to a peer is retired.
    template <typename OtherProto, typename OtherDerived, AbandonmentPolicy OtherPolicy>
    friend class SessionHandleBase;
    template <typename P, typename R, typename L, AbandonmentPolicy Pol>
    friend class SessionHandle;

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

    // Marks the handle consumed without advancing the protocol.  The
    // destructor check still fires for every handle that does not
    // detach, so accidental abandonment stays caught.
    template <typename Reason>
        requires DetachReason<Reason>
    constexpr void detach(Reason /*reason_tag*/) && noexcept {
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
        tracker_.move_from(other.tracker_);
        return *this;
    }

    ~SessionHandleBase() {
        // Under check::Off the whole body is discarded, so a Release
        // handle's destructor is trivial in effect: no branch, no
        // format strings in .rodata, no reference to std::abort.
        if constexpr (Policy::checks_abandonment) {
            if (!tracker_.was_marked() && !is_terminal_state_v<Proto>) {
                constexpr auto pname = type_display_name_v<Proto>;
                constexpr auto hint = detail::next_method_hint<Proto>();
                const auto loc = tracker_.construction_loc();
                const char* loc_file = loc.file_name();
                const char* loc_func = loc.function_name();
                const auto loc_line = loc.line();
                const auto loc_col = loc.column();
                // file_name() returns "" for a default-constructed
                // location, which is what a handle minted without an
                // explicit location carries.
                const bool have_loc = loc_file != nullptr && loc_file[0] != '\0';

                if constexpr (!std::is_void_v<Derived>) {
                    constexpr auto wname = detail::wrapper_class_name<Derived>();
                    constexpr auto fname = type_display_name_v<Derived>;
                    std::fprintf(stderr,
                                 "\n"
                                 "═════════════════════════════════════════════════════════════════════\n"
                                 "fixy::session: ABANDONMENT DETECTED (non-terminal handle)\n"
                                 "═════════════════════════════════════════════════════════════════════\n"
                                 "  Wrapper class:    %.*s\n"
                                 "  Full handle type: %.*s\n"
                                 "  Protocol head:    %.*s\n",
                                 static_cast<int>(wname.size()), wname.data(), static_cast<int>(fname.size()),
                                 fname.data(), static_cast<int>(pname.size()), pname.data());
                } else {
                    std::fprintf(stderr,
                                 "\n"
                                 "═════════════════════════════════════════════════════════════════════\n"
                                 "fixy::session: ABANDONMENT DETECTED (non-terminal handle)\n"
                                 "═════════════════════════════════════════════════════════════════════\n"
                                 "  Wrapper class:    SessionHandle (Derived not provided to base)\n"
                                 "  Protocol head:    %.*s\n",
                                 static_cast<int>(pname.size()), pname.data());
                }
                if (have_loc) {
                    std::fprintf(stderr,
                                 "  Construction at:  %s:%u:%u\n"
                                 "  In function:      %s\n",
                                 loc_file, static_cast<unsigned>(loc_line), static_cast<unsigned>(loc_col), loc_func);
                } else {
                    std::fprintf(stderr, "  Construction at:  <unknown — handle minted without "
                                         "source_location capture>\n");
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
        }
    }
};

namespace detail {

// The sole authorized constructor of a bare handle.  Every handle
// specialization's value constructor is private and befriends only this
// factory and the handle family, so a direct `SessionHandle<Proto, Res,
// Ctx, Pol>{res}` at a call site is rejected as private and cannot
// bypass the well-formedness gate or the Continue-and-Loop resolution.
//
// It is deliberately unconstrained.  Both callers run their own
// admission first, and the exposed LoopCtx parameter is what gives a
// non-void loop context a sanctioned construction site, which the
// public factory cannot reach because it always builds LoopCtx = void.
template <typename Proto, typename Resource, typename LoopCtx, AbandonmentPolicy Policy>
[[nodiscard]] constexpr auto make_session_handle(
    Resource r,
    std::source_location loc = std::source_location::current()) noexcept(std::is_nothrow_move_constructible_v<Resource>)
    -> SessionHandle<Proto, Resource, LoopCtx, Policy> {
    return SessionHandle<Proto, Resource, LoopCtx, Policy>{std::forward<Resource>(r), loc};
}

template <typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy>
[[nodiscard]] constexpr auto step_to_next(Resource r,
                                          std::source_location loc = std::source_location::current()) noexcept {
    if constexpr (std::is_same_v<R, Continue>) {
        using ActiveLoopCtx = session_loop_ctx_inner_t<LoopCtx>;
        static_assert(!std::is_void_v<ActiveLoopCtx>, "fixy::session::diagnostic [Continue_Without_Loop]: "
                                                      "Continue appears outside a Loop context.  "
                                                      "Every Continue must have an enclosing Loop<Body>.");
        using NextBody = typename ActiveLoopCtx::body;
        // The body may itself begin with a Loop or a Continue, so this
        // recurses.  Forwarding `loc` keeps the outermost caller's site
        // rather than replacing it with this frame's.
        return step_to_next<NextBody, Resource, LoopCtx, Policy>(std::forward<Resource>(r), loc);
    } else if constexpr (is_loop_v<R>) {
        using InnerBody = typename R::body;
        using InnerCtx = session_loop_ctx_rebind_inner_t<LoopCtx, R>;
        // Entering an inner Loop shadows the enclosing loop context.
        // That shadowing is what binds Continue to the nearest Loop.
        return step_to_next<InnerBody, Resource, InnerCtx, Policy>(std::forward<Resource>(r), loc);
    } else {
        static_assert(is_head_v<R>, "fixy::session::diagnostic [Protocol_Ill_Formed]: "
                                    "unexpected protocol shape after resolution.  "
                                    "Only Send/Recv/Select/Offer/End/Continue are valid heads.");
        return make_session_handle<R, Resource, LoopCtx, Policy>(std::forward<Resource>(r), loc);
    }
}

}  // namespace detail

template <typename Resource, typename LoopCtx, AbandonmentPolicy Policy>
class [[nodiscard]] SessionHandle<End, Resource, LoopCtx, Policy>
    : public SessionHandleBase<End, SessionHandle<End, Resource, LoopCtx, Policy>, Policy> {
    Resource resource_;

    template <typename P, typename R, typename L, AbandonmentPolicy Pol>
    friend class SessionHandle;

    template <typename FProto, typename FRes, typename FLoop, AbandonmentPolicy FPol>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop, FPol>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<End, SessionHandle<End, Resource, LoopCtx, Policy>, Policy>{loc},
          resource_{std::forward<Resource>(r)} {}

public:
    using protocol = End;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;

    constexpr SessionHandle(SessionHandle&&) noexcept = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept
        requires std::is_move_assignable_v<Resource>
    = default;
    ~SessionHandle() = default;

    // Closing is what hands the Resource back.  It is available only at
    // End, so a caller can recover the Resource only after the protocol
    // has run to completion.
    [[nodiscard]] constexpr Resource close() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        this->mark_consumed_();
        return std::forward<Resource>(resource_);
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy>
class [[nodiscard]] SessionHandle<Send<T, R>, Resource, LoopCtx, Policy>
    : public SessionHandleBase<Send<T, R>, SessionHandle<Send<T, R>, Resource, LoopCtx, Policy>, Policy> {
    Resource resource_;

    template <typename P, typename Res, typename L, AbandonmentPolicy Pol>
    friend class SessionHandle;
    template <typename FProto, typename FRes, typename FLoop, AbandonmentPolicy FPol>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop, FPol>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Send<T, R>, SessionHandle<Send<T, R>, Resource, LoopCtx, Policy>, Policy>{loc},
          resource_{std::forward<Resource>(r)} {}

public:
    using protocol = Send<T, R>;
    using message_type = T;
    using continuation = R;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;

    constexpr SessionHandle(SessionHandle&&) noexcept = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept
        requires std::is_move_assignable_v<Resource>
    = default;
    ~SessionHandle() = default;

    // The Transport is what physically moves the value to the peer.
    // Its signature is void(Resource&, T&&).  The returned handle sits
    // at the continuation, with Continue and Loop already resolved.
    template <typename Transport>
        requires std::is_invocable_v<Transport, Resource&, T&&>
    [[nodiscard]] constexpr auto
    send(T value, Transport transport) && noexcept(std::is_nothrow_invocable_v<Transport, Resource&, T&&>
                                                   && std::is_nothrow_move_constructible_v<Resource>
                                                   && std::is_nothrow_move_constructible_v<T>) {
        std::invoke(transport, resource_, std::move(value));
        this->mark_consumed_();
        return detail::step_to_next<R, Resource, LoopCtx, Policy>(std::forward<Resource>(resource_));
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

template <typename T, typename R, typename Resource, typename LoopCtx, AbandonmentPolicy Policy>
class [[nodiscard]] SessionHandle<Recv<T, R>, Resource, LoopCtx, Policy>
    : public SessionHandleBase<Recv<T, R>, SessionHandle<Recv<T, R>, Resource, LoopCtx, Policy>, Policy> {
    Resource resource_;

    template <typename P, typename Res, typename L, AbandonmentPolicy Pol>
    friend class SessionHandle;
    template <typename FProto, typename FRes, typename FLoop, AbandonmentPolicy FPol>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop, FPol>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Recv<T, R>, SessionHandle<Recv<T, R>, Resource, LoopCtx, Policy>, Policy>{loc},
          resource_{std::forward<Resource>(r)} {}

public:
    using protocol = Recv<T, R>;
    using message_type = T;
    using continuation = R;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;

    constexpr SessionHandle(SessionHandle&&) noexcept = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept
        requires std::is_move_assignable_v<Resource>
    = default;
    ~SessionHandle() = default;

    // The Transport signature is T(Resource&).  The pair is the
    // received value and the handle at the continuation.
    template <typename Transport>
        requires std::is_invocable_r_v<T, Transport, Resource&>
    [[nodiscard]] constexpr auto
    recv(Transport transport) && noexcept(std::is_nothrow_invocable_r_v<T, Transport, Resource&>
                                          && std::is_nothrow_move_constructible_v<Resource>
                                          && std::is_nothrow_move_constructible_v<T>) {
        T value = std::invoke(transport, resource_);
        this->mark_consumed_();
        auto next = detail::step_to_next<R, Resource, LoopCtx, Policy>(std::forward<Resource>(resource_));
        return std::pair{std::move(value), std::move(next)};
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

template <typename... Branches, typename Resource, typename LoopCtx, AbandonmentPolicy Policy>
class [[nodiscard]] SessionHandle<Select<Branches...>, Resource, LoopCtx, Policy>
    : public SessionHandleBase<Select<Branches...>, SessionHandle<Select<Branches...>, Resource, LoopCtx, Policy>,
                               Policy> {
    Resource resource_;

    template <typename P, typename Res, typename L, AbandonmentPolicy Pol>
    friend class SessionHandle;
    template <typename FProto, typename FRes, typename FLoop, AbandonmentPolicy FPol>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop, FPol>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Select<Branches...>, SessionHandle<Select<Branches...>, Resource, LoopCtx, Policy>, Policy>{
              loc},
          resource_{std::forward<Resource>(r)} {}

public:
    using protocol = Select<Branches...>;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;

    static constexpr std::size_t branch_count = sizeof...(Branches);

    // A second empty-choice rejection, so a construction route that
    // reaches this class without passing the factory's own check still
    // fires the same diagnostic.  Subtyping uses of Select<> never
    // instantiate this class and are unaffected.
    static_assert(branch_count > 0, "fixy::session::diagnostic [Empty_Choice_Combinator]: "
                                    "SessionHandle<Select<>>: cannot construct a runnable handle "
                                    "on Select<> with zero branches — there is no branch for "
                                    "select<I>() to choose.  See mint_session_handle for the full "
                                    "diagnostic and remediation.");

    constexpr SessionHandle(SessionHandle&&) noexcept = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept
        requires std::is_move_assignable_v<Resource>
    = default;
    ~SessionHandle() = default;

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
        std::invoke(transport, resource_, I);
        this->mark_consumed_();
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next<Chosen, Resource, LoopCtx, Policy>(std::forward<Resource>(resource_));
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
        this->mark_consumed_();
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next<Chosen, Resource, LoopCtx, Policy>(std::forward<Resource>(resource_));
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

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

template <typename... Branches, typename Resource, typename LoopCtx, AbandonmentPolicy Policy>
class [[nodiscard]] SessionHandle<Offer<Branches...>, Resource, LoopCtx, Policy>
    : public SessionHandleBase<Offer<Branches...>, SessionHandle<Offer<Branches...>, Resource, LoopCtx, Policy>,
                               Policy> {
    Resource resource_;

    template <typename P, typename Res, typename L, AbandonmentPolicy Pol>
    friend class SessionHandle;
    template <typename FProto, typename FRes, typename FLoop, AbandonmentPolicy FPol>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop, FPol>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Offer<Branches...>, SessionHandle<Offer<Branches...>, Resource, LoopCtx, Policy>, Policy>{
              loc},
          resource_{std::forward<Resource>(r)} {}

public:
    using protocol = Offer<Branches...>;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;

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

    constexpr SessionHandle(SessionHandle&&) noexcept = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept
        requires std::is_move_assignable_v<Resource>
    = default;
    ~SessionHandle() = default;

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
        const std::size_t idx = std::invoke(transport, resource_);
        this->mark_consumed_();
        return dispatch_branch_(idx, std::forward<Resource>(resource_), std::move(handler),
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
        this->mark_consumed_();
        using Chosen = std::tuple_element_t<I, branches>;
        return detail::step_to_next<Chosen, Resource, LoopCtx, Policy>(std::forward<Resource>(resource_));
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

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }

private:
    template <std::size_t I>
    static constexpr auto make_branch_handle_(Resource r) {
        using B = std::tuple_element_t<I, branches>;
        return detail::step_to_next<B, Resource, LoopCtx, Policy>(std::forward<Resource>(r));
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

template <typename Proto>
concept WellFormedRunnableProtocol = is_well_formed_v<Proto> && !is_empty_choice_v<Proto>;

// A Proto that starts with a Loop is unrolled one iteration, so the
// returned handle sits at the loop body with the Loop as its LoopCtx.
// Any other head passes through unchanged.
//
// The body static_asserts repeat the concept's checks.  They fire only
// if some route reaches the body without the concept having run, and
// their prose is what explains a rejection the concept states only as a
// boolean.

template <typename Proto, typename Resource, AbandonmentPolicy Policy = DefaultAbandonmentPolicy>
    requires WellFormedRunnableProtocol<Proto> && SessionResource<Resource>
[[nodiscard]] constexpr auto mint_session_handle(Resource r,
                                                 std::source_location loc = std::source_location::current()) noexcept {
    static_assert(is_well_formed_v<Proto>, "fixy::session::diagnostic [Protocol_Ill_Formed]: "
                                           "protocol is ill-formed.  Most likely cause: a Continue "
                                           "appears outside any enclosing Loop<Body>.  Every Continue must "
                                           "have a Loop above it in the protocol tree.");

    // The rejection lives at the handle boundary, not in the type
    // machinery, so subtyping keeps admitting an empty choice as a
    // legitimate operand while handle construction refuses it.
    static_assert(!is_empty_choice_v<Proto>, "fixy::session::diagnostic [Empty_Choice_Combinator]: "
                                             "mint_session_handle<Proto> — Proto contains a "
                                             "reachable empty Select<> / Offer<> / Offer<Sender<R>> "
                                             "(top-level or nested under Send/Recv/Loop/branch).  "
                                             "Cannot construct a runnable handle: Select<> has "
                                             "no branch for select<I>() to choose; Offer<> has no label "
                                             "the peer can signal.  The trait walks recursively so "
                                             "nested empties are caught at mint time, not at the "
                                             "eventual select<I>() / recv() that hits the dead-end.  "
                                             "If you intend a type-level subtyping witness, use the "
                                             "subtyping trait directly; if you intend a runnable "
                                             "handle, add at least one branch at every reachable choice "
                                             "position (e.g., Select<Send<Stop, End>>).");

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

    if constexpr (is_loop_v<Proto>) {
        using Body = typename Proto::body;
        // Forwarding `loc` keeps the caller's site in the abandonment
        // diagnostic even though the unroll inserts an intermediate
        // handle whose own default location would otherwise win.
        return detail::step_to_next<Body, Resource, Proto, Policy>(std::forward<Resource>(r), loc);
    } else {
        static_assert(!std::is_same_v<Proto, Continue>, "fixy::session::diagnostic [Continue_Without_Loop]: "
                                                        "Continue cannot be the top-level protocol.");
        return detail::make_session_handle<Proto, Resource, void, Policy>(std::forward<Resource>(r), loc);
    }
}

// Channel construction carries both endpoints' execution contexts, so
// that the row, vendor, epoch and permission gates run against both
// local protocols rather than only one.  The resource-only form is
// deleted because it carries neither and so could check neither.

template <typename Proto, typename ResourceA, typename ResourceB>
void mint_channel(ResourceA, ResourceB) noexcept =
    delete("[Channel_Needs_Both_Contexts] mint_channel<Proto>(resource_a, resource_b) is removed.  A "
           "channel has two endpoints and each is admitted against its own execution context — the "
           "effect row, the vendor pin and the permission set are per-endpoint — so a form that "
           "carries neither context can check neither side.  Call the ctx-bound "
           "mint_channel<Proto>(ctx_a, ctx_b, resource_a, resource_b) from the session mint layer, or "
           "mint the two endpoints separately with mint_session_handle<Proto> and "
           "mint_session_handle<dual_of_t<Proto>> if no context gate applies.");

}  // namespace fixy::session
