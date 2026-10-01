// The compile-time checks of crucible/cipher/FederationProtocol.h.

#include <crucible/cipher/FederationProtocol.h>

namespace crucible::cipher::federation {

// The offsets below are pinned because a codec written elsewhere
// reaches into the bytes by offset rather than through this struct.
static_assert(sizeof(FederationEntryHeader) == 32, "FederationEntryHeader must be exactly 32 bytes — the wire-format "
                                                   "header size.  Adding a field requires a new protocol version and "
                                                   "an update to every receiver.");
static_assert(alignof(FederationEntryHeader) == 8, "FederationEntryHeader must be 8-byte aligned (the natural "
                                                   "alignment of the embedded ContentHash + RowHash).");
static_assert(std::is_standard_layout_v<FederationEntryHeader>,
              "FederationEntryHeader must be standard-layout to permit "
              "offsetof + std::memcpy-based wire codec.");
static_assert(std::is_trivially_copyable_v<FederationEntryHeader>,
              "FederationEntryHeader must be trivially copyable to permit "
              "std::memcpy round-trip through the byte buffer.");

static_assert(offsetof(FederationEntryHeader, magic) == 0);
static_assert(offsetof(FederationEntryHeader, protocol_version) == 4);
static_assert(offsetof(FederationEntryHeader, universe_cardinality) == 6);
static_assert(offsetof(FederationEntryHeader, content_hash) == 8);
static_assert(offsetof(FederationEntryHeader, row_hash) == 16);
static_assert(offsetof(FederationEntryHeader, payload_size) == 24);
static_assert(offsetof(FederationEntryHeader, reserved) == 28);

static_assert((FEDERATION_MAGIC & 0xFFu) == 'C', "FEDERATION_MAGIC byte 0 must be 'C'.");
static_assert(((FEDERATION_MAGIC >> 8) & 0xFFu) == 'F', "FEDERATION_MAGIC byte 1 must be 'F'.");
static_assert(((FEDERATION_MAGIC >> 16) & 0xFFu) == 'E', "FEDERATION_MAGIC byte 2 must be 'E'.");
static_assert(((FEDERATION_MAGIC >> 24) & 0xFFu) == 'D', "FEDERATION_MAGIC byte 3 must be 'D'.");

static_assert(::foundation::effects::effect_count <= std::uint16_t{0xFFFF},
              "The effect-atom count must fit in the uint16_t wire field.");

// The other binary stream format in this runtime is the graph
// snapshot, whose magic word is spelled out here as a literal rather
// than included, to keep the header light. A stream dispatched to
// the wrong codec would misread twenty-eight bytes of header before
// anything noticed, so the two words must stay distinct. A third
// format has to extend this assertion.
static_assert(FEDERATION_MAGIC != 0x43444147u, "FEDERATION_MAGIC must not collide with the graph-snapshot magic — "
                                               "a federation stream and a graph snapshot must dispatch to "
                                               "different codecs at the magic-check step.");

static_assert(sizeof(FederationEntryHeader::payload_size) == 4,
              "payload_size MUST be a 32-bit field — caps the per-entry "
              "payload at 4 GiB.  A larger artifact fragments into several "
              "entries, or waits for a protocol version with a wider field.");
static_assert(sizeof(FederationEntryHeader::universe_cardinality) == 2,
              "universe_cardinality MUST be a 16-bit field — caps the effect "
              "atom catalog at 65535 entries.");
static_assert(sizeof(FederationEntryHeader::magic) == 4, "magic MUST be a 32-bit field — pinned for byte-stable cross-"
                                                         "platform protocol identification.");
static_assert(sizeof(FederationEntryHeader::protocol_version) == 2,
              "protocol_version MUST be a 16-bit field — supports up to 65536 "
              "wire-format revisions.");
static_assert(sizeof(FederationEntryHeader::reserved) == 4,
              "reserved MUST be a 32-bit field — a later layout claims this "
              "slot, and the width has to be there waiting for it.");

static_assert(::fixy::session::global::is_global_well_formed_v<FederationGlobal<>>);
static_assert(::fixy::session::project_t<FederationGlobal<>, SenderRole>::queue::size == 0);
static_assert(::fixy::session::project_t<FederationGlobal<>, ReceiverRole>::queue::size == 0);
static_assert(::fixy::session::project_t<FederationGlobal<>, CoordRole>::queue::size == 0);
static_assert(std::is_same_v<SenderProto<>, ExpectedSenderProto<>>);
static_assert(std::is_same_v<ReceiverProto<>, ExpectedReceiverProto<>>);
static_assert(std::is_same_v<CoordProto<>, ExpectedCoordProto<>>);
static_assert(role_protocol_matches_v<SenderRole, SenderProto<>>);
static_assert(role_protocol_matches_v<ReceiverRole, ReceiverProto<>>);
static_assert(role_protocol_matches_v<CoordRole, CoordProto<>>);
static_assert(!role_protocol_matches_v<CoordRole, SenderProto<>>);

static_assert(::fixy::session::implementable_on_v<FederationGlobal<>, ::fixy::session::Network::PerPairFifo>);
static_assert(::fixy::session::implementable_on_v<FederationGlobal<>, ::fixy::session::Network::Bag>);
static_assert(::fixy::session::network_refusal_v<FederationGlobal<>, ::fixy::session::Network::Mailbox>
              == ::fixy::session::NetworkRefusal::ReceiverHasTwoSenders);

}  // namespace crucible::cipher::federation
