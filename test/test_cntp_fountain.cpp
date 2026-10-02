#include <crucible/cntp/Fountain.h>
#include <fixy/Ctx.h>
#include <fixy/Qtt.h>
#include <foundation/effects/Ctx.h>

#include <algorithm>
#include <array>
#include "test_assert.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace {

template <std::size_t N>
[[nodiscard]] constexpr std::array<std::byte, N> payload_seed() noexcept {
    std::array<std::byte, N> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::byte>((i * 29U + (i >> 2U) + 0xC3U) & 0xFFU);
    }
    return bytes;
}

template <std::size_t N>
[[nodiscard]] constexpr bool same_prefix(std::array<std::byte, N> const& lhs, std::span<const std::byte> rhs) noexcept {
    if (rhs.size() != N) {
        return false;
    }
    for (std::size_t i = 0; i < N; ++i) {
        if (lhs[i] != rhs[i]) {
            return false;
        }
    }
    return true;
}

template <typename Packet>
[[nodiscard]] Packet equation_packet(std::uint64_t mask, std::byte fill) noexcept {
    Packet packet{};
    packet.source_bytes = Packet::admit_source_bytes(Packet::max_source_bytes).value();
    packet.mask = mask;
    packet.payload.fill(fill);
    return packet;
}

}  // namespace

int main() {
    namespace ci = crucible::cntp;

    using Encoder = ci::FountainEncoder<8, 4>;
    using Packet = ci::FountainPacket<8, 4>;

    static_assert(ci::FountainShape<8, 4, 16>);
    static_assert(!ci::FountainShape<0, 4, 16>);
    static_assert(!ci::FountainShape<8, 0, 16>);
    static_assert(!ci::FountainShape<65, 4, 80>);
    static_assert(Encoder::source_symbols == 8);
    static_assert(Encoder::symbol_bytes == 4);
    static_assert(Encoder::max_source_bytes == 32);
    static_assert(std::is_same_v<Encoder::concurrent_budget,
                                 ::foundation::effects::ConcurrentRow<::foundation::effects::SmBudget<1>>>);
    static_assert(::fixy::qtt_consume_tracked
                  || sizeof(ci::LinearFountainBuffer<std::array<std::byte, 16>>) == sizeof(std::array<std::byte, 16>));

    auto key = crucible::Philox::op_key_det(0x12345678ULL, 19U, crucible::ContentHash{0xCAFEBEEFULL});

    // A codec is start-up work, so each mint takes the cold init context.
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto encoder = ci::mint_fountain_encoder<8, 4>(init);
    auto decoder = ci::mint_fountain_decoder<8, 4>(init);

    {
        constexpr auto payload = payload_seed<29>();
        assert(encoder.start_encoding(std::span<const std::byte>{payload}, key).has_value());

        for (std::uint32_t id = 0; id < 256 && !decoder.complete(); ++id) {
            auto packet = encoder.next_packet();
            assert(packet.has_value());

            const bool lost = id == 1 || id == 3 || id == 6 || id == 11 || id == 17;
            if (lost) {
                continue;
            }

            auto state = decoder.add_packet(*packet);
            assert(state.has_value());
        }

        assert(decoder.complete());
        auto decoded = decoder.extract_decoded();
        assert(decoded.has_value());
        assert(same_prefix(payload, *decoded));
    }

    {
        // A repair mask must pick source symbols without modulo bias.
        // Seven source symbols give spans of seven down to one, none of
        // them a power of two, which is where a plain remainder would
        // favour the low indices.  The check is a sanity sweep rather
        // than a chi-square test: every source symbol must appear at
        // least once across the pure-repair range.  The real claim is
        // the one below it, that two runs from the same seed produce
        // byte-identical masks.
        using Probe = ci::FountainEncoder<7, 8>;
        auto probe = ci::mint_fountain_encoder<7, 8>(init);
        constexpr auto probe_payload = payload_seed<Probe::max_source_bytes>();
        assert(probe.start_encoding(std::span<const std::byte>{probe_payload}, key).has_value());

        std::array<std::uint32_t, 7> inclusion{};
        std::array<std::uint64_t, 256> masks_a{};
        for (std::uint32_t id = 0; id < masks_a.size(); ++id) {
            auto packet = probe.next_packet();
            assert(packet.has_value());
            masks_a[id] = packet->mask;
            for (std::size_t bit = 0; bit < 7; ++bit) {
                if ((packet->mask >> bit) & 1ULL) {
                    inclusion[bit] += 1;
                }
            }
        }
        for (auto count : inclusion) {
            assert(count > 0);
        }

        auto probe_again = ci::mint_fountain_encoder<7, 8>(init);
        assert(probe_again.start_encoding(std::span<const std::byte>{probe_payload}, key).has_value());
        for (std::uint32_t id = 0; id < masks_a.size(); ++id) {
            auto packet = probe_again.next_packet();
            assert(packet.has_value());
            assert(packet->mask == masks_a[id]);
        }
    }

    {
        constexpr auto payload = payload_seed<16>();
        auto a = encoder.encode_packet(std::span<const std::byte>{payload}, key, 12);
        auto b = encoder.encode_packet(std::span<const std::byte>{payload}, key, 12);
        assert(a.has_value());
        assert(b.has_value());
        assert(a->mask == b->mask);
        assert(a->payload == b->payload);
    }

    {
        constexpr auto payload = payload_seed<13>();
        auto owned_payload = ::fixy::mint_linear<std::array<std::byte, payload.size()>>(payload);
        auto owned_packet = encoder.encode_owned(std::move(owned_payload), key, 0);
        assert(owned_packet.has_value());

        auto owned_decoder = ci::mint_fountain_decoder<8, 4>(init);
        auto state = owned_decoder.add_packet_owned(std::move(*owned_packet));
        assert(state.has_value());
        assert(*state == ci::FountainDecodeState::NeedsMore);
        assert(owned_decoder.decoded_count() == 1);
    }

    {
        std::array<std::byte, 0> empty{};
        auto rejected = encoder.start_encoding(empty, key);
        assert(!rejected.has_value());
        assert(rejected.error() == ci::FountainError::InvalidInputSize);

        std::array<std::byte, Encoder::max_source_bytes + 1> too_large{};
        rejected = encoder.start_encoding(too_large, key);
        assert(!rejected.has_value());
        assert(rejected.error() == ci::FountainError::InvalidInputSize);
    }

    {
        auto bad = Packet{};
        bad.symbol_count = 7;
        bad.source_bytes = Packet::admit_source_bytes(16).value();
        bad.mask = 1;

        auto shape_decoder = ci::mint_fountain_decoder<8, 4>(init);
        auto rejected = shape_decoder.add_packet(bad);
        assert(!rejected.has_value());
        assert(rejected.error() == ci::FountainError::PacketShapeMismatch);
    }

    {
        using SmallDecoder = ci::FountainDecoder<4, 4, 4>;
        using SmallPacket = ci::FountainPacket<4, 4>;

        SmallDecoder small = ci::mint_fountain_decoder<4, 4, 4>(init);
        assert(small.add_packet(equation_packet<SmallPacket>(0b0011, std::byte{0x11})).has_value());
        assert(small.add_packet(equation_packet<SmallPacket>(0b0101, std::byte{0x22})).has_value());
        assert(small.add_packet(equation_packet<SmallPacket>(0b0110, std::byte{0x33})).has_value());
        assert(small.add_packet(equation_packet<SmallPacket>(0b1001, std::byte{0x44})).has_value());

        auto rejected = small.add_packet(equation_packet<SmallPacket>(0b1010, std::byte{0x55}));
        assert(!rejected.has_value());
        assert(rejected.error() == ci::FountainError::EquationCapacityExceeded);
    }

    {
        using SmallDecoder = ci::FountainDecoder<4, 4, 4>;
        using SmallPacket = ci::FountainPacket<4, 4>;

        SmallDecoder small = ci::mint_fountain_decoder<4, 4, 4>(init);
        auto first = equation_packet<SmallPacket>(0b0011, std::byte{0x11});
        auto second = first;
        second.payload[0] = std::byte{0x22};

        assert(small.add_packet(first).has_value());
        auto rejected = small.add_packet(second);
        assert(!rejected.has_value());
        assert(rejected.error() == ci::FountainError::InconsistentEquation);
    }

    {
        using SmallDecoder = ci::FountainDecoder<4, 4, 4>;
        using SmallPacket = ci::FountainPacket<4, 4>;

        SmallDecoder small = ci::mint_fountain_decoder<4, 4, 4>(init);
        assert(small.add_packet(equation_packet<SmallPacket>(0b0001, std::byte{0x11})).has_value());

        auto conflicting = equation_packet<SmallPacket>(0b0001, std::byte{0x22});
        auto rejected = small.add_packet(conflicting);
        assert(!rejected.has_value());
        assert(rejected.error() == ci::FountainError::InconsistentEquation);
    }

    return 0;
}
