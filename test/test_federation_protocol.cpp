// The federation entry format is a wire contract between separate
// machines, so the layout assertions in this file are the specification
// and not an implementation detail.  Changing an offset, a field width,
// the magic, or the reserved-field rule breaks every peer that already
// speaks the format, and the assertions here are what says so.
//
// The rest of the file is the receiver's side of that contract: a
// decoder that reads bytes from another machine has to reject every
// malformed shape rather than interpret it, so most tests feed it a
// deliberately broken header and demand a specific error.

#include <crucible/cipher/FederationProtocol.h>
#include <crucible/Serialize.h>  // CDAG_MAGIC
#include <crucible/Types.h>
#include <fixy/Core.h>
#include <fixy/Federation.h>
#include <fixy/Protocol.h>
#include <foundation/core/KeyedHash.h>
#include <foundation/effects/Effect.h>

#include "test_assert.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

using namespace crucible;
namespace fed = crucible::cipher::federation;
using ::foundation::core::Hash256;
using ::foundation::core::index;

// Variadic because a template argument list contains commas, which the
// preprocessor would read as separating macro arguments.
#define ASSERT_TRUE(...) assert((__VA_ARGS__))

// The value of a result that holds one.  A result with an error ends the
// test.
template <class T, class E>
[[nodiscard]] static T value_of(::fixy::Result<T, E> const& result) {
    return result.match([](T const& value) { return value; },
                        [](E const&) -> T { ::fixy::fatal("the result holds an error"); });
}

// The error of a result that holds one.  A result with a value ends the
// test.
template <class T, class E>
[[nodiscard]] static E error_of(::fixy::Result<T, E> const& result) {
    return result.err().expect("the result holds a value");
}

// A buffer of N bytes.
template <std::size_t N>
using Bytes = ::foundation::core::FixedArray<std::byte, N>;

inline constexpr std::size_t kHeader = fed::FEDERATION_HEADER_BYTES;

// The bytes of the integer values, in order.
template <class... Values>
[[nodiscard]] static constexpr Bytes<sizeof...(Values)> bytes_of(Values... values) {
    return Bytes<sizeof...(Values)>{static_cast<std::byte>(values)...};
}

// The first count bytes of a buffer: the bytes of an entry that a
// serialize wrote.
template <std::size_t N>
[[nodiscard]] static ::fixy::View<std::byte const> prefix(Bytes<N> const& buf, std::size_t count) {
    return ::fixy::View<std::byte const>{buf.view()}.window(0, count).expect("the count is inside the buffer");
}

// A key of four words of digest.
[[nodiscard]] static constexpr fed::FederationKey key_of(std::uint64_t first, std::uint64_t second, std::uint64_t third,
                                                         std::uint64_t fourth) {
    return fed::FederationKey{Hash256{first, second, third, fourth}};
}

// The parts of a header that a test sets.  Each part not set is a part of
// a well-formed header of this build.
struct HeaderSpec {
    std::uint32_t magic = fed::FEDERATION_MAGIC;
    std::uint16_t protocol_version = fed::FEDERATION_PROTOCOL_V2;
    std::uint16_t universe_cardinality = 6;
    std::uint32_t payload_size = 0;
    fed::FederationKey key = key_of(0x42, 0x43, 0x44, 0x45);
};

// The 48 bytes of a header, as the wire holds them.
[[nodiscard]] static Bytes<kHeader> header_bytes(HeaderSpec const& spec) {
    fed::FederationEntryHeader const hdr{
        .stamp =
            fed::FederationEntryStamp{
                .magic = spec.magic,
                .protocol_version = spec.protocol_version,
                .universe_cardinality = spec.universe_cardinality,
            },
        .payload_size = spec.payload_size,
        .key = spec.key,
        .reserved = 0,
    };
    Bytes<kHeader> out{};
    ::fixy::encode(hdr, out.view());
    return out;
}

// The key of the federation of the tests: the key of the official vectors
// of BLAKE3, the text "whats the Elvish word for friend".
[[nodiscard]] static constexpr fed::FederationHashKey test_hash_key() noexcept {
    constexpr ::fixy::TextView text{"whats the Elvish word for friend"};
    Bytes<32> bytes{};
    auto cursor = bytes.begin();
    for (char const letter : text) {
        *cursor = static_cast<std::byte>(letter);
        ++cursor;
    }
    return fed::FederationHashKey{::fixy::View<std::byte const, 32>{bytes.view()}};
}

// An empty payload.
inline constexpr ::fixy::View<std::byte const> kNoPayload{};

static void test_header_layout_invariants() {
    static_assert(fed::FEDERATION_STAMP_BYTES == 8);
    static_assert(fed::FEDERATION_HEADER_BYTES == 48);
    static_assert(::fixy::encoded_size_v<fed::FederationKey> == 34);
    static_assert(sizeof(fed::FederationEntryHeader) == fed::FEDERATION_HEADER_BYTES);
    static_assert(fed::FEDERATION_KEY_KIND_OFFSET == 12);
    static_assert(fed::FEDERATION_KEY_WIDTH_OFFSET == 13);

    // The offsets of each field in the image, read from the bytes of one
    // header whose fields hold different values.
    HeaderSpec spec{};
    spec.universe_cardinality = 0x0706;
    spec.payload_size = 0x0B0A0908;
    spec.key = key_of(0x1716151413121110ULL, 0x1F1E1D1C1B1A1918ULL, 0x2726252423222120ULL, 0x2F2E2D2C2B2A2928ULL);
    const Bytes<kHeader> buf = header_bytes(spec);
    assert(buf.at(index<0>()) == std::byte{'C'});
    assert(buf.at(index<4>()) == std::byte{0x02});
    assert(buf.at(index<5>()) == std::byte{0x00});
    assert(buf.at(index<6>()) == std::byte{0x06});
    assert(buf.at(index<7>()) == std::byte{0x07});
    assert(buf.at(index<8>()) == std::byte{0x08});
    assert(buf.at(index<11>()) == std::byte{0x0B});
    assert(buf.at(index<12>()) == std::byte{0x01});
    assert(buf.at(index<13>()) == std::byte{32});
    assert(buf.at(index<14>()) == std::byte{0x10});
    assert(buf.at(index<45>()) == std::byte{0x2F});
    assert(buf.at(index<46>()) == std::byte{0x00});
    assert(buf.at(index<47>()) == std::byte{0x00});

    crucible::test::pass("  test_header_layout_invariants:                  PASSED\n");
}

// The federation entry wire format puts the magic at offset 0 of the
// header, as the bytes 'C', 'F', 'E', 'D' in increasing address order.
// These checks compare the magic with the letters of the format.  A change
// of the constant fails them.

static void test_magic_byte_order() {
    static_assert((fed::FEDERATION_MAGIC & 0xFFu) == 'C');
    static_assert(((fed::FEDERATION_MAGIC >> 8) & 0xFFu) == 'F');
    static_assert(((fed::FEDERATION_MAGIC >> 16) & 0xFFu) == 'E');
    static_assert(((fed::FEDERATION_MAGIC >> 24) & 0xFFu) == 'D');

    // The same claim again through the bytes of a header, which is how a
    // peer decoder actually sees it.
    const Bytes<kHeader> buf = header_bytes(HeaderSpec{});
    assert(buf.at(index<0>()) == std::byte{'C'});
    assert(buf.at(index<1>()) == std::byte{'F'});
    assert(buf.at(index<2>()) == std::byte{'E'});
    assert(buf.at(index<3>()) == std::byte{'D'});

    crucible::test::pass("  test_magic_byte_order:                          PASSED\n");
}

// The federation entry wire format puts the protocol version at offset 4
// of the header, as a 16-bit little-endian word.  Version 2 is the byte
// 0x02 at offset 4 and the byte 0x00 at offset 5.

struct VersionFieldBytes {
    std::uint8_t at_offset_4 = 0;
    std::uint8_t at_offset_5 = 0;
};

static void test_version_byte_order() {
    constexpr VersionFieldBytes version_bytes =
        fixy::decode<VersionFieldBytes>(fed::FEDERATION_PROTOCOL_V2).value_or(VersionFieldBytes{});
    static_assert(version_bytes.at_offset_4 == 0x02);
    static_assert(version_bytes.at_offset_5 == 0x00);

    crucible::test::pass("  test_version_byte_order:                        PASSED\n");
}

static void test_round_trip_basic() {
    const fed::FederationKey key = key_of(0xC0FFEEBA12345678ULL, 0xDEADBEEF56789ABCULL, 3, 4);
    const auto payload = bytes_of(0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08);

    Bytes<kHeader + 16> buf{};
    auto written = fed::serialize_federation_entry(buf.view(), key, payload.view());
    ASSERT_TRUE(written.is_ok());
    assert(value_of(written) == fed::FEDERATION_HEADER_BYTES + payload.size());

    const auto receiver_card = fed::FEDERATION_UNIVERSE_CARDINALITY;
    auto view = fed::deserialize_untrusted_federation_entry(prefix(buf, value_of(written)), receiver_card);
    ASSERT_TRUE(view.is_ok());

    assert(value_of(view).header.stamp.magic == fed::FEDERATION_MAGIC);
    assert(value_of(view).header.stamp.protocol_version == fed::FEDERATION_PROTOCOL_V2);
    assert(value_of(view).header.stamp.universe_cardinality == receiver_card);
    assert(value_of(view).header.key == key);
    assert(value_of(view).header.key.kind() == fed::FederationHashKind::blake3_keyed);
    assert(value_of(view).header.key.width() == fed::FederationKeyWidth::bytes_32);
    assert(value_of(view).header.payload_size == payload.size());
    assert(value_of(view).header.reserved == 0u);

    assert(::fixy::equal(value_of(view).payload, payload.view()));

    crucible::test::pass("  test_round_trip_basic:                          PASSED\n");
}

// An entry with no payload is legal, and means something: it announces
// possession of an artifact without shipping it, leaving the receiver to
// ask for the bytes by key if it wants them.

static void test_round_trip_empty_payload() {
    const fed::FederationKey key = key_of(0x1111222233334444ULL, 0xAAAABBBBCCCCDDDDULL, 0, 0);

    Bytes<kHeader> buf{};
    auto written = fed::serialize_federation_entry(buf.view(), key, kNoPayload);
    ASSERT_TRUE(written.is_ok());
    assert(value_of(written) == fed::FEDERATION_HEADER_BYTES);  // header only

    auto view = fed::deserialize_untrusted_federation_entry(prefix(buf, value_of(written)),
                                                            fed::FEDERATION_UNIVERSE_CARDINALITY);
    ASSERT_TRUE(view.is_ok());
    assert(value_of(view).payload.size() == 0);
    assert(value_of(view).header.payload_size == 0u);

    crucible::test::pass("  test_round_trip_empty_payload:                  PASSED\n");
}

// The sentinel key, each byte of the digest 0xFF, is the empty-slot
// marker of an open-addressing table.  Sending it would write an empty
// marker into a live slot on the receiver.

static void test_serialize_rejects_sentinel() {
    Bytes<kHeader> buf{};
    auto written = fed::serialize_federation_entry(buf.view(), fed::FederationKey::sentinel(), kNoPayload);
    ASSERT_TRUE(!written.is_ok());
    assert(error_of(written) == fed::FederationError::SentinelKey);

    crucible::test::pass("  test_serialize_rejects_sentinel:                PASSED\n");
}

// A key with a zero digest is what a default-constructed key looks like,
// so a sender emitting one has forgotten to fill it in.

static void test_serialize_rejects_zero() {
    const fed::FederationKey zero{};
    assert(zero.is_zero());
    assert(!zero.is_sentinel());

    Bytes<kHeader> buf{};
    auto written = fed::serialize_federation_entry(buf.view(), zero, kNoPayload);
    ASSERT_TRUE(!written.is_ok());
    assert(error_of(written) == fed::FederationError::ZeroKey);

    crucible::test::pass("  test_serialize_rejects_zero:                    PASSED\n");
}

// Serialize refuses rather than truncating, because a partial entry on
// the wire desynchronizes everything after it in the stream.

static void test_serialize_rejects_undersized_buffer() {
    const Bytes<16> payload{};

    Bytes<kHeader + 8> buf{};  // 48 + 8 < 48 + 16
    auto written = fed::serialize_federation_entry(buf.view(), HeaderSpec{}.key, payload.view());
    ASSERT_TRUE(!written.is_ok());
    assert(error_of(written) == fed::FederationError::OutputBufferTooSmall);

    // Smaller than the header alone.
    Bytes<16> tiny_buf{};
    auto tiny_written = fed::serialize_federation_entry(tiny_buf.view(), HeaderSpec{}.key, kNoPayload);
    ASSERT_TRUE(!tiny_written.is_ok());
    assert(error_of(tiny_written) == fed::FederationError::OutputBufferTooSmall);

    crucible::test::pass("  test_serialize_rejects_undersized_buffer:       PASSED\n");
}

static void test_deserialize_rejects_truncated_header() {
    const Bytes<kHeader - 1> short_buf{};
    auto view = fed::deserialize_untrusted_federation_entry(short_buf.view(), 6);
    ASSERT_TRUE(!view.is_ok());
    assert(error_of(view) == fed::FederationError::TruncatedHeader);

    auto empty_view = fed::deserialize_untrusted_federation_entry(kNoPayload, 6);
    ASSERT_TRUE(!empty_view.is_ok());
    assert(error_of(empty_view) == fed::FederationError::TruncatedHeader);

    crucible::test::pass("  test_deserialize_rejects_truncated_header:      PASSED\n");
}

// Everything in this header is valid except the magic, so the decoder
// has to reject on the magic alone rather than on a downstream field.

static void test_deserialize_rejects_bad_magic() {
    HeaderSpec spec{};
    spec.magic = 0xDEADBEEFu;  // not FEDERATION_MAGIC
    const Bytes<kHeader> buf = header_bytes(spec);

    auto view = fed::deserialize_untrusted_federation_entry(buf.view(), 6);
    ASSERT_TRUE(!view.is_ok());
    assert(error_of(view) == fed::FederationError::BadMagic);

    crucible::test::pass("  test_deserialize_rejects_bad_magic:             PASSED\n");
}

// The version check is strict equality, not a minimum.  A later version
// may move a field, and reading it under this layout would misinterpret
// the bytes rather than fail.

static void test_deserialize_rejects_unsupported_version() {
    HeaderSpec spec{};
    spec.protocol_version = 99u;  // not V2
    const Bytes<kHeader> buf = header_bytes(spec);

    auto view = fed::deserialize_untrusted_federation_entry(buf.view(), 6);
    ASSERT_TRUE(!view.is_ok());
    assert(error_of(view) == fed::FederationError::UnsupportedVersion);

    crucible::test::pass("  test_deserialize_rejects_unsupported_version:   PASSED\n");
}

// An entry of version 1 carried two 64-bit hashes from offset 8, so its
// bytes at the offsets of the kind and the width of a key hold a hash.  The
// reader refuses it for its version before it reads those bytes, and no
// reader of version 1 stays.

static void test_deserialize_rejects_version_one() {
    HeaderSpec spec{};
    spec.protocol_version = 1u;
    Bytes<kHeader> buf = header_bytes(spec);
    buf.at(index<12>()) = std::byte{0xEF};
    buf.at(index<13>()) = std::byte{0xBE};
    buf.at(index<46>()) = std::byte{0xAD};

    auto view = fed::deserialize_untrusted_federation_entry(buf.view(), 6);
    ASSERT_TRUE(!view.is_ok());
    assert(error_of(view) == fed::FederationError::UnsupportedVersion);

    crucible::test::pass("  test_deserialize_rejects_version_one:           PASSED\n");
}

// The kind and the width of the key name its hash.  A reader refuses each
// kind and each width that it does not know, so a peer with a different
// hash never lands in a slot of this hash.

static void test_deserialize_rejects_kind_and_width() {
    {
        Bytes<kHeader> buf = header_bytes(HeaderSpec{});
        buf.at(index<12>()) = std::byte{2};
        auto view = fed::deserialize_untrusted_federation_entry(buf.view(), 6);
        ASSERT_TRUE(!view.is_ok());
        assert(error_of(view) == fed::FederationError::UnsupportedHashKind);
    }

    {
        Bytes<kHeader> buf = header_bytes(HeaderSpec{});
        buf.at(index<12>()) = std::byte{0};
        auto view = fed::deserialize_untrusted_federation_entry(buf.view(), 6);
        ASSERT_TRUE(!view.is_ok());
        assert(error_of(view) == fed::FederationError::UnsupportedHashKind);
    }

    // A width of 16 bytes is the width that version 1 had in its two words.
    {
        Bytes<kHeader> buf = header_bytes(HeaderSpec{});
        buf.at(index<13>()) = std::byte{16};
        auto view = fed::deserialize_untrusted_federation_entry(buf.view(), 6);
        ASSERT_TRUE(!view.is_ok());
        assert(error_of(view) == fed::FederationError::UnsupportedKeyWidth);
    }

    // The kind comes first in the image, so a header with both faults gives
    // the fault of the kind.
    {
        Bytes<kHeader> buf = header_bytes(HeaderSpec{});
        buf.at(index<12>()) = std::byte{9};
        buf.at(index<13>()) = std::byte{64};
        auto view = fed::deserialize_untrusted_federation_entry(buf.view(), 6);
        ASSERT_TRUE(!view.is_ok());
        assert(error_of(view) == fed::FederationError::UnsupportedHashKind);
    }

    crucible::test::pass("  test_deserialize_rejects_kind_and_width:        PASSED\n");
}

// The reserved field is the extension point.  Rejecting a non-zero
// value is what lets a later version put a field there and still be
// sure that no older receiver quietly ignored it.

static void test_deserialize_rejects_reserved_nonzero() {
    {
        Bytes<kHeader> buf = header_bytes(HeaderSpec{});
        buf.at(index<46>()) = std::byte{0x01};
        auto view = fed::deserialize_untrusted_federation_entry(buf.view(), 6);
        ASSERT_TRUE(!view.is_ok());
        assert(error_of(view) == fed::FederationError::ReservedNonZero);
    }

    {
        Bytes<kHeader> buf = header_bytes(HeaderSpec{});
        buf.at(index<47>()) = std::byte{0x80};
        auto view = fed::deserialize_untrusted_federation_entry(buf.view(), 6);
        ASSERT_TRUE(!view.is_ok());
        assert(error_of(view) == fed::FederationError::ReservedNonZero);
    }

    crucible::test::pass("  test_deserialize_rejects_reserved_nonzero:      PASSED\n");
}

// Each entry carries the number of effect atoms its sender knew about.
// The rule is asymmetric: a sender that knew fewer atoms than the
// receiver is fine, because every atom it used is still known and still
// means the same thing.  A sender that knew more is refused, because it
// may have keyed the entry on an atom this receiver would silently read
// as a different one.

static void test_universe_cardinality_acceptance() {
    auto encode_with_stamp = [](std::uint16_t stamp) {
        HeaderSpec spec{};
        spec.universe_cardinality = stamp;
        return header_bytes(spec);
    };

    // Equal.
    {
        const Bytes<kHeader> buf = encode_with_stamp(6);
        auto view = fed::deserialize_untrusted_federation_entry(buf.view(), 6);
        ASSERT_TRUE(view.is_ok());
    }

    // Sender knew fewer.
    {
        const Bytes<kHeader> buf = encode_with_stamp(3);
        auto view = fed::deserialize_untrusted_federation_entry(buf.view(), 6);
        ASSERT_TRUE(view.is_ok());
    }

    // Sender knew more.
    {
        const Bytes<kHeader> buf = encode_with_stamp(7);
        auto view = fed::deserialize_untrusted_federation_entry(buf.view(), 6);
        ASSERT_TRUE(!view.is_ok());
        assert(error_of(view) == fed::FederationError::UniverseCardinalityTooHigh);
    }

    // A sender that used no atoms at all is acceptable to anyone.
    {
        const Bytes<kHeader> buf = encode_with_stamp(0);
        auto view = fed::deserialize_untrusted_federation_entry(buf.view(), 6);
        ASSERT_TRUE(view.is_ok());
    }

    crucible::test::pass("  test_universe_cardinality_acceptance:           PASSED\n");
}

// The same rule again as a pure predicate, which a caller can consult
// before building an entry rather than after decoding one.

static void test_accepts_cardinality_predicate() {
    static_assert(fed::federation_accepts_cardinality(0, 0));
    static_assert(fed::federation_accepts_cardinality(0, 6));
    static_assert(fed::federation_accepts_cardinality(3, 6));
    static_assert(fed::federation_accepts_cardinality(6, 6));
    static_assert(!fed::federation_accepts_cardinality(7, 6));
    static_assert(!fed::federation_accepts_cardinality(64, 6));
    static_assert(fed::federation_accepts_cardinality(64, 64));
    static_assert(!fed::federation_accepts_cardinality(65, 64));

    crucible::test::pass("  test_accepts_cardinality_predicate:             PASSED\n");
}

// The receiver repeats the sender's key checks, because the sender is
// another machine and may not have made them.

static void test_deserialize_rejects_sentinel_and_zero() {
    {
        HeaderSpec spec{};
        spec.key = fed::FederationKey::sentinel();
        const Bytes<kHeader> buf = header_bytes(spec);
        auto view = fed::deserialize_untrusted_federation_entry(buf.view(), 6);
        ASSERT_TRUE(!view.is_ok());
        assert(error_of(view) == fed::FederationError::SentinelKey);
    }

    {
        HeaderSpec spec{};
        spec.key = fed::FederationKey{};
        const Bytes<kHeader> buf = header_bytes(spec);
        auto view = fed::deserialize_untrusted_federation_entry(buf.view(), 6);
        ASSERT_TRUE(!view.is_ok());
        assert(error_of(view) == fed::FederationError::ZeroKey);
    }

    crucible::test::pass("  test_deserialize_rejects_sentinel_and_zero:     PASSED\n");
}

// A declared payload larger than the bytes that follow is refused, not
// clipped to what is there.

static void test_deserialize_rejects_truncated_payload() {
    Bytes<kHeader + 4> buf{};  // the header and 4 payload bytes
    HeaderSpec spec{};
    spec.payload_size = 100u;  // claims 100 bytes; only 4 remain
    const Bytes<kHeader> header = header_bytes(spec);
    ::fixy::copy(buf.view().window<kHeader>(0).expect("the buffer holds the header"), header.view());

    auto view = fed::deserialize_untrusted_federation_entry(buf.view(), 6);
    ASSERT_TRUE(!view.is_ok());
    assert(error_of(view) == fed::FederationError::TruncatedPayload);

    crucible::test::pass("  test_deserialize_rejects_truncated_payload:     PASSED\n");
}

// The encoder must have no hidden input.  Two calls with the same key
// and payload have to produce the same bytes, or a content-addressed
// cache built on those bytes stops being content-addressed.

static void test_serialize_is_deterministic() {
    const fed::FederationKey key = key_of(0xC0FFEEBA12345678ULL, 0xDEADBEEF56789ABCULL, 7, 8);
    const auto payload = bytes_of(0xAA, 0xBB, 0xCC, 0xDD);

    Bytes<kHeader + 16> buf_a{};
    Bytes<kHeader + 16> buf_b{};
    auto wa = fed::serialize_federation_entry(buf_a.view(), key, payload.view());
    auto wb = fed::serialize_federation_entry(buf_b.view(), key, payload.view());
    ASSERT_TRUE(wa.is_ok());
    ASSERT_TRUE(wb.is_ok());
    assert(value_of(wa) == value_of(wb));
    assert(::fixy::equal(prefix(buf_a, value_of(wa)), prefix(buf_b, value_of(wb))));

    crucible::test::pass("  test_serialize_is_deterministic:                PASSED\n");
}

// A caller that only wants the header must not get a weaker check than
// one that wants the whole entry.  The two decoders have to agree on
// every rejection, or peeking at a header would admit a stream the full
// decode would refuse.

static void test_header_overload_agreement() {
    {
        HeaderSpec spec{};
        spec.magic = 0xDEADBEEFu;
        const Bytes<kHeader> buf = header_bytes(spec);

        auto h = fed::deserialize_federation_header(buf.view(), 6);
        auto e = fed::deserialize_untrusted_federation_entry(buf.view(), 6);
        ASSERT_TRUE(!h.is_ok());
        ASSERT_TRUE(!e.is_ok());
        assert(error_of(h) == error_of(e));
        assert(error_of(h) == fed::FederationError::BadMagic);
    }

    {
        Bytes<kHeader> buf = header_bytes(HeaderSpec{});
        buf.at(index<13>()) = std::byte{31};
        auto h = fed::deserialize_federation_header(buf.view(), 6);
        auto e = fed::deserialize_untrusted_federation_entry(buf.view(), 6);
        assert(error_of(h) == error_of(e));
        assert(error_of(h) == fed::FederationError::UnsupportedKeyWidth);
    }

    {
        Bytes<kHeader> buf{};
        auto written = fed::serialize_federation_entry(buf.view(), HeaderSpec{}.key, kNoPayload);
        ASSERT_TRUE(written.is_ok());

        // The sender stamps the atom count of this build, so the receiver of
        // this build states the same count.
        auto h =
            fed::deserialize_federation_header(prefix(buf, value_of(written)), fed::FEDERATION_UNIVERSE_CARDINALITY);
        auto e = fed::deserialize_untrusted_federation_entry(prefix(buf, value_of(written)),
                                                             fed::FEDERATION_UNIVERSE_CARDINALITY);
        ASSERT_TRUE(h.is_ok());
        ASSERT_TRUE(e.is_ok());
        assert(value_of(h).key == value_of(e).header.key);
    }

    crucible::test::pass("  test_header_overload_agreement:                 PASSED\n");
}

// A new error added to the enum without a name lands on the unknown
// fallback, which this catches.

static void test_error_name_coverage() {
    using E = fed::FederationError;
    constexpr ::fixy::FixedArray<E, 12> all{
        E::None,
        E::BadMagic,
        E::UnsupportedVersion,
        E::UniverseCardinalityTooHigh,
        E::SentinelKey,
        E::ZeroKey,
        E::ReservedNonZero,
        E::TruncatedHeader,
        E::TruncatedPayload,
        E::OutputBufferTooSmall,
        E::UnsupportedHashKind,
        E::UnsupportedKeyWidth,
    };
    for (auto e : all) {
        const ::fixy::TextView nm = fed::federation_error_name(e);
        assert(nm.size() != 0);
        assert(nm != "<unknown FederationError>");
    }

    assert(fed::federation_error_name(E::None) == "None");
    assert(fed::federation_error_name(E::BadMagic) == "BadMagic");
    assert(fed::federation_error_name(E::UniverseCardinalityTooHigh) == "UniverseCardinalityTooHigh");
    assert(fed::federation_error_name(E::UnsupportedKeyWidth) == "UnsupportedKeyWidth");

    crucible::test::pass("  test_error_name_coverage:                       PASSED\n");
}

// The payload is every byte value once, so a codec that mangled one
// particular value has nowhere to hide.

static void test_round_trip_full_byte_range() {
    const fed::FederationKey key = key_of(0x123456789ABCDEF0ULL, 0xFEDCBA9876543210ULL, 1, 2);
    Bytes<256> payload{};
    std::size_t position = 0;
    for (std::byte& byte : payload) {
        byte = static_cast<std::byte>(position);
        ++position;
    }

    Bytes<fed::FEDERATION_HEADER_BYTES + 256> buf{};
    auto written = fed::serialize_federation_entry(buf.view(), key, payload.view());
    ASSERT_TRUE(written.is_ok());

    auto view = fed::deserialize_untrusted_federation_entry(prefix(buf, value_of(written)),
                                                            fed::FEDERATION_UNIVERSE_CARDINALITY);
    ASSERT_TRUE(view.is_ok());
    assert(::fixy::equal(value_of(view).payload, payload.view()));

    crucible::test::pass("  test_round_trip_full_byte_range:                PASSED\n");
}

// The decoded payload points into the caller's buffer rather than into
// a copy, which is what lets a receiver take a very large payload
// without allocating.  It also means the buffer has to outlive the view.

static void test_view_payload_aliases_input() {
    const auto payload = bytes_of(1, 2, 3, 4, 5, 6, 7, 8);
    Bytes<kHeader + 16> buf{};
    auto written = fed::serialize_federation_entry(buf.view(), HeaderSpec{}.key, payload.view());
    ASSERT_TRUE(written.is_ok());

    auto view = fed::deserialize_untrusted_federation_entry(prefix(buf, value_of(written)),
                                                            fed::FEDERATION_UNIVERSE_CARDINALITY);
    ASSERT_TRUE(view.is_ok());

    // The first byte of the payload is the byte of the buffer after the header.
    assert(::fixy::same_object(value_of(view).payload.get(0).expect("the payload holds eight bytes"),
                               buf.get(fed::FEDERATION_HEADER_BYTES).expect("the buffer holds the payload")));

    crucible::test::pass("  test_view_payload_aliases_input:                PASSED\n");
}

// The digest sits at offset 14 as its 32 bytes in the order of the
// output of the hash, so two digests that differ in one word differ on
// the wire in the bytes of that word.

static void test_digest_on_wire() {
    const fed::FederationKey key =
        key_of(0x1111111111111111ULL, 0x2222222222222222ULL, 0x3333333333333333ULL, 0x4444444444444444ULL);
    const fed::FederationKey swapped =
        key_of(0x2222222222222222ULL, 0x1111111111111111ULL, 0x3333333333333333ULL, 0x4444444444444444ULL);

    Bytes<kHeader> buf_n{};
    Bytes<kHeader> buf_s{};
    auto wn = fed::serialize_federation_entry(buf_n.view(), key, kNoPayload);
    auto ws = fed::serialize_federation_entry(buf_s.view(), swapped, kNoPayload);
    ASSERT_TRUE(wn.is_ok());
    ASSERT_TRUE(ws.is_ok());
    assert(value_of(wn) == value_of(ws));  // same byte count
    assert(!::fixy::equal(prefix(buf_n, value_of(wn)), prefix(buf_s, value_of(ws))));
    assert(::fixy::equal(prefix(buf_n, kHeader).window(14, 32).expect("the header holds the digest"),
                         ::fixy::View<std::byte const>{key.digest()}));
    // The last two words agree, so the last 16 bytes of the two digests
    // agree.
    assert(::fixy::equal(prefix(buf_n, kHeader).window(30, 16).expect("the header holds the digest"),
                         prefix(buf_s, kHeader).window(30, 16).expect("the header holds the digest")));

    crucible::test::pass("  test_digest_on_wire:                            PASSED\n");
}

// The receiver's cardinality is a parameter, not a compile-time
// constant read from this process.  That is what lets a caller decode
// on behalf of a peer, and what this test pins by driving one buffer
// against three different receiver values.

static void test_receiver_cardinality_is_explicit() {
    HeaderSpec spec{};
    spec.universe_cardinality = 10u;
    const Bytes<kHeader> buf = header_bytes(spec);

    auto rej = fed::deserialize_untrusted_federation_entry(buf.view(), 5);
    ASSERT_TRUE(!rej.is_ok());
    assert(error_of(rej) == fed::FederationError::UniverseCardinalityTooHigh);

    auto eq = fed::deserialize_untrusted_federation_entry(buf.view(), 10);
    ASSERT_TRUE(eq.is_ok());

    auto big = fed::deserialize_untrusted_federation_entry(buf.view(), 100);
    ASSERT_TRUE(big.is_ok());

    crucible::test::pass("  test_receiver_cardinality_is_explicit:          PASSED\n");
}

// The returned error is the only failure channel the codec has, so
// every entry point stays noexcept.

static void test_codec_is_noexcept() {
    Bytes<kHeader> buf{};
    const fed::FederationKey key = HeaderSpec{}.key;
    static_assert(noexcept(fed::serialize_federation_entry(buf.view(), key, kNoPayload)));
    static_assert(noexcept(fed::deserialize_federation_header(kNoPayload, std::uint16_t{6})));
    static_assert(noexcept(fed::deserialize_untrusted_federation_entry(kNoPayload, std::uint16_t{6})));
    static_assert(noexcept(fed::federation_accepts_cardinality(std::uint16_t{6}, std::uint16_t{6})));
    static_assert(noexcept(fed::federation_error_name(fed::FederationError::None)));
    static_assert(noexcept(fed::federation_key_of_bytes(test_hash_key(), kNoPayload)));

    crucible::test::pass("  test_codec_is_noexcept:                         PASSED\n");
}

// The cardinality field is 16 bits, so the largest value it can hold is
// the interesting case for the comparison it feeds.

static void test_cardinality_boundary_uint16_max() {
    constexpr std::uint16_t MAX_CARD = 0xFFFFu;

    static_assert(fed::federation_accepts_cardinality(MAX_CARD, MAX_CARD));
    static_assert(fed::federation_accepts_cardinality(0, MAX_CARD));
    static_assert(!fed::federation_accepts_cardinality(MAX_CARD, MAX_CARD - 1));

    HeaderSpec spec{};
    spec.universe_cardinality = MAX_CARD;
    const Bytes<kHeader> buf = header_bytes(spec);

    auto accept = fed::deserialize_untrusted_federation_entry(buf.view(), MAX_CARD);
    ASSERT_TRUE(accept.is_ok());
    assert(value_of(accept).header.stamp.universe_cardinality == MAX_CARD);

    auto reject_one_below = fed::deserialize_untrusted_federation_entry(buf.view(), MAX_CARD - 1u);
    ASSERT_TRUE(!reject_one_below.is_ok());
    assert(error_of(reject_one_below) == fed::FederationError::UniverseCardinalityTooHigh);

    crucible::test::pass("  test_cardinality_boundary_uint16_max:           PASSED\n");
}

// An exactly-sized buffer must be accepted.  An off-by-one in the size
// check would refuse it, and the refusal reads as a legitimate
// too-small error rather than as a bug.

static void test_buffer_exactly_fits() {
    const fed::FederationKey key = key_of(0x1111222233334444ULL, 0x5555666677778888ULL, 0, 1);
    const auto payload = bytes_of(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);

    Bytes<kHeader + 16> buf{};  // 48 + 16, exactly
    auto written = fed::serialize_federation_entry(buf.view(), key, payload.view());
    ASSERT_TRUE(written.is_ok());
    assert(value_of(written) == kHeader + 16);

    Bytes<kHeader + 15> short_buf{};
    auto rejected = fed::serialize_federation_entry(short_buf.view(), key, payload.view());
    ASSERT_TRUE(!rejected.is_ok());
    assert(error_of(rejected) == fed::FederationError::OutputBufferTooSmall);

    // The same boundary with no payload at all.
    Bytes<kHeader> tight_buf{};
    auto tight = fed::serialize_federation_entry(tight_buf.view(), key, kNoPayload);
    ASSERT_TRUE(tight.is_ok());
    assert(value_of(tight) == kHeader);

    crucible::test::pass("  test_buffer_exactly_fits:                       PASSED\n");
}

// A transport that batches several entries into one buffer hands the
// decoder more bytes than the first entry occupies.  The payload span
// has to stop at the declared size, or the first entry would swallow
// the second.

static void test_extra_bytes_at_end() {
    const auto payload = bytes_of(0xAA, 0xBB, 0xCC, 0xDD);

    Bytes<kHeader + 16> buf{};
    auto written = fed::serialize_federation_entry(buf.view(), HeaderSpec{}.key, payload.view());
    ASSERT_TRUE(written.is_ok());
    assert(value_of(written) == kHeader + 4);

    // A value that appears nowhere in the payload, so its absence from
    // the decoded payload is meaningful.
    ::fixy::fill(
        buf.view().window(kHeader + 4, buf.size() - kHeader - 4).expect("the buffer holds bytes after the entry"),
        std::byte{0xEE});

    // Decode over the whole buffer, not over the bytes just written.
    auto view = fed::deserialize_untrusted_federation_entry(buf.view(), fed::FEDERATION_UNIVERSE_CARDINALITY);
    ASSERT_TRUE(view.is_ok());
    assert(value_of(view).header.payload_size == 4u);
    assert(::fixy::equal(value_of(view).payload, payload.view()));

    crucible::test::pass("  test_extra_bytes_at_end:                        PASSED\n");
}

// Only the full sentinel and the full zero digest are refused.  A digest
// with one byte away from each is a legal key: improbable under any decent
// hash, but nothing forbids it, and refusing it would drop real entries.

static void test_partial_sentinel_accepted() {
    constexpr std::uint64_t all_ones = ::fixy::max_of<std::uint64_t>;

    {
        const fed::FederationKey key = key_of(all_ones, all_ones, all_ones, 0x7FFFFFFFFFFFFFFFULL);
        assert(!key.is_sentinel());
        assert(!key.is_zero());

        Bytes<kHeader> buf{};
        auto written = fed::serialize_federation_entry(buf.view(), key, kNoPayload);
        ASSERT_TRUE(written.is_ok());

        auto view = fed::deserialize_untrusted_federation_entry(buf.view(), fed::FEDERATION_UNIVERSE_CARDINALITY);
        ASSERT_TRUE(view.is_ok());
        assert(value_of(view).header.key == key);
    }

    {
        const fed::FederationKey key = key_of(0, 0, 0, 1);
        assert(!key.is_sentinel());
        assert(!key.is_zero());

        Bytes<kHeader> buf{};
        auto written = fed::serialize_federation_entry(buf.view(), key, kNoPayload);
        ASSERT_TRUE(written.is_ok());
    }

    crucible::test::pass("  test_partial_sentinel_accepted:                 PASSED\n");
}

// The federation key of a content is the keyed hash of BLAKE3 under the
// key of the federation.  With the key and the input of the official
// vectors (the byte i % 251 at i), the digest is the official output.

static void test_key_is_keyed_blake3() {
    Bytes<64> input{};
    std::size_t position = 0;
    for (std::byte& byte : input) {
        byte = static_cast<std::byte>(position % 251);
        ++position;
    }
    const fed::FederationKey key = fed::federation_key_of_bytes(test_hash_key(), input.view());
    // The vector of 64 bytes: ba8ced36f327700d213f120b1a207a3b8c04330528586f414d09f2f7d9ccb7e6.
    const fed::FederationKey expected =
        key_of(0x0d7027f336ed8cbaULL, 0x3b7a201a0b123f21ULL, 0x416f58280533048cULL, 0xe6b7ccd9f7f2094dULL);
    assert(key == expected);
    assert(key.kind() == fed::FederationHashKind::blake3_keyed);
    assert(key.width() == fed::FederationKeyWidth::bytes_32);

    crucible::test::pass("  test_key_is_keyed_blake3:                       PASSED\n");
}

// Two values with one 64-bit id get two federation keys.  The 64-bit hash
// of the Protocol family folds a block of 16 bytes as the folded product
// of its two words, each xored with a seed, and a product does not tell
// its two factors apart.  So a value and the value with its two words
// swapped through the seeds have one 64-bit id.  The keyed hash reads the
// bytes of the image, and the two images differ.

struct[[= ::fixy::derive_protocol{}]] TwoWords {
    std::uint64_t low = 0;
    std::uint64_t high = 0;
};

static void test_one_id_two_keys() {
    constexpr std::uint64_t start_seed = 0x13198a2e03707344ULL;
    constexpr std::uint64_t fold_seed = 0x243f6a8885a308d3ULL;
    const TwoWords first{.low = 1, .high = 2};
    const TwoWords second{.low = 2 ^ fold_seed ^ start_seed, .high = 1 ^ start_seed ^ fold_seed};

    // The positive control: the two values differ and share one 64-bit id.
    assert(!::fixy::equal(first, second));
    assert(::fixy::hash(first).raw() == ::fixy::hash(second).raw());

    const fed::FederationKey first_key = fed::federation_key_of(test_hash_key(), first);
    const fed::FederationKey second_key = fed::federation_key_of(test_hash_key(), second);
    assert(!(first_key == second_key));
    assert(!first_key.is_zero() && !second_key.is_zero());

    crucible::test::pass("  test_one_id_two_keys:                           PASSED\n");
}

// Two keys of two federations differ for one content, and a key changes
// when one byte of the content changes.

static void test_key_and_content_take_part() {
    const auto content = bytes_of(1, 2, 3, 4, 5, 6, 7, 8);
    Bytes<32> other_key_bytes{};
    other_key_bytes.at(index<0>()) = std::byte{1};
    const fed::FederationHashKey other_hash_key{::fixy::View<std::byte const, 32>{other_key_bytes.view()}};

    const fed::FederationKey key = fed::federation_key_of_bytes(test_hash_key(), content.view());
    assert(!(key == fed::federation_key_of_bytes(other_hash_key, content.view())));
    const auto changed = bytes_of(1, 2, 3, 4, 5, 6, 7, 9);
    assert(!(key == fed::federation_key_of_bytes(test_hash_key(), changed.view())));
    assert(key == fed::federation_key_of_bytes(test_hash_key(), content.view()));

    crucible::test::pass("  test_key_and_content_take_part:                 PASSED\n");
}

// The query side folds at compile time.

namespace constexpr_witnesses {
static_assert(fed::federation_accepts_cardinality(0, 0));
static_assert(fed::federation_accepts_cardinality(6, 6));
static_assert(!fed::federation_accepts_cardinality(7, 6));

static_assert(fed::federation_error_name(fed::FederationError::None) == "None");
static_assert(fed::federation_error_name(fed::FederationError::BadMagic) == "BadMagic");
static_assert(fed::federation_error_name(fed::FederationError::UniverseCardinalityTooHigh)
              == "UniverseCardinalityTooHigh");

static_assert(fed::FEDERATION_HEADER_BYTES == 48u);

// The keyed hash runs in a constant evaluation, and it gives the bits of
// the run-time arm.
static_assert(!fed::federation_key_of(test_hash_key(), TwoWords{.low = 1, .high = 2}).is_zero());
}  // namespace constexpr_witnesses

// The same calls again at runtime, so that making either function
// non-constexpr breaks the assertions above without silently removing
// the check itself.
static void test_constexpr_witnesses() {
    [[maybe_unused]] auto card_check = fed::federation_accepts_cardinality(6, 6);
    [[maybe_unused]] auto name_check = fed::federation_error_name(fed::FederationError::None);
    assert(card_check);
    assert(name_check == "None");
    constexpr fed::FederationKey constant_key = fed::federation_key_of(test_hash_key(), TwoWords{.low = 1, .high = 2});
    assert(constant_key == fed::federation_key_of(test_hash_key(), TwoWords{.low = 1, .high = 2}));

    crucible::test::pass("  test_constexpr_witnesses:                       PASSED\n");
}

// A federation stream and a graph snapshot are told apart by their
// magic alone.  If the two constants ever became equal, each stream
// would dispatch to the other's decoder, so this file pulls in both
// headers to pin the inequality in one place.

static void test_magic_collision_with_cdag() {
    static_assert(fed::FEDERATION_MAGIC != crucible::CDAG_MAGIC, "FEDERATION_MAGIC must not collide with CDAG_MAGIC.");

    // The first byte on the wire already differs, so a decoder that
    // reads one byte at a time rejects the wrong stream immediately.
    constexpr auto fed_first_byte = static_cast<std::uint8_t>(fed::FEDERATION_MAGIC & 0xFFu);
    constexpr auto cdag_first_byte = static_cast<std::uint8_t>(crucible::CDAG_MAGIC & 0xFFu);
    static_assert(fed_first_byte == 'C');
    static_assert(cdag_first_byte == 'G');
    static_assert(fed_first_byte != cdag_first_byte);

    crucible::test::pass("  test_magic_collision_with_cdag:                 PASSED\n");
}

static void test_field_width_pins() {
    static_assert(sizeof(fed::FederationEntryStamp::magic) == 4);
    static_assert(sizeof(fed::FederationEntryStamp::protocol_version) == 2);
    static_assert(sizeof(fed::FederationEntryStamp::universe_cardinality) == 2);
    static_assert(sizeof(fed::FederationEntryHeader::payload_size) == 4);
    static_assert(sizeof(fed::FederationKey) == 34);
    static_assert(sizeof(fed::FederationEntryHeader::reserved) == 2);

    // The widths sum to the header size, so the layout carries no
    // padding.
    static_assert(4 + 2 + 2 + 4 + 34 + 2 == 48);

    crucible::test::pass("  test_field_width_pins:                          PASSED\n");
}

// The payload alternates two values, so a copy shifted by one byte
// comes back inverted rather than equal.

static void test_heap_buffer_round_trip() {
    const fed::FederationKey key = key_of(0x0F0F0F0F0F0F0F0FULL, 0xF0F0F0F0F0F0F0F0ULL, 5, 6);
    const auto alloc = ::foundation::effects::testing::test().alloc;

    ::fixy::HeapArray<std::byte> payload = ::fixy::mint_heap_array(alloc, 1024, std::byte{0x55});
    bool is_odd = false;
    for (std::byte& byte : payload.view()) {
        if (is_odd) byte = std::byte{0xAA};
        is_odd = !is_odd;
    }

    ::fixy::HeapArray<std::byte> buf =
        ::fixy::mint_heap_array(alloc, fed::FEDERATION_HEADER_BYTES + payload.size(), std::byte{0});
    auto written = fed::serialize_federation_entry(buf.view(), key, payload.view());
    ASSERT_TRUE(written.is_ok());
    assert(value_of(written) == buf.size());

    auto view = fed::deserialize_untrusted_federation_entry(buf.view(), fed::FEDERATION_UNIVERSE_CARDINALITY);
    ASSERT_TRUE(view.is_ok());
    assert(::fixy::equal(value_of(view).payload, payload.view()));

    crucible::test::pass("  test_heap_buffer_round_trip:                    PASSED\n");
}

// The permissioned decode takes a peer token from the federation door of
// fixy/Federation.h, and the tag of the view is the tag of that token.
// The organization is a class at namespace scope with external linkage,
// because the reflection hash that names it on the wire refuses a class
// with internal linkage.

struct FederationProtocolTestOrg {};

static void test_permissioned_decode_tags_the_view() {
    namespace fe = ::foundation::effects;
    namespace fp = ::foundation::permissions;
    namespace door = ::fixy::federation;
    using ::foundation::core::SipKeyBytes;
    using IoCtx = fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::IO>>;
    using Admission = fp::FederationAdmission<FederationProtocolTestOrg>;
    using Policy = door::policy::admit_orgs<FederationProtocolTestOrg>;

    const IoCtx ctx{fe::testing::bg()};
    SipKeyBytes shared_key{};
    std::uint8_t next = 1;
    for (std::byte& byte : shared_key) {
        byte = static_cast<std::byte>(next);
        ++next;
    }
    Admission admission =
        Admission::mint_federation_admission<Policy>(ctx, ::fixy::mint_secret<SipKeyBytes>(shared_key));
    auto local_cipher = fp::mint_permission_root<door::LocalCipherTag>(ctx);
    auto [peer_key [[maybe_unused]], handshake] = door::sign_handshake<FederationProtocolTestOrg>(
        ::fixy::mint_secret<SipKeyBytes>(shared_key), door::PeerKeyFingerprint{42}, door::Nonce{1});
    auto [returned_cipher [[maybe_unused]], admitted] =
        admission.mint_federation_admittance(std::move(local_cipher), handshake);
    auto peer = std::move(admitted).expect("the shared key verifies the handshake");

    const fed::FederationKey key = key_of(0x1111111111111111ULL, 0x2222222222222222ULL, 0, 0);
    const auto body = bytes_of(1, 2, 3, 4);
    Bytes<kHeader + 16> buf{};
    auto written = fed::serialize_federation_entry(buf.view(), key, body.view());
    ASSERT_TRUE(written.is_ok());

    auto tagged =
        fed::deserialize_federation_entry(peer, prefix(buf, value_of(written)), fed::FEDERATION_UNIVERSE_CARDINALITY);
    ASSERT_TRUE(tagged.is_ok());
    using TaggedView = std::remove_cvref_t<decltype(value_of(tagged))>;
    static_assert(std::is_same_v<typename TaggedView::tag_type, fp::tag::FederatedPeer<FederationProtocolTestOrg>>);

    const fed::FederationEntryView view = value_of(tagged).value();
    assert(view.header.key == key);
    assert(::fixy::equal(view.payload, body.view()));

    crucible::test::pass("  permissioned_decode_tags_the_view:               PASSED\n");
}

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_federation_protocol — wire-format witness\n");
    test_header_layout_invariants();
    test_magic_byte_order();
    test_version_byte_order();
    test_round_trip_basic();
    test_round_trip_empty_payload();
    test_serialize_rejects_sentinel();
    test_serialize_rejects_zero();
    test_serialize_rejects_undersized_buffer();
    test_deserialize_rejects_truncated_header();
    test_deserialize_rejects_bad_magic();
    test_deserialize_rejects_unsupported_version();
    test_deserialize_rejects_version_one();
    test_deserialize_rejects_kind_and_width();
    test_deserialize_rejects_reserved_nonzero();
    test_universe_cardinality_acceptance();
    test_accepts_cardinality_predicate();
    test_deserialize_rejects_sentinel_and_zero();
    test_deserialize_rejects_truncated_payload();
    test_serialize_is_deterministic();
    test_header_overload_agreement();
    test_error_name_coverage();
    test_round_trip_full_byte_range();
    test_view_payload_aliases_input();
    test_digest_on_wire();
    test_receiver_cardinality_is_explicit();
    test_codec_is_noexcept();
    test_permissioned_decode_tags_the_view();
    test_cardinality_boundary_uint16_max();
    test_buffer_exactly_fits();
    test_extra_bytes_at_end();
    test_partial_sentinel_accepted();
    test_key_is_keyed_blake3();
    test_one_id_two_keys();
    test_key_and_content_take_part();
    test_constexpr_witnesses();
    test_magic_collision_with_cdag();
    test_field_width_pins();
    test_heap_buffer_round_trip();
    crucible::test::pass("test_federation_protocol: 38 groups, all passed\n");
    return 0;
}
