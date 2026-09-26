#pragma once

// Fountain packets from the network.  Each packet field comes from the
// input, the way a wire decoder would fill it.  The decoder must survive
// any packet sequence, and once it reports Complete, the decoded span holds
// exactly the source byte count that the packets declared.

#include "../harness.h"

#include <crucible/cntp/Fountain.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

namespace crucible::fuzz::boundary {

// Eight systematic packets, one per source symbol, so the decoder
// completes on the last one.
[[nodiscard]] inline Seeds seeds_fountain() {
    std::vector<std::uint8_t> seed;
    for (std::uint32_t symbol = 0; symbol < 8; ++symbol) {
        append_raw(seed, symbol);
        append_raw(seed, std::uint16_t{8});
        append_raw(seed, std::uint16_t{16});
        append_raw(seed, std::uint16_t{128});
        append_raw(seed, std::uint64_t{1} << symbol);
        for (std::uint8_t b = 0; b < 16; ++b) seed.push_back(static_cast<std::uint8_t>(symbol * 16 + b));
    }
    return {seed};
}

inline void run_fountain(std::span<const std::uint8_t> bytes) {
    using Decoder = ::crucible::cntp::FountainDecoder<8, 16>;
    using Packet = Decoder::packet_type;

    const ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    Decoder decoder = ::crucible::cntp::mint_fountain_decoder<8, 16>(init);
    ByteCursor cursor{bytes};
    std::size_t declared_source_bytes = 0;
    while (cursor.remaining() >= 8) {
        Packet packet{};
        packet.encoding_id = cursor.take<std::uint32_t>();
        packet.symbol_count = cursor.take<std::uint16_t>();
        packet.bytes_per_symbol = cursor.take<std::uint16_t>();
        // A wire decoder admits the count before it builds the packet.
        const auto source_bytes = std::size_t{cursor.take<std::uint16_t>()};
        auto admitted = Packet::admit_source_bytes(source_bytes);
        if (!admitted) continue;
        packet.source_bytes = *admitted;
        packet.mask = cursor.take<std::uint64_t>();
        const auto payload = cursor.take_bytes(packet.payload.size());
        for (std::size_t i = 0; i < payload.size(); ++i) packet.payload[i] = std::byte{payload[i]};

        auto state = decoder.add_packet(packet);
        if (!state) continue;
        declared_source_bytes = source_bytes;
        if (*state == ::crucible::cntp::FountainDecodeState::Complete) {
            auto decoded = decoder.extract_decoded();
            CRUCIBLE_FUZZ_CLAIM("fountain", decoded.has_value());
            CRUCIBLE_FUZZ_CLAIM("fountain", decoded->size() <= Decoder::max_source_bytes);
            CRUCIBLE_FUZZ_CLAIM("fountain", decoded->size() == declared_source_bytes);
            return;
        }
    }
}

}  // namespace crucible::fuzz::boundary
