#include <crucible/cntp/Fountain.h>
#include <fixy/Ctx.h>
#include <fixy/Qtt.h>
#include <foundation/effects/Ctx.h>

#include "test_assert.h"
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

namespace ci = crucible::cntp;

template <std::size_t N>
using Bytes = ::foundation::core::FixedArray<std::byte, N>;

template <std::size_t N>
[[nodiscard]] constexpr Bytes<N> payload_seed() noexcept {
    Bytes<N> bytes{};
    for (auto index : bytes.indices()) {
        auto const i = index.raw();
        bytes.at(index) = static_cast<std::byte>((i * 29U + (i >> 2U) + 0xC3U) & 0xFFU);
    }
    return bytes;
}

template <typename Packet>
[[nodiscard]] Packet equation_packet(std::uint64_t mask, std::byte fill) noexcept {
    Packet packet{};
    packet.source_bytes = Packet::admit_source_bytes(Packet::max_source_bytes).expect("the full capacity fits");
    packet.mask = mask;
    ::fixy::fill(packet.payload.view(), fill);
    return packet;
}

// The error of a call that refuses its input.
template <typename Value>
[[nodiscard]] ci::FountainError error_of(::fixy::Result<Value, ci::FountainError> const& result) noexcept {
    return result.err().expect("the call refuses its input");
}

}  // namespace

int main() {
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
    static_assert(::fixy::qtt_consume_tracked || sizeof(ci::LinearFountainBuffer<Bytes<16>>) == sizeof(Bytes<16>));

    auto key = crucible::Philox::op_key_det(0x12345678ULL, 19U, crucible::ContentHash{0xCAFEBEEFULL});

    // A codec is start-up work, so each mint takes the cold init context.
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto encoder = ci::mint_fountain_encoder<8, 4>(init);
    auto decoder = ci::mint_fountain_decoder<8, 4>(init);

    {
        constexpr auto payload = payload_seed<29>();
        assert(encoder.start_encoding(payload.view(), key).is_ok());

        for (std::uint32_t id = 0; id < 256 && !decoder.complete(); ++id) {
            auto packet = encoder.next_packet().expect("the encoder is started");

            const bool lost = id == 1 || id == 3 || id == 6 || id == 11 || id == 17;
            if (lost) {
                continue;
            }

            assert(decoder.add_packet(packet).is_ok());
        }

        assert(decoder.complete());
        auto decoded = decoder.extract_decoded().expect("the decoder is complete");
        assert(::fixy::equal(payload.view(), decoded));
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
        assert(probe.start_encoding(probe_payload.view(), key).is_ok());

        ::foundation::core::FixedArray<std::uint32_t, 7> inclusion{};
        ::foundation::core::FixedArray<std::uint64_t, 256> masks_a{};
        for (auto& mask : masks_a) {
            auto packet = probe.next_packet().expect("the encoder is started");
            mask = packet.mask;
            for (auto bit : inclusion.indices()) {
                if ((packet.mask >> bit.raw()) & 1ULL) {
                    inclusion.at(bit) += 1;
                }
            }
        }
        for (auto count : inclusion) {
            assert(count > 0);
        }

        auto probe_again = ci::mint_fountain_encoder<7, 8>(init);
        assert(probe_again.start_encoding(probe_payload.view(), key).is_ok());
        for (auto const mask : masks_a) {
            assert(probe_again.next_packet().expect("the encoder is started").mask == mask);
        }
    }

    {
        constexpr auto payload = payload_seed<16>();
        auto a = encoder.encode_packet(payload.view(), key, 12).expect("16 bytes fit");
        auto b = encoder.encode_packet(payload.view(), key, 12).expect("16 bytes fit");
        assert(a.mask == b.mask);
        assert(a.payload == b.payload);
    }

    {
        constexpr auto payload = payload_seed<13>();
        auto owned_payload = ::fixy::mint_linear<Bytes<payload.size()>>(payload);
        auto owned_packet = encoder.encode_owned(std::move(owned_payload), key, 0).expect("13 bytes fit");

        auto owned_decoder = ci::mint_fountain_decoder<8, 4>(init);
        auto state = owned_decoder.add_packet_owned(std::move(owned_packet)).expect("the packet has the shape");
        assert(state == ci::FountainDecodeState::NeedsMore);
        assert(owned_decoder.decoded_count() == 1);
    }

    {
        assert(error_of(encoder.start_encoding(::fixy::View<std::byte const>{}, key))
               == ci::FountainError::InvalidInputSize);

        Bytes<Encoder::max_source_bytes + 1> too_large{};
        assert(error_of(encoder.start_encoding(too_large.view(), key)) == ci::FountainError::InvalidInputSize);
    }

    {
        auto bad = Packet{};
        bad.symbol_count = 7;
        bad.source_bytes = Packet::admit_source_bytes(16).expect("16 bytes fit");
        bad.mask = 1;

        auto shape_decoder = ci::mint_fountain_decoder<8, 4>(init);
        assert(error_of(shape_decoder.add_packet(bad)) == ci::FountainError::PacketShapeMismatch);
    }

    {
        using SmallDecoder = ci::FountainDecoder<4, 4, 4>;
        using SmallPacket = ci::FountainPacket<4, 4>;

        SmallDecoder small = ci::mint_fountain_decoder<4, 4, 4>(init);
        assert(small.add_packet(equation_packet<SmallPacket>(0b0011, std::byte{0x11})).is_ok());
        assert(small.add_packet(equation_packet<SmallPacket>(0b0101, std::byte{0x22})).is_ok());
        assert(small.add_packet(equation_packet<SmallPacket>(0b0110, std::byte{0x33})).is_ok());
        assert(small.add_packet(equation_packet<SmallPacket>(0b1001, std::byte{0x44})).is_ok());

        assert(error_of(small.add_packet(equation_packet<SmallPacket>(0b1010, std::byte{0x55})))
               == ci::FountainError::EquationCapacityExceeded);
    }

    {
        using SmallDecoder = ci::FountainDecoder<4, 4, 4>;
        using SmallPacket = ci::FountainPacket<4, 4>;

        SmallDecoder small = ci::mint_fountain_decoder<4, 4, 4>(init);
        auto first = equation_packet<SmallPacket>(0b0011, std::byte{0x11});
        auto second = first;
        second.payload.at(::fixy::index<0>()) = std::byte{0x22};

        assert(small.add_packet(first).is_ok());
        assert(error_of(small.add_packet(second)) == ci::FountainError::InconsistentEquation);
    }

    {
        using SmallDecoder = ci::FountainDecoder<4, 4, 4>;
        using SmallPacket = ci::FountainPacket<4, 4>;

        SmallDecoder small = ci::mint_fountain_decoder<4, 4, 4>(init);
        assert(small.add_packet(equation_packet<SmallPacket>(0b0001, std::byte{0x11})).is_ok());

        auto conflicting = equation_packet<SmallPacket>(0b0001, std::byte{0x22});
        assert(error_of(small.add_packet(conflicting)) == ci::FountainError::InconsistentEquation);
    }

    return 0;
}
