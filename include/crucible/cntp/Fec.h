#pragma once

// Reed-Solomon erasure coding for CNT-P payload shards.
//
// Systematic (K+M) code over GF(2^8): the first K output shards are the
// padded input bytes, the following M shards are parity.  Decode chooses
// any K surviving shards, inverts the corresponding generator submatrix,
// and reconstructs the original K data shards.

#include <crucible/Platform.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/Concurrent.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Simd.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <iterator>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>

#if defined(__AVX2__)
#include <immintrin.h>
#elif (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace crucible::cntp {

template <std::uint8_t K, std::uint8_t M>
concept ReedSolomonShape =
    K > 0 &&
    M > 0 &&
    (static_cast<unsigned>(K) + static_cast<unsigned>(M)) <= 256U;

enum class FecError : std::uint8_t {
    LengthOverflow,
    InvalidInputSize,
    InvalidOutputSize,
    ErasureMaskSizeMismatch,
    TooManyErasures,
    SingularMatrix,
};

using FecShardBytes = safety::Refined<safety::positive, std::size_t>;

template <typename Buffer>
using LinearShardBuffer = safety::Linear<Buffer>;

template <typename T>
concept ByteElement =
    std::same_as<std::remove_cv_t<T>, std::byte> ||
    (sizeof(T) == 1 && std::is_trivially_copyable_v<T>);

template <typename Buffer>
concept ByteContiguousBuffer = requires(Buffer const& buffer) {
    std::data(buffer);
    std::size(buffer);
} && std::is_pointer_v<decltype(std::data(std::declval<Buffer const&>()))> &&
     ByteElement<std::remove_pointer_t<
         decltype(std::data(std::declval<Buffer const&>()))>>;

template <typename Buffer>
concept MutableByteContiguousBuffer =
    ByteContiguousBuffer<Buffer> &&
    (!std::is_const_v<std::remove_pointer_t<
        decltype(std::data(std::declval<Buffer&>()))>>);

namespace detail {

inline constexpr std::uint16_t gf_poly = 0x11dU;

struct GfTables {
    std::array<std::uint8_t, 512> exp{};
    std::array<std::uint8_t, 256> log{};
    std::array<std::uint8_t, 256> inv{};
    std::array<std::array<std::uint8_t, 256>, 256> mul{};
};

[[nodiscard]] consteval GfTables make_gf_tables() noexcept {
    GfTables tables{};

    std::uint16_t x = 1;
    for (std::uint16_t i = 0; i < 255; ++i) {
        tables.exp[i] = static_cast<std::uint8_t>(x);
        tables.log[static_cast<std::uint8_t>(x)] =
            static_cast<std::uint8_t>(i);
        x = static_cast<std::uint16_t>(x << 1U);
        if ((x & 0x100U) != 0U) {
            x ^= gf_poly;
        }
    }
    for (std::uint16_t i = 255; i < tables.exp.size(); ++i) {
        tables.exp[i] = tables.exp[i - 255U];
    }

    for (std::uint16_t a = 0; a < 256; ++a) {
        for (std::uint16_t b = 0; b < 256; ++b) {
            if (a == 0 || b == 0) {
                tables.mul[a][b] = 0;
            } else {
                const auto e = static_cast<std::size_t>(tables.log[a]) +
                               static_cast<std::size_t>(tables.log[b]);
                tables.mul[a][b] = tables.exp[e];
            }
        }
    }
    for (std::uint16_t a = 1; a < 256; ++a) {
        tables.inv[a] = tables.exp[255U - tables.log[a]];
    }
    return tables;
}

inline constexpr GfTables gf = make_gf_tables();

[[nodiscard, gnu::const]] constexpr std::uint8_t
mul(std::uint8_t a, std::uint8_t b) noexcept {
    return gf.mul[a][b];
}

[[nodiscard, gnu::const]] constexpr std::uint8_t
inv(std::uint8_t a) noexcept {
    return gf.inv[a];
}

template <std::size_t N>
using SquareMatrix = std::array<std::array<std::uint8_t, N>, N>;

template <std::uint8_t K, std::uint8_t M>
using GeneratorMatrix =
    std::array<std::array<std::uint8_t, K>,
               static_cast<std::size_t>(K) + static_cast<std::size_t>(M)>;

template <std::uint8_t K, std::uint8_t M>
    requires ReedSolomonShape<K, M>
[[nodiscard]] consteval GeneratorMatrix<K, M>
make_generator_matrix() noexcept {
    GeneratorMatrix<K, M> matrix{};

    for (std::size_t row = 0; row < K; ++row) {
        matrix[row][row] = 1;
    }

    for (std::size_t parity = 0; parity < M; ++parity) {
        const auto y = static_cast<std::uint8_t>(K + parity);
        for (std::size_t col = 0; col < K; ++col) {
            const auto x = static_cast<std::uint8_t>(col);
            matrix[static_cast<std::size_t>(K) + parity][col] =
                inv(static_cast<std::uint8_t>(x ^ y));
        }
    }
    return matrix;
}

template <std::size_t N>
[[nodiscard]] constexpr bool
invert_matrix(SquareMatrix<N> input, SquareMatrix<N>& inverse) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        inverse[i].fill(0);
        inverse[i][i] = 1;
    }

    for (std::size_t col = 0; col < N; ++col) {
        std::size_t pivot = col;
        while (pivot < N && input[pivot][col] == 0) {
            ++pivot;
        }
        if (pivot == N) {
            return false;
        }
        if (pivot != col) {
            std::swap(input[pivot], input[col]);
            std::swap(inverse[pivot], inverse[col]);
        }

        const std::uint8_t scale = inv(input[col][col]);
        for (std::size_t j = 0; j < N; ++j) {
            input[col][j] = mul(input[col][j], scale);
            inverse[col][j] = mul(inverse[col][j], scale);
        }

        for (std::size_t row = 0; row < N; ++row) {
            if (row == col) {
                continue;
            }
            const std::uint8_t factor = input[row][col];
            if (factor == 0) {
                continue;
            }
            for (std::size_t j = 0; j < N; ++j) {
                input[row][j] ^= mul(factor, input[col][j]);
                inverse[row][j] ^= mul(factor, inverse[col][j]);
            }
        }
    }
    return true;
}

#if defined(__AVX2__)
struct alignas(32) NibbleTables {
    alignas(32) std::array<std::uint8_t, 32> lo{};
    alignas(32) std::array<std::uint8_t, 32> hi{};
};

[[nodiscard]] inline NibbleTables
make_nibble_tables(std::uint8_t coeff) noexcept {
    NibbleTables tables{};
    for (std::uint8_t i = 0; i < 16; ++i) {
        const auto lo = mul(coeff, i);
        const auto hi = mul(coeff, static_cast<std::uint8_t>(i << 4U));
        tables.lo[i] = lo;
        tables.hi[i] = hi;
        tables.lo[static_cast<std::size_t>(i) + 16U] = lo;
        tables.hi[static_cast<std::size_t>(i) + 16U] = hi;
    }
    return tables;
}

CRUCIBLE_HOT void xor_bytes_avx2(std::byte* dst,
                                 std::byte const* src,
                                 std::size_t len) noexcept {
    std::size_t i = 0;
    for (; i + 32 <= len; i += 32) {
        auto const a = _mm256_loadu_si256(
            reinterpret_cast<__m256i const*>(dst + i));
        auto const b = _mm256_loadu_si256(
            reinterpret_cast<__m256i const*>(src + i));
        _mm256_storeu_si256(
            reinterpret_cast<__m256i*>(dst + i),
            _mm256_xor_si256(a, b));
    }
    for (; i < len; ++i) {
        dst[i] ^= src[i];
    }
}

CRUCIBLE_HOT void mul_xor_avx2(std::byte* dst,
                               std::byte const* src,
                               std::uint8_t coeff,
                               std::size_t len) noexcept {
    if (coeff == 0) {
        return;
    }
    if (coeff == 1) {
        xor_bytes_avx2(dst, src, len);
        return;
    }

    const auto tables = make_nibble_tables(coeff);
    const auto lo_table = _mm256_load_si256(
        reinterpret_cast<__m256i const*>(tables.lo.data()));
    const auto hi_table = _mm256_load_si256(
        reinterpret_cast<__m256i const*>(tables.hi.data()));
    const auto mask = _mm256_set1_epi8(0x0f);

    std::size_t i = 0;
    for (; i + 32 <= len; i += 32) {
        const auto bytes = _mm256_loadu_si256(
            reinterpret_cast<__m256i const*>(src + i));
        const auto lo = _mm256_and_si256(bytes, mask);
        const auto hi = _mm256_and_si256(_mm256_srli_epi16(bytes, 4), mask);
        const auto prod = _mm256_xor_si256(
            _mm256_shuffle_epi8(lo_table, lo),
            _mm256_shuffle_epi8(hi_table, hi));
        const auto old = _mm256_loadu_si256(
            reinterpret_cast<__m256i const*>(dst + i));
        _mm256_storeu_si256(
            reinterpret_cast<__m256i*>(dst + i),
            _mm256_xor_si256(old, prod));
    }
    for (; i < len; ++i) {
        dst[i] ^= static_cast<std::byte>(
            mul(static_cast<std::uint8_t>(src[i]), coeff));
    }
}
#elif (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(__aarch64__)
CRUCIBLE_HOT void xor_bytes_neon(std::byte* dst,
                                 std::byte const* src,
                                 std::size_t len) noexcept {
    std::size_t i = 0;
    for (; i + 16 <= len; i += 16) {
        auto const a = vld1q_u8(
            reinterpret_cast<std::uint8_t const*>(dst + i));
        auto const b = vld1q_u8(
            reinterpret_cast<std::uint8_t const*>(src + i));
        vst1q_u8(reinterpret_cast<std::uint8_t*>(dst + i), veorq_u8(a, b));
    }
    for (; i < len; ++i) {
        dst[i] ^= src[i];
    }
}

CRUCIBLE_HOT void mul_xor_neon(std::byte* dst,
                               std::byte const* src,
                               std::uint8_t coeff,
                               std::size_t len) noexcept {
    if (coeff == 0) {
        return;
    }
    if (coeff == 1) {
        xor_bytes_neon(dst, src, len);
        return;
    }

    const auto tables = make_nibble_tables(coeff);
    const auto lo_table = vld1q_u8(tables.lo.data());
    const auto hi_table = vld1q_u8(tables.hi.data());
    const auto mask = vdupq_n_u8(0x0f);

    std::size_t i = 0;
    for (; i + 16 <= len; i += 16) {
        const auto bytes = vld1q_u8(
            reinterpret_cast<std::uint8_t const*>(src + i));
        const auto lo = vandq_u8(bytes, mask);
        const auto hi = vandq_u8(vshrq_n_u8(bytes, 4), mask);
        const auto prod = veorq_u8(vqtbl1q_u8(lo_table, lo),
                                   vqtbl1q_u8(hi_table, hi));
        const auto old = vld1q_u8(
            reinterpret_cast<std::uint8_t const*>(dst + i));
        vst1q_u8(reinterpret_cast<std::uint8_t*>(dst + i),
                 veorq_u8(old, prod));
    }
    for (; i < len; ++i) {
        dst[i] ^= static_cast<std::byte>(
            mul(static_cast<std::uint8_t>(src[i]), coeff));
    }
}
#endif

CRUCIBLE_HOT void xor_bytes_scalar(std::byte* dst,
                                   std::byte const* src,
                                   std::size_t len) noexcept {
    for (std::size_t i = 0; i < len; ++i) {
        dst[i] ^= src[i];
    }
}

CRUCIBLE_HOT void mul_xor(std::byte* dst,
                          std::byte const* src,
                          std::uint8_t coeff,
                          std::size_t len) noexcept {
    if (coeff == 0) {
        return;
    }
#if defined(__AVX2__)
    mul_xor_avx2(dst, src, coeff, len);
#elif (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(__aarch64__)
    mul_xor_neon(dst, src, coeff, len);
#else
    if (coeff == 1) {
        xor_bytes_scalar(dst, src, len);
        return;
    }
    for (std::size_t i = 0; i < len; ++i) {
        dst[i] ^= static_cast<std::byte>(
            mul(static_cast<std::uint8_t>(src[i]), coeff));
    }
#endif
}

template <ByteContiguousBuffer Buffer>
[[nodiscard]] inline std::span<const std::byte>
bytes(Buffer const& buffer) noexcept {
    return {
        reinterpret_cast<std::byte const*>(std::data(buffer)),
        static_cast<std::size_t>(std::size(buffer)),
    };
}

template <MutableByteContiguousBuffer Buffer>
[[nodiscard]] inline std::span<std::byte>
mutable_bytes(Buffer& buffer) noexcept {
    return {
        reinterpret_cast<std::byte*>(std::data(buffer)),
        static_cast<std::size_t>(std::size(buffer)),
    };
}

}  // namespace detail

template <std::uint8_t K, std::uint8_t M>
    requires ReedSolomonShape<K, M>
class ReedSolomon {
public:
    static constexpr std::uint8_t data_shards = K;
    static constexpr std::uint8_t parity_shards = M;
    static constexpr std::uint16_t total_shards =
        static_cast<std::uint16_t>(K) + static_cast<std::uint16_t>(M);
    static constexpr std::size_t alignment = 32;
    using concurrent_budget =
        effects::ConcurrentRow<effects::SmBudget<1>>;

    [[nodiscard]] static constexpr std::size_t
    shard_bytes_for(std::size_t input_size) noexcept {
        const auto q = input_size / K;
        const auto r = input_size % K;
        return q + (r == 0 ? 0U : 1U);
    }

    [[nodiscard]] static constexpr std::size_t
    encoded_size_for(std::size_t input_size) noexcept {
        const auto shard_bytes = shard_bytes_for(input_size);
        constexpr auto max = std::numeric_limits<std::size_t>::max();
        if (shard_bytes > max / total_shards) {
            return max;
        }
        return shard_bytes * total_shards;
    }

    [[nodiscard]] std::expected<void, FecError>
    encode(std::span<const std::byte> input,
           std::span<std::byte> output_with_parity) const noexcept {
        if (input.empty()) {
            return std::unexpected(FecError::InvalidInputSize);
        }
        const auto shard_bytes = shard_bytes_for(input.size());
        const auto expected = encoded_size_for(input.size());
        if (expected == std::numeric_limits<std::size_t>::max()) {
            return std::unexpected(FecError::LengthOverflow);
        }
        if (output_with_parity.size() != expected) {
            return std::unexpected(FecError::InvalidOutputSize);
        }

        std::memset(output_with_parity.data(), 0, output_with_parity.size());
        std::memcpy(output_with_parity.data(), input.data(), input.size());

        for (std::size_t p = 0; p < M; ++p) {
            auto* parity = output_with_parity.data() +
                (static_cast<std::size_t>(K) + p) * shard_bytes;
            for (std::size_t d = 0; d < K; ++d) {
                auto const* data =
                    output_with_parity.data() + d * shard_bytes;
                detail::mul_xor(
                    parity,
                    data,
                    generator_[static_cast<std::size_t>(K) + p][d],
                    shard_bytes);
            }
        }
        return {};
    }

    [[nodiscard]] std::expected<void, FecError>
    decode(std::span<const std::byte> received,
           std::span<const bool> erasure_mask,
           std::span<std::byte> output) const noexcept {
        if (received.empty() || (received.size() % total_shards) != 0) {
            return std::unexpected(FecError::InvalidInputSize);
        }
        if (erasure_mask.size() != total_shards) {
            return std::unexpected(FecError::ErasureMaskSizeMismatch);
        }

        const auto shard_bytes = received.size() / total_shards;
        if (output.empty() ||
            output.size() >
                static_cast<std::size_t>(K) * shard_bytes) {
            return std::unexpected(FecError::InvalidOutputSize);
        }

        std::array<std::size_t, K> selected{};
        std::size_t selected_count = 0;
        std::size_t erased_count = 0;
        for (std::size_t i = 0; i < total_shards; ++i) {
            if (erasure_mask[i]) {
                ++erased_count;
                continue;
            }
            if (selected_count < K) {
                selected[selected_count++] = i;
            }
        }
        if (erased_count > M || selected_count < K) {
            return std::unexpected(FecError::TooManyErasures);
        }

        detail::SquareMatrix<K> decode_matrix{};
        detail::SquareMatrix<K> inverse{};
        for (std::size_t row = 0; row < K; ++row) {
            decode_matrix[row] = generator_[selected[row]];
        }
        if (!detail::invert_matrix<K>(decode_matrix, inverse)) {
            return std::unexpected(FecError::SingularMatrix);
        }

        for (std::size_t out_shard = 0; out_shard < K; ++out_shard) {
            const auto out_offset = out_shard * shard_bytes;
            if (out_offset >= output.size()) {
                break;
            }
            const auto live_bytes =
                std::min(shard_bytes, output.size() - out_offset);
            auto* dst = output.data() + out_offset;
            std::memset(dst, 0, live_bytes);

            for (std::size_t src = 0; src < K; ++src) {
                auto const* shard =
                    received.data() + selected[src] * shard_bytes;
                detail::mul_xor(dst, shard, inverse[out_shard][src],
                                live_bytes);
            }
        }
        return {};
    }

    template <ByteContiguousBuffer Input, MutableByteContiguousBuffer Output>
    [[nodiscard]] std::expected<void, FecError>
    encode_owned(LinearShardBuffer<Input>&& input,
                 Output& output) const noexcept {
        return encode(detail::bytes(input.peek()), detail::mutable_bytes(output));
    }

    template <ByteContiguousBuffer Input, MutableByteContiguousBuffer Output>
    [[nodiscard]] std::expected<void, FecError>
    decode_owned(LinearShardBuffer<Input>&& received,
                 std::span<const bool> erasure_mask,
                 Output& output) const noexcept {
        return decode(detail::bytes(received.peek()), erasure_mask,
                      detail::mutable_bytes(output));
    }

private:
    inline static constexpr auto generator_ =
        detail::make_generator_matrix<K, M>();
};

template <std::uint8_t K, std::uint8_t M>
    requires ReedSolomonShape<K, M>
[[nodiscard]] constexpr ReedSolomon<K, M>
mint_reed_solomon(effects::Init) noexcept {
    return ReedSolomon<K, M>{};
}

static_assert(ReedSolomonShape<10, 2>);
static_assert(!ReedSolomonShape<0, 2>);
static_assert(!ReedSolomonShape<10, 0>);
static_assert(!ReedSolomonShape<255, 2>);
static_assert(sizeof(LinearShardBuffer<std::span<std::byte>>) ==
              sizeof(std::span<std::byte>));

}  // namespace crucible::cntp
