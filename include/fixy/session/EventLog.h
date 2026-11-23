#pragma once

// A typed, append-only record of the operations a session did, in the
// order it did them, so that a run can be replayed step for step.
//
// ── The record ──────────────────────────────────────────────────────
//
// Each event is 72 bytes, so a whole log drains to durable storage as
// one block of bytes.  The layout and the byte values of each lane are
// fixed, so a stored log decodes unchanged.  Each kind of operation reads
// the two general lanes and the two control bytes in its own way.  The
// factories write each reading, and the accessors read it back.
//
// The fields are private.  A SessionEvent exists only through a factory
// or through decode_session_event, and both produce only valid events:
// a factory checks each enum argument against the enumerators, and the
// decoder checks each byte.  So every accessor is total: it never reads
// a byte that is not a value of its enum.
//
// ── Bytes from outside ──────────────────────────────────────────────
//
// decode_session_event is the only path from bytes to an event.  It
// refuses a short span, an operation byte that names no operation, a
// control byte outside the enum that the operation reads it as, a crash
// cause outside CrashCause, a threshold on a kind that carries none, and
// a padding byte that is not zero.  The allowed values come from the
// enumerators of each enum, found by reflection, so a new enumerator
// needs no second edit here.
//
// Without these checks, a corrupt or hostile log gives an enum value
// that no enumerator names, and replay dispatches on it.
//
// ── The lanes of a stored log ───────────────────────────────────────
//
//   * The crash lane holds a CrashCause (fixy/session/CrashCause.h).  A stored
//     log can hold the value 3 there, a crash graded "no throw", which is
//     a contradiction.  It decodes as CrashCause::Unknown.
//   * The checkpoint kinds Checkpoint_Base and Checkpoint_Rollback record
//     a choice that each endpoint made alone.  They decode, so a stored
//     log reads, but no factory writes them.  The coordinated primitives
//     of fixy/session/Checkpoint.h have their own kinds: CheckpointCommit,
//     CheckpointRoll and CheckpointAbort.
//   * The hand-off kinds EpochedDelegate and EpochedAccept carry an epoch
//     threshold and a generation threshold.  They decode with the two
//     thresholds, but no factory writes them.
//   * A send or a select to a crashed peer is recorded with the fate
//     LostToCrashedPeer in the control byte, so replay can tell a
//     message the peer got from one it never got.
//   * The schema hash is the stable type id of foundation/reflect/
//     Hash.h, of the spelling of the payload with each brand erased.  The
//     recorder of fixy/session/Recording.h gives it (default_schema_hash).
//   * A Select or an Offer of a keyed choice records the label word of
//     the branch in the schema lane, beside the index of the branch.  A
//     replay against a keyed choice needs the label word
//     (replayed_branch in fixy/session/Recording.h).
//
// ── One writer ──────────────────────────────────────────────────────
//
// A log has one writer: the SessionEventWriter that
// mint_session_event_writer gives.  A second mint for one log ends the
// process with Event_Log_Writer_Twice, so no two recorders append to one
// log from two threads.  The writer is move-only, and a recorder moves it
// from each handle to the next, so one handle at a time appends.  Each
// endpoint of a run records into its own log.
//
// merge_session_logs reads the logs of a run as one sequence: by step id,
// and for one step id in the order of the logs in its arguments.  The
// sequence depends on no schedule of the threads.  An append is a plain
// vector push, so a reader reads a log after its writer ends, or on the
// thread of its writer.

#include <fixy/Mutation.h>
#include <fixy/session/CrashCause.h>
#include <fixy/session/StepId.h>

#include <foundation/Pinned.h>
#include <foundation/algebra/lattices/CipherTierLattice.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Id.h>
#include <foundation/core/Region.h>
#include <foundation/core/Side.h>
#include <foundation/reflect/EnumName.h>
#include <foundation/reflect/Hash.h>

#include <array>
#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace fixy::session {

// ── Identifiers ─────────────────────────────────────────────────────
//
// Each identifier is a distinct type over a plain integer, which stops a
// caller from swapping two of them silently.

struct SessionTagId {
    std::uint64_t value = 0;
    constexpr auto operator<=>(const SessionTagId&) const noexcept = default;
};
struct RoleTagId {
    std::uint64_t value = 0;
    constexpr auto operator<=>(const RoleTagId&) const noexcept = default;
};
// The hash of a payload type.
struct SchemaHash {
    std::uint64_t value = 0;
    constexpr auto operator<=>(const SchemaHash&) const noexcept = default;
};
// The hash of a payload value.  Zero means the value was not hashed.
struct PayloadHash {
    std::uint64_t value = 0;
    constexpr auto operator<=>(const PayloadHash&) const noexcept = default;
};
// The hash of the recovery path taken after a crash.  Zero means none.
struct RecoveryPathHash {
    std::uint64_t value = 0;
    constexpr auto operator<=>(const RecoveryPathHash&) const noexcept = default;
};
// The hash of a protocol or of a saved state.
struct StateHash {
    std::uint64_t value = 0;
    constexpr auto operator<=>(const StateHash&) const noexcept = default;
};
// The hash of the permission set that a delegated session moves.
struct InnerPermSetHash {
    std::uint64_t value = 0;
    constexpr auto operator<=>(const InnerPermSetHash&) const noexcept = default;
};
// The label word of the branch that a Select or an Offer of a keyed
// choice took (fixy/session/Wire.h).  Zero in a positional
// choice, and for a branch that is no label.
struct LabelWord {
    std::uint64_t value = 0;
    constexpr auto operator<=>(const LabelWord&) const noexcept = default;
};
// The step of an event is StepId of fixy/session/StepId.h.

// ── The control vocabulary ──────────────────────────────────────────

enum class SessionOp : std::uint8_t {
    Send = 1,
    Recv = 2,
    Select = 3,
    Offer = 4,
    Close = 5,
    Detach = 6,
    Stop = 7,
    Checkpoint_Base = 8,
    Checkpoint_Rollback = 9,
    Delegate = 10,
    Accept = 11,
    StorePending = 12,
    StoreCommitted = 13,
    LoadFromTier = 14,
    TierPromote = 15,
    TierDemote = 16,
    TierRestore = 17,
    EpochedDelegate = 18,
    EpochedAccept = 19,
    CheckpointCommit = 20,
    CheckpointRoll = 21,
    CheckpointAbort = 22,
};

enum class StopReasonKind : std::uint8_t {
    Unknown = 0,
    PeerCrashed = 1,
    LocalAbort = 2,
    Recovery = 3,
};

// The reading of the control byte for the two legacy checkpoint kinds.
enum class CheckpointChoice : std::uint8_t {
    Base = 1,
    Rollback = 2,
};

enum class DetachReasonKind : std::uint8_t {
    Unknown = 0,
    InfiniteLoopProtocol = 1,
    TransportClosedOutOfBand = 2,
    TestInstrumentation = 3,
    AsyncCancellation = 4,
    OwnerLifetimeBoundEarlyExit = 5,
};

// What happened to a message this endpoint sent or selected.
enum class DeliveryFate : std::uint8_t {
    Delivered = 0,
    LostToCrashedPeer = 1,
};

// Which side of a coordinated checkpoint label this endpoint was.
enum class CheckpointRole : std::uint8_t {
    Active = 0,
    Passive = 1,
};

using ::foundation::algebra::lattices::CipherTierTag;

// The place of a persistence event in the order of the events of its store:
// the next value of a count that the store keeps.  The store reads no clock
// for it, so two runs of one computation give the same places.
using CipherEventOrder = ::foundation::core::Id<struct CipherEventOrderTag, std::uint64_t>;

// The two roles of a tier in a persistence event.
namespace side {
// The tier that the event takes the content from.
struct SourceTier final {};
// The tier that the event puts the content in.
struct TargetTier final {};
}  // namespace side

using SourceTierOf = ::foundation::core::Side<side::SourceTier, CipherTierTag>;
using TargetTierOf = ::foundation::core::Side<side::TargetTier, CipherTierTag>;

namespace detail::event_log {

// The byte values that name an enumerator of E, found by reflection.
template <typename E>
inline constexpr std::array<bool, 256> enumerator_bytes = [] {
    std::array<bool, 256> table{};
    ::foundation::core::for_each_enumerator_text<E>(
        [&table](E value, ::foundation::core::TextView) noexcept { table[static_cast<std::uint8_t>(value)] = true; });
    return table;
}();

template <typename E>
[[nodiscard]] constexpr bool names_enumerator(std::uint8_t byte) noexcept {
    return enumerator_bytes<E>[byte];
}

}  // namespace detail::event_log

[[nodiscard]] constexpr ::foundation::core::TextView session_op_name(SessionOp op) noexcept {
    return ::foundation::core::enum_name_text(op);
}

[[nodiscard]] constexpr bool session_op_is_cipher(SessionOp op) noexcept {
    return op == SessionOp::StorePending || op == SessionOp::StoreCommitted || op == SessionOp::LoadFromTier
        || op == SessionOp::TierPromote || op == SessionOp::TierDemote || op == SessionOp::TierRestore;
}

[[nodiscard]] constexpr bool session_op_commits_cipher_head(SessionOp op) noexcept {
    return op == SessionOp::StoreCommitted || op == SessionOp::TierPromote || op == SessionOp::TierDemote
        || op == SessionOp::TierRestore;
}

[[nodiscard]] constexpr bool session_op_is_epoched(SessionOp op) noexcept {
    return op == SessionOp::EpochedDelegate || op == SessionOp::EpochedAccept;
}

// ── Decoding ────────────────────────────────────────────────────────

enum class EventDecodeError : std::uint8_t {
    Truncated,
    UnknownOperation,
    ControlByteOutOfRange,
    BranchByteOutOfRange,
    CrashCauseOutOfRange,
    ThresholdOnPlainKind,
    NonZeroPadding,
};

inline constexpr std::size_t session_event_size = 72;

class SessionEvent;

[[nodiscard]] constexpr std::expected<SessionEvent, EventDecodeError>
decode_session_event(::foundation::core::View<std::byte const> bytes) noexcept;

namespace detail::event_log {

// The record as bytes, with every control byte as a plain integer, so
// that reading a corrupt record creates no invalid enum value.
struct RawEvent {
    std::uint64_t step_id = 0;
    std::uint64_t session = 0;
    std::uint64_t from_role = 0;
    std::uint64_t to_role = 0;
    std::uint64_t payload_schema = 0;
    std::uint64_t payload_hash = 0;
    std::uint64_t epoch_threshold = 0;
    std::uint64_t generation_threshold = 0;
    std::uint8_t op = 0;
    std::uint8_t branch_index = 0;
    std::uint8_t reason_kind = 0;
    std::array<std::uint8_t, 5> pad{};
};

}  // namespace detail::event_log

// ── The event ───────────────────────────────────────────────────────

class SessionEvent {
    StepId step_id_{std::uint64_t{0}};
    SessionTagId session_{};
    RoleTagId from_role_{};
    RoleTagId to_role_{};
    SchemaHash payload_schema_{};
    PayloadHash payload_hash_{};
    std::uint64_t epoch_threshold_ = 0;
    std::uint64_t generation_threshold_ = 0;
    SessionOp op_ = SessionOp::Send;
    std::uint8_t branch_index_ = 0;
    std::uint8_t reason_kind_ = 0;
    std::array<std::uint8_t, 5> pad_{};

    // User-provided, so that no constructor of the event is trivial.  A
    // trivial constructor makes an implicit-lifetime type, and
    // std::start_lifetime_as then builds an event over a buffer.
    constexpr SessionEvent() noexcept {}

    constexpr SessionEvent(SessionOp op, RoleTagId from, RoleTagId to, std::uint64_t schema, std::uint64_t payload,
                           std::uint8_t branch = 0, std::uint8_t reason = 0) noexcept
        : from_role_{from},
          to_role_{to},
          payload_schema_{schema},
          payload_hash_{payload},
          op_{op},
          branch_index_{branch},
          reason_kind_{reason} {}

    friend class SessionEventLog;
    friend constexpr std::expected<SessionEvent, EventDecodeError>
    decode_session_event(::foundation::core::View<std::byte const> bytes) noexcept;

public:
    // User-provided, and not defaulted.  A defaulted copy is trivial, so
    // the event was trivially copyable, and std::bit_cast built one from
    // bytes that no session step wrote.  The one way from bytes to an
    // event is decode_session_event, which checks each byte.  The copy
    // compiles to the same 72-byte move.
    constexpr SessionEvent(const SessionEvent& other) noexcept { *this = other; }
    constexpr SessionEvent& operator=(const SessionEvent&) noexcept = default;
    constexpr ~SessionEvent() = default;

    // ── Factories ───────────────────────────────────────────────────

    [[nodiscard]] static constexpr SessionEvent send(RoleTagId self, RoleTagId peer, SchemaHash schema,
                                                     PayloadHash payload = {},
                                                     DeliveryFate fate = DeliveryFate::Delivered) noexcept {
        CRUCIBLE_PRE(detail::event_log::names_enumerator<DeliveryFate>(static_cast<std::uint8_t>(fate)));
        return SessionEvent{
            SessionOp::Send, self, peer, schema.value, payload.value, 0, static_cast<std::uint8_t>(fate)};
    }

    [[nodiscard]] static constexpr SessionEvent recv(RoleTagId self, RoleTagId peer, SchemaHash schema,
                                                     PayloadHash payload = {}) noexcept {
        return SessionEvent{SessionOp::Recv, peer, self, schema.value, payload.value};
    }

    // The label word goes to the schema lane.  It is zero in a positional
    // choice.
    [[nodiscard]] static constexpr SessionEvent select(RoleTagId self, RoleTagId peer, std::uint8_t branch,
                                                       DeliveryFate fate = DeliveryFate::Delivered,
                                                       LabelWord label = {}) noexcept {
        CRUCIBLE_PRE(detail::event_log::names_enumerator<DeliveryFate>(static_cast<std::uint8_t>(fate)));
        return SessionEvent{SessionOp::Select, self, peer, label.value, 0, branch, static_cast<std::uint8_t>(fate)};
    }

    [[nodiscard]] static constexpr SessionEvent offer(RoleTagId self, RoleTagId peer, std::uint8_t branch,
                                                      LabelWord label = {}) noexcept {
        return SessionEvent{SessionOp::Offer, peer, self, label.value, 0, branch};
    }

    [[nodiscard]] static constexpr SessionEvent close(RoleTagId self, RoleTagId peer) noexcept {
        return SessionEvent{SessionOp::Close, self, peer, 0, 0};
    }

    [[nodiscard]] static constexpr SessionEvent detach(RoleTagId self, RoleTagId peer, DetachReasonKind reason,
                                                       SchemaHash reason_schema = {}) noexcept {
        CRUCIBLE_PRE(detail::event_log::names_enumerator<DetachReasonKind>(static_cast<std::uint8_t>(reason)));
        return SessionEvent{
            SessionOp::Detach, self, peer, reason_schema.value, 0, 0, static_cast<std::uint8_t>(reason)};
    }

    [[nodiscard]] static constexpr SessionEvent stop(RoleTagId self, RoleTagId peer, RoleTagId stopped,
                                                     StopReasonKind reason, CrashCause cause,
                                                     RecoveryPathHash recovery_path = {}) noexcept {
        CRUCIBLE_PRE(detail::event_log::names_enumerator<StopReasonKind>(static_cast<std::uint8_t>(reason)));
        CRUCIBLE_PRE(detail::event_log::names_enumerator<CrashCause>(static_cast<std::uint8_t>(cause)));
        SessionEvent event{
            SessionOp::Stop, self, peer, stopped.value, recovery_path.value, 0, static_cast<std::uint8_t>(reason)};
        event.pad_[0] = static_cast<std::uint8_t>(cause);
        return event;
    }

    // One event per endpoint for each coordinated checkpoint label.  The
    // role says whether this endpoint selected the label or received it.
    [[nodiscard]] static constexpr SessionEvent checkpoint_commit(RoleTagId self, RoleTagId peer, CheckpointRole role,
                                                                  StateHash saved_state = {}) noexcept {
        CRUCIBLE_PRE(detail::event_log::names_enumerator<CheckpointRole>(static_cast<std::uint8_t>(role)));
        return SessionEvent{SessionOp::CheckpointCommit,    self, peer, 0, saved_state.value, 0,
                            static_cast<std::uint8_t>(role)};
    }

    [[nodiscard]] static constexpr SessionEvent checkpoint_roll(RoleTagId self, RoleTagId peer,
                                                                CheckpointRole role) noexcept {
        CRUCIBLE_PRE(detail::event_log::names_enumerator<CheckpointRole>(static_cast<std::uint8_t>(role)));
        return SessionEvent{SessionOp::CheckpointRoll, self, peer, 0, 0, 0, static_cast<std::uint8_t>(role)};
    }

    [[nodiscard]] static constexpr SessionEvent checkpoint_abort(RoleTagId self, RoleTagId peer,
                                                                 CheckpointRole role) noexcept {
        CRUCIBLE_PRE(detail::event_log::names_enumerator<CheckpointRole>(static_cast<std::uint8_t>(role)));
        return SessionEvent{SessionOp::CheckpointAbort, self, peer, 0, 0, 0, static_cast<std::uint8_t>(role)};
    }

    [[nodiscard]] static constexpr SessionEvent delegate_handoff(RoleTagId sender, RoleTagId recipient,
                                                                 StateHash delegated_proto,
                                                                 InnerPermSetHash inner_perm_set = {}) noexcept {
        return SessionEvent{SessionOp::Delegate, sender, recipient, delegated_proto.value, inner_perm_set.value};
    }

    [[nodiscard]] static constexpr SessionEvent accept_handoff(RoleTagId recipient, RoleTagId sender,
                                                               StateHash accepted_proto,
                                                               InnerPermSetHash inner_perm_set = {}) noexcept {
        return SessionEvent{SessionOp::Accept, sender, recipient, accepted_proto.value, inner_perm_set.value};
    }

    // A persistence event.  The lanes hold the place of the event in the
    // order of the events of its store and the content hash, and the two
    // control bytes hold the source and target tiers.
    [[nodiscard]] static constexpr SessionEvent
    cipher_event(SessionOp op, StepId step, StateHash content, CipherEventOrder order,
                 SourceTierOf from_tier = SourceTierOf{CipherTierTag::Cold},
                 TargetTierOf to_tier = TargetTierOf{CipherTierTag::Cold}) noexcept {
        CRUCIBLE_PRE(session_op_is_cipher(op));
        CRUCIBLE_PRE(detail::event_log::names_enumerator<CipherTierTag>(static_cast<std::uint8_t>(from_tier.value())));
        CRUCIBLE_PRE(detail::event_log::names_enumerator<CipherTierTag>(static_cast<std::uint8_t>(to_tier.value())));
        SessionEvent event{op,
                           RoleTagId{},
                           RoleTagId{},
                           order.raw(),
                           content.value,
                           static_cast<std::uint8_t>(from_tier.value()),
                           static_cast<std::uint8_t>(to_tier.value())};
        event.step_id_ = step;
        return event;
    }

    // ── Accessors ───────────────────────────────────────────────────

    [[nodiscard]] constexpr StepId step_id() const noexcept { return step_id_; }
    [[nodiscard]] constexpr SessionTagId session() const noexcept { return session_; }
    [[nodiscard]] constexpr SessionOp op() const noexcept { return op_; }
    [[nodiscard]] constexpr RoleTagId from_role() const noexcept { return from_role_; }
    [[nodiscard]] constexpr RoleTagId to_role() const noexcept { return to_role_; }
    [[nodiscard]] constexpr SchemaHash payload_schema() const noexcept { return payload_schema_; }
    [[nodiscard]] constexpr PayloadHash payload_hash() const noexcept { return payload_hash_; }
    [[nodiscard]] constexpr std::uint8_t branch_index() const noexcept { return branch_index_; }

    // The accessors below read a lane in the reading of one kind.  On an
    // event of another kind the answer carries no meaning, so a caller
    // checks op() first.  Every answer is a value of its enum.

    [[nodiscard]] constexpr DeliveryFate delivery_fate() const noexcept {
        return static_cast<DeliveryFate>(reason_kind_);
    }
    [[nodiscard]] constexpr LabelWord label_word() const noexcept { return LabelWord{payload_schema_.value}; }
    [[nodiscard]] constexpr RoleTagId stopped_role() const noexcept { return RoleTagId{payload_schema_.value}; }
    [[nodiscard]] constexpr StopReasonKind stop_reason() const noexcept {
        return static_cast<StopReasonKind>(reason_kind_);
    }
    [[nodiscard]] constexpr CrashCause crash_cause() const noexcept { return static_cast<CrashCause>(pad_[0]); }
    [[nodiscard]] constexpr RecoveryPathHash recovery_path() const noexcept {
        return RecoveryPathHash{payload_hash_.value};
    }
    [[nodiscard]] constexpr DetachReasonKind detach_reason() const noexcept {
        return static_cast<DetachReasonKind>(reason_kind_);
    }
    [[nodiscard]] constexpr CheckpointRole checkpoint_role() const noexcept {
        return static_cast<CheckpointRole>(reason_kind_);
    }
    [[nodiscard]] constexpr CheckpointChoice legacy_checkpoint_choice() const noexcept {
        return static_cast<CheckpointChoice>(reason_kind_);
    }
    [[nodiscard]] constexpr StateHash saved_state() const noexcept { return StateHash{payload_hash_.value}; }
    [[nodiscard]] constexpr StateHash delegated_proto() const noexcept { return StateHash{payload_schema_.value}; }
    [[nodiscard]] constexpr InnerPermSetHash inner_perm_set() const noexcept {
        return InnerPermSetHash{payload_hash_.value};
    }
    [[nodiscard]] constexpr std::uint64_t min_epoch() const noexcept { return epoch_threshold_; }
    [[nodiscard]] constexpr std::uint64_t min_generation() const noexcept { return generation_threshold_; }
    [[nodiscard]] constexpr StateHash cipher_content() const noexcept { return StateHash{payload_hash_.value}; }
    [[nodiscard]] constexpr CipherEventOrder cipher_event_order() const noexcept {
        return CipherEventOrder{payload_schema_.value};
    }
    [[nodiscard]] constexpr CipherTierTag cipher_from_tier() const noexcept {
        return static_cast<CipherTierTag>(branch_index_);
    }
    [[nodiscard]] constexpr CipherTierTag cipher_to_tier() const noexcept {
        return static_cast<CipherTierTag>(reason_kind_);
    }

    // The bytes, for a drain to durable storage.
    [[nodiscard]] constexpr ::foundation::core::FixedArray<std::byte, session_event_size> encode() const noexcept {
        detail::event_log::RawEvent raw{};
        raw.step_id = step_id_.raw();
        raw.session = session_.value;
        raw.from_role = from_role_.value;
        raw.to_role = to_role_.value;
        raw.payload_schema = payload_schema_.value;
        raw.payload_hash = payload_hash_.value;
        raw.epoch_threshold = epoch_threshold_;
        raw.generation_threshold = generation_threshold_;
        raw.op = static_cast<std::uint8_t>(op_);
        raw.branch_index = branch_index_;
        raw.reason_kind = reason_kind_;
        raw.pad = pad_;
        return std::bit_cast<::foundation::core::FixedArray<std::byte, session_event_size>>(raw);
    }
};

namespace detail::event_log {

// The enum the control byte of each kind is read as, and whether the
// branch byte and the crash lane carry a value.
[[nodiscard]] constexpr bool control_byte_valid(SessionOp op, std::uint8_t byte) noexcept {
    switch (op) {
        case SessionOp::Send:
        case SessionOp::Select:
            return names_enumerator<DeliveryFate>(byte);
        case SessionOp::Stop:
            return names_enumerator<StopReasonKind>(byte);
        case SessionOp::Detach:
            return names_enumerator<DetachReasonKind>(byte);
        case SessionOp::Checkpoint_Base:
            return byte == static_cast<std::uint8_t>(CheckpointChoice::Base);
        case SessionOp::Checkpoint_Rollback:
            return byte == static_cast<std::uint8_t>(CheckpointChoice::Rollback);
        case SessionOp::CheckpointCommit:
        case SessionOp::CheckpointRoll:
        case SessionOp::CheckpointAbort:
            return names_enumerator<CheckpointRole>(byte);
        case SessionOp::StorePending:
        case SessionOp::StoreCommitted:
        case SessionOp::LoadFromTier:
        case SessionOp::TierPromote:
        case SessionOp::TierDemote:
        case SessionOp::TierRestore:
            return names_enumerator<CipherTierTag>(byte);
        case SessionOp::Recv:
        case SessionOp::Offer:
        case SessionOp::Close:
        case SessionOp::Delegate:
        case SessionOp::Accept:
        case SessionOp::EpochedDelegate:
        case SessionOp::EpochedAccept:
        default:
            return byte == 0;
    }
}

[[nodiscard]] constexpr bool branch_byte_valid(SessionOp op, std::uint8_t byte) noexcept {
    if (op == SessionOp::Select || op == SessionOp::Offer) return true;
    if (session_op_is_cipher(op)) return names_enumerator<CipherTierTag>(byte);
    return byte == 0;
}

}  // namespace detail::event_log

// The event of the first session_event_size bytes.  Complexity: constant.
[[nodiscard]] constexpr std::expected<SessionEvent, EventDecodeError>
decode_session_event(::foundation::core::View<std::byte const> bytes) noexcept {
    using detail::event_log::names_enumerator;
    if (bytes.size() < session_event_size) return std::unexpected(EventDecodeError::Truncated);
    auto const record =
        bytes.template window<session_event_size>(0).expect("the test above found the bytes of one record");
    ::foundation::core::FixedArray<std::byte, session_event_size> block{};
    block.with_mut([record](auto run) noexcept {
        for (auto const index : record.indices()) {
            auto byte = run.get_mut(index.raw()).expect("an index of the record is inside the block");
            *byte = record.at(index);
        }
    });
    const auto raw = std::bit_cast<detail::event_log::RawEvent>(block);

    if (!names_enumerator<SessionOp>(raw.op)) return std::unexpected(EventDecodeError::UnknownOperation);
    const auto op = static_cast<SessionOp>(raw.op);
    if (!detail::event_log::control_byte_valid(op, raw.reason_kind)) {
        return std::unexpected(EventDecodeError::ControlByteOutOfRange);
    }
    if (!detail::event_log::branch_byte_valid(op, raw.branch_index)) {
        return std::unexpected(EventDecodeError::BranchByteOutOfRange);
    }
    if (op == SessionOp::Stop) {
        // A stored log can hold 3 for a crash graded "no throw".  The byte
        // value is CrashCause::Unknown, so the log reads.
        if (!names_enumerator<CrashCause>(raw.pad[0])) return std::unexpected(EventDecodeError::CrashCauseOutOfRange);
    } else if (raw.pad[0] != 0) {
        return std::unexpected(EventDecodeError::NonZeroPadding);
    }
    for (std::size_t index = 1; index < std::size(raw.pad); ++index) {
        if (raw.pad[index] != 0) return std::unexpected(EventDecodeError::NonZeroPadding);
    }
    if (!session_op_is_epoched(op) && (raw.epoch_threshold != 0 || raw.generation_threshold != 0)) {
        return std::unexpected(EventDecodeError::ThresholdOnPlainKind);
    }

    SessionEvent event{op,
                       RoleTagId{raw.from_role},
                       RoleTagId{raw.to_role},
                       raw.payload_schema,
                       raw.payload_hash,
                       raw.branch_index,
                       raw.reason_kind};
    event.step_id_ = StepId{raw.step_id};
    event.session_ = SessionTagId{raw.session};
    event.epoch_threshold_ = raw.epoch_threshold;
    event.generation_threshold_ = raw.generation_threshold;
    event.pad_[0] = raw.pad[0];
    return event;
}

// The decode of a whole block.  It stops at the first bad record and
// names its index, so a corrupt log never replays a prefix that looks
// complete.
struct EventLogDecodeFailure {
    std::size_t index = 0;
    EventDecodeError error = EventDecodeError::Truncated;
};

// The events take one allocation for the count of records.  Complexity:
// linear in the number of records.
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::HeapArray<SessionEvent>, EventLogDecodeFailure>
decode_session_log(::foundation::core::Alloc allocation, ::foundation::core::View<std::byte const> bytes) {
    if (bytes.size() % session_event_size != 0) {
        return ::foundation::core::err(
            EventLogDecodeFailure{bytes.size() / session_event_size, EventDecodeError::Truncated});
    }
    ::foundation::core::HeapArray<SessionEvent> events = ::foundation::core::mint_heap_array(
        allocation, bytes.size() / session_event_size, SessionEvent::close(RoleTagId{}, RoleTagId{}));
    std::size_t index = 0;
    for (::foundation::core::View<std::byte const, session_event_size> const record :
         bytes.chunks<session_event_size>()) {
        auto decoded = decode_session_event(record);
        if (!decoded) return ::foundation::core::err(EventLogDecodeFailure{index, decoded.error()});
        const SessionEvent& value = *decoded;
        events.with_element(
            index, [&value](SessionEvent& event) noexcept { event = value; },
            [] noexcept { ::foundation::core::fatal("the events hold a slot for each record"); });
        ++index;
    }
    return events;
}

// ── The log ─────────────────────────────────────────────────────────

// The key that orders a log of events: the step of each event, and the
// order of the steps.  A store that keeps its own log of events orders
// it with the same key, so both logs agree on what a step order is.
struct StepIdKeyFn {
    constexpr StepId operator()(const SessionEvent& event) const noexcept { return event.step_id(); }
};

struct StepIdLess {
    constexpr bool operator()(StepId lhs, StepId rhs) const noexcept { return lhs < rhs; }
};

namespace detail::event_log {

[[noreturn]] [[gnu::cold, gnu::noinline]] inline void abort_on_step_overflow() noexcept {
    std::fprintf(stderr, "fixy::session: the session event log minted its last step id.  A log holds at most "
                         "2^64 - 2 steps.  Start a new log.\n");
    std::abort();
}

[[noreturn]] [[gnu::cold, gnu::noinline]] inline void abort_on_second_writer() noexcept {
    ::foundation::core::fatal(
        "fixy::session: diagnostic [Event_Log_Writer_Twice]: a second writer was minted for a session event log "
        "that already has one.  Two writers append from two threads with no order.  Give each endpoint a log of "
        "its own, and read the logs as one sequence with merge_session_logs.");
}

}  // namespace detail::event_log

class SessionEventWriter;

// The step counter is the ordering identity of the log, so the log does
// not move.  A drain moves the events out instead.  Only the one writer of
// the log appends (One writer, in the head of this header).
class [[nodiscard]] SessionEventLog : ::foundation::Pinned<SessionEventLog> {
    ::fixy::OrderedAppendOnly<SessionEvent, StepIdKeyFn, StepIdLess> log_ =
        ::fixy::mint_ordered_append_only<SessionEvent, StepIdKeyFn, StepIdLess>();
    SessionTagId session_id_{};
    ::fixy::AtomicMonotonic<std::uint64_t> step_counter_ = ::fixy::mint_atomic_monotonic<std::uint64_t>(0);
    // Set when the one writer of this log is minted.
    ::foundation::core::Atomic<bool> has_writer_{false};

    friend class SessionEventWriter;

    [[nodiscard]] StepId next_step_() noexcept {
        const std::uint64_t prior = step_counter_.bump();
        // The highest value of the word is the empty value of an Option of a
        // step, so the last step of a log is the value below it.
        if (prior >= std::numeric_limits<std::uint64_t>::max() - 1) [[unlikely]]
            detail::event_log::abort_on_step_overflow();
        return StepId{prior + 1};
    }

    void record_now_(SessionEvent event) {
        event.step_id_ = next_step_();
        event.session_ = session_id_;
        log_.append(event);
    }

    void record_(SessionEvent event) { log_.append(event); }

public:
    using event_type = SessionEvent;
    using storage_type = std::vector<SessionEvent>;

    explicit SessionEventLog(SessionTagId id = {}) noexcept : session_id_{id} {}

    // The deletion states its reason, so a copy gives one error.  An
    // implicit deletion would also list each member that cannot copy.
    SessionEventLog(const SessionEventLog&) =
        delete("the step counter is the ordering identity of a session event log, and a copy would fork the counter");

    [[nodiscard]] SessionTagId session() const noexcept { return session_id_; }
    [[nodiscard]] std::size_t size() const noexcept { return log_.size(); }
    [[nodiscard]] bool empty() const noexcept { return log_.empty(); }
    [[nodiscard]] const SessionEvent& operator[](std::size_t index) const noexcept { return log_[index]; }
    [[nodiscard]] const SessionEvent& back() const noexcept { return log_.back(); }
    [[nodiscard]] auto begin() const noexcept { return log_.begin(); }
    [[nodiscard]] auto end() const noexcept { return log_.end(); }

    [[nodiscard]] storage_type drain() && noexcept(std::is_nothrow_move_constructible_v<storage_type>) {
        return std::move(log_).drain();
    }
};

// The one writer of one session event log.  Only mint_session_event_writer
// builds it, and each log has one, so no two recorders append to one log.
// The writer is move-only: a recorder moves it from each handle to the
// next, so one handle at a time appends.  A moved-from writer names no log,
// and an append through it ends the process.
//
// The move constructor is user-provided, so the class is not trivially
// copyable and std::bit_cast cannot build one.
class [[nodiscard]] SessionEventWriter {
    SessionEventLog* log_ = nullptr;

    constexpr explicit SessionEventWriter(SessionEventLog& log) noexcept : log_{&log} {}

    // Claims the one writer of a log.  A second claim for the same log ends
    // the process with Event_Log_Writer_Twice.
    [[nodiscard]] static SessionEventWriter claim_(SessionEventLog& log) noexcept {
        if (log.has_writer_.exchange_acq_rel(true)) [[unlikely]]
            detail::event_log::abort_on_second_writer();
        return SessionEventWriter{log};
    }

    friend SessionEventWriter mint_session_event_writer(SessionEventLog& log) noexcept;

public:
    constexpr SessionEventWriter(SessionEventWriter&& other) noexcept : log_{other.log_} { other.log_ = nullptr; }
    constexpr SessionEventWriter& operator=(SessionEventWriter&& other) noexcept {
        SessionEventLog* const taken = other.log_;
        other.log_ = nullptr;
        log_ = taken;
        return *this;
    }
    SessionEventWriter(const SessionEventWriter&) =
        delete("a session event log has one writer, and a copy is a second");
    SessionEventWriter&
    operator=(const SessionEventWriter&) = delete("a session event log has one writer, and a copy is a second");
    constexpr ~SessionEventWriter() = default;

    // Stamps the next step and the session of the log, then appends.
    void record_now(SessionEvent event) {
        CRUCIBLE_PRE(log_ != nullptr);
        log_->record_now_(event);
    }

    // Appends an event whose step the caller chose, for a replay from a
    // snapshot.  The step must not go backward.
    void record(SessionEvent event) {
        CRUCIBLE_PRE(log_ != nullptr);
        log_->record_(event);
    }
};

// Mints the one writer of a log.  A second mint for the same log ends the
// process with Event_Log_Writer_Twice.
[[nodiscard]] inline SessionEventWriter mint_session_event_writer(SessionEventLog& log) noexcept {
    return SessionEventWriter::claim_(log);
}

void mint_session_event_writer(const SessionEventLog&) noexcept = delete(
    "[Event_Log_Writer_From_Const_Log] a const log is a view of the record, and a view gives no right to "
    "append.  Mint the writer from the log that this endpoint owns.");

void mint_session_event_writer(SessionEventLog&&) noexcept = delete(
    "[Event_Log_Writer_From_Temporary_Log] the log is a temporary.  The writer keeps its address, so the log "
    "must outlive each handle that records into it.");

// ── The reader of the logs of a run ──────────────────────────────────

// The events of `first` and `rest` as one sequence: by step id, and for
// one step id in the order of the logs in the arguments.  Each log has one
// writer and steps that do not decrease, so the sequence depends on no
// schedule of the threads.  Read the logs after their writers end.  The
// events take one allocation.  Each argument after the first is a log too:
// the list of the logs below refuses another type.  Complexity: O(n * k)
// for n events in k logs.
template <typename... Rest>
[[nodiscard]] ::foundation::core::HeapArray<SessionEvent>
merge_session_logs(::foundation::core::Alloc allocation, const SessionEventLog& first, const Rest&... rest) {
    constexpr std::size_t log_count = 1 + sizeof...(Rest);
    const SessionEventLog* const logs[log_count] = {&first, &rest...};
    std::size_t next[log_count]{};
    std::size_t total = 0;
    for (const SessionEventLog* const log : logs)
        total += log->size();
    ::foundation::core::HeapArray<SessionEvent> events =
        ::foundation::core::mint_heap_array(allocation, total, SessionEvent::close(RoleTagId{}, RoleTagId{}));
    for (std::size_t place = 0; place < total; ++place) {
        // The first log, in the order of the arguments, whose next event has
        // the least step.  A tie keeps the earlier log.
        std::size_t chosen = log_count;
        for (std::size_t index = 0; index < log_count; ++index) {
            if (next[index] == logs[index]->size()) continue;
            const StepId step = (*logs[index])[next[index]].step_id();
            if (chosen == log_count || StepIdLess{}(step, (*logs[chosen])[next[chosen]].step_id())) chosen = index;
        }
        const SessionEvent& merged = (*logs[chosen])[next[chosen]];
        events.with_element(
            place, [&merged](SessionEvent& event) noexcept { event = merged; },
            [] noexcept { ::foundation::core::fatal("the events hold a slot for each event of the logs"); });
        ++next[chosen];
    }
    return events;
}

}  // namespace fixy::session
