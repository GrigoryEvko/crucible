#include <crucible/cntp/Fec.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

namespace ci = crucible::cntp;

template <std::size_t N>
using Bytes = ::foundation::core::FixedArray<std::byte, N>;

template <std::size_t N>
using Flags = ::foundation::core::FixedArray<bool, N>;

template <std::size_t N>
[[nodiscard]] constexpr Bytes<N> payload_seed() noexcept {
    Bytes<N> bytes{};
    for (auto index : bytes.indices()) {
        auto const i = index.raw();
        auto const byte = static_cast<std::byte>((i * 37U + (i >> 1U) + 0x5AU) & 0xFFU);
        bytes.with_element(index, [byte](std::byte& element) noexcept { element = byte; });
    }
    return bytes;
}

// Writes the byte fill over the shard at position shard of an encoded block.
template <std::size_t N>
void poison_shard(Bytes<N>& encoded, std::size_t shard, std::size_t shard_bytes, std::byte fill) {
    encoded.with_mut([shard, shard_bytes, fill](auto run) noexcept {
        ::fixy::fill(run.view().window(shard * shard_bytes, shard_bytes).expect("the shard lies inside the block"),
                     fill);
    });
}

// The encode of input into the block encoded, through the frame door of the
// block.
template <typename Codec, std::size_t EncodedBytes>
[[nodiscard]] ::fixy::Result<::fixy::Unit, ci::FecError>
encode_into(Codec const& codec, ::fixy::View<std::byte const> input, Bytes<EncodedBytes>& encoded) noexcept {
    return encoded.with_mut([&codec, input](auto run) noexcept { return codec.encode(input, run.view()); });
}

// The decode of the received block into decoded, through the frame door of
// decoded.
template <typename Codec, std::size_t DecodedBytes>
[[nodiscard]] ::fixy::Result<::fixy::Unit, ci::FecError>
decode_into(Codec const& codec, ::fixy::View<std::byte const> received, ::fixy::View<bool const> erasures,
            Bytes<DecodedBytes>& decoded) noexcept {
    return decoded.with_mut(
        [&codec, received, erasures](auto run) noexcept { return codec.decode(received, erasures, run.view()); });
}

template <typename Codec, std::size_t InputBytes, std::size_t EncodedBytes>
void verify_all_recoverable_erasures(Codec const& codec, Bytes<InputBytes> const& payload,
                                     Bytes<EncodedBytes> const& encoded) {
    constexpr auto total = Codec::total_shards;
    static_assert(total < 64);
    const auto shard_bytes = Codec::shard_bytes_for(InputBytes);
    const auto mask_limit = std::uint64_t{1} << total;

    for (std::uint64_t bits = 0; bits < mask_limit; ++bits) {
        auto damaged = encoded;
        Flags<total> erasures{};
        std::size_t erased = 0;

        for (auto shard : erasures.indices()) {
            const auto bit = std::uint64_t{1} << shard.raw();
            if ((bits & bit) == 0) {
                continue;
            }
            erasures.with_element(shard, [](bool& is_erased) noexcept { is_erased = true; });
            ++erased;
            poison_shard(damaged, shard.raw(), shard_bytes, std::byte{0xA5});
        }
        if (erased > Codec::parity_shards) {
            continue;
        }

        Bytes<InputBytes> decoded{};
        CRUCIBLE_TEST_ASSERT(kTest, decode_into(codec, damaged.view(), erasures.view(), decoded).is_ok());
        CRUCIBLE_TEST_ASSERT(kTest, payload == decoded);
    }
}

// A vector kernel of mul_xor steps one register at a time, and a shard that
// is shorter than one register reaches only the scalar tail.  This check
// compares the kernel that the build selects with the scalar product, for
// each coefficient, for lengths around one, two and three register steps,
// and at an address that is not aligned for a register.
void verify_mul_xor_against_scalar_product() {
    namespace detail = crucible::cntp::detail;
    constexpr std::size_t buffer_bytes = 112;
    constexpr auto source_seed = payload_seed<buffer_bytes>();
    ::foundation::core::FixedArray<std::size_t, 2> const offsets{0, 1};
    ::foundation::core::FixedArray<std::size_t, 15> const lengths{0,  1,  15, 16, 17, 31, 32, 33,
                                                                  63, 64, 65, 95, 96, 97, 110};
    for (const std::size_t offset : offsets) {
        for (const std::size_t len : lengths) {
            for (unsigned coeff = 0; coeff < 256U; ++coeff) {
                const auto factor = static_cast<std::uint8_t>(coeff);
                Bytes<buffer_bytes> target{};
                for (auto index : target.indices()) {
                    auto const byte = static_cast<std::byte>((index.raw() * 11U + coeff) & 0xFFU);
                    target.with_element(index, [byte](std::byte& element) noexcept { element = byte; });
                }
                Bytes<buffer_bytes> expected = target;
                for (std::size_t i = 0; i < len; ++i) {
                    auto const position =
                        ::fixy::Index<buffer_bytes>::check(offset + i).expect("the byte lies inside the buffer");
                    const auto source_byte = static_cast<std::uint8_t>(source_seed.at(position));
                    auto const product = static_cast<std::byte>(detail::mul(source_byte, factor));
                    expected.with_element(position, [product](std::byte& element) noexcept { element ^= product; });
                }
                auto const source = source_seed.view().window(offset, len).expect("the run lies inside the source");
                target.with_mut([offset, len, source, factor](auto run) noexcept {
                    detail::mul_xor(run.view().window(offset, len).expect("the run lies inside the target"), source,
                                    factor);
                });
                CRUCIBLE_TEST_ASSERT(kTest, expected == target);
            }
        }
    }
}

}  // namespace

int main() {
    using Rs42 = ci::ReedSolomon<4, 2>;
    using Rs102 = ci::ReedSolomon<10, 2>;

    static_assert(ci::ReedSolomonShape<4, 2>);
    static_assert(!ci::ReedSolomonShape<0, 2>);
    static_assert(!ci::ReedSolomonShape<4, 0>);
    static_assert(!ci::ReedSolomonShape<255, 2>);
    static_assert(Rs42::data_shards == 4);
    static_assert(Rs42::parity_shards == 2);
    static_assert(Rs42::total_shards == 6);
    static_assert(Rs42::alignment == 32);
    static_assert(Rs42::shard_bytes_for(0) == 0);
    static_assert(Rs42::shard_bytes_for(1) == 1);
    static_assert(Rs42::shard_bytes_for(17) == 5);
    static_assert(Rs42::encoded_size_for(17) == 30);
    static_assert(sizeof(ci::LinearShardBuffer<Bytes<8>>) == sizeof(Bytes<8>));
    static_assert(std::same_as<Rs42::concurrent_budget,
                               ::foundation::effects::ConcurrentRow<::foundation::effects::SmBudget<1>>>);
    static_assert(ci::ByteContiguousBuffer<Bytes<8>> && ci::ByteContiguousBuffer<::fixy::View<std::byte const>>);
    static_assert(ci::MutableByteContiguousBuffer<Bytes<8>> && !ci::MutableByteContiguousBuffer<Bytes<8> const>);

    auto rs42 = ci::mint_reed_solomon<4, 2>(::foundation::effects::testing::init());

    {
        constexpr auto payload = payload_seed<17>();
        Bytes<Rs42::encoded_size_for(payload.size())> encoded{};
        CRUCIBLE_TEST_ASSERT(kTest, encode_into(rs42, payload.view(), encoded).is_ok());
        verify_all_recoverable_erasures(rs42, payload, encoded);

        Flags<Rs42::total_shards> erasures{};
        Bytes<payload.size()> decoded{};
        CRUCIBLE_TEST_ASSERT(kTest, decode_into(rs42, encoded.view(), erasures.view(), decoded).is_ok());
        CRUCIBLE_TEST_ASSERT(kTest, payload == decoded);
    }

    verify_mul_xor_against_scalar_product();

    {
        // Each shard holds 104 bytes: three register steps of 32 bytes and a
        // tail of eight bytes.
        constexpr auto payload = payload_seed<413>();
        Bytes<Rs42::encoded_size_for(payload.size())> encoded{};
        CRUCIBLE_TEST_ASSERT(kTest, encode_into(rs42, payload.view(), encoded).is_ok());
        verify_all_recoverable_erasures(rs42, payload, encoded);
    }

    {
        constexpr auto payload = payload_seed<19>();
        constexpr auto shard_bytes = Rs42::shard_bytes_for(payload.size());
        Bytes<Rs42::encoded_size_for(payload.size())> encoded{};
        CRUCIBLE_TEST_ASSERT(kTest, encode_into(rs42, payload.view(), encoded).is_ok());

        Flags<Rs42::total_shards> erasures{};
        erasures.with_element(::fixy::index<0>(), [](bool& is_erased) noexcept { is_erased = true; });
        erasures.with_element(::fixy::index<2>(), [](bool& is_erased) noexcept { is_erased = true; });
        poison_shard(encoded, 0, shard_bytes, std::byte{0xEE});
        poison_shard(encoded, 2, shard_bytes, std::byte{0xEE});

        Bytes<payload.size()> decoded{};
        CRUCIBLE_TEST_ASSERT(kTest, decode_into(rs42, encoded.view(), erasures.view(), decoded).is_ok());
        CRUCIBLE_TEST_ASSERT(kTest, payload == decoded);
    }

    {
        constexpr auto payload = payload_seed<31>();
        constexpr auto shard_bytes = Rs42::shard_bytes_for(payload.size());
        Bytes<Rs42::encoded_size_for(payload.size())> encoded{};
        CRUCIBLE_TEST_ASSERT(kTest, encode_into(rs42, payload.view(), encoded).is_ok());

        Flags<Rs42::total_shards> erasures{};
        erasures.with_element(::fixy::index<1>(), [](bool& is_erased) noexcept { is_erased = true; });
        erasures.with_element(::fixy::index<5>(), [](bool& is_erased) noexcept { is_erased = true; });
        poison_shard(encoded, 1, shard_bytes, std::byte{0xEE});
        poison_shard(encoded, 5, shard_bytes, std::byte{0xEE});

        Bytes<payload.size()> decoded{};
        CRUCIBLE_TEST_ASSERT(kTest, decode_into(rs42, encoded.view(), erasures.view(), decoded).is_ok());
        CRUCIBLE_TEST_ASSERT(kTest, payload == decoded);
    }

    {
        constexpr auto payload = payload_seed<23>();
        Bytes<Rs42::encoded_size_for(payload.size())> encoded{};
        CRUCIBLE_TEST_ASSERT(kTest, encode_into(rs42, payload.view(), encoded).is_ok());

        Flags<Rs42::total_shards> too_many{};
        too_many.with_element(::fixy::index<0>(), [](bool& is_erased) noexcept { is_erased = true; });
        too_many.with_element(::fixy::index<1>(), [](bool& is_erased) noexcept { is_erased = true; });
        too_many.with_element(::fixy::index<4>(), [](bool& is_erased) noexcept { is_erased = true; });

        Bytes<payload.size()> decoded{};
        auto rejected = decode_into(rs42, encoded.view(), too_many.view(), decoded);
        CRUCIBLE_TEST_ASSERT(kTest,
                             rejected.err().expect("three erasures are too many") == ci::FecError::TooManyErasures);
    }

    {
        constexpr auto payload = payload_seed<13>();
        auto owned_payload = ::fixy::mint_linear<Bytes<payload.size()>>(payload);
        Bytes<Rs42::encoded_size_for(payload.size())> encoded{};
        CRUCIBLE_TEST_ASSERT(kTest, rs42.encode_owned(std::move(owned_payload), encoded).is_ok());

        auto owned_encoded = ::fixy::mint_linear<decltype(encoded)>(encoded);
        Flags<Rs42::total_shards> erasures{};
        Bytes<payload.size()> decoded{};
        CRUCIBLE_TEST_ASSERT(kTest, rs42.decode_owned(std::move(owned_encoded), erasures.view(), decoded).is_ok());
        CRUCIBLE_TEST_ASSERT(kTest, payload == decoded);
    }

    {
        auto rejected = rs42.encode(::fixy::View<std::byte const>{}, ::fixy::View<std::byte>{});
        CRUCIBLE_TEST_ASSERT(kTest,
                             rejected.err().expect("an empty input is refused") == ci::FecError::InvalidInputSize);
    }

    {
        constexpr auto payload = payload_seed<9>();
        Bytes<1> too_small{};
        auto rejected = encode_into(rs42, payload.view(), too_small);
        CRUCIBLE_TEST_ASSERT(kTest,
                             rejected.err().expect("a short output is refused") == ci::FecError::InvalidOutputSize);
    }

    {
        auto rs102 = ci::mint_reed_solomon<10, 2>(::foundation::effects::testing::init());
        constexpr auto payload = payload_seed<103>();
        constexpr auto shard_bytes = Rs102::shard_bytes_for(payload.size());
        Bytes<Rs102::encoded_size_for(payload.size())> encoded{};
        CRUCIBLE_TEST_ASSERT(kTest, encode_into(rs102, payload.view(), encoded).is_ok());

        Flags<Rs102::total_shards> erasures{};
        erasures.with_element(::fixy::index<3>(), [](bool& is_erased) noexcept { is_erased = true; });
        erasures.with_element(::fixy::index<10>(), [](bool& is_erased) noexcept { is_erased = true; });
        poison_shard(encoded, 3, shard_bytes, std::byte{0xEE});
        poison_shard(encoded, 10, shard_bytes, std::byte{0xEE});

        Bytes<payload.size()> decoded{};
        CRUCIBLE_TEST_ASSERT(kTest, decode_into(rs102, encoded.view(), erasures.view(), decoded).is_ok());
        CRUCIBLE_TEST_ASSERT(kTest, payload == decoded);
    }

    {
        constexpr auto payload = payload_seed<17>();
        Bytes<Rs42::encoded_size_for(payload.size())> encoded{};
        CRUCIBLE_TEST_ASSERT(kTest, encode_into(rs42, payload.view(), encoded).is_ok());

        Flags<Rs42::total_shards - 1> short_mask{};
        Bytes<payload.size()> decoded{};
        auto rejected = decode_into(rs42, encoded.view(), short_mask.view(), decoded);
        CRUCIBLE_TEST_ASSERT(kTest,
                             rejected.err().expect("a short mask is refused") == ci::FecError::ErasureMaskSizeMismatch);
    }

    return 0;
}
