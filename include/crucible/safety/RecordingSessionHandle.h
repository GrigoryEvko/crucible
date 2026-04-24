#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto — RecordingSessionHandle<Proto, Resource>
//
// Task #404 SAFEINT-B15 from misc/24_04_2026_safety_integration.md
// §15 — the audit-trail wrapper around SessionHandle that records
// every Send / Recv / Select / Offer / close into a SessionEventLog.
//
// Design contract:
//
//   * The wrapper is OPT-IN per-handle — production code that needs
//     replay-safety constructs RecordingSessionHandles, production
//     code that wants raw performance keeps using bare SessionHandle.
//     This is the same opt-in pattern as the rest of the framework
//     (Linear/Refined/Tagged are all opt-in wrappers).
//
//   * The recorded event_log lives outside the wrapper (passed by
//     reference at construction).  This lets a single log capture a
//     multi-handle session: Sender's wrapper and Receiver's wrapper
//     both write into the same SessionEventLog, producing a unified
//     audit trail with monotonic step_ids across both sides.
//
//   * The wrapper inherits from SessionHandleBase<Proto> for the
//     abandonment-check destructor.  It does NOT inherit from
//     SessionHandle<Proto, Resource>, because SessionHandle's
//     consumer methods are intentionally rvalue-ref-only and the
//     wrapper needs to mark BOTH itself and the inner handle
//     consumed at each step.  Composition (hold an inner handle by
//     value) handles this cleanly.
//
//   * Each consumer method (close / send / recv / select / pick /
//     branch) records a SessionEvent BEFORE forwarding to the inner
//     handle.  The "before" ordering matters: if the transport
//     callback aborts (network error, peer crash), the audit trail
//     reflects the attempted operation, not silently swallowed
//     state.
//
//   * Re-wrapping the next-state handle is automatic: after the
//     inner method returns, the wrapper constructs a new
//     RecordingSessionHandle around the new SessionHandle, carrying
//     forward the log pointer and role IDs.  Loop / Continue
//     resolution propagates through the inner detail::step_to_next
//     unchanged.
//
// ─── Per-Proto specialisation surface ──────────────────────────────
//
//   RecordingSessionHandle<End,           R, L>  → close()
//   RecordingSessionHandle<Send<T, K>,    R, L>  → send(value, transport)
//   RecordingSessionHandle<Recv<T, K>,    R, L>  → recv(transport)
//   RecordingSessionHandle<Select<Bs...>, R, L>  → select<I>([transport])
//   RecordingSessionHandle<Offer<Bs...>,  R, L>  → pick<I>(),
//                                                  branch(transport, handler)
//
// Loop<B> / Continue / Stop are unrolled by the framework's existing
// step_to_next / make_session_handle machinery before the wrapper
// sees them — same as for plain SessionHandle.
//
// ─── Worked-example pattern ────────────────────────────────────────
//
//   SessionEventLog log{SessionTagId{42}};
//   auto bare = make_session_handle<MyProto>(my_resource);
//   auto rec  = make_recording<MyProto, R>(
//       std::move(bare), log,
//       /*self*/ RoleTagId{1}, /*peer*/ RoleTagId{2});
//
//   auto next = std::move(rec).send(payload, my_transport);
//   // log now contains a SessionEvent{op=Send, from=1, to=2,
//   //                                 schema=hash<Payload>, ...}
//
// ─── References ────────────────────────────────────────────────────
//
//   misc/24_04_2026_safety_integration.md §15 — design rationale
//   safety/Session.h          — the underlying SessionHandle types
//   safety/SessionEventLog.h  — the log primitive
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/safety/Session.h>
#include <crucible/safety/SessionEventLog.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

// Forward declaration; specialisations follow.
template <typename Proto, typename Resource, typename LoopCtx = void>
class RecordingSessionHandle;

namespace detail {

// Build a RecordingSessionHandle from a freshly-stepped inner handle.
// Routes Continue / Loop resolution through the framework's existing
// step_to_next so the wrapper sees the same protocol surface the bare
// handle would have produced.  The inner SessionHandle is constructed
// by step_to_next; we then wrap it with the same log + role context.
template <typename NextHandle>
[[nodiscard]] constexpr auto wrap_next_(
    NextHandle        next,
    SessionEventLog&  log,
    RoleTagId         self_role,
    RoleTagId         peer_role) noexcept
{
    using NextProto    = typename NextHandle::protocol;
    using NextResource = typename NextHandle::resource_type;
    using NextLoopCtx  = typename NextHandle::loop_ctx;
    return RecordingSessionHandle<NextProto, NextResource, NextLoopCtx>{
        std::move(next), log, self_role, peer_role};
}

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── RecordingSessionHandle<End, …> — terminal wrapper ──────────────
// ═════════════════════════════════════════════════════════════════════

template <typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingSessionHandle<End, Resource, LoopCtx>
    : public SessionHandleBase<End,
                               RecordingSessionHandle<End, Resource, LoopCtx>>
{
    SessionHandle<End, Resource, LoopCtx> inner_;
    SessionEventLog* log_      = nullptr;
    RoleTagId        self_role_{};
    RoleTagId        peer_role_{};

public:
    using protocol      = End;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using inner_type    = SessionHandle<End, Resource, LoopCtx>;

    constexpr RecordingSessionHandle(
        inner_type inner,
        SessionEventLog& log,
        RoleTagId self,
        RoleTagId peer,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<End,
                            RecordingSessionHandle<End, Resource, LoopCtx>>{loc}
        , inner_{std::move(inner)}, log_{&log},
          self_role_{self}, peer_role_{peer} {}

    constexpr RecordingSessionHandle(RecordingSessionHandle&&) noexcept = default;
    constexpr RecordingSessionHandle& operator=(RecordingSessionHandle&&) noexcept = default;
    ~RecordingSessionHandle() = default;

    // Record a Close event then close the inner handle, yielding the
    // Resource.  Recording happens BEFORE the close so the audit trail
    // reflects intent even if the close itself throws (it can't —
    // close() is noexcept-friendly — but the discipline is consistent
    // with the other consumer methods that DO call user transports).
    [[nodiscard]] constexpr Resource close() &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        log_->record_now(SessionEvent{
            .from_role = self_role_,
            .to_role   = peer_role_,
            .op        = SessionOp::Close,
        });
        this->mark_consumed_();
        return std::move(inner_).close();
    }

    // Non-consuming inspection passes through.
    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }

    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── RecordingSessionHandle<Send<T, R>, …> ──────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T, typename R, typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingSessionHandle<Send<T, R>, Resource, LoopCtx>
    : public SessionHandleBase<Send<T, R>,
                               RecordingSessionHandle<Send<T, R>, Resource, LoopCtx>>
{
    SessionHandle<Send<T, R>, Resource, LoopCtx> inner_;
    SessionEventLog* log_      = nullptr;
    RoleTagId        self_role_{};
    RoleTagId        peer_role_{};

public:
    using protocol      = Send<T, R>;
    using message_type  = T;
    using continuation  = R;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using inner_type    = SessionHandle<Send<T, R>, Resource, LoopCtx>;

    constexpr RecordingSessionHandle(
        inner_type inner,
        SessionEventLog& log,
        RoleTagId self,
        RoleTagId peer,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Send<T, R>,
                            RecordingSessionHandle<Send<T, R>, Resource, LoopCtx>>{loc}
        , inner_{std::move(inner)}, log_{&log},
          self_role_{self}, peer_role_{peer} {}

    constexpr RecordingSessionHandle(RecordingSessionHandle&&) noexcept = default;
    constexpr RecordingSessionHandle& operator=(RecordingSessionHandle&&) noexcept = default;
    ~RecordingSessionHandle() = default;

    template <typename Transport>
        requires std::is_invocable_v<Transport, Resource&, T&&>
    [[nodiscard]] constexpr auto send(T value, Transport transport) &&
    {
        log_->record_now(SessionEvent{
            .from_role      = self_role_,
            .to_role        = peer_role_,
            .payload_schema = default_schema_hash<T>,
            .payload_hash   = default_payload_hash_fn<T>(value),
            .op             = SessionOp::Send,
        });
        this->mark_consumed_();
        auto next = std::move(inner_).send(std::move(value), std::move(transport));
        return detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── RecordingSessionHandle<Recv<T, R>, …> ──────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T, typename R, typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingSessionHandle<Recv<T, R>, Resource, LoopCtx>
    : public SessionHandleBase<Recv<T, R>,
                               RecordingSessionHandle<Recv<T, R>, Resource, LoopCtx>>
{
    SessionHandle<Recv<T, R>, Resource, LoopCtx> inner_;
    SessionEventLog* log_      = nullptr;
    RoleTagId        self_role_{};
    RoleTagId        peer_role_{};

public:
    using protocol      = Recv<T, R>;
    using message_type  = T;
    using continuation  = R;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using inner_type    = SessionHandle<Recv<T, R>, Resource, LoopCtx>;

    constexpr RecordingSessionHandle(
        inner_type inner,
        SessionEventLog& log,
        RoleTagId self,
        RoleTagId peer,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Recv<T, R>,
                            RecordingSessionHandle<Recv<T, R>, Resource, LoopCtx>>{loc}
        , inner_{std::move(inner)}, log_{&log},
          self_role_{self}, peer_role_{peer} {}

    constexpr RecordingSessionHandle(RecordingSessionHandle&&) noexcept = default;
    constexpr RecordingSessionHandle& operator=(RecordingSessionHandle&&) noexcept = default;
    ~RecordingSessionHandle() = default;

    // Recv records AFTER the transport call so the payload_hash
    // reflects the actually-received value (vs the Send case where
    // we record BEFORE because we already have the value by argument).
    // The consumed_-mark + return ordering matches the Send path.
    template <typename Transport>
        requires std::is_invocable_r_v<T, Transport, Resource&>
    [[nodiscard]] constexpr auto recv(Transport transport) &&
    {
        auto [value, next] = std::move(inner_).recv(std::move(transport));
        log_->record_now(SessionEvent{
            .from_role      = peer_role_,
            .to_role        = self_role_,
            .payload_schema = default_schema_hash<T>,
            .payload_hash   = default_payload_hash_fn<T>(value),
            .op             = SessionOp::Recv,
        });
        this->mark_consumed_();
        auto wrapped_next = detail::wrap_next_(
            std::move(next), *log_, self_role_, peer_role_);
        return std::pair{std::move(value), std::move(wrapped_next)};
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── RecordingSessionHandle<Select<Bs...>, …> ───────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename... Branches, typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingSessionHandle<Select<Branches...>, Resource, LoopCtx>
    : public SessionHandleBase<Select<Branches...>,
                               RecordingSessionHandle<Select<Branches...>, Resource, LoopCtx>>
{
    SessionHandle<Select<Branches...>, Resource, LoopCtx> inner_;
    SessionEventLog* log_      = nullptr;
    RoleTagId        self_role_{};
    RoleTagId        peer_role_{};

public:
    using protocol      = Select<Branches...>;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using inner_type    = SessionHandle<Select<Branches...>, Resource, LoopCtx>;
    static constexpr std::size_t branch_count = sizeof...(Branches);

    constexpr RecordingSessionHandle(
        inner_type inner,
        SessionEventLog& log,
        RoleTagId self,
        RoleTagId peer,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Select<Branches...>,
                            RecordingSessionHandle<Select<Branches...>, Resource, LoopCtx>>{loc}
        , inner_{std::move(inner)}, log_{&log},
          self_role_{self}, peer_role_{peer} {}

    constexpr RecordingSessionHandle(RecordingSessionHandle&&) noexcept = default;
    constexpr RecordingSessionHandle& operator=(RecordingSessionHandle&&) noexcept = default;
    ~RecordingSessionHandle() = default;

    // Transport-driven select: signal choice to peer + record.
    template <std::size_t I, typename Transport>
        requires (I < sizeof...(Branches))
              && std::is_invocable_v<Transport, Resource&, std::size_t>
    [[nodiscard]] constexpr auto select(Transport transport) &&
    {
        log_->record_now(SessionEvent{
            .from_role    = self_role_,
            .to_role      = peer_role_,
            .op           = SessionOp::Select,
            .branch_index = static_cast<uint8_t>(I),
        });
        this->mark_consumed_();
        auto next = std::move(inner_).template select<I>(std::move(transport));
        return detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
    }

    // Renamed to `select_local<I>()` (#377).  Same recording + same
    // mark_consumed_ pattern; the wrapper does not distinguish the
    // wire-vs-in-memory variant in the event log because both equally
    // determine the protocol shape.  The rename surfaces the wire
    // ABSENCE — useful for human-auditable event-log inspections.
    template <std::size_t I>
        requires (I < sizeof...(Branches))
    [[nodiscard]] constexpr auto select_local() &&
    {
        log_->record_now(SessionEvent{
            .from_role    = self_role_,
            .to_role      = peer_role_,
            .op           = SessionOp::Select,
            .branch_index = static_cast<uint8_t>(I),
        });
        this->mark_consumed_();
        auto next = std::move(inner_).template select_local<I>();
        return detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
    }

    // Deleted `select<I>()` overload (#377) — same discipline as the
    // bare SessionHandle and CrashWatchedHandle.
    template <std::size_t I>
    void select() && = delete(
        "[Wire_Variant_Required] RecordingSessionHandle<Select<...>>::"
        "select<I>() without arguments is no longer allowed (#377).  "
        "Use `select<I>(transport)` for the wire path or "
        "`select_local<I>()` for the in-memory variant.");

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── RecordingSessionHandle<Offer<Bs...>, …> ────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename... Branches, typename Resource, typename LoopCtx>
class [[nodiscard]] RecordingSessionHandle<Offer<Branches...>, Resource, LoopCtx>
    : public SessionHandleBase<Offer<Branches...>,
                               RecordingSessionHandle<Offer<Branches...>, Resource, LoopCtx>>
{
    SessionHandle<Offer<Branches...>, Resource, LoopCtx> inner_;
    SessionEventLog* log_      = nullptr;
    RoleTagId        self_role_{};
    RoleTagId        peer_role_{};

public:
    using protocol      = Offer<Branches...>;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    using inner_type    = SessionHandle<Offer<Branches...>, Resource, LoopCtx>;
    static constexpr std::size_t branch_count = sizeof...(Branches);

    constexpr RecordingSessionHandle(
        inner_type inner,
        SessionEventLog& log,
        RoleTagId self,
        RoleTagId peer,
        std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Offer<Branches...>,
                            RecordingSessionHandle<Offer<Branches...>, Resource, LoopCtx>>{loc}
        , inner_{std::move(inner)}, log_{&log},
          self_role_{self}, peer_role_{peer} {}

    constexpr RecordingSessionHandle(RecordingSessionHandle&&) noexcept = default;
    constexpr RecordingSessionHandle& operator=(RecordingSessionHandle&&) noexcept = default;
    ~RecordingSessionHandle() = default;

    // Renamed to `pick_local<I>()` (#377) — caller already learned the
    // branch out-of-band, no peer label is being received here.  The
    // event log still records the Offer transition; only the wire
    // semantic differs.
    template <std::size_t I>
        requires (I < sizeof...(Branches))
    [[nodiscard]] constexpr auto pick_local() &&
    {
        log_->record_now(SessionEvent{
            .from_role    = peer_role_,        // peer made the choice
            .to_role      = self_role_,        // self learns the choice
            .op           = SessionOp::Offer,
            .branch_index = static_cast<uint8_t>(I),
        });
        this->mark_consumed_();
        auto next = std::move(inner_).template pick_local<I>();
        return detail::wrap_next_(std::move(next), *log_, self_role_, peer_role_);
    }

    // Deleted `pick<I>()` overload (#377) — same discipline as bare
    // SessionHandle and CrashWatchedHandle.
    template <std::size_t I>
    void pick() && = delete(
        "[Wire_Variant_Required] RecordingSessionHandle<Offer<...>>::"
        "pick<I>() without arguments is no longer allowed (#377).  "
        "Use `pick_local<I>()` to advance without receiving a peer "
        "label, or call the peer-receiving variant when one is "
        "available.");

    // Transport-driven branch — receives the index from peer, then
    // dispatches to the user's handler with the branch's RecordingSession-
    // Handle.  We capture the index AFTER the transport call (because
    // that's when we know it) and record before invoking the handler;
    // the per-branch handler then sees a wrapped handle whose log
    // already contains the Offer event for this dispatch.
    //
    // The handler's inputs are RecordingSessionHandle wrappers (not
    // bare SessionHandles) so the recording propagates through every
    // branch.  Handler must accept the wrapped types and return a
    // common Result (or all void).
    template <typename Transport, typename Handler>
        requires std::is_invocable_r_v<std::size_t, Transport, Resource&>
    constexpr auto branch(Transport transport, Handler handler) &&
    {
        // Wrap the inner branch dispatch by interposing on the transport
        // to capture the index, then record-and-rewrap each branch
        // handle before invoking the handler.
        auto* log_ptr     = log_;
        auto  self_role   = self_role_;
        auto  peer_role   = peer_role_;
        auto  recording_handler =
            [log_ptr, self_role, peer_role,
             handler = std::move(handler)](auto inner_branch_handle) mutable {
                using BranchHandle = decltype(inner_branch_handle);
                using BranchProto    = typename BranchHandle::protocol;
                using BranchResource = typename BranchHandle::resource_type;
                using BranchLoopCtx  = typename BranchHandle::loop_ctx;
                RecordingSessionHandle<
                    BranchProto, BranchResource, BranchLoopCtx>
                    wrapped_branch{
                        std::move(inner_branch_handle),
                        *log_ptr, self_role, peer_role};
                return std::invoke(std::move(handler), std::move(wrapped_branch));
            };

        // Interpose on the transport: capture the index for the log
        // record before forwarding it through to the inner branch().
        auto recording_transport =
            [log_ptr, self_role, peer_role,
             tx = std::move(transport)](Resource& r) mutable -> std::size_t {
                const std::size_t idx = std::invoke(tx, r);
                log_ptr->record_now(SessionEvent{
                    .from_role    = peer_role,    // peer picked
                    .to_role      = self_role,    // self learns
                    .op           = SessionOp::Offer,
                    .branch_index = static_cast<uint8_t>(idx),
                });
                return idx;
            };

        this->mark_consumed_();
        return std::move(inner_).branch(
            std::move(recording_transport),
            std::move(recording_handler));
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr SessionEventLog& event_log() const noexcept { return *log_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── make_recording — convenience factory ────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Wrap an existing SessionHandle in a RecordingSessionHandle.  Just a
// constructor call written as a free function so the protocol /
// resource template parameters are deducible at the call site.

template <typename Proto, typename Resource, typename LoopCtx>
[[nodiscard]] constexpr auto make_recording(
    SessionHandle<Proto, Resource, LoopCtx> inner,
    SessionEventLog& log,
    RoleTagId self,
    RoleTagId peer) noexcept
{
    return RecordingSessionHandle<Proto, Resource, LoopCtx>{
        std::move(inner), log, self, peer};
}

}  // namespace crucible::safety::proto
