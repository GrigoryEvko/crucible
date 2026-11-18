// The compile-time checks of crucible/cipher/FederationProtocol.h.

#include <crucible/cipher/FederationProtocol.h>

#include <fixy/session/Network.h>
#include <fixy/session/Projection.h>

namespace crucible::cipher::federation {

// The header is the encode image of FederationEntryHeader.  The sizes
// below pin the image, and the offsets of the two bytes that decode can
// refuse in the key.
static_assert(FEDERATION_STAMP_BYTES == 8, "the stamp of each version is its magic, version and atom count");
static_assert(FEDERATION_HEADER_BYTES == 48, "the image of FederationEntryHeader is the 48-byte wire header.  "
                                             "Adding a field requires a new protocol version and an update to "
                                             "every receiver.");
static_assert(::fixy::encoded_size_v<FederationKey> == 34,
              "a federation key is its kind, its width and the 32 bytes of its digest");
static_assert(sizeof(FederationKey) == 34 && alignof(FederationKey) == 1,
              "the digest is bytes, so the key holds no padding byte");
static_assert(sizeof(FederationEntryHeader) == FEDERATION_HEADER_BYTES, "the header holds no padding byte");
static_assert(::fixy::Decodable<FederationEntryStamp> && ::fixy::Decodable<FederationEntryHeader>);
static_assert(FEDERATION_KEY_KIND_OFFSET == 12 && FEDERATION_KEY_WIDTH_OFFSET == 13);
static_assert(static_cast<std::size_t>(FederationKeyWidth::bytes_32) == FEDERATION_DIGEST_BYTES);

static_assert((FEDERATION_MAGIC & 0xFFu) == 'C', "FEDERATION_MAGIC byte 0 must be 'C'.");
static_assert(((FEDERATION_MAGIC >> 8) & 0xFFu) == 'F', "FEDERATION_MAGIC byte 1 must be 'F'.");
static_assert(((FEDERATION_MAGIC >> 16) & 0xFFu) == 'E', "FEDERATION_MAGIC byte 2 must be 'E'.");
static_assert(((FEDERATION_MAGIC >> 24) & 0xFFu) == 'D', "FEDERATION_MAGIC byte 3 must be 'D'.");

static_assert(::foundation::effects::effect_count <= std::uint16_t{0xFFFF},
              "The effect-atom count must fit in the uint16_t wire field.");

// The other binary stream format in this runtime is the graph
// snapshot, whose magic word is spelled out here as a literal rather
// than included, to keep the header light. A stream dispatched to
// the wrong codec would misread forty-four bytes of header before
// anything noticed, so the two words must stay distinct. A third
// format has to extend this assertion.
static_assert(FEDERATION_MAGIC != 0x43444147u, "FEDERATION_MAGIC must not collide with the graph-snapshot magic — "
                                               "a federation stream and a graph snapshot must dispatch to "
                                               "different codecs at the magic-check step.");

static_assert(sizeof(FederationEntryHeader::payload_size) == 4,
              "payload_size MUST be a 32-bit field — caps the per-entry "
              "payload at 4 GiB.  A larger artifact fragments into several "
              "entries, or waits for a protocol version with a wider field.");
static_assert(sizeof(FederationEntryStamp::universe_cardinality) == 2,
              "universe_cardinality MUST be a 16-bit field — caps the effect "
              "atom catalog at 65535 entries.");
static_assert(sizeof(FederationEntryStamp::magic) == 4, "magic MUST be a 32-bit field — pinned for byte-stable cross-"
                                                        "platform protocol identification.");
static_assert(sizeof(FederationEntryStamp::protocol_version) == 2,
              "protocol_version MUST be a 16-bit field — supports up to 65536 "
              "wire-format revisions.");
static_assert(sizeof(FederationEntryHeader::reserved) == 2,
              "reserved MUST be a 16-bit field — a later layout claims this "
              "slot, and the width has to be there waiting for it.");

// The header states each local type, and each one is the projection of the
// global type onto its role, with an empty queue at the start.
static_assert(::fixy::session::global::is_global_well_formed_v<FederationGlobal<>>);
static_assert(::fixy::session::project_t<FederationGlobal<>, SenderRole>::queue::size == 0);
static_assert(::fixy::session::project_t<FederationGlobal<>, ReceiverRole>::queue::size == 0);
static_assert(::fixy::session::project_t<FederationGlobal<>, CoordRole>::queue::size == 0);
static_assert(std::is_same_v<::fixy::session::project_t<FederationGlobal<>, SenderRole>::local, SenderProto<>>);
static_assert(std::is_same_v<::fixy::session::project_t<FederationGlobal<>, ReceiverRole>::local, ReceiverProto<>>);
static_assert(std::is_same_v<::fixy::session::project_t<FederationGlobal<>, CoordRole>::local, CoordProto<>>);
struct CheckKey {};
static_assert(
    std::is_same_v<::fixy::session::project_t<FederationGlobal<CheckKey>, SenderRole>::local, SenderProto<CheckKey>>);
static_assert(std::is_same_v<::fixy::session::project_t<FederationGlobal<CheckKey>, ReceiverRole>::local,
                             ReceiverProto<CheckKey>>);
static_assert(
    std::is_same_v<::fixy::session::project_t<FederationGlobal<CheckKey>, CoordRole>::local, CoordProto<CheckKey>>);
static_assert(role_protocol_matches_v<SenderRole, SenderProto<>>);
static_assert(role_protocol_matches_v<ReceiverRole, ReceiverProto<>>);
static_assert(role_protocol_matches_v<CoordRole, CoordProto<>>);
static_assert(!role_protocol_matches_v<CoordRole, SenderProto<>>);

static_assert(::fixy::session::implementable_on_v<FederationGlobal<>, ::fixy::session::Network::PerPairFifo>);
static_assert(::fixy::session::implementable_on_v<FederationGlobal<>, ::fixy::session::Network::Bag>);
static_assert(::fixy::session::network_refusal_v<FederationGlobal<>, ::fixy::session::Network::Mailbox>
              == ::fixy::session::NetworkRefusal::ReceiverHasTwoSenders);

}  // namespace crucible::cipher::federation
