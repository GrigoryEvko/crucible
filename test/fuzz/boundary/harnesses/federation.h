#pragma once

// A federation entry from a peer.  The first two bytes of the input choose
// the receiver's atom count, so the fuzzer also plays an older and a newer
// peer.  An accepted entry names a real key, its payload lies inside the
// input, and the entry writes back to bytes that read to the same key and
// payload.

#include "../harness.h"

#include <crucible/cipher/FederationProtocol.h>
#include <foundation/effects/Effect.h>

#include <algorithm>
#include <vector>

namespace crucible::fuzz::boundary {

// The atom count of this build's effect universe, which is what a receiver
// states when it reads a federation entry.
inline constexpr auto kReceiverAtomCount = static_cast<std::uint16_t>(::foundation::effects::effect_count);

[[nodiscard]] inline Seeds seeds_federation() {
    namespace fed = ::crucible::cipher::federation;
    Seeds seeds;
    for (std::string_view payload : {std::string_view{}, std::string_view{"payload bytes"}}) {
        std::vector<std::uint8_t> seed;
        append_raw(seed, kReceiverAtomCount);
        std::vector<std::uint8_t> entry(fed::FEDERATION_HEADER_BYTES + payload.size());
        const auto body = text_bytes(payload);
        auto written = fed::serialize_federation_entry(std::span<std::uint8_t>{entry},
                                                       KernelCacheKey{ContentHash{0x1234}, RowHash{0x5678}}, body);
        if (written.is_ok()) entry.resize(std::move(written).expect("the seed entry fits its buffer"));
        append_bytes(seed, entry);
        seeds.push_back(std::move(seed));
    }
    return seeds;
}

inline void run_federation(std::span<const std::uint8_t> bytes) {
    namespace fed = ::crucible::cipher::federation;
    ByteCursor cursor{bytes};
    const auto receiver_cardinality = cursor.take<std::uint16_t>();
    const auto entry_bytes = cursor.rest();

    auto decoded = fed::deserialize_untrusted_federation_entry(entry_bytes, receiver_cardinality);
    if (decoded.is_err()) return;
    const fed::FederationEntryView view = std::move(decoded).expect("the entry decoded");

    const KernelCacheKey key{view.header.content_hash, view.header.row_hash};
    CRUCIBLE_FUZZ_CLAIM("federation", !key.is_zero() && !key.is_sentinel());
    CRUCIBLE_FUZZ_CLAIM("federation", view.header.universe_cardinality <= receiver_cardinality);
    CRUCIBLE_FUZZ_CLAIM("federation", view.payload.size() == view.header.payload_size);
    CRUCIBLE_FUZZ_CLAIM(
        "federation", view.payload.empty()
                          || (view.payload.data() >= entry_bytes.data()
                              && view.payload.data() + view.payload.size() <= entry_bytes.data() + entry_bytes.size()));

    std::vector<std::uint8_t> out(fed::FEDERATION_HEADER_BYTES + view.payload.size());
    auto written = fed::serialize_federation_entry(std::span<std::uint8_t>{out}, key, view.payload);
    CRUCIBLE_FUZZ_CLAIM("federation", written.is_ok());
    const std::size_t written_bytes = std::move(written).expect("an accepted entry writes back");
    auto read_again = fed::deserialize_untrusted_federation_entry(
        std::span<const std::uint8_t>{out.data(), written_bytes}, kReceiverAtomCount);
    CRUCIBLE_FUZZ_CLAIM("federation", read_again.is_ok());
    const fed::FederationEntryView again = std::move(read_again).expect("the written entry reads back");
    CRUCIBLE_FUZZ_CLAIM("federation", again.header.content_hash == view.header.content_hash);
    CRUCIBLE_FUZZ_CLAIM("federation", again.header.row_hash == view.header.row_hash);
    CRUCIBLE_FUZZ_CLAIM("federation", std::ranges::equal(again.payload, view.payload));
}

}  // namespace crucible::fuzz::boundary
