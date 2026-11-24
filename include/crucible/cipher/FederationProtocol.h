#pragma once

// This file is the wire format, not a transport. It fixes the byte
// layout, the magic, the version stamp, and the rules every transport
// applies before it trusts an entry.
//
// The header is the encode image of FederationEntryHeader
// (foundation/core/Protocol.h), 48 bytes, with the payload immediately
// after it:
//
//  Offset Size  Field
//   0      4    magic                 'CFED' as a little-endian word
//   4      2    protocol_version      2
//   6      2    universe_cardinality  the sender's effect-atom count
//   8      4    payload_size          bytes following the header
//  12      1    key kind              1: BLAKE3 in its keyed mode
//  13      1    key width             32: the bytes of the digest
//  14     32    key digest
//  46      2    reserved              zero
//  48+   ...    payload               opaque bytes
//
// Key.  A federation key is the keyed hash of its content under the
// key of the federation: BLAKE3 in its keyed mode, with its native output
// of 32 bytes (foundation/core/KeyedHash.h).  A federation spans
// organizations, so a member can be hostile, and a member holds the key of
// the federation.  Under a known key a hash keeps only its collision
// resistance, and the birthday bound gives 2^(n/2) of it for n bits.  So
// the key takes 256 bits and not 128: 128 bits give 64 bits of resistance,
// which a member with resources can reach, and 256 bits give 128.  An id
// inside one process stays a 64-bit hash, because no peer chooses its
// input there.
//
// The kind and the width are part of the key, and decode refuses each
// other value of the two.  So a later change of the hash is a new kind or
// a new width that this reader refuses, never a silent collision of two
// hashes in one slot.
//
// The integrity of the payload is the receiver's business. This layer
// never sees the payload in raw form, so a receiver hashes the bytes
// itself and compares the result against the key.
//
// The cardinality stamp carries the append-only rule for effect atoms
// onto the wire. An atom's value never moves and a new atom takes the
// next free position. So a stamp at or below the receiver's own count
// means the receiver knows every atom the sender used. A stamp above
// it means the sender used atoms this receiver cannot interpret, and
// the entry is refused rather than read under a meaning that the
// receiver does not hold.
//
// Two key values never travel. A sentinel key, each byte of its digest
// 0xFF, is the empty-slot marker of an open-addressed table, and accepting
// one lets a peer poison its own lookup termination. A zero digest means
// the sender left the key unset, and accepting one aims traffic at the
// most vulnerable bucket on the far side. Both are refused on write and
// on read.
//
// The reserved word is written as zero and any other value is
// refused. That is what makes a later revision safe. A reader of this
// version rejects newer traffic outright instead of reading two
// bytes as something they are not.
//
// Each multi-byte field is little-endian in the image on each target, so
// the layout does not change with the host.
//
// The session protocol at the end of this header fixes the legal order
// of the messages around one entry.

#include <crucible/Types.h>
#include <fixy/Core.h>
#include <fixy/Federation.h>
#include <fixy/Protocol.h>
#include <fixy/Tagged.h>
#include <fixy/session/ContentAddressed.h>
#include <fixy/session/Global.h>
#include <fixy/session/Projection.h>
#include <fixy/session/Protocol.h>
#include <foundation/core/KeyedHash.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::cipher::federation {

// The bytes in memory read 'C', 'F', 'E', 'D' in increasing address
// order, which is the order a receiver scanning a stream meets them.
inline constexpr std::uint32_t FEDERATION_MAGIC = 0x44454643u;

// A version is compared for equality and never fallen back from. A
// header of another version is refused before the reader reads its key.
inline constexpr std::uint16_t FEDERATION_PROTOCOL_V2 = 2u;

// The count of effect atoms of this build: the stamp that a sender writes,
// and the count that a receiver of this build states.  The check file of
// this header states that the count fits the 16-bit field.
inline constexpr std::uint16_t FEDERATION_UNIVERSE_CARDINALITY =
    static_cast<std::uint16_t>(::foundation::effects::effect_count);

// The hash of a federation key.
enum class FederationHashKind : std::uint8_t {
    // BLAKE3 in its keyed mode (foundation/core/KeyedHash.h).
    blake3_keyed = 1,
};

// The count of bytes of the digest of a federation key.
enum class FederationKeyWidth : std::uint8_t {
    bytes_32 = 32,
};

inline constexpr std::size_t FEDERATION_DIGEST_BYTES = 32;

// The key of the keyed hash of one federation, which each member holds.
// The key gives no secrecy against a member, because a member holds it.
// It keeps the keys of two federations apart, and an outsider who does
// not hold it cannot compute a key of the federation.
class FederationHashKey final {
public:
    constexpr explicit FederationHashKey(::fixy::View<std::byte const, FEDERATION_DIGEST_BYTES> bytes) noexcept {
        bytes_.with_mut([bytes](auto run) noexcept {
            auto cursor = bytes.begin();
            for (std::byte& byte : run) {
                byte = *cursor;
                ++cursor;
            }
        });
    }

    // The key of the keyed hash.  Complexity: one read of the 32 bytes.
    [[nodiscard]] constexpr ::foundation::core::KeyedHashKeyBytes keyed_hash_key() const noexcept {
        return ::foundation::core::KeyedHashKeyBytes{bytes_.view()};
    }

private:
    ::fixy::FixedArray<std::byte, FEDERATION_DIGEST_BYTES> bytes_{};
};

// The identity of an entry on the wire: the kind and the width of its
// hash, and the digest.  A default key has a zero digest, which no entry
// carries.  The digest is bytes, so the key has no padding and its image
// is its 34 bytes in memory.
class[[= ::fixy::derive_protocol{}]] FederationKey final {
public:
    constexpr FederationKey() noexcept = default;

    // The key of a digest of keyed_hash.
    constexpr explicit FederationKey(::foundation::core::Hash256 digest) noexcept : digest_{digest.bytes()} {}

    [[nodiscard]] constexpr FederationHashKind kind() const noexcept { return kind_; }
    [[nodiscard]] constexpr FederationKeyWidth width() const noexcept { return width_; }
    [[nodiscard]] constexpr ::fixy::View<std::byte const, FEDERATION_DIGEST_BYTES> digest() const noexcept {
        return digest_.view();
    }

    // Each byte of the digest is zero: the sender left the key unset.
    [[nodiscard]] constexpr bool is_zero() const noexcept { return has_each_byte_(std::byte{0}); }

    // Each byte of the digest is 0xFF: the empty-slot marker of a table.
    [[nodiscard]] constexpr bool is_sentinel() const noexcept { return has_each_byte_(std::byte{0xFF}); }

    [[nodiscard]] static constexpr FederationKey sentinel() noexcept {
        FederationKey key{};
        key.digest_.with_mut([](auto run) noexcept {
            for (std::byte& byte : run) {
                byte = std::byte{0xFF};
            }
        });
        return key;
    }

    [[nodiscard]] friend constexpr bool operator==(FederationKey const& left, FederationKey const& right) noexcept {
        return left.kind_ == right.kind_ && left.width_ == right.width_ && ::fixy::equal(left.digest_, right.digest_);
    }

private:
    [[nodiscard]] constexpr bool has_each_byte_(std::byte value) const noexcept {
        for (std::byte const byte : digest_) {
            if (byte != value) return false;
        }
        return true;
    }

    FederationHashKind kind_ = FederationHashKind::blake3_keyed;
    FederationKeyWidth width_ = FederationKeyWidth::bytes_32;
    ::fixy::FixedArray<std::byte, FEDERATION_DIGEST_BYTES> digest_{};
};

// The federation key of the bytes of a content under the key of the
// federation.  Complexity: one compression for each 64 bytes of the
// content (foundation/core/KeyedHash.h).
[[nodiscard]] constexpr FederationKey federation_key_of_bytes(FederationHashKey const& hash_key,
                                                              ::fixy::View<std::byte const> content) noexcept {
    return FederationKey{::foundation::core::keyed_hash(hash_key.keyed_hash_key(), content)};
}

// The federation key of a value: the keyed hash of its encode image.  The
// image holds no padding and has the same bytes on each target.
template <::fixy::Encodable T>
[[nodiscard]] constexpr FederationKey federation_key_of(FederationHashKey const& hash_key, T const& value) noexcept {
    ::fixy::FixedArray<std::byte, ::fixy::encoded_size_v<T>> image{};
    image.with_mut([&value](auto run) noexcept { ::fixy::encode(value, std::move(run)); });
    return federation_key_of_bytes(hash_key, ::fixy::View<std::byte const>{image.view()});
}

// The part of the header that each version keeps: a reader reads it first,
// and it refuses a header of another version before it reads the rest.
struct[[= ::fixy::derive_protocol{}]] FederationEntryStamp {
    std::uint32_t magic = 0;
    std::uint16_t protocol_version = 0;
    std::uint16_t universe_cardinality = 0;
};

struct[[= ::fixy::derive_protocol{}]] FederationEntryHeader {
    FederationEntryStamp stamp{};
    std::uint32_t payload_size = 0;
    FederationKey key{};
    [[= ::fixy::pad_part{}]] std::uint16_t reserved = 0;
};

inline constexpr std::size_t FEDERATION_STAMP_BYTES = ::fixy::encoded_size_v<FederationEntryStamp>;
inline constexpr std::size_t FEDERATION_HEADER_BYTES = ::fixy::encoded_size_v<FederationEntryHeader>;

// The offsets of the two bytes of the key that decode can refuse.
inline constexpr std::uint32_t FEDERATION_KEY_KIND_OFFSET = FEDERATION_STAMP_BYTES + 4;
inline constexpr std::uint32_t FEDERATION_KEY_WIDTH_OFFSET = FEDERATION_KEY_KIND_OFFSET + 1;

enum class FederationError : std::uint8_t {
    None = 0,
    BadMagic = 1,
    UnsupportedVersion = 2,
    UniverseCardinalityTooHigh = 3,
    SentinelKey = 4,
    ZeroKey = 5,
    ReservedNonZero = 6,
    TruncatedHeader = 7,
    TruncatedPayload = 8,
    OutputBufferTooSmall = 9,
    UnsupportedHashKind = 10,
    UnsupportedKeyWidth = 11,
};

// The name is the enumerator's identifier, read by reflection, so a new
// error cannot be added without a name.  A value that no enumerator
// holds prints "<unknown FederationError>".
[[nodiscard]] inline constexpr ::fixy::TextView federation_error_name(FederationError e) noexcept {
    return ::foundation::core::enum_name_text(e);
}

// The error of a header that decode refuses.  Three parts of the header
// can hold no value: the kind and the width of the key, and the reserved
// word.
[[nodiscard]] inline constexpr FederationError federation_error_of_fault(::fixy::DecodeFault fault) noexcept {
    if (fault.refusal() == ::fixy::DecodeRefusal::pad_byte) return FederationError::ReservedNonZero;
    if (fault.offset() == FEDERATION_KEY_KIND_OFFSET) return FederationError::UnsupportedHashKind;
    CRUCIBLE_FATAL_INVARIANT(fault.offset() == FEDERATION_KEY_WIDTH_OFFSET);
    return FederationError::UnsupportedKeyWidth;
}

// The rejection rules are checked in the body and reported, rather
// than asserted as preconditions. A caller that hands over a buffer
// too small, or a key it forgot to fill in, deserves an error it can
// route, not a terminated process.

// The entry goes into the run of out_buf, the ViewMut of a frame: the header
// in the first bytes, and the payload after it.
template <class Brand, std::size_t Extent>
[[nodiscard]] inline ::fixy::Result<std::size_t, FederationError>
serialize_federation_entry(::fixy::ViewMut<std::byte, Brand, Extent> out_buf, FederationKey const& key,
                           ::fixy::View<std::byte const> payload) noexcept {
    if (key.is_sentinel()) {
        return ::fixy::err(FederationError::SentinelKey);
    }
    if (key.is_zero()) {
        return ::fixy::err(FederationError::ZeroKey);
    }

    // The payload size is a 32-bit field of the header.
    CRUCIBLE_TRY(payload_size,
                 ::fixy::narrow<std::uint32_t>(payload.size()).ok_or(FederationError::OutputBufferTooSmall));

    const std::size_t total_bytes = FEDERATION_HEADER_BYTES + payload.size();
    if (out_buf.size() < total_bytes) {
        return ::fixy::err(FederationError::OutputBufferTooSmall);
    }

    const FederationEntryHeader hdr{
        .stamp =
            FederationEntryStamp{
                .magic = FEDERATION_MAGIC,
                .protocol_version = FEDERATION_PROTOCOL_V2,
                .universe_cardinality = FEDERATION_UNIVERSE_CARDINALITY,
            },
        .payload_size = payload_size,
        .key = key,
        .reserved = 0,
    };
    auto [header_bytes, rest] = ::fixy::split_at(std::move(out_buf), FEDERATION_HEADER_BYTES)
                                    .expect("the size check puts the header inside the buffer");
    ::fixy::encode(hdr, ::fixy::take_window<FEDERATION_HEADER_BYTES>(std::move(header_bytes), 0)
                            .expect("the header part holds the header"));

    // An entry with no payload bytes is valid and meaningful. It
    // announces the key, and the receiver looks the artifact up in its
    // own store from the key.
    if (payload.size() != 0) {
        ::fixy::copy(::fixy::take_window(std::move(rest), 0, payload.size())
                         .expect("the size check puts the payload inside the buffer"),
                     payload)
            .expect("the window holds the count of the payload");
    }

    return total_bytes;
}

// The receiver's own atom count arrives as an argument instead of
// being read from a global, so a test can stand in as a newer or an
// older peer.
//
// The caller reads the payload itself and checks that it hashes to
// the key in the returned header.

[[nodiscard]] inline ::fixy::Result<FederationEntryHeader, FederationError>
deserialize_federation_header(::fixy::View<std::byte const> in_buf, std::uint16_t receiver_cardinality) noexcept {
    CRUCIBLE_TRY(header_bytes, in_buf.window<FEDERATION_HEADER_BYTES>(0).ok_or(FederationError::TruncatedHeader));

    // Each bit pattern of the stamp is a value, so its decode cannot
    // fail.  The stamp comes first, so a header of another version is
    // refused for its version and not for a byte of its key.
    const FederationEntryStamp stamp =
        ::fixy::decode<FederationEntryStamp>(
            header_bytes.window<FEDERATION_STAMP_BYTES>(0).expect("the stamp is the start of the header"))
            .expect("each bit pattern of the stamp is a value");

    if (stamp.magic != FEDERATION_MAGIC) {
        return ::fixy::err(FederationError::BadMagic);
    }

    if (stamp.protocol_version != FEDERATION_PROTOCOL_V2) {
        return ::fixy::err(FederationError::UnsupportedVersion);
    }

    CRUCIBLE_TRY(hdr, ::fixy::decode<FederationEntryHeader>(header_bytes).map_err(federation_error_of_fault));

    // A sender at or below this count used only atoms this receiver
    // already knows, which is the safe direction of the append-only
    // rule.
    if (hdr.stamp.universe_cardinality > receiver_cardinality) {
        return ::fixy::err(FederationError::UniverseCardinalityTooHigh);
    }

    if (hdr.key.is_sentinel()) {
        return ::fixy::err(FederationError::SentinelKey);
    }
    if (hdr.key.is_zero()) {
        return ::fixy::err(FederationError::ZeroKey);
    }

    const std::size_t bytes_after_header = in_buf.size() - FEDERATION_HEADER_BYTES;
    if (hdr.payload_size > bytes_after_header) {
        return ::fixy::err(FederationError::TruncatedPayload);
    }

    return hdr;
}

// The payload View borrows the input buffer. It stays valid only
// while that buffer does.

struct FederationEntryView {
    FederationEntryHeader header{};
    ::fixy::View<std::byte const> payload{};
};

[[nodiscard]] inline ::fixy::Result<FederationEntryView, FederationError>
deserialize_untrusted_federation_entry(::fixy::View<std::byte const> in_buf,
                                       std::uint16_t receiver_cardinality) noexcept {
    CRUCIBLE_TRY(header, deserialize_federation_header(in_buf, receiver_cardinality));

    return FederationEntryView{
        .header = header,
        .payload = in_buf.window(FEDERATION_HEADER_BYTES, header.payload_size)
                       .expect("the header check puts the payload inside the buffer"),
    };
}

// The peer token comes only from the federation door of
// fixy/Federation.h, so a caller that holds one admitted a peer of Org.
// The tag of the view is the tag of that token.  It records where the
// view came from and is no check: fixy::Tagged lets any caller mint a
// value under this tag.
template <typename Org, typename Brand>
[[nodiscard]] inline ::fixy::Result<
    ::fixy::Tagged<FederationEntryView, ::foundation::permissions::tag::FederatedPeer<Org>>, FederationError>
deserialize_federation_entry(const ::foundation::permissions::Permission<
                                 ::foundation::permissions::tag::FederatedPeer<Org>, Brand>& peer_permission,
                             ::fixy::View<std::byte const> in_buf, std::uint16_t receiver_cardinality) noexcept {
    (void)peer_permission;

    CRUCIBLE_TRY(view, deserialize_untrusted_federation_entry(in_buf, receiver_cardinality));
    return ::fixy::mint_tagged<::foundation::permissions::tag::FederatedPeer<Org>>(view);
}

[[nodiscard]] inline constexpr bool federation_accepts_cardinality(std::uint16_t sender_cardinality,
                                                                   std::uint16_t receiver_cardinality) noexcept {
    return sender_cardinality <= receiver_cardinality;
}

// ── The session protocol ─────────────────────────────────────────────
//
// The protocol has three roles.  The sender sends the header of an
// entry to the coordinator, and the coordinator sends an Ack back.  The
// coordinator then sends a pull request to the receiver, and the
// receiver sends the body.  The coordinator is a role of the global
// type, so the protocol is one description of three parties and not two
// binary descriptions with no relation.
//
// The global type is written on fixy/session/Global.h, and each local
// type below is its projection (fixy/session/Projection.h) onto one role.
// The header states each local type, and the check file of the header
// proves that each one is the projection, so a unit that includes the
// header compiles no projection.  A carrier states its network model
// (fixy/session/NetworkModel.h).  The protocol has no choice, and the
// coordinator receives from two senders.  So it is implementable on
// per-pair FIFO and on a bag, but not on a mailbox (fixy/session/Network.h),
// and the check file proves that too.  A mint that gives the handle of a
// role must first ask ::fixy::session::CarrierImplements with the global
// type and the carrier.
//
// No production file makes the handle of a role, so this header carries
// the types and no mint.

struct SenderRole {};
struct ReceiverRole {};
struct CoordRole {};

// The label of each message of the global type.
struct HeaderLabel {};
struct AckLabel {};
struct PullLabel {};
struct BodyLabel {};

struct AnyFederationKey {};

template <typename KeyTag = AnyFederationKey>
struct Ack {
    using key_tag = KeyTag;
    FederationKey key{};
};

template <typename KeyTag = AnyFederationKey>
struct PullRequest {
    using key_tag = KeyTag;
    FederationKey key{};
};

template <typename KeyTag = AnyFederationKey>
struct FederationEntryPayload {
    using key_tag = KeyTag;
    ::fixy::View<std::byte const> bytes{};
};

template <typename KeyTag = AnyFederationKey>
using HeaderPayload = ::fixy::session::ContentAddressed<FederationEntryHeader>;

template <typename KeyTag = AnyFederationKey>
using BodyPayload = ::fixy::session::ContentAddressed<FederationEntryPayload<KeyTag>>;

template <typename KeyTag = AnyFederationKey>
using FederationGlobal = ::fixy::session::global::Rec<::fixy::session::global::Msg<
    SenderRole, CoordRole, HeaderLabel, HeaderPayload<KeyTag>,
    ::fixy::session::global::Msg<
        CoordRole, SenderRole, AckLabel, Ack<KeyTag>,
        ::fixy::session::global::Msg<CoordRole, ReceiverRole, PullLabel, PullRequest<KeyTag>,
                                     ::fixy::session::global::Msg<ReceiverRole, CoordRole, BodyLabel,
                                                                  BodyPayload<KeyTag>,
                                                                  ::fixy::session::global::Var>>>>>;

// A projection gives a queue and a local type.  At the start of the
// protocol each queue is empty, so a role protocol is the local type.
template <typename KeyTag = AnyFederationKey>
using SenderProto = ::fixy::session::Loop<::fixy::session::Send<
    ::fixy::session::PeerMsg<CoordRole, HeaderLabel, HeaderPayload<KeyTag>>,
    ::fixy::session::Recv<::fixy::session::PeerMsg<CoordRole, AckLabel, Ack<KeyTag>>, ::fixy::session::Continue>>>;

template <typename KeyTag = AnyFederationKey>
using ReceiverProto = ::fixy::session::Loop<
    ::fixy::session::Recv<::fixy::session::PeerMsg<CoordRole, PullLabel, PullRequest<KeyTag>>,
                          ::fixy::session::Send<::fixy::session::PeerMsg<CoordRole, BodyLabel, BodyPayload<KeyTag>>,
                                                ::fixy::session::Continue>>>;

template <typename KeyTag = AnyFederationKey>
using CoordProto = ::fixy::session::Loop<::fixy::session::Recv<
    ::fixy::session::PeerMsg<SenderRole, HeaderLabel, HeaderPayload<KeyTag>>,
    ::fixy::session::Send<::fixy::session::PeerMsg<SenderRole, AckLabel, Ack<KeyTag>>,
                          ::fixy::session::Send<::fixy::session::PeerMsg<ReceiverRole, PullLabel, PullRequest<KeyTag>>,
                                                ::fixy::session::Recv<::fixy::session::PeerMsg<ReceiverRole, BodyLabel,
                                                                                               BodyPayload<KeyTag>>,
                                                                      ::fixy::session::Continue>>>>>;

template <typename Role, typename Proto, typename KeyTag = AnyFederationKey>
struct role_protocol_matches : std::false_type {};

template <typename KeyTag>
struct role_protocol_matches<SenderRole, SenderProto<KeyTag>, KeyTag> : std::true_type {};

template <typename KeyTag>
struct role_protocol_matches<ReceiverRole, ReceiverProto<KeyTag>, KeyTag> : std::true_type {};

template <typename KeyTag>
struct role_protocol_matches<CoordRole, CoordProto<KeyTag>, KeyTag> : std::true_type {};

template <typename Role, typename Proto, typename KeyTag = AnyFederationKey>
inline constexpr bool role_protocol_matches_v = role_protocol_matches<Role, Proto, KeyTag>::value;

}  // namespace crucible::cipher::federation
