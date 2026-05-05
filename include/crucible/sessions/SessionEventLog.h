#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto — typed append-only session event log
//
// Task #404 SAFEINT-B15 from misc/24_04_2026_safety_integration.md
// §15.  Promotes the bit-exact-replay discipline from an ad-hoc
// trace to a typed structure.  The log records the original L1
// combinators (Send / Recv / Select / Offer / Close), explicit
// non-terminal detaches, and the GAPS-050..052 extension set:
// Stop, Checkpoint_Base, Checkpoint_Rollback, Delegate, and Accept.
// OrderedAppendOnly gives every event a monotonic step_id and
// structurally forbids in-place rewrite of past events.
//
// Two pieces ship in this header:
//
//   * `SessionEvent` — the per-operation record (POD, 56 bytes,
//     TriviallyCopyable for fast bulk-drain to Cipher).  Carries
//     step_id, session, from_role, to_role, op kind, and two typed
//     64-bit payload lanes interpreted by the selected SessionOp
//     (payload schema/hash for Send/Recv, checkpoint ids, delegated
//     protocol hashes, crash recovery hashes, etc.).
//
//   * `SessionEventLog` — the log primitive.  Wraps an
//     OrderedAppendOnly<SessionEvent, StepIdKeyFn> (step monotonicity
//     enforced by Mutation.h's contract).  Owns a session identifier
//     and a step-counter (AtomicMonotonic) so multiple writers can
//     synthesise step_ids without colliding.  Pinned: the atomic
//     counter and the log storage IS the log identity; movement
//     would fork it.
//
// The recording-handle wrapper that USES this log lives in a
// sibling header (safety/RecordingSessionHandle.h, also #404) — the
// log primitive is reusable independently for any code that wants
// to record protocol events manually.
//
// ─── Strong-ID convention ──────────────────────────────────────────
//
// Each ID is a thin TypeSafe wrapper around uint64_t with no
// implicit conversions and no arithmetic.  `value` is the only
// observable; comparison + ordering are defaulted via <=>.  Inhabits
// the same TypeSafe discipline as OpIndex / SchemaHash / etc.
// Strong typing prevents the canonical "swapped from_role and
// to_role" bug at the call site.
//
// ─── Determinism contract ──────────────────────────────────────────
//
// step_id is monotonically non-decreasing within a single
// SessionEventLog (enforced by OrderedAppendOnly's contract).
// `record(ev)` does NOT auto-stamp the step_id — the caller computes
// it via `next_step()`.  This separation lets the recording wrapper
// stamp the step_id atomically per-op without serialising the log
// itself behind a lock.  The step counter is AtomicMonotonic so
// multiple recording threads see strictly-increasing values.
//
// ─── References ────────────────────────────────────────────────────
//
//   misc/24_04_2026_safety_integration.md §15 — design rationale
//   safety/Mutation.h — OrderedAppendOnly + AtomicMonotonic
//   bridges/RecordingSessionHandle.h — the wrapper that emits events
//   safety/Pinned.h — Pinned constraint
//   GAPS-050 — Stop event representation
//   GAPS-051 — Checkpoint_Base / Checkpoint_Rollback events
//   GAPS-052 — Delegate / Accept handoff events
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/Types.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Pinned.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace crucible::safety::proto {

// ═════════════════════════════════════════════════════════════════════
// ── Strong IDs ───────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Identifier for a logical session — typically derived from the
// participating roles' tag types or assigned at channel-establishment
// time.  Distinct sessions logged to the same physical event log
// remain separable via this field.
struct SessionTagId {
    uint64_t value = 0;
    constexpr auto operator<=>(const SessionTagId&) const noexcept = default;
    constexpr bool operator==(const SessionTagId&)  const noexcept = default;
};

// Identifier for a participant role — typically derived from the
// role tag type (Client / Server / Coord / Follower / etc.) by a
// role-tag-to-id mapping.  No semantic interpretation at the log
// level; the consumer interprets.
struct RoleTagId {
    uint64_t value = 0;
    constexpr auto operator<=>(const RoleTagId&) const noexcept = default;
    constexpr bool operator==(const RoleTagId&)  const noexcept = default;
};

// Hash of the payload's compile-time type — typically derived from a
// __PRETTY_FUNCTION__-style identifier, so identical payload types
// across TUs produce identical SchemaHashes.  `default_schema_hash<T>`
// below is the canonical computation.
struct SchemaHash {
    uint64_t value = 0;
    constexpr auto operator<=>(const SchemaHash&) const noexcept = default;
    constexpr bool operator==(const SchemaHash&)  const noexcept = default;
};

// Hash of the payload value — opt-in.  The recording wrapper defaults
// to `PayloadHash{0}` for types that don't expose a hashing function;
// users with strict replay-determinism requirements provide their own
// hash.  Zero is a sentinel meaning "not hashed".
struct PayloadHash {
    uint64_t value = 0;
    constexpr auto operator<=>(const PayloadHash&) const noexcept = default;
    constexpr bool operator==(const PayloadHash&)  const noexcept = default;
};

// Hash of the recovery path chosen after a Stop event.  Zero is the
// sentinel for "no recovery path was selected or recorded".
struct RecoveryPathHash {
    uint64_t value = 0;
    constexpr auto operator<=>(const RecoveryPathHash&) const noexcept = default;
    constexpr bool operator==(const RecoveryPathHash&)  const noexcept = default;
};

// Identifier for an application-level checkpoint.  The event log does
// not interpret it; replay code uses it to match a transition against
// the saved state selected by the application/Cipher layer.
struct CheckpointId {
    uint64_t value = 0;
    constexpr auto operator<=>(const CheckpointId&) const noexcept = default;
    constexpr bool operator==(const CheckpointId&)  const noexcept = default;
};

// Hash of the permission set transferred with a delegated session.
// Zero means "no inner permission set recorded" and is the correct
// default for plain SessionHandle delegation.
struct InnerPermSetHash {
    uint64_t value = 0;
    constexpr auto operator<=>(const InnerPermSetHash&) const noexcept = default;
    constexpr bool operator==(const InnerPermSetHash&)  const noexcept = default;
};

// Monotonic per-log step counter.  Strictly non-decreasing within a
// SessionEventLog (enforced by OrderedAppendOnly's contract).  Distinct
// SessionEventLogs maintain separate StepId sequences.
struct StepId {
    uint64_t value = 0;
    constexpr auto operator<=>(const StepId&) const noexcept = default;
    constexpr bool operator==(const StepId&)  const noexcept = default;
};

// ═════════════════════════════════════════════════════════════════════
// ── SessionOp — what kind of operation produced this event ──────────
// ═════════════════════════════════════════════════════════════════════

namespace event_detail {

enum class SessionOp : uint8_t {
    Send   = 1,   // Send<T, K>::send() — payload sent to peer
    Recv   = 2,   // Recv<T, K>::recv() — payload received from peer
    Select = 3,   // Select<Bs...>::select<I>() — branch chosen by self
    Offer  = 4,   // Offer<Bs...>::pick<I>() — branch chosen by peer
    Close  = 5,   // End::close() — terminal, session completed
    Detach = 6,   // SessionHandle::detach(reason) — abandoned at non-terminal
    Stop   = 7,   // Stop_g<C>::close() — crash-stop terminal observed
    Checkpoint_Base     = 8,  // CheckpointedSession::base()
    Checkpoint_Rollback = 9,  // CheckpointedSession::rollback()
    Delegate = 10, // Delegate<T, K>::delegate()
    Accept   = 11, // Accept<T, K>::accept()
};

}  // namespace event_detail

using SessionOp = event_detail::SessionOp;

[[nodiscard]] constexpr std::string_view session_op_name(SessionOp op) noexcept {
    switch (op) {
        case SessionOp::Send:   return "Send";
        case SessionOp::Recv:   return "Recv";
        case SessionOp::Select: return "Select";
        case SessionOp::Offer:  return "Offer";
        case SessionOp::Close:  return "Close";
        case SessionOp::Detach: return "Detach";
        case SessionOp::Stop:   return "Stop";
        case SessionOp::Checkpoint_Base:     return "Checkpoint_Base";
        case SessionOp::Checkpoint_Rollback: return "Checkpoint_Rollback";
        case SessionOp::Delegate: return "Delegate";
        case SessionOp::Accept:   return "Accept";
        default:                return "?";
    }
}

// Reason classifier for SessionOp::Stop.  Kept as a one-byte enum so
// the fixed 56-byte SessionEvent layout survives the Stop extension.
enum class StopReasonKind : uint8_t {
    Unknown     = 0,
    PeerCrashed = 1,
    LocalAbort  = 2,
    Recovery    = 3,
};

// Choice classifier for CheckpointedSession events.  Stored in the
// same one-byte control slot as StopReasonKind; the SessionOp selects
// which interpretation is valid.
enum class CheckpointChoice : uint8_t {
    Base     = 1,
    Rollback = 2,
};

// ═════════════════════════════════════════════════════════════════════
// ── SessionEvent — the per-operation record ─────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Layout: 56 bytes.  TriviallyCopyable for memcpy-bulk-drain into
// the Cipher's cold tier.  Field ordering minimises padding while
// keeping the most-queried fields (step_id, session, op) at the head.
//
// Stop events reuse the two generic payload lanes to preserve the
// fixed-size record:
//   payload_schema.value -> peer_tag
//   payload_hash.value   -> recovery_path_hash
//   reason_kind          -> StopReasonKind
//
// Checkpoint events reuse the same lanes:
//   payload_schema.value -> checkpoint_id
//   payload_hash.value   -> saved_state_content_hash.raw()
//   reason_kind          -> CheckpointChoice
//
// Delegate/Accept events reuse the role fields plus the payload lanes:
//   from_role/to_role    -> sender/recipient role
//   payload_schema.value -> delegated_proto_hash.raw()
//   payload_hash.value   -> inner_perm_set_hash
//
// The typed helpers below make that variant payload explicit without
// adding storage or runtime dispatch.

struct SessionEvent {
    StepId       step_id        {};
    SessionTagId session        {};
    RoleTagId    from_role      {};
    RoleTagId    to_role        {};
    SchemaHash   payload_schema {};
    PayloadHash  payload_hash   {};
    SessionOp    op             = SessionOp::Send;
    uint8_t      branch_index   = 0;   // Select/Offer: chosen index; else 0
    uint8_t      reason_kind    = 0;   // Stop: StopReasonKind; else 0
    uint8_t      pad[5]{};              // explicit zero-init padding

    [[nodiscard]] static constexpr SessionEvent stop(
        RoleTagId self,
        RoleTagId peer,
        RoleTagId stopped_peer,
        StopReasonKind reason = StopReasonKind::PeerCrashed,
        RecoveryPathHash recovery_path = {}) noexcept
    {
        return SessionEvent{
            .from_role      = self,
            .to_role        = peer,
            .payload_schema = SchemaHash{stopped_peer.value},
            .payload_hash   = PayloadHash{recovery_path.value},
            .op             = SessionOp::Stop,
            .reason_kind    = static_cast<uint8_t>(reason),
        };
    }

    [[nodiscard]] static constexpr SessionEvent checkpoint_base(
        RoleTagId self,
        RoleTagId peer,
        CheckpointId checkpoint,
        ::crucible::ContentHash saved_state = {}) noexcept
    {
        return SessionEvent{
            .from_role      = self,
            .to_role        = peer,
            .payload_schema = SchemaHash{checkpoint.value},
            .payload_hash   = PayloadHash{saved_state.raw()},
            .op             = SessionOp::Checkpoint_Base,
            .reason_kind    = static_cast<uint8_t>(CheckpointChoice::Base),
        };
    }

    [[nodiscard]] static constexpr SessionEvent checkpoint_rollback(
        RoleTagId self,
        RoleTagId peer,
        CheckpointId checkpoint,
        ::crucible::ContentHash saved_state = {}) noexcept
    {
        return SessionEvent{
            .from_role      = self,
            .to_role        = peer,
            .payload_schema = SchemaHash{checkpoint.value},
            .payload_hash   = PayloadHash{saved_state.raw()},
            .op             = SessionOp::Checkpoint_Rollback,
            .reason_kind    = static_cast<uint8_t>(CheckpointChoice::Rollback),
        };
    }

    [[nodiscard]] static constexpr SessionEvent delegate_handoff(
        RoleTagId sender,
        RoleTagId recipient,
        ::crucible::ContentHash delegated_proto_hash,
        InnerPermSetHash inner_perm_set = {}) noexcept
    {
        return SessionEvent{
            .from_role      = sender,
            .to_role        = recipient,
            .payload_schema = SchemaHash{delegated_proto_hash.raw()},
            .payload_hash   = PayloadHash{inner_perm_set.value},
            .op             = SessionOp::Delegate,
        };
    }

    [[nodiscard]] static constexpr SessionEvent accept_handoff(
        RoleTagId recipient,
        RoleTagId sender,
        ::crucible::ContentHash accepted_proto_hash,
        InnerPermSetHash inner_perm_set = {}) noexcept
    {
        return SessionEvent{
            .from_role      = sender,
            .to_role        = recipient,
            .payload_schema = SchemaHash{accepted_proto_hash.raw()},
            .payload_hash   = PayloadHash{inner_perm_set.value},
            .op             = SessionOp::Accept,
        };
    }

    [[nodiscard]] constexpr RoleTagId stop_peer_tag() const noexcept {
        return RoleTagId{payload_schema.value};
    }

    [[nodiscard]] constexpr StopReasonKind stop_reason_kind() const noexcept {
        return static_cast<StopReasonKind>(reason_kind);
    }

    [[nodiscard]] constexpr RecoveryPathHash stop_recovery_path_hash() const noexcept {
        return RecoveryPathHash{payload_hash.value};
    }

    [[nodiscard]] constexpr CheckpointId checkpoint_id() const noexcept {
        return CheckpointId{payload_schema.value};
    }

    [[nodiscard]] constexpr CheckpointChoice checkpoint_choice() const noexcept {
        return static_cast<CheckpointChoice>(reason_kind);
    }

    [[nodiscard]] constexpr ::crucible::ContentHash
    checkpoint_saved_state_content_hash() const noexcept {
        return ::crucible::ContentHash::from_raw(payload_hash.value);
    }

    [[nodiscard]] constexpr ::crucible::ContentHash
    delegated_proto_hash() const noexcept {
        return ::crucible::ContentHash::from_raw(payload_schema.value);
    }

    [[nodiscard]] constexpr RoleTagId delegate_recipient_role_tag() const noexcept {
        return to_role;
    }

    [[nodiscard]] constexpr RoleTagId accept_sender_role_tag() const noexcept {
        return from_role;
    }

    [[nodiscard]] constexpr InnerPermSetHash inner_perm_set_hash() const noexcept {
        return InnerPermSetHash{payload_hash.value};
    }
};

static_assert(sizeof(SessionEvent) == 56,
    "SessionEvent layout must be exactly 56 bytes — Cipher cold-tier "
    "serialisation depends on the fixed size.  If a field changes, "
    "bump the layout version and update the deserialiser.");
static_assert(std::is_trivially_copyable_v<SessionEvent>,
    "SessionEvent must be TriviallyCopyable for fast bulk drain.");

// ═════════════════════════════════════════════════════════════════════
// ── KeyFn / Cmp for OrderedAppendOnly<SessionEvent, ...> ────────────
// ═════════════════════════════════════════════════════════════════════
//
// OrderedAppendOnly requires stateless KeyFn + Cmp (so the contract
// can construct them per-call).  Project step_id and compare on its
// underlying value.

struct StepIdKeyFn {
    constexpr StepId operator()(const SessionEvent& e) const noexcept {
        return e.step_id;
    }
};

struct StepIdLess {
    constexpr bool operator()(StepId a, StepId b) const noexcept {
        return a.value < b.value;
    }
};

// ═════════════════════════════════════════════════════════════════════
// ── default_schema_hash<T> — compile-time type identifier ───────────
// ═════════════════════════════════════════════════════════════════════
//
// FNV-1a over __PRETTY_FUNCTION__ — same type across TUs hashes
// identically (the function-name stamp is a function of T's spelling
// in the GCC mangling, which is stable per (T, target-triple)).
// Cross-target stability is a non-goal here; replay determinism
// within a fixed build is the contract.

namespace detail {

// FNV-1a over a string view at consteval.
[[nodiscard]] inline consteval uint64_t fnv1a_64(std::string_view s) noexcept {
    constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
    constexpr uint64_t kFnvPrime       = 0x100000001b3ULL;
    uint64_t h = kFnvOffsetBasis;
    for (char c : s) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        h *= kFnvPrime;
    }
    return h;
}

template <typename T>
[[nodiscard]] inline consteval std::string_view pretty_function_for() noexcept {
    return std::string_view{__PRETTY_FUNCTION__};
}

}  // namespace detail

template <typename T>
inline constexpr SchemaHash default_schema_hash{
    detail::fnv1a_64(detail::pretty_function_for<T>())
};

template <typename T>
inline constexpr ::crucible::ContentHash default_proto_hash =
    ::crucible::ContentHash::from_raw(default_schema_hash<T>.value);

// ═════════════════════════════════════════════════════════════════════
// ── default_payload_hash<T> — opt-in payload hashing ────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Default is the sentinel PayloadHash{0} ("not hashed").  Users with
// strict-replay-audit requirements specialise this template for their
// payload type:
//
//   template <>
//   inline constexpr auto crucible::safety::proto::default_payload_hash_fn<MyPayload> =
//       [](const MyPayload& p) noexcept -> PayloadHash {
//           return PayloadHash{my_hasher(p)};
//       };
//
// The recording wrapper consults this function template at compile
// time; the cost per record is whatever the user's hasher costs
// (defaulted to a sentinel that does no work).

template <typename T>
inline constexpr auto default_payload_hash_fn =
    [](const T& /*v*/) noexcept -> PayloadHash {
        return PayloadHash{0};
    };

// ═════════════════════════════════════════════════════════════════════
// ── SessionEventLog — the append-only log ───────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Pinned: the atomic step counter IS the log's identity for ordering.
// Movement would fork the counter across two distinct objects,
// breaking the monotone-step invariant downstream consumers rely on.

class [[nodiscard]] SessionEventLog : Pinned<SessionEventLog> {
    OrderedAppendOnly<SessionEvent, StepIdKeyFn, StepIdLess> log_{};
    SessionTagId               session_id_{};
    AtomicMonotonic<uint64_t>  step_counter_{0};

public:
    using event_type   = SessionEvent;
    using storage_type = std::vector<SessionEvent>;

    // Construct with an explicit session identifier — typically derived
    // by the user from the role-tag types or supplied by the channel-
    // establishment site.  Default (SessionTagId{0}) is permitted for
    // single-session test code where the identifier carries no
    // information.
    constexpr explicit SessionEventLog(SessionTagId id = {}) noexcept
        : session_id_{id} {}

    // Mint the next monotonic step_id.  Safe to call concurrently from
    // multiple recording threads — AtomicMonotonic guarantees strictly-
    // increasing values across threads via fetch_max on x86-64 / ARM.
    [[nodiscard]] StepId next_step() noexcept {
        // AtomicMonotonic doesn't expose a fetch-and-bump primitive
        // directly (try_advance returns bool); compose from the get +
        // try_advance pair.  The CAS loop is implicit in try_advance's
        // fast path on integral T + std::less<T>.
        for (;;) {
            const uint64_t prev = step_counter_.get();
            const uint64_t next = prev + 1;
            if (step_counter_.try_advance(next)) {
                return StepId{next};
            }
            // Another thread bumped past us; retry from the new value.
        }
    }

    // Record an event.  step_id MUST be non-decreasing relative to
    // the last appended event (OrderedAppendOnly's contract).  Use
    // next_step() to mint a fresh step_id; manual step_ids are
    // permitted for replay-from-snapshot scenarios where the caller
    // controls ordering.
    void record(SessionEvent ev) {
        log_.append(std::move(ev));
    }

    // Convenience: stamp + record in one call.  Most callers want
    // this; the manual `next_step() + record` split exists for
    // replay-style use cases.
    void record_now(SessionEvent ev) {
        ev.step_id = next_step();
        ev.session = session_id_;
        log_.append(std::move(ev));
    }

    // Canonical event append entrypoint used by replay-facing wrappers.
    // It preserves the same stamping semantics as record_now while
    // making the call site read in protocol-event vocabulary.
    void append_event(SessionEvent ev) {
        record_now(std::move(ev));
    }

    struct ReplayRange {
        using const_iterator = SessionEventLog::storage_type::const_iterator;

        const_iterator first{};
        const_iterator last {};

        [[nodiscard]] const_iterator begin() const noexcept { return first; }
        [[nodiscard]] const_iterator end()   const noexcept { return last;  }
    };

    [[nodiscard]] ReplayRange replay_iter() const noexcept {
        return ReplayRange{log_.begin(), log_.end()};
    }

    // Read-only accessors.
    [[nodiscard]] SessionTagId session() const noexcept { return session_id_; }
    [[nodiscard]] std::size_t  size()    const noexcept { return log_.size(); }
    [[nodiscard]] bool         empty()   const noexcept { return log_.empty(); }

    [[nodiscard]] const SessionEvent& operator[](std::size_t i) const noexcept {
        return log_[i];
    }
    [[nodiscard]] const SessionEvent& front() const noexcept { return log_.front(); }
    [[nodiscard]] const SessionEvent& back()  const noexcept { return log_.back();  }

    [[nodiscard]] auto begin() const noexcept { return log_.begin(); }
    [[nodiscard]] auto end()   const noexcept { return log_.end();   }

    // Consuming drain — yield the underlying storage and leave *this
    // empty.  Used at end-of-session to ship the log to durable
    // storage (Cipher cold tier).  The Pinned constraint forbids
    // moving the log itself, but draining yields the backing vector,
    // which is freely movable.
    [[nodiscard]] storage_type drain() && noexcept(
        std::is_nothrow_move_constructible_v<storage_type>)
    {
        return std::move(log_).drain();
    }
};

}  // namespace crucible::safety::proto
