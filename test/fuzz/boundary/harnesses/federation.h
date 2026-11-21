#pragma once

// A federation entry from a peer.  The first two bytes of the input choose
// the receiver's atom count, so the fuzzer also plays an older and a newer
// peer.  An accepted entry names a real key, its payload lies inside the
// input, and the entry writes back to bytes that read to the same key and
// payload.

#include "../harness.h"

#include <crucible/cipher/FederationProtocol.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::fuzz::boundary {

// The atom count of this build's effect universe, which is what a receiver
// states when it reads a federation entry.
inline constexpr auto kReceiverAtomCount = static_cast<std::uint16_t>(::foundation::effects::effect_count);

[[nodiscard]] inline SeedList seeds_federation() noexcept {
    namespace fed = ::crucible::cipher::federation;
    SeedList seeds = empty_seed_list();
    const ::fixy::FixedArray<::fixy::TextView, 2> payloads{::fixy::TextView{}, ::fixy::TextView{"payload bytes"}};
    for (const ::fixy::TextView payload : payloads) {
        ::foundation::core::FixedArray<std::byte, 16> body{};
        std::size_t count = 0;
        body.with_mut([payload, &count](auto run) noexcept {
            for (const char character : payload) {
                *run.get(count).expect("a seed payload fits 16 bytes") = static_cast<std::byte>(character);
                ++count;
            }
        });
        ::foundation::core::FixedArray<std::byte, fed::FEDERATION_HEADER_BYTES + 16> entry{};
        auto written = entry.with_mut([&body, count](auto run) noexcept {
            return fed::serialize_federation_entry(
                run.view(), fed::FederationKey{::foundation::core::Hash256{0x1234, 0x5678, 0x9abc, 0xdef0}},
                ::fixy::View<std::byte const>{body.view()}.window(0, count).expect("the payload is inside its buffer"));
        });
        const std::size_t written_bytes = std::move(written).expect("the seed entry fits its buffer");
        SeedWriter seed{};
        seed.append_raw(kReceiverAtomCount);
        seed.append_bytes(::fixy::View<std::byte const>{entry.view()}
                              .window(0, written_bytes)
                              .expect("the entry is inside its buffer"));
        add_seed(seeds, seed.finish());
    }
    return seeds;
}

inline void run_federation(::fixy::View<std::byte const> bytes) noexcept {
    namespace fed = ::crucible::cipher::federation;
    InputCursor cursor{bytes};
    const auto receiver_cardinality = cursor.take<std::uint16_t>();
    const ::fixy::View<std::byte const> entry_bytes = cursor.rest();

    auto decoded = fed::deserialize_untrusted_federation_entry(entry_bytes, receiver_cardinality);
    if (decoded.is_err()) return;
    const fed::FederationEntryView view = std::move(decoded).expect("the entry decoded");

    const fed::FederationKey key = view.header.key;
    CRUCIBLE_FUZZ_CLAIM("federation", !key.is_zero() && !key.is_sentinel());
    CRUCIBLE_FUZZ_CLAIM("federation", key.kind() == fed::FederationHashKind::blake3_keyed
                                          && key.width() == fed::FederationKeyWidth::bytes_32);
    CRUCIBLE_FUZZ_CLAIM("federation", view.header.stamp.universe_cardinality <= receiver_cardinality);
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
    CRUCIBLE_FUZZ_CLAIM("federation", again.header.key == view.header.key);
    CRUCIBLE_FUZZ_CLAIM("federation", ::fixy::equal(again.payload, view.payload));
}

}  // namespace crucible::fuzz::boundary
