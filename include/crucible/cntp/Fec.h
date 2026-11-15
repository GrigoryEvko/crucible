#pragma once

// On the wire a shard set is K systematic data shards followed by M parity
// shards, each of equal length and contiguous in index order.  A peer
// decoder must assume the same ordering.

#include <fixy/Core.h>
#include <fixy/Qtt.h>
#include <foundation/Platform.h>
#include <foundation/Simd.h>
#include <foundation/effects/Concurrent.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Resources.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::cntp {

template <std::uint8_t K, std::uint8_t M>
concept ReedSolomonShape = K > 0 && M > 0 && (static_cast<unsigned>(K) + static_cast<unsigned>(M)) <= 256U;

enum class FecError : std::uint8_t {
    LengthOverflow,
    InvalidInputSize,
    InvalidOutputSize,
    ErasureMaskSizeMismatch,
    TooManyErasures,
    SingularMatrix,
};

template <typename Buffer>
using LinearShardBuffer = ::fixy::Linear<Buffer>;

// A buffer whose bytes are one contiguous run: a View of bytes, or an owner
// whose view() gives one, such as a FixedArray or a HeapArray of bytes.
template <typename Buffer>
concept ByteContiguousBuffer =
    std::convertible_to<Buffer const&, ::fixy::View<std::byte const>> || requires(Buffer const& buffer) {
        { buffer.view() } -> std::convertible_to<::fixy::View<std::byte const>>;
    };

// A buffer whose bytes a codec can write.
template <typename Buffer>
concept MutableByteContiguousBuffer =
    std::convertible_to<Buffer&, ::fixy::View<std::byte>> || requires(Buffer& buffer) {
        { buffer.view() } -> std::convertible_to<::fixy::View<std::byte>>;
    };

namespace detail {

inline constexpr std::uint16_t gf_poly = 0x11dU;

using GfRow = ::fixy::FixedArray<std::uint8_t, 256>;

struct GfTables {
    ::fixy::FixedArray<std::uint8_t, 512> exp{};
    GfRow log{};
    GfRow inv{};
    ::fixy::FixedArray<GfRow, 256> mul{};
};

// The index of a byte in a table of 256 entries.  Each byte is below 256, so
// the optimizer removes the check.
[[nodiscard, gnu::const]] constexpr ::fixy::Index<256> byte_index(std::uint8_t value) noexcept {
    return ::fixy::Index<256>::check(value).expect("a byte is below 256");
}

// The index of a power in the table exp, which holds 510 powers and two
// zeros.
[[nodiscard, gnu::const]] constexpr ::fixy::Index<512> power_index(std::size_t power) noexcept {
    return ::fixy::Index<512>::check(power).expect("a sum of two logarithms is below 510");
}

template <class Field>
[[nodiscard]] consteval GfTables make_gf_tables() noexcept {
    GfTables tables{};

    std::uint16_t x = 1;
    for (::fixy::Index<255> power : ::fixy::IndexRange<255>{}) {
        tables.exp.at(power) = static_cast<std::uint8_t>(x);
        tables.log.at(byte_index(static_cast<std::uint8_t>(x))) = static_cast<std::uint8_t>(power.raw());
        x = static_cast<std::uint16_t>(x << 1U);
        if ((x & 0x100U) != 0U) {
            x ^= gf_poly;
        }
    }
    for (auto power : tables.exp.indices()) {
        if (power.raw() >= 255) {
            tables.exp.at(power) = tables.exp.at(power_index(power.raw() - 255U));
        }
    }

    for (auto a : tables.mul.indices()) {
        if (a.raw() == 0) {
            continue;
        }
        GfRow& row = tables.mul.at(a);
        for (auto b : row.indices()) {
            if (b.raw() != 0) {
                row.at(b) = tables.exp.at(power_index(static_cast<std::size_t>(tables.log.at(a))
                                                      + static_cast<std::size_t>(tables.log.at(b))));
            }
        }
    }
    for (auto a : tables.inv.indices()) {
        if (a.raw() != 0) {
            tables.inv.at(a) = tables.exp.at(power_index(255U - tables.log.at(a)));
        }
    }
    return tables;
}

// The evaluation of the tables takes about 16.5 million operations.  So the
// tables are a variable template, and each function that reads them is a
// template too, over a parameter that no caller names.  Only a translation
// unit that instantiates a codec then evaluates the tables, and a function
// that is not a template must not read them.  The initializer depends on
// the parameter, because GCC evaluates a call that does not depend on it
// where the template stands.  For the same reason, a template that has the
// parameter names it in each call of mul, of inv and of a template of this
// file that reads them.  A call without it can make each includer evaluate
// the tables, as a call of make_nibble_tables without it did.
template <class Field = void>
inline constexpr GfTables gf = make_gf_tables<Field>();

template <class Field = void>
[[nodiscard, gnu::const]] constexpr std::uint8_t mul(std::uint8_t a, std::uint8_t b) noexcept {
    return gf<Field>.mul.at(byte_index(a)).at(byte_index(b));
}

template <class Field = void>
[[nodiscard, gnu::const]] constexpr std::uint8_t inv(std::uint8_t a) noexcept {
    return gf<Field>.inv.at(byte_index(a));
}

// The byte whose bits are the exclusive or of the bits of a byte and of a
// value.
[[nodiscard, gnu::const]] constexpr std::byte xor_byte(std::byte lhs, std::uint8_t rhs) noexcept {
    return static_cast<std::byte>(static_cast<std::uint8_t>(lhs) ^ rhs);
}

template <std::size_t N>
using SquareMatrix = ::fixy::FixedArray<::fixy::FixedArray<std::uint8_t, N>, N>;

template <std::uint8_t K, std::uint8_t M>
using GeneratorMatrix =
    ::fixy::FixedArray<::fixy::FixedArray<std::uint8_t, K>, static_cast<std::size_t>(K) + static_cast<std::size_t>(M)>;

template <std::uint8_t K, std::uint8_t M>
    requires ReedSolomonShape<K, M>
[[nodiscard]] consteval GeneratorMatrix<K, M> make_generator_matrix() noexcept {
    GeneratorMatrix<K, M> matrix{};

    for (::fixy::Index<K> row : ::fixy::IndexRange<K>{}) {
        matrix.at(row).at(row) = 1;
    }

    for (::fixy::Index<M> parity : ::fixy::IndexRange<M>{}) {
        const auto y = static_cast<std::uint8_t>(K + parity.raw());
        auto& row = matrix.at(::fixy::Index<static_cast<std::size_t>(K) + M>::check(K + parity.raw())
                                  .expect("a parity row is a row of the matrix"));
        for (auto col : row.indices()) {
            row.at(col) = inv(static_cast<std::uint8_t>(static_cast<std::uint8_t>(col.raw()) ^ y));
        }
    }
    return matrix;
}

// The first row at or after col whose entry at col is not zero.
template <std::size_t N>
[[nodiscard]] constexpr ::fixy::Option<::fixy::Index<N>> pivot_row(SquareMatrix<N> const& matrix,
                                                                   ::fixy::Index<N> col) noexcept {
    for (auto row : matrix.indices()) {
        if (row >= col && matrix.at(row).at(col) != 0) {
            return ::fixy::some(row);
        }
    }
    return ::fixy::none;
}

template <std::size_t N>
[[nodiscard]] constexpr bool invert_matrix(SquareMatrix<N> input, SquareMatrix<N>& inverse) noexcept {
    for (auto row : inverse.indices()) {
        ::fixy::fill(inverse.at(row).view(), std::uint8_t{0});
        inverse.at(row).at(row) = 1;
    }

    for (auto col : input.indices()) {
        auto found = pivot_row(input, col);
        if (found.is_none()) {
            return false;
        }
        auto const pivot = std::move(found).expect("the search found a pivot");
        if (pivot != col) {
            ::fixy::swap(input.at(pivot), input.at(col));
            ::fixy::swap(inverse.at(pivot), inverse.at(col));
        }

        const std::uint8_t scale = inv(input.at(col).at(col));
        for (auto j : input.indices()) {
            input.at(col).at(j) = mul(input.at(col).at(j), scale);
            inverse.at(col).at(j) = mul(inverse.at(col).at(j), scale);
        }

        for (auto row : input.indices()) {
            if (row == col) {
                continue;
            }
            const std::uint8_t factor = input.at(row).at(col);
            if (factor == 0) {
                continue;
            }
            for (auto j : input.indices()) {
                input.at(row).at(j) ^= mul(factor, input.at(col).at(j));
                inverse.at(row).at(j) ^= mul(factor, inverse.at(col).at(j));
            }
        }
    }
    return true;
}

#if defined(__AVX2__) || ((defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(__aarch64__))
// The products of coeff and each low nibble, and of coeff and each high
// nibble: the two tables of 16 bytes of a GF(2^8) multiply by nibbles.
struct NibbleTables {
    ::fixy::FixedArray<std::uint8_t, 16> lo{};
    ::fixy::FixedArray<std::uint8_t, 16> hi{};
};

template <class Field = void>
[[nodiscard]] inline NibbleTables make_nibble_tables(std::uint8_t coeff) noexcept {
    NibbleTables tables{};
    for (::fixy::Index<16> nibble : ::fixy::IndexRange<16>{}) {
        const auto value = static_cast<std::uint8_t>(nibble.raw());
        tables.lo.at(nibble) = mul<Field>(coeff, value);
        tables.hi.at(nibble) = mul<Field>(coeff, static_cast<std::uint8_t>(value << 4U));
    }
    return tables;
}

// One register of byte lanes: 32 lanes with AVX2, and 16 lanes with NEON.
// A vector kernel steps one register per iteration, so its stride is the
// size of the register.  The kernels use the operations of
// foundation/Simd.h only, so one kernel serves the two targets.
#if defined(__AVX2__)
using KernelBytes = ::foundation::simd::vec<std::uint8_t, 32>;
using KernelHalfwords = ::foundation::simd::vec<std::uint16_t, 16>;
#else
using KernelBytes = ::foundation::simd::vec<std::uint8_t, 16>;
using KernelHalfwords = ::foundation::simd::vec<std::uint16_t, 8>;
#endif
using NibbleTableBytes = ::foundation::simd::vec<std::uint8_t, 16>;

CRUCIBLE_HOT void xor_bytes_simd(::fixy::View<std::byte> dst, ::fixy::View<std::byte const> src) noexcept {
    namespace simd = ::foundation::simd;
    auto const [target_rest, source_rest] =
        simd::for_each_register_run<KernelBytes>(dst, src, [](auto target, auto source) noexcept {
            simd::store(simd::load<KernelBytes>(target) ^ simd::load<KernelBytes>(source), target);
        });
    for (auto [target, source] : ::fixy::zip(target_rest, source_rest).expect("the two rests have one count")) {
        *target = xor_byte(*target, static_cast<std::uint8_t>(*source));
    }
}

// The product of each byte and coeff is the exclusive or of the products
// of its low nibble and of its high nibble, which lookup_nibbles reads from
// the two tables.  The lookup reads the low four bits of each lane, so the
// low nibble needs no mask.  A shift of each 16-bit halfword by four puts
// the high nibble of each byte in the low four bits of that byte, and the
// bits that come from the next byte are above them.  That shift is one
// vpsrlw with AVX2.  A shift of each byte is a slower affine instruction
// on a host with GFNI.
template <class Field = void>
CRUCIBLE_HOT void mul_xor_simd(::fixy::View<std::byte> dst, ::fixy::View<std::byte const> src,
                               std::uint8_t coeff) noexcept {
    namespace simd = ::foundation::simd;
    if (coeff == 0) {
        return;
    }
    if (coeff == 1) {
        xor_bytes_simd(dst, src);
        return;
    }

    const auto tables = make_nibble_tables<Field>(coeff);
    const NibbleTableBytes lo_table = simd::load<NibbleTableBytes>(tables.lo.view());
    const NibbleTableBytes hi_table = simd::load<NibbleTableBytes>(tables.hi.view());

    auto const [target_rest, source_rest] =
        simd::for_each_register_run<KernelBytes>(dst, src, [&](auto target, auto source) noexcept {
            const KernelBytes bytes = simd::load<KernelBytes>(source);
            const KernelBytes high = simd::bit_cast<KernelBytes>(simd::bit_cast<KernelHalfwords>(bytes) >> 4);
            const KernelBytes prod = simd::lookup_nibbles(lo_table, bytes) ^ simd::lookup_nibbles(hi_table, high);
            simd::store(simd::load<KernelBytes>(target) ^ prod, target);
        });
    for (auto [target, source] : ::fixy::zip(target_rest, source_rest).expect("the two rests have one count")) {
        *target = xor_byte(*target, mul<Field>(static_cast<std::uint8_t>(*source), coeff));
    }
}
#endif

CRUCIBLE_HOT void xor_bytes_scalar(::fixy::View<std::byte> dst, ::fixy::View<std::byte const> src) noexcept {
    for (auto [target, source] : ::fixy::zip(dst, src).expect("the two shards have one count")) {
        *target = xor_byte(*target, static_cast<std::uint8_t>(*source));
    }
}

// The product of each byte of src and coeff, added into the byte of dst at
// the same position.  The two Views have one count.
template <class Field = void>
CRUCIBLE_HOT void mul_xor(::fixy::View<std::byte> dst, ::fixy::View<std::byte const> src, std::uint8_t coeff) noexcept {
    if (coeff == 0) {
        return;
    }
#if defined(__AVX2__) || ((defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(__aarch64__))
    mul_xor_simd<Field>(dst, src, coeff);
#else
    if (coeff == 1) {
        xor_bytes_scalar(dst, src);
        return;
    }
    for (auto [target, source] : ::fixy::zip(dst, src).expect("the two shards have one count")) {
        *target = xor_byte(*target, mul<Field>(static_cast<std::uint8_t>(*source), coeff));
    }
#endif
}

template <ByteContiguousBuffer Buffer>
[[nodiscard]] constexpr ::fixy::View<std::byte const> bytes(Buffer const& buffer) noexcept {
    if constexpr (requires { buffer.view(); }) {
        return buffer.view();
    } else {
        return buffer;
    }
}

template <MutableByteContiguousBuffer Buffer>
[[nodiscard]] constexpr ::fixy::View<std::byte> mutable_bytes(Buffer& buffer) noexcept {
    if constexpr (requires { buffer.view(); }) {
        return buffer.view();
    } else {
        return buffer;
    }
}

}  // namespace detail

template <std::uint8_t K, std::uint8_t M>
    requires ReedSolomonShape<K, M>
class ReedSolomon {
public:
    static constexpr std::uint8_t data_shards = K;
    static constexpr std::uint8_t parity_shards = M;
    static constexpr std::uint16_t total_shards = static_cast<std::uint16_t>(K) + static_cast<std::uint16_t>(M);
    static constexpr std::size_t alignment = 32;
    using concurrent_budget = ::foundation::effects::ConcurrentRow<::foundation::effects::SmBudget<1>>;

    [[nodiscard]] static constexpr std::size_t shard_bytes_for(std::size_t input_size) noexcept {
        const auto q = input_size / K;
        const auto r = input_size % K;
        return q + (r == 0 ? 0U : 1U);
    }

    [[nodiscard]] static constexpr std::size_t encoded_size_for(std::size_t input_size) noexcept {
        const auto shard_bytes = shard_bytes_for(input_size);
        constexpr auto max = ::fixy::max_of<std::size_t>;
        if (shard_bytes > max / total_shards) {
            return max;
        }
        return shard_bytes * total_shards;
    }

    // Each shard of the output is checked as a window of the output, one
    // compare for each shard and none in the byte loops.
    [[nodiscard]] ::fixy::Result<::fixy::Unit, FecError>
    encode(::fixy::View<std::byte const> input, ::fixy::View<std::byte> output_with_parity) const noexcept {
        if (input.size() == 0) {
            return ::fixy::err(FecError::InvalidInputSize);
        }
        const auto shard_bytes = shard_bytes_for(input.size());
        const auto expected = encoded_size_for(input.size());
        if (expected == ::fixy::max_of<std::size_t>) {
            return ::fixy::err(FecError::LengthOverflow);
        }
        if (output_with_parity.size() != expected) {
            return ::fixy::err(FecError::InvalidOutputSize);
        }

        ::fixy::fill(output_with_parity, std::byte{0});
        ::fixy::copy(output_with_parity.window(0, input.size()).expect("the input fits in the data shards"), input)
            .expect("the window has the count of the input");

        for (::fixy::Index<M> parity_shard : ::fixy::IndexRange<M>{}) {
            auto const& row = generator().at(row_of(static_cast<std::size_t>(K) + parity_shard.raw()));
            auto const parity =
                shard_of(output_with_parity, static_cast<std::size_t>(K) + parity_shard.raw(), shard_bytes);
            for (::fixy::Index<K> data_shard : ::fixy::IndexRange<K>{}) {
                detail::mul_xor(parity, shard_of(output_with_parity, data_shard.raw(), shard_bytes),
                                row.at(data_shard));
            }
        }
        return ::fixy::Unit{};
    }

    [[nodiscard]] ::fixy::Result<::fixy::Unit, FecError> decode(::fixy::View<std::byte const> received,
                                                                ::fixy::View<bool const> erasure_mask,
                                                                ::fixy::View<std::byte> output) const noexcept {
        if (received.size() == 0 || (received.size() % total_shards) != 0) {
            return ::fixy::err(FecError::InvalidInputSize);
        }
        if (erasure_mask.size() != total_shards) {
            return ::fixy::err(FecError::ErasureMaskSizeMismatch);
        }

        const auto shard_bytes = received.size() / total_shards;
        if (output.size() == 0 || output.size() > static_cast<std::size_t>(K) * shard_bytes) {
            return ::fixy::err(FecError::InvalidOutputSize);
        }

        // The first K shards that the mask does not erase, and their rows of
        // the generator.
        ::fixy::FixedArray<std::size_t, K> selected{};
        detail::SquareMatrix<K> decode_matrix{};
        std::size_t selected_count = 0;
        std::size_t erased_count = 0;
        auto const mask =
            erasure_mask.template window<total_shards>(0).expect("the mask holds one flag for each shard");
        for (::fixy::Index<total_shards> shard : ::fixy::IndexRange<total_shards>{}) {
            if (mask.at(shard)) {
                ++erased_count;
                continue;
            }
            if (selected_count < K) {
                auto const slot = ::fixy::Index<K>::check(selected_count).expect("the count is below K");
                selected.at(slot) = shard.raw();
                decode_matrix.at(slot) = generator().at(shard);
                ++selected_count;
            }
        }
        if (erased_count > M || selected_count < K) {
            return ::fixy::err(FecError::TooManyErasures);
        }

        detail::SquareMatrix<K> inverse{};
        if (!detail::invert_matrix<K>(decode_matrix, inverse)) {
            return ::fixy::err(FecError::SingularMatrix);
        }

        for (::fixy::Index<K> out_shard : ::fixy::IndexRange<K>{}) {
            const auto out_offset = out_shard.raw() * shard_bytes;
            if (out_offset >= output.size()) {
                break;
            }
            const auto live_bytes = ::fixy::min(shard_bytes, output.size() - out_offset);
            auto const dst = output.window(out_offset, live_bytes).expect("the live bytes lie inside the output");
            ::fixy::fill(dst, std::byte{0});

            for (::fixy::Index<K> src : ::fixy::IndexRange<K>{}) {
                auto const shard = received.window(selected.at(src) * shard_bytes, live_bytes)
                                       .expect("a selected shard lies inside the received block");
                detail::mul_xor(dst, shard, inverse.at(out_shard).at(src));
            }
        }
        return ::fixy::Unit{};
    }

    template <ByteContiguousBuffer Input, MutableByteContiguousBuffer Output>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, FecError> encode_owned(LinearShardBuffer<Input>&& input,
                                                                      Output& output) const noexcept {
        return encode(detail::bytes(input.peek()), detail::mutable_bytes(output));
    }

    template <ByteContiguousBuffer Input, MutableByteContiguousBuffer Output>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, FecError> decode_owned(LinearShardBuffer<Input>&& received,
                                                                      ::fixy::View<bool const> erasure_mask,
                                                                      Output& output) const noexcept {
        return decode(detail::bytes(received.peek()), erasure_mask, detail::mutable_bytes(output));
    }

private:
    // The generator matrix of this shape.  It is a static local of a member
    // function and not a static data member, so a translation unit that
    // only names the class evaluates neither the matrix nor the field tables
    // that the matrix reads.  The local is constexpr, so it has constant
    // initialization and no guard.
    [[nodiscard]] static constexpr detail::GeneratorMatrix<K, M> const& generator() noexcept {
        static constexpr detail::GeneratorMatrix<K, M> matrix = detail::make_generator_matrix<K, M>();
        return matrix;
    }

    // The index of a row of the generator.
    [[nodiscard]] static constexpr ::fixy::Index<total_shards> row_of(std::size_t shard) noexcept {
        return ::fixy::Index<total_shards>::check(shard).expect("a shard is below the count of shards");
    }

    // The bytes of the shard at position shard of a block of shards.
    [[nodiscard]] static constexpr ::fixy::View<std::byte> shard_of(::fixy::View<std::byte> block, std::size_t shard,
                                                                    std::size_t shard_bytes) noexcept {
        return block.window(shard * shard_bytes, shard_bytes).expect("the shard lies inside the block");
    }
};

template <std::uint8_t K, std::uint8_t M>
    requires ReedSolomonShape<K, M>
[[nodiscard]] constexpr ReedSolomon<K, M> mint_reed_solomon(::foundation::effects::Init) noexcept {
    return ReedSolomon<K, M>{};
}

}  // namespace crucible::cntp
