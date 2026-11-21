#pragma once

// Fountain packets from the network.  Each packet field comes from the
// input, the way a wire decoder would fill it.  The decoder must survive
// any packet sequence, and once it reports Complete, the decoded span holds
// exactly the source byte count that the packets declared.

#include "../harness.h"

#include <crucible/cntp/Fountain.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::fuzz::boundary {

// Eight systematic packets, one per source symbol, so the decoder
// completes on the last one.
[[nodiscard]] inline SeedList seeds_fountain() noexcept {
    SeedWriter seed{};
    for (std::uint32_t symbol = 0; symbol < 8; ++symbol) {
        seed.append_raw(symbol);
        seed.append_raw(std::uint16_t{8});
        seed.append_raw(std::uint16_t{16});
        seed.append_raw(std::uint16_t{128});
        seed.append_raw(std::uint64_t{1} << symbol);
        for (std::uint8_t b = 0; b < 16; ++b)
            seed.append_raw(static_cast<std::uint8_t>(symbol * 16 + b));
    }
    SeedList seeds = empty_seed_list();
    add_seed(seeds, seed.finish());
    return seeds;
}

inline void run_fountain(::fixy::View<std::byte const> bytes) noexcept {
    using Decoder = ::crucible::cntp::FountainDecoder<8, 16>;
    using Packet = Decoder::packet_type;

    const ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    Decoder decoder = ::crucible::cntp::mint_fountain_decoder<8, 16>(init);
    InputCursor cursor{bytes};
    std::size_t declared_source_bytes = 0;
    while (cursor.remaining() >= 8) {
        Packet packet{};
        packet.encoding_id = cursor.take<std::uint32_t>();
        packet.symbol_count = cursor.take<std::uint16_t>();
        packet.bytes_per_symbol = cursor.take<std::uint16_t>();
        // A wire decoder admits the count before it builds the packet.
        const auto source_bytes = std::size_t{cursor.take<std::uint16_t>()};
        auto admitted = Packet::admit_source_bytes(source_bytes);
        if (admitted.is_err()) continue;
        packet.source_bytes = std::move(admitted).expect("the admission took the count");
        packet.mask = cursor.take<std::uint64_t>();
        const ::fixy::View<std::byte const> payload = cursor.take_bytes(packet.payload.size());
        for (auto index : packet.payload.indices()) {
            if (index.raw() >= payload.size()) break;
            packet.payload.with_element(index, [payload, index](std::byte& element) noexcept {
                element = *payload.get(index.raw()).expect("the index is below the count of the payload");
            });
        }

        auto state = decoder.add_packet(packet);
        if (state.is_err()) continue;
        declared_source_bytes = source_bytes;
        if (std::move(state).expect("the decoder took the packet") == ::crucible::cntp::FountainDecodeState::Complete) {
            auto decoded = decoder.extract_decoded();
            CRUCIBLE_FUZZ_CLAIM("fountain", decoded.is_ok());
            const auto decoded_bytes =
                decoded.match([](::fixy::View<std::byte const> view) noexcept { return view.size(); },
                              [](::crucible::cntp::FountainError) noexcept { return std::size_t{0}; });
            CRUCIBLE_FUZZ_CLAIM("fountain", decoded_bytes <= Decoder::max_source_bytes);
            CRUCIBLE_FUZZ_CLAIM("fountain", decoded_bytes == declared_source_bytes);
            return;
        }
    }
}

}  // namespace crucible::fuzz::boundary
