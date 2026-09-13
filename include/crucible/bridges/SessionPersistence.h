#pragma once

// One event log per persisted session, drained into the store as a growing
// prefix.  Each flush writes only the suffix that has not been written yet, so
// a session interrupted between flushes persists as a truncated but internally
// consistent prefix rather than a gap.  The session algebra itself is not
// re-implemented here.  Every protocol step is forwarded to the recording
// layer, and this layer only decides when to write.

// The store is forward-declared rather than included, since only a handful of
// its names are needed to parse this header.  The template bodies below still
// need the complete type, so a translation unit that instantiates any of them
// has to include the store's own header itself.
#include <crucible/bridges/RecordingPermissionedSessionHandle.h>
#include <crucible/bridges/RecordingSessionHandle.h>
#include <crucible/cipher/SessionPersistenceSurface.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Time.h>
#include <crucible/safety/IsSessionHandle.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace crucible::safety::proto {

struct SessionPersistencePolicy {
    std::size_t count_threshold = 1000;
    std::chrono::steady_clock::duration time_threshold = std::chrono::seconds{5};
};

template <typename CallerRow>
class SessionPersistenceState {
public:
    // The clock source is pinned to the monotonic one and taken as a
    // constructor parameter, so the choice is made at the boundary and a
    // calibrated reader can be substituted without touching this body.
    using ClockReaderType = ::crucible::fixy::time::ClockReader<::crucible::fixy::time::ClockSource_v::Monotonic>;

private:
    Cipher& cipher_;
    // The view is the caller's mint-time witness, stored rather than re-minted
    // at each flush.  Re-minting would abort inside the mint if the store had
    // closed in between, instead of failing at the call site that owes the
    // persistence guarantee.  The store outlives this state by the contract of
    // the constructor parameter.
    CipherOpenView view_;
    SessionEventLog log_;
    std::size_t flushed_count_ = 0;
    SessionPersistencePolicy policy_{};
    std::uint64_t last_flush_ns_ = 0;
    [[no_unique_address]] ClockReaderType clock_reader_{};

public:
    using caller_row = CallerRow;
    using required_row = CipherSessionEventPersistenceRow;

    static_assert(::crucible::effects::Subrow<required_row, CallerRow>,
                  "SessionPersistenceState<CallerRow>: CallerRow must contain "
                  "IO + Block because persistence writes Cipher files.");

    static_assert(::std::is_empty_v<ClockReaderType>, "ClockReader must be empty (stateless witness); "
                                                      "[[no_unique_address]] would otherwise consume a real byte.");
    static_assert(ClockReaderType::source == ::crucible::fixy::time::ClockSource_v::Monotonic,
                  "SessionPersistenceState clock provenance is MONOTONIC.");

    SessionPersistenceState(Cipher& cipher, CipherOpenView const& view, SessionTagId session,
                            SessionPersistencePolicy policy, ClockReaderType clock_reader = {}) noexcept
        : cipher_{cipher}, view_{view}, log_{session}, policy_{policy}, last_flush_ns_{0}, clock_reader_{clock_reader} {
        last_flush_ns_ = clock_reader_.read().consume();
    }

    SessionPersistenceState(const SessionPersistenceState&) = delete;
    SessionPersistenceState& operator=(const SessionPersistenceState&) = delete;
    SessionPersistenceState(SessionPersistenceState&&) = delete;
    SessionPersistenceState& operator=(SessionPersistenceState&&) = delete;

    // Every consumed protocol step flushes, so a session that runs to a
    // terminal step needs nothing here.  A state dropped without reaching one,
    // through an early return or a state built only for replay staging, would
    // otherwise lose its trailing events, leaving a prefix shorter than the
    // session actually ran.  A failure aborts, as at every other flush gate:
    // there is no later moment at which these events could still be written.
    ~SessionPersistenceState() noexcept {
        if (pending_count() > 0) {
            if (!flush_all()) [[unlikely]] {
                std::abort();
            }
        }
    }

    [[nodiscard]] SessionEventLog& log() noexcept { return log_; }
    [[nodiscard]] const SessionEventLog& log() const noexcept { return log_; }
    [[nodiscard]] std::size_t flushed_count() const noexcept { return flushed_count_; }

    [[nodiscard]] std::size_t pending_count() const noexcept { return log_.size() - flushed_count_; }

    [[nodiscard]] bool flush_if_due() {
        const std::size_t pending = pending_count();
        if (pending == 0) return true;
        if (policy_.count_threshold != 0 && pending >= policy_.count_threshold) {
            return flush_all();
        }
        const auto time_threshold_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(policy_.time_threshold).count();
        if (time_threshold_ns > 0) {
            const std::uint64_t now_ns = clock_reader_.read().consume();
            const std::uint64_t elapsed_ns = now_ns - last_flush_ns_;
            if (static_cast<long long>(elapsed_ns) < time_threshold_ns) return true;
            return flush_all();
        }
        return true;
    }

    [[nodiscard]] bool flush_all() {
        const std::size_t pending = pending_count();
        if (pending == 0) return true;

        const SessionEvent* first = &log_[flushed_count_];
        const auto events = std::span<const SessionEvent>{first, pending};
        const ContentHash hash = cipher_.template persist_session_events<CallerRow>(view_, events);
        if (hash) {
            flushed_count_ = log_.size();
            last_flush_ns_ = clock_reader_.read().consume();
            return true;
        }
        return false;
    }
};

template <typename Inner, typename CallerRow>
class PersistedSessionHandle;

// The detach reason tags and the log's reason-kind enum are declared
// independently, and neither side depends on the other.  The mapping between
// them lives here because a persisted handle is the only place both are needed
// at once.
//
// The mapping is lossy for a reason type defined outside the framework, which
// resolves to Unknown.  The schema hash recorded alongside the kind still names
// the exact type, so offline replay loses nothing.
template <typename Reason>
inline constexpr DetachReasonKind detach_reason_kind_v = DetachReasonKind::Unknown;

template <>
inline constexpr DetachReasonKind detach_reason_kind_v<detach_reason::InfiniteLoopProtocol> =
    DetachReasonKind::InfiniteLoopProtocol;

template <>
inline constexpr DetachReasonKind detach_reason_kind_v<detach_reason::TransportClosedOutOfBand> =
    DetachReasonKind::TransportClosedOutOfBand;

template <>
inline constexpr DetachReasonKind detach_reason_kind_v<detach_reason::TestInstrumentation> =
    DetachReasonKind::TestInstrumentation;

template <>
inline constexpr DetachReasonKind detach_reason_kind_v<detach_reason::AsyncCancellation> =
    DetachReasonKind::AsyncCancellation;

template <>
inline constexpr DetachReasonKind detach_reason_kind_v<detach_reason::OwnerLifetimeBoundEarlyExit> =
    DetachReasonKind::OwnerLifetimeBoundEarlyExit;

namespace detail {

template <typename T>
struct is_expected : std::false_type {};

template <typename T, typename E>
struct is_expected<std::expected<T, E>> : std::true_type {};

template <typename T>
inline constexpr bool is_expected_v = is_expected<std::remove_cvref_t<T>>::value;

template <typename T>
struct is_pair : std::false_type {};

template <typename A, typename B>
struct is_pair<std::pair<A, B>> : std::true_type {};

template <typename T>
inline constexpr bool is_pair_v = is_pair<std::remove_cvref_t<T>>::value;

template <typename T>
struct is_persisted_session_handle : std::false_type {};

template <typename Inner, typename CallerRow>
struct is_persisted_session_handle<PersistedSessionHandle<Inner, CallerRow>> : std::true_type {};

template <typename T>
inline constexpr bool is_persisted_session_handle_v = is_persisted_session_handle<std::remove_cvref_t<T>>::value;

template <typename T, typename CallerRow>
struct persisted_result;

template <typename T, typename CallerRow>
struct persisted_result {
    using type = T;
    static constexpr bool carries_handle = false;
};

template <typename T, typename CallerRow>
    requires(::crucible::safety::extract::IsSessionHandle<T> && !is_persisted_session_handle_v<T>)
struct persisted_result<T, CallerRow> {
    using type = PersistedSessionHandle<T, CallerRow>;
    static constexpr bool carries_handle = true;
};

template <typename T, typename CallerRow>
    requires is_persisted_session_handle_v<T>
struct persisted_result<T, CallerRow> {
    using type = T;
    static constexpr bool carries_handle = true;
};

template <typename A, typename B, typename CallerRow>
struct persisted_result<std::pair<A, B>, CallerRow> {
    using wrapped_second = typename persisted_result<B, CallerRow>::type;
    using type = std::pair<A, wrapped_second>;
    static constexpr bool carries_handle = persisted_result<B, CallerRow>::carries_handle;
};

template <typename T, typename E, typename CallerRow>
struct persisted_result<std::expected<T, E>, CallerRow> {
    using wrapped_value = typename persisted_result<T, CallerRow>::type;
    using type = std::expected<wrapped_value, E>;
    static constexpr bool carries_handle = persisted_result<T, CallerRow>::carries_handle;
};

template <typename T, typename CallerRow>
using persisted_result_t = typename persisted_result<std::remove_cvref_t<T>, CallerRow>::type;

template <typename T, typename CallerRow>
inline constexpr bool persisted_result_carries_handle_v =
    persisted_result<std::remove_cvref_t<T>, CallerRow>::carries_handle;

template <typename CallerRow>
struct StateCarrier {
    std::unique_ptr<SessionPersistenceState<CallerRow>> owned{};
    SessionPersistenceState<CallerRow>* state = nullptr;
};

template <typename T, typename CallerRow>
[[nodiscard]] persisted_result_t<T, CallerRow> wrap_persisted_result(T&& value, StateCarrier<CallerRow> carrier);

template <typename T, typename CallerRow>
    requires(::crucible::safety::extract::IsSessionHandle<std::remove_cvref_t<T>> && !is_persisted_session_handle_v<T>)
[[nodiscard]] persisted_result_t<T, CallerRow> wrap_session_handle(T&& value, StateCarrier<CallerRow> carrier) {
    return persisted_result_t<T, CallerRow>{std::forward<T>(value), std::move(carrier.owned), carrier.state};
}

template <typename T, typename CallerRow>
    requires is_persisted_session_handle_v<T>
[[nodiscard]] persisted_result_t<T, CallerRow> wrap_session_handle(T&& value, StateCarrier<CallerRow>) {
    return std::forward<T>(value);
}

template <typename A, typename B, typename CallerRow>
[[nodiscard]] persisted_result_t<std::pair<A, B>, CallerRow> wrap_pair_result(std::pair<A, B>&& value,
                                                                              StateCarrier<CallerRow> carrier) {
    return {
        std::move(value.first),
        wrap_persisted_result<B, CallerRow>(std::move(value.second), std::move(carrier)),
    };
}

template <typename T, typename E, typename CallerRow>
[[nodiscard]] persisted_result_t<std::expected<T, E>, CallerRow> wrap_expected_result(std::expected<T, E>&& value,
                                                                                      StateCarrier<CallerRow> carrier) {
    using Out = persisted_result_t<std::expected<T, E>, CallerRow>;
    if (!value) {
        return Out{std::unexpected{std::move(value.error())}};
    }
    return Out{wrap_persisted_result<T, CallerRow>(std::move(*value), std::move(carrier))};
}

template <typename T, typename CallerRow>
[[nodiscard]] persisted_result_t<T, CallerRow> wrap_persisted_result(T&& value, StateCarrier<CallerRow> carrier) {
    using Raw = std::remove_cvref_t<T>;
    if constexpr (is_expected_v<Raw>) {
        return wrap_expected_result(std::forward<T>(value), std::move(carrier));
    } else if constexpr (is_pair_v<Raw>) {
        return wrap_pair_result(std::forward<T>(value), std::move(carrier));
    } else if constexpr (::crucible::safety::extract::IsSessionHandle<Raw>) {
        return wrap_session_handle(std::forward<T>(value), std::move(carrier));
    } else {
        return std::forward<T>(value);
    }
}

}  // namespace detail

template <typename Inner, typename CallerRow>
class [[nodiscard]] PersistedSessionHandle
    : public SessionHandleBase<typename Inner::protocol, PersistedSessionHandle<Inner, CallerRow>> {
    Inner inner_;
    std::unique_ptr<SessionPersistenceState<CallerRow>> owned_state_{};
    SessionPersistenceState<CallerRow>* state_ = nullptr;

    using Base = SessionHandleBase<typename Inner::protocol, PersistedSessionHandle<Inner, CallerRow>>;

    [[nodiscard]] detail::StateCarrier<CallerRow> release_state_() noexcept {
        return detail::StateCarrier<CallerRow>{
            .owned = std::move(owned_state_),
            .state = state_,
        };
    }

    template <typename Result>
    [[nodiscard]] auto finish_result_(Result&& result) && {
        using Raw = std::remove_cvref_t<Result>;
        [[assume(state_ != nullptr)]];
        if constexpr (detail::is_expected_v<Raw>) {
            if (!result) {
                flush_all_or_abort_();
                return detail::wrap_persisted_result<Result, CallerRow>(std::forward<Result>(result), {});
            }
        }
        if constexpr (detail::persisted_result_carries_handle_v<Raw, CallerRow>) {
            if (!state_->flush_if_due()) [[unlikely]] {
                std::abort();
            }
            return detail::wrap_persisted_result<Result, CallerRow>(std::forward<Result>(result), release_state_());
        } else {
            flush_all_or_abort_();
            return std::forward<Result>(result);
        }
    }

    void flush_all_or_abort_() {
        [[assume(state_ != nullptr)]];
        if (!state_->flush_all()) [[unlikely]] {
            std::abort();
        }
    }

    template <typename F>
    decltype(auto) finish_call_(F&& f) && {
        this->mark_consumed_();
        if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
            std::invoke(std::forward<F>(f));
            flush_all_or_abort_();
        } else {
            auto result = std::invoke(std::forward<F>(f));
            return std::move(*this).finish_result_(std::move(result));
        }
    }

public:
    using inner_type = Inner;
    using protocol = typename Inner::protocol;
    using resource_type = typename Inner::resource_type;
    using loop_ctx = typename Inner::loop_ctx;
    using caller_row = CallerRow;
    using state_type = SessionPersistenceState<CallerRow>;

    PersistedSessionHandle(Inner inner, std::unique_ptr<state_type> state,
                           std::source_location loc = std::source_location::current()) noexcept
        : Base{loc}, inner_{std::move(inner)}, owned_state_{std::move(state)}, state_{owned_state_.get()} {}

    PersistedSessionHandle(Inner inner, std::unique_ptr<state_type> state, state_type* borrowed_state,
                           std::source_location loc = std::source_location::current()) noexcept
        : Base{loc},
          inner_{std::move(inner)},
          owned_state_{std::move(state)},
          state_{borrowed_state != nullptr ? borrowed_state : owned_state_.get()} {}

    PersistedSessionHandle(Inner inner, state_type& borrowed_state,
                           std::source_location loc = std::source_location::current()) noexcept
        : Base{loc}, inner_{std::move(inner)}, state_{&borrowed_state} {}

    PersistedSessionHandle(PersistedSessionHandle&&) noexcept = default;
    PersistedSessionHandle& operator=(PersistedSessionHandle&&) noexcept = default;
    ~PersistedSessionHandle() = default;

    template <typename... Args>
        requires requires(Inner&& inner, Args&&... args) { std::move(inner).close(std::forward<Args>(args)...); }
    [[nodiscard]] decltype(auto) close(Args&&... args) && {
        return std::move(*this).finish_call_(
            [&]() -> decltype(auto) { return std::move(inner_).close(std::forward<Args>(args)...); });
    }

    template <typename... Args>
        requires requires(Inner&& inner, Args&&... args) { std::move(inner).send(std::forward<Args>(args)...); }
    [[nodiscard]] decltype(auto) send(Args&&... args) && {
        return std::move(*this).finish_call_(
            [&]() -> decltype(auto) { return std::move(inner_).send(std::forward<Args>(args)...); });
    }

    template <typename... Args>
        requires requires(Inner&& inner, Args&&... args) { std::move(inner).recv(std::forward<Args>(args)...); }
    [[nodiscard]] decltype(auto) recv(Args&&... args) && {
        return std::move(*this).finish_call_(
            [&]() -> decltype(auto) { return std::move(inner_).recv(std::forward<Args>(args)...); });
    }

    template <std::size_t I, typename... Args>
        requires requires(Inner&& inner, Args&&... args) {
            std::move(inner).template select<I>(std::forward<Args>(args)...);
        }
    [[nodiscard]] decltype(auto) select(Args&&... args) && {
        return std::move(*this).finish_call_(
            [&]() -> decltype(auto) { return std::move(inner_).template select<I>(std::forward<Args>(args)...); });
    }

    template <std::size_t I>
        requires requires(Inner&& inner) { std::move(inner).template select_local<I>(); }
    [[nodiscard]] decltype(auto) select_local() && {
        return std::move(*this).finish_call_(
            [&]() -> decltype(auto) { return std::move(inner_).template select_local<I>(); });
    }

    template <std::size_t I>
        requires requires(Inner&& inner) { std::move(inner).template pick_local<I>(); }
    [[nodiscard]] decltype(auto) pick_local() && {
        return std::move(*this).finish_call_(
            [&]() -> decltype(auto) { return std::move(inner_).template pick_local<I>(); });
    }

    template <typename... Args>
        requires requires(Inner&& inner, Args&&... args) { std::move(inner).branch(std::forward<Args>(args)...); }
    decltype(auto) branch(Args&&... args) && {
        return std::move(*this).finish_call_(
            [&]() -> decltype(auto) { return std::move(inner_).branch(std::forward<Args>(args)...); });
    }

    template <typename... Args>
        requires requires(Inner&& inner, Args&&... args) { std::move(inner).base(std::forward<Args>(args)...); }
    [[nodiscard]] decltype(auto) base(Args&&... args) && {
        return std::move(*this).finish_call_(
            [&]() -> decltype(auto) { return std::move(inner_).base(std::forward<Args>(args)...); });
    }

    template <typename... Args>
        requires requires(Inner&& inner, Args&&... args) { std::move(inner).rollback(std::forward<Args>(args)...); }
    [[nodiscard]] decltype(auto) rollback(Args&&... args) && {
        return std::move(*this).finish_call_(
            [&]() -> decltype(auto) { return std::move(inner_).rollback(std::forward<Args>(args)...); });
    }

    template <typename... Args>
        requires requires(Inner&& inner, Args&&... args) { std::move(inner).delegate(std::forward<Args>(args)...); }
    [[nodiscard]] decltype(auto) delegate(Args&&... args) && {
        return std::move(*this).finish_call_(
            [&]() -> decltype(auto) { return std::move(inner_).delegate(std::forward<Args>(args)...); });
    }

    template <typename... Args>
        requires requires(Inner&& inner, Args&&... args) {
            std::move(inner).delegate_local(std::forward<Args>(args)...);
        }
    [[nodiscard]] decltype(auto) delegate_local(Args&&... args) && {
        return std::move(*this).finish_call_(
            [&]() -> decltype(auto) { return std::move(inner_).delegate_local(std::forward<Args>(args)...); });
    }

    template <typename... Args>
        requires requires(Inner&& inner, Args&&... args) { std::move(inner).accept(std::forward<Args>(args)...); }
    [[nodiscard]] decltype(auto) accept(Args&&... args) && {
        return std::move(*this).finish_call_(
            [&]() -> decltype(auto) { return std::move(inner_).accept(std::forward<Args>(args)...); });
    }

    template <typename... Args>
        requires requires(Inner&& inner, Args&&... args) { std::move(inner).accept_with(std::forward<Args>(args)...); }
    [[nodiscard]] decltype(auto) accept_with(Args&&... args) && {
        return std::move(*this).finish_call_(
            [&]() -> decltype(auto) { return std::move(inner_).accept_with(std::forward<Args>(args)...); });
    }

    // A detach ends the session without a terminal step, so it flushes
    // everything pending before the state dies.  Both this wrapper and the
    // inner handle are marked consumed, which is what keeps either destructor
    // from reporting an abandoned protocol.
    //
    // The recorded roles are zero.  Role identity is not tracked through this
    // chain, and the reason's schema hash is what tells two reason types apart
    // in the record.
    template <typename Reason>
        requires ::crucible::safety::proto::DetachReason<Reason>
    void detach(Reason reason_tag) && {
        [[assume(state_ != nullptr)]];
        this->mark_consumed_();
        state_->log().record_now(::crucible::safety::proto::SessionEvent::detach(
            ::crucible::safety::proto::RoleTagId{}, ::crucible::safety::proto::RoleTagId{},
            ::crucible::safety::proto::detach_reason_kind_v<Reason>,
            ::crucible::safety::proto::default_schema_hash<Reason>));
        std::move(inner_).detach(reason_tag);
        flush_all_or_abort_();
    }

    [[nodiscard]] bool flush() & {
        [[assume(state_ != nullptr)]];
        return state_->flush_all();
    }

    [[nodiscard]] std::size_t pending_persisted_events() const noexcept {
        [[assume(state_ != nullptr)]];
        return state_->pending_count();
    }

    [[nodiscard]] std::size_t flushed_persisted_events() const noexcept {
        [[assume(state_ != nullptr)]];
        return state_->flushed_count();
    }

    [[nodiscard]] SessionEventLog& event_log() const noexcept {
        [[assume(state_ != nullptr)]];
        return state_->log();
    }

    [[nodiscard]] resource_type& resource() & noexcept { return inner_.resource(); }

    [[nodiscard]] const resource_type& resource() const& noexcept { return inner_.resource(); }

    template <::crucible::effects::IsExecCtx Ctx>
        requires ::crucible::effects::CtxAdmits<Ctx, CipherSessionEventPersistenceRow>
    [[nodiscard]] static std::vector<SessionEvent> replay(Ctx const&, Cipher& cipher, CipherOpenView const& view,
                                                          SessionTagId session, StepId from_step = {}) {
        return cipher.load_session_events(view, session, from_step);
    }
};

template <typename Proto, ::crucible::effects::IsExecCtx Ctx, typename Resource>
    requires ::crucible::effects::CtxAdmits<Ctx, CipherSessionEventPersistenceRow>
// This factory is deliberately not constexpr, unlike the others of its kind.
// It allocates the persistence state, and declaring it constexpr would state a
// cost it does not have.
// §XXI carve-out: cx=alloc — the mint heap-allocates the persistence state.
[[nodiscard]] auto mint_persisted_session(Ctx const& ctx, Cipher& cipher, CipherOpenView const& view,
                                          Resource&& resource, SessionTagId session, RoleTagId self, RoleTagId peer,
                                          SessionPersistencePolicy policy = {}) noexcept {
    using CallerRow = typename Ctx::row_type;
    auto state = std::make_unique<SessionPersistenceState<CallerRow>>(
        cipher, view, session, policy,
        ::crucible::fixy::time::mint_clock_reader<::crucible::fixy::time::ClockSource_v::Monotonic>(ctx));
    auto bare = mint_session_handle<Proto>(std::forward<Resource>(resource));
    auto recording = mint_recording_session(std::move(bare), state->log(), self, peer);
    return PersistedSessionHandle<decltype(recording), CallerRow>{std::move(recording), std::move(state)};
}

template <typename Proto, typename Resource>
void mint_persisted_session(Cipher&, CipherOpenView const&, Resource&&, SessionTagId, RoleTagId, RoleTagId,
                            SessionPersistencePolicy = {}) =
    delete("[PersistedSession_CtxRequired] mint_persisted_session<Proto> "
           "requires an execution context whose row admits Cipher "
           "persistence effects.");

template <typename Proto, ::crucible::effects::IsExecCtx Ctx, typename Resource>
void mint_persisted_session(Ctx const&, Cipher&, Resource&&, SessionTagId, RoleTagId, RoleTagId,
                            SessionPersistencePolicy = {}) =
    delete("[PersistedSession_OpenViewRequired] "
           "mint_persisted_session<Proto> requires CipherOpenView at "
           "the mint boundary; pass cipher.mint_open_view() explicitly.");

template <::crucible::effects::IsExecCtx Ctx, typename Proto, typename Resource, typename LoopCtx>
    requires ::crucible::effects::CtxAdmits<Ctx, CipherSessionEventPersistenceRow>
[[nodiscard]] auto mint_persisted_session(Ctx const& ctx, SessionHandle<Proto, Resource, LoopCtx> inner, Cipher& cipher,
                                          CipherOpenView const& view, SessionTagId session, RoleTagId self,
                                          RoleTagId peer, SessionPersistencePolicy policy = {}) noexcept {
    using CallerRow = typename Ctx::row_type;
    auto state = std::make_unique<SessionPersistenceState<CallerRow>>(
        cipher, view, session, policy,
        ::crucible::fixy::time::mint_clock_reader<::crucible::fixy::time::ClockSource_v::Monotonic>(ctx));
    auto recording = mint_recording_session(std::move(inner), state->log(), self, peer);
    return PersistedSessionHandle<decltype(recording), CallerRow>{std::move(recording), std::move(state)};
}

template <::crucible::effects::IsExecCtx Ctx, typename Proto, typename Resource, typename LoopCtx>
void mint_persisted_session(Ctx const&, SessionHandle<Proto, Resource, LoopCtx>, Cipher&, SessionTagId, RoleTagId,
                            RoleTagId, SessionPersistencePolicy = {}) =
    delete("[PersistedSession_OpenViewRequired] "
           "mint_persisted_session(ctx, handle, cipher, ...) requires "
           "CipherOpenView at the mint boundary; pass "
           "cipher.mint_open_view() explicitly.");

template <::crucible::effects::IsExecCtx Ctx, typename Proto, typename PS, typename Resource, typename LoopCtx>
    requires ::crucible::effects::CtxAdmits<Ctx, CipherSessionEventPersistenceRow>
[[nodiscard]] auto mint_persisted_session(Ctx const& ctx, PermissionedSessionHandle<Proto, PS, Resource, LoopCtx> inner,
                                          Cipher& cipher, CipherOpenView const& view, SessionTagId session,
                                          RoleTagId self, RoleTagId peer,
                                          SessionPersistencePolicy policy = {}) noexcept {
    using CallerRow = typename Ctx::row_type;
    auto state = std::make_unique<SessionPersistenceState<CallerRow>>(
        cipher, view, session, policy,
        ::crucible::fixy::time::mint_clock_reader<::crucible::fixy::time::ClockSource_v::Monotonic>(ctx));
    auto recording = mint_recording_session(std::move(inner), state->log(), self, peer);
    return PersistedSessionHandle<decltype(recording), CallerRow>{std::move(recording), std::move(state)};
}

template <::crucible::effects::IsExecCtx Ctx, typename Proto, typename PS, typename Resource, typename LoopCtx>
void mint_persisted_session(Ctx const&, PermissionedSessionHandle<Proto, PS, Resource, LoopCtx>, Cipher&, SessionTagId,
                            RoleTagId, RoleTagId, SessionPersistencePolicy = {}) =
    delete("[PersistedSession_OpenViewRequired] "
           "mint_persisted_session(ctx, psh, cipher, ...) requires "
           "CipherOpenView at the mint boundary; pass "
           "cipher.mint_open_view() explicitly.");

}  // namespace crucible::safety::proto
