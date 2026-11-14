#pragma once

// A federation entry from a peer.  The first two bytes of the input choose
// the receiver's atom count, so the fuzzer also plays an older and a newer
// peer.  An accepted entry names a real key, its payload lies inside the
// input, and the entry writes back to bytes that read to the same key and
// payload.

#include "../harness.h"

#include <crucible/cipher/FederationProtocol.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>

#include <vector>

namespace crucible::fuzz::boundary {

// The atom count of this build's effect universe, which is what a receiver
// states when it reads a federation entry.
inline constexpr auto kReceiverAtomCount = static_cast<std::uint16_t>(::foundation::effects::effect_count);

[[nodiscard]] inline Seeds seeds_federation() {
    namespace fed = ::crucible::cipher::federation;
    Seeds seeds;
    for (const ::fixy::TextView payload : {::fixy::TextView{}, ::fixy::TextView{"payload bytes"}}) {
        ::foundation::core::FixedArray<std::byte, 16> body{};
        std::size_t count = 0;
        for (const char character : payload) {
            *body.get(count).expect("a seed payload fits 16 bytes") = static_cast<std::byte>(character);
            ++count;
        }
        ::foundation::core::FixedArray<std::byte, 64> entry{};
        auto written = fed::serialize_federation_entry(
            entry.view(), KernelCacheKey{ContentHash{0x1234}, RowHash{0x5678}},
            ::fixy::View<std::byte const>{body.view()}.window(0, count).expect("the payload is inside its buffer"));
        const std::size_t written_bytes = std::move(written).expect("the seed entry fits its buffer");
        std::vector<std::uint8_t> seed;
        append_raw(seed, kReceiverAtomCount);
        for (const std::byte byte : ::fixy::View<std::byte const>{entry.view()}
                                        .window(0, written_bytes)
                                        .expect("the entry is inside its buffer")) {
            seed.push_back(static_cast<std::uint8_t>(byte));
        }
        seeds.push_back(std::move(seed));
    }
    return seeds;
}

inline void run_federation(std::span<const std::uint8_t> bytes) {
    namespace fed = ::crucible::cipher::federation;
    ByteCursor cursor{bytes};
    const auto receiver_cardinality = cursor.take<std::uint16_t>();
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the fuzzer gives its input as a pointer to raw bytes and a count")
    const std::span<const std::byte> raw = as_bytes_view(cursor.rest());
    const ::fixy::View<std::byte const> entry_bytes = ::fixy::mint_foreign_view(raw.data(), raw.size());
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

    auto decoded = fed::deserialize_untrusted_federation_entry(entry_bytes, receiver_cardinality);
    if (decoded.is_err()) return;
    const fed::FederationEntryView view = std::move(decoded).expect("the entry decoded");

    const KernelCacheKey key{view.header.content_hash, view.header.row_hash};
    CRUCIBLE_FUZZ_CLAIM("federation", !key.is_zero() && !key.is_sentinel());
    CRUCIBLE_FUZZ_CLAIM("federation", view.header.universe_cardinality <= receiver_cardinality);
    CRUCIBLE_FUZZ_CLAIM("federation", view.payload.size() == view.header.payload_size);
    // The payload lies inside the input, from the first byte after the
    // header.
    CRUCIBLE_FUZZ_CLAIM("federation", view.payload.size() <= entry_bytes.size() - fed::FEDERATION_HEADER_BYTES);
    CRUCIBLE_FUZZ_CLAIM("federation",
                        view.payload.size() == 0
                            || ::fixy::same_object(
                                view.payload.get(0).expect("the payload holds a byte"),
                                entry_bytes.get(fed::FEDERATION_HEADER_BYTES).expect("the input holds the payload")));

    ::fixy::HeapArray<std::byte> out =
        ::fixy::mint_heap_array(test_alloc(), fed::FEDERATION_HEADER_BYTES + view.payload.size(), std::byte{0});
    auto written = fed::serialize_federation_entry(out.view(), key, view.payload);
    CRUCIBLE_FUZZ_CLAIM("federation", written.is_ok());
    CRUCIBLE_FUZZ_CLAIM("federation", std::move(written).expect("an accepted entry writes back") == out.size());
    auto read_again = fed::deserialize_untrusted_federation_entry(out.view(), kReceiverAtomCount);
    CRUCIBLE_FUZZ_CLAIM("federation", read_again.is_ok());
    const fed::FederationEntryView again = std::move(read_again).expect("the written entry reads back");
    CRUCIBLE_FUZZ_CLAIM("federation", again.header.content_hash == view.header.content_hash);
    CRUCIBLE_FUZZ_CLAIM("federation", again.header.row_hash == view.header.row_hash);
    CRUCIBLE_FUZZ_CLAIM("federation", ::fixy::equal(again.payload, view.payload));
}

}  // namespace crucible::fuzz::boundary
