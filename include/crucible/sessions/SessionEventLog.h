#pragma once

// A typed append-only record of the operations a session performed, in
// the order it performed them, so a run can be replayed step for step.
//
// Every identifier here is a distinct type over a plain integer, which
// is what stops a caller from silently swapping the two role fields.
//
// A step identifier is non-decreasing inside one log.  Recording does
// not stamp the step itself: the caller mints one first.  Splitting the
// two lets a writer take its step number atomically without holding the
// log while it does so.

#include <crucible/Platform.h>
#include <crucible/Types.h>
#include <crucible/algebra/lattices/CrashLattice.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Pinned.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace crucible::safety::proto {

// Several sessions may share one physical log, and this field is what
// keeps them separable.
struct SessionTagId {
    uint64_t value = 0;
    constexpr auto operator<=>(const SessionTagId&) const noexcept = default;
    constexpr bool operator==(const SessionTagId&) const noexcept = default;
};

// The log assigns no meaning to a role identifier.  The consumer maps
// it back to the role tag it came from.
struct RoleTagId {
    uint64_t value = 0;
    constexpr auto operator<=>(const RoleTagId&) const noexcept = default;
    constexpr bool operator==(const RoleTagId&) const noexcept = default;
};

// Hash of the payload's compile-time type.  The same payload type
// hashes identically in every translation unit of one build.
struct SchemaHash {
    uint64_t value = 0;
    constexpr auto operator<=>(const SchemaHash&) const noexcept = default;
    constexpr bool operator==(const SchemaHash&) const noexcept = default;
};

// Hash of the payload value.  Zero is the sentinel for a payload that
// was not hashed, so hashing is opt in.
struct PayloadHash {
    uint64_t value = 0;
    constexpr auto operator<=>(const PayloadHash&) const noexcept = default;
    constexpr bool operator==(const PayloadHash&) const noexcept = default;
};

// Hash of the recovery path chosen after a crash.  Zero means no
// recovery path was selected or recorded.
struct RecoveryPathHash {
    uint64_t value = 0;
    constexpr auto operator<=>(const RecoveryPathHash&) const noexcept = default;
    constexpr bool operator==(const RecoveryPathHash&) const noexcept = default;
};

// The log does not interpret a checkpoint identifier.  Replay uses it
// to match a transition against the saved state the application kept.
struct CheckpointId {
    uint64_t value = 0;
    constexpr auto operator<=>(const CheckpointId&) const noexcept = default;
    constexpr bool operator==(const CheckpointId&) const noexcept = default;
};

// Hash of the permission set transferred with a delegated session.
// Zero means none was recorded, which is right for a delegation that
// moves no permissions.
struct InnerPermSetHash {
    uint64_t value = 0;
    constexpr auto operator<=>(const InnerPermSetHash&) const noexcept = default;
    constexpr bool operator==(const InnerPermSetHash&) const noexcept = default;
};

// Non-decreasing inside one log.  Two logs keep independent sequences.
struct StepId {
    uint64_t value = 0;
    constexpr auto operator<=>(const StepId&) const noexcept = default;
    constexpr bool operator==(const StepId&) const noexcept = default;
};

namespace event_detail {

enum class SessionOp : uint8_t {
    Send = 1,
    Recv = 2,
    Select = 3,  // branch chosen by this side
    Offer = 4,  // branch chosen by the peer
    Close = 5,  // terminal, session completed
    Detach = 6,  // abandoned at a non-terminal position
    Stop = 7,  // crash-stop terminal observed
    Checkpoint_Base = 8,
    Checkpoint_Rollback = 9,
    Delegate = 10,
    Accept = 11,
    StorePending = 12,  // store observed before durable commit
    StoreCommitted = 13,
    LoadFromTier = 14,
    TierPromote = 15,
    TierDemote = 16,
    TierRestore = 17,  // cold restore into a warmer tier
    EpochedDelegate = 18,
    EpochedAccept = 19,
};

}  // namespace event_detail

using SessionOp = event_detail::SessionOp;

[[nodiscard]] constexpr std::string_view session_op_name(SessionOp op) noexcept {
    switch (op) {
        case SessionOp::Send:
            return "Send";
        case SessionOp::Recv:
            return "Recv";
        case SessionOp::Select:
            return "Select";
        case SessionOp::Offer:
            return "Offer";
        case SessionOp::Close:
            return "Close";
        case SessionOp::Detach:
            return "Detach";
        case SessionOp::Stop:
            return "Stop";
        case SessionOp::Checkpoint_Base:
            return "Checkpoint_Base";
        case SessionOp::Checkpoint_Rollback:
            return "Checkpoint_Rollback";
        case SessionOp::Delegate:
            return "Delegate";
        case SessionOp::Accept:
            return "Accept";
        case SessionOp::StorePending:
            return "StorePending";
        case SessionOp::StoreCommitted:
            return "StoreCommitted";
        case SessionOp::LoadFromTier:
            return "LoadFromTier";
        case SessionOp::TierPromote:
            return "TierPromote";
        case SessionOp::TierDemote:
            return "TierDemote";
        case SessionOp::TierRestore:
            return "TierRestore";
        case SessionOp::EpochedDelegate:
            return "EpochedDelegate";
        case SessionOp::EpochedAccept:
            return "EpochedAccept";
        default:
            return "?";
    }
}

[[nodiscard]] constexpr bool session_op_is_cipher(SessionOp op) noexcept {
    switch (op) {
        case SessionOp::StorePending:
        case SessionOp::StoreCommitted:
        case SessionOp::LoadFromTier:
        case SessionOp::TierPromote:
        case SessionOp::TierDemote:
        case SessionOp::TierRestore:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr bool session_op_commits_cipher_head(SessionOp op) noexcept {
    switch (op) {
        case SessionOp::StoreCommitted:
        case SessionOp::TierPromote:
        case SessionOp::TierDemote:
        case SessionOp::TierRestore:
            return true;
        default:
            return false;
    }
}

// One byte, so that adding it left the fixed record size unchanged.
enum class StopReasonKind : uint8_t {
    Unknown = 0,
    PeerCrashed = 1,
    LocalAbort = 2,
    Recovery = 3,
};

// Shares the one-byte control slot with the crash reason.  The
// operation kind decides which reading is valid.
enum class CheckpointChoice : uint8_t {
    Base = 1,
    Rollback = 2,
};

// Replay recovers why a handle was detached from this classifier alone.
// A reason tag defined outside the framework lands on Unknown, and the
// schema lane still identifies its exact type for offline audit.
enum class DetachReasonKind : uint8_t {
    Unknown = 0,
    InfiniteLoopProtocol = 1,
    TransportClosedOutOfBand = 2,
    TestInstrumentation = 3,
    AsyncCancellation = 4,
    OwnerLifetimeBoundEarlyExit = 5,
};

// Persistence events share the same record.  This names how the two
// payload lanes and the two control bytes read for those events, and
// adds no storage of its own.
struct CipherEventPayload {
    ::crucible::ContentHash content_hash{};
    uint64_t timestamp_ns = 0;
    uint8_t from_tier = 0;
    uint8_t to_tier = 0;
};

// The record is one fixed size for every operation kind, which is what
// lets a whole log drain to durable storage as a block of bytes.  Each
// kind therefore reinterprets the two general payload lanes and the two
// control bytes rather than growing the record.  The factories and
// accessors below spell out each reading, and no runtime dispatch or
// extra storage is involved.

struct SessionEvent {
    StepId step_id{};
    SessionTagId session{};
    RoleTagId from_role{};
    RoleTagId to_role{};
    SchemaHash payload_schema{};
    PayloadHash payload_hash{};
    uint64_t epoch_threshold = 0;  // zero for every non-epoched kind
    uint64_t generation_threshold = 0;  // zero for every non-epoched kind
    SessionOp op = SessionOp::Send;
    uint8_t branch_index = 0;  // chosen branch on a choice, else zero
    uint8_t reason_kind = 0;
    uint8_t pad[5]{};

    // A call site that knows the crash class at compile time passes it
    // through, because replay must be able to tell the tiers apart:
    // recovery that unwinds is only valid for some of them.
    [[nodiscard]] static constexpr SessionEvent stop(RoleTagId self, RoleTagId peer, RoleTagId stopped_peer,
                                                     StopReasonKind reason = StopReasonKind::PeerCrashed,
                                                     RecoveryPathHash recovery_path = {},
                                                     ::crucible::algebra::lattices::CrashClass crash_class =
                                                         ::crucible::algebra::lattices::CrashClass::Abort) noexcept {
        return SessionEvent{
            .from_role = self,
            .to_role = peer,
            .payload_schema = SchemaHash{stopped_peer.value},
            .payload_hash = PayloadHash{recovery_path.value},
            .op = SessionOp::Stop,
            .reason_kind = static_cast<uint8_t>(reason),
            .pad = {static_cast<uint8_t>(crash_class), 0, 0, 0, 0},
        };
    }

    [[nodiscard]] static constexpr SessionEvent checkpoint_base(RoleTagId self, RoleTagId peer, CheckpointId checkpoint,
                                                                ::crucible::ContentHash saved_state = {}) noexcept {
        return SessionEvent{
            .from_role = self,
            .to_role = peer,
            .payload_schema = SchemaHash{checkpoint.value},
            .payload_hash = PayloadHash{saved_state.raw()},
            .op = SessionOp::Checkpoint_Base,
            .reason_kind = static_cast<uint8_t>(CheckpointChoice::Base),
        };
    }

    [[nodiscard]] static constexpr SessionEvent checkpoint_rollback(RoleTagId self, RoleTagId peer,
                                                                    CheckpointId checkpoint,
                                                                    ::crucible::ContentHash saved_state = {}) noexcept {
        return SessionEvent{
            .from_role = self,
            .to_role = peer,
            .payload_schema = SchemaHash{checkpoint.value},
            .payload_hash = PayloadHash{saved_state.raw()},
            .op = SessionOp::Checkpoint_Rollback,
            .reason_kind = static_cast<uint8_t>(CheckpointChoice::Rollback),
        };
    }

    // Only a handle wrapped by a recording or persisting bridge emits
    // this event.  A plain handle records nothing and merely marks
    // itself consumed.  A caller that does not track role identity
    // passes zero for the roles, since the schema lane carries the
    // reason type, which is the datum an audit reads.
    [[nodiscard]] static constexpr SessionEvent detach(RoleTagId self, RoleTagId peer, DetachReasonKind reason_kind,
                                                       SchemaHash reason_schema = {}) noexcept {
        return SessionEvent{
            .from_role = self,
            .to_role = peer,
            .payload_schema = reason_schema,
            .op = SessionOp::Detach,
            .reason_kind = static_cast<uint8_t>(reason_kind),
        };
    }

    [[nodiscard]] static constexpr SessionEvent delegate_handoff(RoleTagId sender, RoleTagId recipient,
                                                                 ::crucible::ContentHash delegated_proto_hash,
                                                                 InnerPermSetHash inner_perm_set = {}) noexcept {
        return SessionEvent{
            .from_role = sender,
            .to_role = recipient,
            .payload_schema = SchemaHash{delegated_proto_hash.raw()},
            .payload_hash = PayloadHash{inner_perm_set.value},
            .op = SessionOp::Delegate,
        };
    }

    [[nodiscard]] static constexpr SessionEvent accept_handoff(RoleTagId recipient, RoleTagId sender,
                                                               ::crucible::ContentHash accepted_proto_hash,
                                                               InnerPermSetHash inner_perm_set = {}) noexcept {
        return SessionEvent{
            .from_role = sender,
            .to_role = recipient,
            .payload_schema = SchemaHash{accepted_proto_hash.raw()},
            .payload_hash = PayloadHash{inner_perm_set.value},
            .op = SessionOp::Accept,
        };
    }

    // The two reshard-guard thresholds get lanes of their own, so replay
    // can re-validate the handoff against the live epoch chain without
    // reading the source the handoff came from.
    [[nodiscard]] static constexpr SessionEvent
    epoched_delegate_handoff(RoleTagId sender, RoleTagId recipient, ::crucible::ContentHash delegated_proto_hash,
                             std::uint64_t min_epoch, std::uint64_t min_generation,
                             InnerPermSetHash inner_perm_set = {}) noexcept {
        return SessionEvent{
            .from_role = sender,
            .to_role = recipient,
            .payload_schema = SchemaHash{delegated_proto_hash.raw()},
            .payload_hash = PayloadHash{inner_perm_set.value},
            .epoch_threshold = min_epoch,
            .generation_threshold = min_generation,
            .op = SessionOp::EpochedDelegate,
        };
    }

    [[nodiscard]] static constexpr SessionEvent epoched_accept_handoff(RoleTagId recipient, RoleTagId sender,
                                                                       ::crucible::ContentHash accepted_proto_hash,
                                                                       std::uint64_t min_epoch,
                                                                       std::uint64_t min_generation,
                                                                       InnerPermSetHash inner_perm_set = {}) noexcept {
        return SessionEvent{
            .from_role = sender,
            .to_role = recipient,
            .payload_schema = SchemaHash{accepted_proto_hash.raw()},
            .payload_hash = PayloadHash{inner_perm_set.value},
            .epoch_threshold = min_epoch,
            .generation_threshold = min_generation,
            .op = SessionOp::EpochedAccept,
        };
    }

    [[nodiscard]] static constexpr SessionEvent cipher_event(SessionOp op, StepId step,
                                                             ::crucible::ContentHash content_hash,
                                                             uint64_t timestamp_ns, uint8_t from_tier = 0,
                                                             uint8_t to_tier = 0) noexcept {
        return SessionEvent{
            .step_id = step,
            .session = SessionTagId{0},
            .payload_schema = SchemaHash{timestamp_ns},
            .payload_hash = PayloadHash{content_hash.raw()},
            .op = op,
            .branch_index = from_tier,
            .reason_kind = to_tier,
        };
    }

    [[nodiscard]] static constexpr SessionEvent cipher_store_pending(StepId step, ::crucible::ContentHash content_hash,
                                                                     uint64_t timestamp_ns = 0) noexcept {
        return cipher_event(SessionOp::StorePending, step, content_hash, timestamp_ns);
    }

    [[nodiscard]] static constexpr SessionEvent
    cipher_store_committed(StepId step, ::crucible::ContentHash content_hash, uint64_t timestamp_ns = 0) noexcept {
        return cipher_event(SessionOp::StoreCommitted, step, content_hash, timestamp_ns);
    }

    [[nodiscard]] static constexpr SessionEvent cipher_load_from_tier(StepId step, ::crucible::ContentHash content_hash,
                                                                      uint8_t from_tier,
                                                                      uint64_t timestamp_ns = 0) noexcept {
        return cipher_event(SessionOp::LoadFromTier, step, content_hash, timestamp_ns, from_tier, from_tier);
    }

    [[nodiscard]] static constexpr SessionEvent cipher_tier_promote(StepId step, ::crucible::ContentHash content_hash,
                                                                    uint8_t from_tier, uint8_t to_tier,
                                                                    uint64_t timestamp_ns = 0) noexcept {
        return cipher_event(SessionOp::TierPromote, step, content_hash, timestamp_ns, from_tier, to_tier);
    }

    [[nodiscard]] static constexpr SessionEvent cipher_tier_demote(StepId step, ::crucible::ContentHash content_hash,
                                                                   uint8_t from_tier, uint8_t to_tier,
                                                                   uint64_t timestamp_ns = 0) noexcept {
        return cipher_event(SessionOp::TierDemote, step, content_hash, timestamp_ns, from_tier, to_tier);
    }

    [[nodiscard]] static constexpr SessionEvent cipher_tier_restore(StepId step, ::crucible::ContentHash content_hash,
                                                                    uint8_t from_tier, uint8_t to_tier,
                                                                    uint64_t timestamp_ns = 0) noexcept {
        return cipher_event(SessionOp::TierRestore, step, content_hash, timestamp_ns, from_tier, to_tier);
    }

    [[nodiscard]] constexpr RoleTagId stop_peer_tag() const noexcept { return RoleTagId{payload_schema.value}; }

    [[nodiscard]] constexpr StopReasonKind stop_reason_kind() const noexcept {
        return static_cast<StopReasonKind>(reason_kind);
    }

    [[nodiscard]] constexpr RecoveryPathHash stop_recovery_path_hash() const noexcept {
        return RecoveryPathHash{payload_hash.value};
    }

    // Meaningful only once the operation kind is known to be a crash.
    // On any other kind the byte is zero, which reads as the first tier.
    [[nodiscard]] constexpr ::crucible::algebra::lattices::CrashClass stop_crash_class() const noexcept {
        return static_cast<::crucible::algebra::lattices::CrashClass>(pad[0]);
    }

    [[nodiscard]] constexpr CheckpointId checkpoint_id() const noexcept { return CheckpointId{payload_schema.value}; }

    [[nodiscard]] constexpr CheckpointChoice checkpoint_choice() const noexcept {
        return static_cast<CheckpointChoice>(reason_kind);
    }

    [[nodiscard]] constexpr ::crucible::ContentHash checkpoint_saved_state_content_hash() const noexcept {
        return ::crucible::ContentHash::from_raw(payload_hash.value);
    }

    [[nodiscard]] constexpr ::crucible::ContentHash delegated_proto_hash() const noexcept {
        return ::crucible::ContentHash::from_raw(payload_schema.value);
    }

    [[nodiscard]] constexpr RoleTagId delegate_recipient_role_tag() const noexcept { return to_role; }

    [[nodiscard]] constexpr RoleTagId accept_sender_role_tag() const noexcept { return from_role; }

    [[nodiscard]] constexpr InnerPermSetHash inner_perm_set_hash() const noexcept {
        return InnerPermSetHash{payload_hash.value};
    }

    // Both read zero on any kind that carries no thresholds, so a caller
    // that cares about threshold meaning checks the operation kind first.
    [[nodiscard]] constexpr std::uint64_t epoched_min_epoch() const noexcept { return epoch_threshold; }

    [[nodiscard]] constexpr std::uint64_t epoched_min_generation() const noexcept { return generation_threshold; }

    [[nodiscard]] constexpr bool is_cipher_event() const noexcept { return session_op_is_cipher(op); }

    [[nodiscard]] constexpr bool commits_cipher_head() const noexcept { return session_op_commits_cipher_head(op); }

    [[nodiscard]] constexpr ::crucible::ContentHash cipher_content_hash() const noexcept {
        return ::crucible::ContentHash::from_raw(payload_hash.value);
    }

    [[nodiscard]] constexpr uint64_t cipher_timestamp_ns() const noexcept { return payload_schema.value; }

    [[nodiscard]] constexpr uint8_t cipher_from_tier() const noexcept { return branch_index; }

    [[nodiscard]] constexpr uint8_t cipher_to_tier() const noexcept { return reason_kind; }

    [[nodiscard]] constexpr CipherEventPayload cipher_payload() const noexcept {
        return CipherEventPayload{
            .content_hash = cipher_content_hash(),
            .timestamp_ns = cipher_timestamp_ns(),
            .from_tier = cipher_from_tier(),
            .to_tier = cipher_to_tier(),
        };
    }
};

static_assert(sizeof(SessionEvent) == 72,
              "SessionEvent must be exactly 72 bytes, because durable serialisation depends on the fixed record "
              "size.  A change to any field also needs a layout-version bump and a matching deserialiser.");
static_assert(std::is_trivially_copyable_v<SessionEvent>,
              "SessionEvent must be TriviallyCopyable for fast bulk drain.");

// The ordering check constructs these per call, so both must be
// stateless.

struct StepIdKeyFn {
    constexpr StepId operator()(const SessionEvent& e) const noexcept { return e.step_id; }
};

struct StepIdLess {
    constexpr bool operator()(StepId a, StepId b) const noexcept { return a.value < b.value; }
};

// The hash is taken over the compiler's own spelling of the type, which
// is stable for one type on one target and identical across translation
// units.  Stability across targets is not promised, because the
// contract is replay determinism inside a single build.

namespace detail {

[[nodiscard]] inline consteval uint64_t fnv1a_64(std::string_view s) noexcept {
    constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
    constexpr uint64_t kFnvPrime = 0x100000001b3ULL;
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
inline constexpr SchemaHash default_schema_hash{detail::fnv1a_64(detail::pretty_function_for<T>())};

template <typename T>
inline constexpr ::crucible::ContentHash default_proto_hash =
    ::crucible::ContentHash::from_raw(default_schema_hash<T>.value);

// Payload hashing is opt in: the default returns the not-hashed
// sentinel and does no work.  A caller that wants replay audit over
// payload values specialises this for its own payload type and pays
// whatever its hasher costs.

template <typename T>
inline constexpr auto default_payload_hash_fn = [](const T& /*v*/) noexcept -> PayloadHash { return PayloadHash{0}; };

// The atomic step counter is the log's ordering identity.  Moving the
// log would fork that counter across two objects and break the
// monotonic-step invariant consumers depend on.

class [[nodiscard]] SessionEventLog : Pinned<SessionEventLog> {
    OrderedAppendOnly<SessionEvent, StepIdKeyFn, StepIdLess> log_{};
    SessionTagId session_id_{};
    AtomicMonotonic<uint64_t> step_counter_{0};

public:
    using event_type = SessionEvent;
    using storage_type = std::vector<SessionEvent>;

    // The default identifier suits a single-session log, where the field
    // carries no information.
    constexpr explicit SessionEventLog(SessionTagId id = {}) noexcept : session_id_{id} {}

    // Safe to call from several recording threads at once.
    [[nodiscard]] StepId next_step() noexcept {
        // The counter offers no fetch-and-bump operation, only a
        // conditional advance, so the read and the advance are composed
        // here into a retry loop.
        for (;;) {
            const uint64_t prev = step_counter_.get();
            if (prev == std::numeric_limits<uint64_t>::max()) [[unlikely]] {
                std::abort();
            }
            const uint64_t next = prev + 1;
            if (step_counter_.try_advance(next)) {
                return StepId{next};
            }
            // Another thread advanced past this value, so retry from
            // the new one.
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    // The step identifier must not go backwards against the last
    // appended event.  A caller that controls ordering itself, such as
    // one replaying from a snapshot, supplies its own.
    void record(SessionEvent ev) { log_.append(std::move(ev)); }

    void record_now(SessionEvent ev) {
        ev.step_id = next_step();
        ev.session = session_id_;
        log_.append(std::move(ev));
    }

    void append_event(SessionEvent ev) { record_now(std::move(ev)); }

    struct ReplayRange {
        using const_iterator = SessionEventLog::storage_type::const_iterator;

        const_iterator first{};
        const_iterator last{};

        [[nodiscard]] const_iterator begin() const noexcept { return first; }
        [[nodiscard]] const_iterator end() const noexcept { return last; }
    };

    [[nodiscard]] ReplayRange replay_iter() const noexcept { return ReplayRange{log_.begin(), log_.end()}; }

    [[nodiscard]] SessionTagId session() const noexcept { return session_id_; }
    [[nodiscard]] std::size_t size() const noexcept { return log_.size(); }
    [[nodiscard]] bool empty() const noexcept { return log_.empty(); }

    [[nodiscard]] const SessionEvent& operator[](std::size_t i) const noexcept { return log_[i]; }
    [[nodiscard]] const SessionEvent& front() const noexcept { return log_.front(); }
    [[nodiscard]] const SessionEvent& back() const noexcept { return log_.back(); }

    [[nodiscard]] auto begin() const noexcept { return log_.begin(); }
    [[nodiscard]] auto end() const noexcept { return log_.end(); }

    // The log itself cannot move, but its backing storage can, so a
    // drain is how an ended session ships its events to durable storage.
    [[nodiscard]] storage_type drain() && noexcept(std::is_nothrow_move_constructible_v<storage_type>) {
        return std::move(log_).drain();
    }
};

}  // namespace crucible::safety::proto
