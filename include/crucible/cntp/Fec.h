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

// The AVX2 kernels call GCC builtins, so they include no intrinsics header.
// Only the NEON kernels include one.
#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(__aarch64__)
#include <arm_neon.h>
#endif

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

using GfRow = ::foundation::core::FixedArray<std::uint8_t, 256>;

struct GfTables {
    ::foundation::core::FixedArray<std::uint8_t, 512> exp{};
    GfRow log{};
    GfRow inv{};
    ::foundation::core::FixedArray<GfRow, 256> mul{};
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
using SquareMatrix = ::foundation::core::FixedArray<::foundation::core::FixedArray<std::uint8_t, N>, N>;

template <std::uint8_t K, std::uint8_t M>
using GeneratorMatrix = ::foundation::core::FixedArray<::foundation::core::FixedArray<std::uint8_t, K>,
                                                       static_cast<std::size_t>(K) + static_cast<std::size_t>(M)>;

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
using NibbleTable = ::foundation::core::FixedArray<std::uint8_t, 32>;

struct alignas(32) NibbleTables {
    alignas(32) NibbleTable lo{};
    alignas(32) NibbleTable hi{};
};

template <class Field = void>
[[nodiscard]] inline NibbleTables make_nibble_tables(std::uint8_t coeff) noexcept {
    NibbleTables tables{};
    for (::fixy::Index<16> low : ::fixy::IndexRange<16>{}) {
        const auto value = static_cast<std::uint8_t>(low.raw());
        const auto lo = mul<Field>(coeff, value);
        const auto hi = mul<Field>(coeff, static_cast<std::uint8_t>(value << 4U));
        auto const upper = ::fixy::Index<32>::check(low.raw() + 16U).expect("the upper half holds 16 entries");
        tables.lo.at(low) = lo;
        tables.hi.at(low) = hi;
        tables.lo.at(upper) = lo;
        tables.hi.at(upper) = hi;
    }
    return tables;
}
#endif

// A vector kernel steps one register per iteration, so its stride is the
// size of the register type that its loads use.
#if defined(__AVX2__)
// One AVX2 register as 32 byte lanes, and as the 16 halfword lanes that the
// shift builtin takes.  Each operation of a kernel is a function call.  GCC
// evaluates the arguments of a call from right to left, and that order sets
// the register allocation of the kernel.
using Avx2Bytes = ::foundation::simd::vec<char, 32>;
using Avx2Halfwords = ::foundation::simd::vec<short, 16>;

// The facade of foundation/Simd.h has no byte shuffle, and the GF(2^8)
// nibble-table lookup needs one.  So the kernel calls the builtin of
// vpshufb.  Each 128-bit half of the table serves the indices of its own
// half, and the nibble tables hold the same 16 bytes in each half.
[[nodiscard]] CRUCIBLE_INLINE Avx2Bytes shuffle_bytes_avx2(Avx2Bytes table, Avx2Bytes indices) noexcept {
    return Avx2Bytes{__builtin_ia32_pshufb256(table.raw(), indices.raw())};
}

// A logical right shift of each 16-bit lane, as vpsrlw does it.
[[nodiscard]] CRUCIBLE_INLINE Avx2Bytes shift_halfwords_right_avx2(Avx2Bytes value, int count) noexcept {
    return ::foundation::simd::bit_cast<Avx2Bytes>(
        Avx2Halfwords{__builtin_ia32_psrlwi256(::foundation::simd::bit_cast<Avx2Halfwords>(value).raw(), count)});
}

// The bitwise operations of two registers.
[[nodiscard]] CRUCIBLE_INLINE Avx2Bytes and_avx2(Avx2Bytes lhs, Avx2Bytes rhs) noexcept { return lhs & rhs; }

[[nodiscard]] CRUCIBLE_INLINE Avx2Bytes xor_avx2(Avx2Bytes lhs, Avx2Bytes rhs) noexcept { return lhs ^ rhs; }

CRUCIBLE_HOT void xor_bytes_avx2(::fixy::View<std::byte> dst, ::fixy::View<std::byte const> src) noexcept {
    namespace simd = ::foundation::simd;
    auto const [target_rest, source_rest] =
        simd::for_each_register_run<Avx2Bytes>(dst, src, [](auto target, auto source) noexcept {
            simd::store(xor_avx2(simd::load<Avx2Bytes>(target), simd::load<Avx2Bytes>(source)), target);
        });
    for (auto [target, source] : ::fixy::zip(target_rest, source_rest).expect("the two rests have one count")) {
        *target = xor_byte(*target, static_cast<std::uint8_t>(*source));
    }
}

template <class Field = void>
CRUCIBLE_HOT void mul_xor_avx2(::fixy::View<std::byte> dst, ::fixy::View<std::byte const> src,
                               std::uint8_t coeff) noexcept {
    namespace simd = ::foundation::simd;
    if (coeff == 0) {
        return;
    }
    if (coeff == 1) {
        xor_bytes_avx2(dst, src);
        return;
    }

    const auto tables = make_nibble_tables<Field>(coeff);
    const Avx2Bytes lo_table = simd::load<Avx2Bytes>(tables.lo.view());
    const Avx2Bytes hi_table = simd::load<Avx2Bytes>(tables.hi.view());
    const Avx2Bytes mask{char{0x0f}};

    auto const [target_rest, source_rest] =
        simd::for_each_register_run<Avx2Bytes>(dst, src, [&](auto target, auto source) noexcept {
            const Avx2Bytes bytes = simd::load<Avx2Bytes>(source);
            const Avx2Bytes lo = and_avx2(bytes, mask);
            const Avx2Bytes hi = and_avx2(shift_halfwords_right_avx2(bytes, 4), mask);
            const Avx2Bytes prod = xor_avx2(shuffle_bytes_avx2(lo_table, lo), shuffle_bytes_avx2(hi_table, hi));
            const Avx2Bytes old = simd::load<Avx2Bytes>(target);
            simd::store(xor_avx2(old, prod), target);
        });
    for (auto [target, source] : ::fixy::zip(target_rest, source_rest).expect("the two rests have one count")) {
        *target = xor_byte(*target, mul<Field>(static_cast<std::uint8_t>(*source), coeff));
    }
}
#elif (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(__aarch64__)
// One NEON register as 16 byte lanes.  Its raw vector converts to and from
// uint8x16_t, the type of the intrinsics.
using NeonBytes = ::foundation::simd::vec<std::uint8_t, 16>;

CRUCIBLE_HOT void xor_bytes_neon(::fixy::View<std::byte> dst, ::fixy::View<std::byte const> src) noexcept {
    namespace simd = ::foundation::simd;
    auto const [target_rest, source_rest] =
        simd::for_each_register_run<NeonBytes>(dst, src, [](auto target, auto source) noexcept {
            const NeonBytes::raw_type sum =
                veorq_u8(simd::load<NeonBytes>(target).raw(), simd::load<NeonBytes>(source).raw());
            simd::store(NeonBytes{sum}, target);
        });
    for (auto [target, source] : ::fixy::zip(target_rest, source_rest).expect("the two rests have one count")) {
        *target = xor_byte(*target, static_cast<std::uint8_t>(*source));
    }
}

template <class Field = void>
CRUCIBLE_HOT void mul_xor_neon(::fixy::View<std::byte> dst, ::fixy::View<std::byte const> src,
                               std::uint8_t coeff) noexcept {
    namespace simd = ::foundation::simd;
    if (coeff == 0) {
        return;
    }
    if (coeff == 1) {
        xor_bytes_neon(dst, src);
        return;
    }

    const auto tables = make_nibble_tables<Field>(coeff);
    const auto lo_table =
        simd::load<NeonBytes>(tables.lo.view().template window<16>(0).expect("the low half of the table")).raw();
    const auto hi_table =
        simd::load<NeonBytes>(tables.hi.view().template window<16>(0).expect("the low half of the table")).raw();
    const auto mask = vdupq_n_u8(0x0f);

    auto const [target_rest, source_rest] =
        simd::for_each_register_run<NeonBytes>(dst, src, [&](auto target, auto source) noexcept {
            const auto bytes = simd::load<NeonBytes>(source).raw();
            const auto lo = vandq_u8(bytes, mask);
            const auto hi = vandq_u8(vshrq_n_u8(bytes, 4), mask);
            const auto prod = veorq_u8(vqtbl1q_u8(lo_table, lo), vqtbl1q_u8(hi_table, hi));
            const auto old = simd::load<NeonBytes>(target).raw();
            const NeonBytes::raw_type sum = veorq_u8(old, prod);
            simd::store(NeonBytes{sum}, target);
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
#if defined(__AVX2__)
    mul_xor_avx2<Field>(dst, src, coeff);
#elif (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(__aarch64__)
    mul_xor_neon<Field>(dst, src, coeff);
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
        ::foundation::core::FixedArray<std::size_t, K> selected{};
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
