#include <crucible/cntp/Fec.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
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
        bytes[i] =
            static_cast<std::byte>((i * 37U + (i >> 1U) + 0x5AU) & 0xFFU);
    }
    return bytes;
}

template <std::size_t ShardBytes, std::size_t... Shards>
constexpr void poison_shards(std::span<std::byte> encoded,
                             std::index_sequence<Shards...>) noexcept {
    ((std::fill_n(encoded.data() + Shards * ShardBytes,
                  ShardBytes,
                  static_cast<std::byte>(0xEE))), ...);
}

template <std::size_t N>
[[nodiscard]] constexpr bool
same_prefix(std::array<std::byte, N> const& lhs,
            std::array<std::byte, N> const& rhs) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        if (lhs[i] != rhs[i]) {
            return false;
        }
    }
    return true;
}

template <typename Codec, std::size_t InputBytes, std::size_t EncodedBytes>
void verify_all_recoverable_erasures(
    Codec const& codec,
    std::array<std::byte, InputBytes> const& payload,
    std::array<std::byte, EncodedBytes> const& encoded) {
    constexpr auto total = Codec::total_shards;
    static_assert(total < 64);
    const auto shard_bytes = Codec::shard_bytes_for(InputBytes);
    const auto mask_limit = std::uint64_t{1} << total;

    for (std::uint64_t bits = 0; bits < mask_limit; ++bits) {
        auto damaged = encoded;
        std::array<bool, total> erasures{};
        std::size_t erased = 0;

        for (std::size_t shard = 0; shard < total; ++shard) {
            const auto bit = std::uint64_t{1} << shard;
            if ((bits & bit) == 0) {
                continue;
            }
            erasures[shard] = true;
            ++erased;
            std::fill_n(damaged.data() + shard * shard_bytes,
                        shard_bytes,
                        static_cast<std::byte>(0xA5));
        }
        if (erased > Codec::parity_shards) {
            continue;
        }

        std::array<std::byte, InputBytes> decoded{};
        const auto decoded_ok = codec.decode(damaged, erasures, decoded);
        assert(decoded_ok.has_value());
        assert(same_prefix(payload, decoded));
    }
}

}  // namespace

int main() {
    namespace ci = crucible::cntp;

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
    static_assert(sizeof(ci::LinearShardBuffer<std::array<std::byte, 8>>) ==
                  sizeof(std::array<std::byte, 8>));
    static_assert(std::same_as<
                  Rs42::concurrent_budget,
                  crucible::effects::ConcurrentRow<
                      crucible::effects::SmBudget<1>>>);

    auto rs42 = ci::mint_reed_solomon<4, 2>(crucible::effects::Init{});

    {
        constexpr auto payload = payload_seed<17>();
        std::array<std::byte, Rs42::encoded_size_for(payload.size())> encoded{};
        auto ok = rs42.encode(std::span<const std::byte>{payload}, encoded);
        assert(ok.has_value());
        verify_all_recoverable_erasures(rs42, payload, encoded);

        std::array<bool, Rs42::total_shards> erasures{};
        std::array<std::byte, payload.size()> decoded{};
        auto decoded_ok = rs42.decode(encoded, erasures, decoded);
        assert(decoded_ok.has_value());
        assert(same_prefix(payload, decoded));
    }

    {
        constexpr auto payload = payload_seed<19>();
        constexpr auto shard_bytes = Rs42::shard_bytes_for(payload.size());
        std::array<std::byte, Rs42::encoded_size_for(payload.size())> encoded{};
        assert(rs42.encode(payload, encoded).has_value());

        std::array<bool, Rs42::total_shards> erasures{};
        erasures[0] = true;
        erasures[2] = true;
        poison_shards<shard_bytes>(encoded, std::index_sequence<0, 2>{});

        std::array<std::byte, payload.size()> decoded{};
        auto decoded_ok = rs42.decode(encoded, erasures, decoded);
        assert(decoded_ok.has_value());
        assert(same_prefix(payload, decoded));
    }

    {
        constexpr auto payload = payload_seed<31>();
        constexpr auto shard_bytes = Rs42::shard_bytes_for(payload.size());
        std::array<std::byte, Rs42::encoded_size_for(payload.size())> encoded{};
        assert(rs42.encode(payload, encoded).has_value());

        std::array<bool, Rs42::total_shards> erasures{};
        erasures[1] = true;
        erasures[5] = true;
        poison_shards<shard_bytes>(encoded, std::index_sequence<1, 5>{});

        std::array<std::byte, payload.size()> decoded{};
        auto decoded_ok = rs42.decode(encoded, erasures, decoded);
        assert(decoded_ok.has_value());
        assert(same_prefix(payload, decoded));
    }

    {
        constexpr auto payload = payload_seed<23>();
        std::array<std::byte, Rs42::encoded_size_for(payload.size())> encoded{};
        assert(rs42.encode(payload, encoded).has_value());

        std::array<bool, Rs42::total_shards> too_many{};
        too_many[0] = true;
        too_many[1] = true;
        too_many[4] = true;

        std::array<std::byte, payload.size()> decoded{};
        auto rejected = rs42.decode(encoded, too_many, decoded);
        assert(!rejected.has_value());
        assert(rejected.error() == ci::FecError::TooManyErasures);
    }

    {
        constexpr auto payload = payload_seed<13>();
        ci::LinearShardBuffer<std::array<std::byte, payload.size()>>
            owned_payload{payload};
        std::array<std::byte, Rs42::encoded_size_for(payload.size())> encoded{};
        assert(rs42.encode_owned(std::move(owned_payload), encoded).has_value());

        ci::LinearShardBuffer<decltype(encoded)> owned_encoded{encoded};
        std::array<bool, Rs42::total_shards> erasures{};
        std::array<std::byte, payload.size()> decoded{};
        assert(rs42.decode_owned(std::move(owned_encoded), erasures, decoded)
                   .has_value());
        assert(same_prefix(payload, decoded));
    }

    {
        std::array<std::byte, 0> empty{};
        std::array<std::byte, 0> output{};
        auto rejected = rs42.encode(empty, output);
        assert(!rejected.has_value());
        assert(rejected.error() == ci::FecError::InvalidInputSize);
    }

    {
        constexpr auto payload = payload_seed<9>();
        std::array<std::byte, 1> too_small{};
        auto rejected = rs42.encode(payload, too_small);
        assert(!rejected.has_value());
        assert(rejected.error() == ci::FecError::InvalidOutputSize);
    }

    {
        auto rs102 = ci::mint_reed_solomon<10, 2>(crucible::effects::Init{});
        constexpr auto payload = payload_seed<103>();
        constexpr auto shard_bytes = Rs102::shard_bytes_for(payload.size());
        std::array<std::byte, Rs102::encoded_size_for(payload.size())> encoded{};
        assert(rs102.encode(payload, encoded).has_value());

        std::array<bool, Rs102::total_shards> erasures{};
        erasures[3] = true;
        erasures[10] = true;
        poison_shards<shard_bytes>(encoded, std::index_sequence<3, 10>{});

        std::array<std::byte, payload.size()> decoded{};
        assert(rs102.decode(encoded, erasures, decoded).has_value());
        assert(same_prefix(payload, decoded));
    }

    {
        constexpr auto payload = payload_seed<17>();
        std::array<std::byte, Rs42::encoded_size_for(payload.size())> encoded{};
        assert(rs42.encode(payload, encoded).has_value());

        std::array<bool, Rs42::total_shards - 1> short_mask{};
        std::array<std::byte, payload.size()> decoded{};
        auto rejected = rs42.decode(encoded, short_mask, decoded);
        assert(!rejected.has_value());
        assert(rejected.error() == ci::FecError::ErasureMaskSizeMismatch);
    }

    return 0;
}
