#pragma once

#include <crucible/Philox.h>
#include <crucible/cntp/Fec.h>
#include <fixy/Bands.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <foundation/Platform.h>
#include <foundation/effects/Concurrent.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Resources.h>
#include <foundation/effects/Row.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::cntp {

struct LtFountain {};

template <typename Algorithm>
concept FountainAlgorithm = std::same_as<Algorithm, LtFountain>;

template <std::size_t SourceSymbols, std::size_t SymbolBytes, std::size_t MaxEquations>
concept FountainShape = SourceSymbols > 0 && SourceSymbols <= 64 && SymbolBytes > 0
                     && (SymbolBytes <= ::fixy::max_of<std::uint16_t>) && MaxEquations >= SourceSymbols;

// A codec holds its tables by value, so a mint zeroes every byte of them.
// That is start-up work, so the mint takes a context that admits the
// initialization row, and the shape must hold.
template <class Ctx, std::size_t SourceSymbols, std::size_t SymbolBytes, std::size_t MaxEquations, typename Algorithm>
concept CtxFitsFountainMint =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Init>>
    && FountainShape<SourceSymbols, SymbolBytes, MaxEquations> && FountainAlgorithm<Algorithm>;

enum class FountainError : std::uint8_t {
    InvalidInputSize,
    InvalidOutputSize,
    PacketShapeMismatch,
    EquationCapacityExceeded,
    InconsistentEquation,
    DecodeIncomplete,
    EncoderNotStarted,
};

enum class FountainDecodeState : std::uint8_t {
    NeedsMore,
    Complete,
};

using FountainSeed = Philox::DetSafePureKey;

template <typename Buffer>
using LinearFountainBuffer = ::fixy::Linear<Buffer>;

template <std::size_t SymbolBytes>
using FountainSymbol = ::foundation::core::FixedArray<std::byte, SymbolBytes>;

template <std::size_t SourceSymbols, std::size_t SymbolBytes>
    requires FountainShape<SourceSymbols, SymbolBytes, SourceSymbols>
struct FountainPacket {
    static constexpr std::size_t source_symbols = SourceSymbols;
    static constexpr std::size_t symbol_bytes = SymbolBytes;
    static constexpr std::size_t max_source_bytes = SourceSymbols * SymbolBytes;

    // A source holds at least one byte and at most the full capacity.
    static constexpr auto source_bytes_range = ::fixy::in_range<std::size_t{1}, max_source_bytes>;
    using source_byte_count = ::fixy::Refined<source_bytes_range, std::size_t>;

    // The one door to a byte count.  A count outside the range comes back
    // as the error, so a wire decoder can refuse the packet and go on.
    [[nodiscard]] static constexpr ::fixy::Result<source_byte_count, FountainError>
    admit_source_bytes(std::size_t bytes) noexcept {
        return ::fixy::admit_refined<source_bytes_range>(bytes, FountainError::InvalidInputSize);
    }

    std::uint32_t encoding_id = 0;
    std::uint16_t symbol_count = static_cast<std::uint16_t>(SourceSymbols);
    std::uint16_t bytes_per_symbol = static_cast<std::uint16_t>(SymbolBytes);
    source_byte_count source_bytes = ::fixy::mint_refined<source_bytes_range>(std::size_t{1});
    std::uint64_t mask = 0;
    FountainSymbol<SymbolBytes> payload{};
};

template <std::size_t SourceSymbols, std::size_t SymbolBytes>
using LinearFountainPacket = ::fixy::Linear<FountainPacket<SourceSymbols, SymbolBytes>>;

template <std::size_t SourceSymbols, std::size_t SymbolBytes, typename Algorithm>
    requires FountainShape<SourceSymbols, SymbolBytes, SourceSymbols> && FountainAlgorithm<Algorithm>
class FountainEncoder;

template <std::size_t SourceSymbols, std::size_t SymbolBytes, std::size_t MaxEquations, typename Algorithm>
    requires FountainShape<SourceSymbols, SymbolBytes, MaxEquations> && FountainAlgorithm<Algorithm>
class FountainDecoder;

// The one door to each codec.  The default constructor of each codec is
// private, and its mint below is the one friend.
template <std::size_t SourceSymbols, std::size_t SymbolBytes, typename Algorithm = LtFountain,
          ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsFountainMint<Ctx, SourceSymbols, SymbolBytes, SourceSymbols, Algorithm>
[[nodiscard]] constexpr FountainEncoder<SourceSymbols, SymbolBytes, Algorithm>
mint_fountain_encoder(Ctx const&) noexcept;

template <std::size_t SourceSymbols, std::size_t SymbolBytes,
          std::size_t MaxEquations = SourceSymbols + (SourceSymbols / 2U) + 8U, typename Algorithm = LtFountain,
          ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsFountainMint<Ctx, SourceSymbols, SymbolBytes, MaxEquations, Algorithm>
[[nodiscard]] constexpr FountainDecoder<SourceSymbols, SymbolBytes, MaxEquations, Algorithm>
mint_fountain_decoder(Ctx const&) noexcept;

namespace fountain_detail {

template <std::size_t SourceSymbols>
[[nodiscard]] consteval std::uint64_t valid_mask() noexcept {
    if constexpr (SourceSymbols == 64) {
        return ::fixy::max_of<std::uint64_t>;
    } else {
        return (std::uint64_t{1} << SourceSymbols) - 1U;
    }
}

[[nodiscard, gnu::const]] constexpr std::uint64_t repair_offset(std::uint32_t encoding_id,
                                                                std::uint32_t lane) noexcept {
    return (static_cast<std::uint64_t>(encoding_id) << 32U) | static_cast<std::uint64_t>(lane);
}

template <std::size_t SourceSymbols>
    requires(SourceSymbols > 0 && SourceSymbols <= 64)
[[nodiscard]] constexpr std::uint8_t sample_ideal_soliton_degree(std::uint32_t uniform) noexcept {
    if constexpr (SourceSymbols == 1) {
        return 1;
    } else {
        constexpr auto range = std::uint64_t{1} << 32U;
        for (std::size_t d = 1; d <= SourceSymbols; ++d) {
            std::uint64_t numerator = 1;
            std::uint64_t denominator = SourceSymbols;
            if (d > 1) {
                numerator = (d * SourceSymbols) + d - SourceSymbols;
                denominator = SourceSymbols * d;
            }

            const auto lhs = static_cast<std::uint64_t>(uniform) * denominator;
            const auto rhs = numerator * range;
            if (lhs < rhs) {
                return static_cast<std::uint8_t>(d);
            }
        }
        return static_cast<std::uint8_t>(SourceSymbols);
    }
}

template <std::size_t SourceSymbols>
    requires(SourceSymbols > 0 && SourceSymbols <= 64)
[[nodiscard]] CRUCIBLE_HOT std::uint64_t repair_mask(std::uint32_t encoding_id, FountainSeed seed) noexcept {
    const auto degree_rng = Philox::generate_det(repair_offset(encoding_id, 0), seed).peek();
    const auto degree = sample_ideal_soliton_degree<SourceSymbols>(degree_rng.at(::fixy::index<0>()));

    ::foundation::core::FixedArray<std::uint8_t, SourceSymbols> permutation{};
    for (auto position : permutation.indices()) {
        permutation.at(position) = static_cast<std::uint8_t>(position.raw());
    }

    std::uint64_t mask = 0;
    for (::fixy::Index<SourceSymbols> step : permutation.indices()) {
        const std::size_t i = step.raw();
        if (i >= degree) {
            break;
        }
        const auto rng =
            Philox::generate_det(repair_offset(encoding_id, static_cast<std::uint32_t>(i + 1)), seed).peek();
        const auto span = SourceSymbols - i;
        // Lemire's nearly-divisionless range rather than `% span`, which
        // is biased.  The residual bias is span/2^64, at most 2^-58 for
        // span <= 64.  The __uint128_t multiply is bit-identical on every
        // supported platform, so the draw stays deterministic.  The high
        // word of the product is below span, so j is below SourceSymbols.
        const auto uniform64 = (static_cast<std::uint64_t>(rng.at(::fixy::index<1>())) << 32U)
                             | static_cast<std::uint64_t>(rng.at(::fixy::index<0>()));
        const auto product = static_cast<__uint128_t>(uniform64) * static_cast<__uint128_t>(span);
        const auto j = i + static_cast<std::size_t>(product >> 64U);
        [[assume(j < SourceSymbols)]];
        ::fixy::swap(permutation.at(step),
                     permutation.at(::fixy::Index<SourceSymbols>::check(j).expect("the draw is a remaining symbol")));
        mask |= std::uint64_t{1} << permutation.at(step);
    }
    return mask;
}

template <std::size_t SourceSymbols>
    requires(SourceSymbols > 0 && SourceSymbols <= 64)
[[nodiscard]] CRUCIBLE_HOT std::uint64_t packet_mask(std::uint32_t encoding_id, FountainSeed seed) noexcept {
    if (encoding_id < SourceSymbols) {
        return std::uint64_t{1} << encoding_id;
    }
    return repair_mask<SourceSymbols>(encoding_id, seed);
}

// The bytes of the source symbol at position symbol of input, with zeros
// past the end of the input.
template <std::size_t SymbolBytes>
CRUCIBLE_HOT void copy_symbol(::fixy::View<std::byte const> input, std::size_t symbol,
                              FountainSymbol<SymbolBytes>& out) noexcept {
    ::fixy::fill(out.view(), std::byte{0});

    const auto start = symbol * SymbolBytes;
    if (start >= input.size()) {
        return;
    }

    const auto live = ::fixy::min(SymbolBytes, input.size() - start);
    ::fixy::copy(out.view().window(0, live).expect("the live bytes fit in a symbol"),
                 input.window(start, live).expect("the live bytes lie inside the input"))
        .expect("the two windows have the count of the live bytes");
}

template <std::size_t SymbolBytes>
[[nodiscard]] CRUCIBLE_HOT bool zero_symbol(FountainSymbol<SymbolBytes> const& symbol) noexcept {
    return symbol == FountainSymbol<SymbolBytes>{};
}

}  // namespace fountain_detail

template <std::size_t SourceSymbols, std::size_t SymbolBytes, typename Algorithm = LtFountain>
    requires FountainShape<SourceSymbols, SymbolBytes, SourceSymbols> && FountainAlgorithm<Algorithm>
class FountainEncoder {
public:
    using packet_type = FountainPacket<SourceSymbols, SymbolBytes>;
    using source_byte_count = typename packet_type::source_byte_count;
    using concurrent_budget = ::foundation::effects::ConcurrentRow<::foundation::effects::SmBudget<1>>;

    static constexpr std::size_t source_symbols = SourceSymbols;
    static constexpr std::size_t symbol_bytes = SymbolBytes;
    static constexpr std::size_t max_source_bytes = packet_type::max_source_bytes;

    [[nodiscard]] static constexpr ::fixy::Result<source_byte_count, FountainError>
    admit_source_bytes(std::size_t bytes) noexcept {
        return packet_type::admit_source_bytes(bytes);
    }

    [[nodiscard]] ::fixy::Result<::fixy::Unit, FountainError> start_encoding(::fixy::View<std::byte const> input,
                                                                             FountainSeed seed) noexcept {
        auto admitted = admit_source_bytes(input.size());
        if (admitted.is_err()) {
            return ::fixy::err(admitted.err().expect("the admission refused the count"));
        }

        input_ = input;
        seed_ = seed;
        source_bytes_ = std::move(admitted).expect("the admission took the count");
        next_id_ = 0;
        active_ = true;
        return ::fixy::Unit{};
    }

    [[nodiscard]] ::fixy::Result<packet_type, FountainError> next_packet() noexcept {
        if (!active_) {
            return ::fixy::err(FountainError::EncoderNotStarted);
        }

        auto packet = encode_packet(input_, seed_, source_bytes_, next_id_);
        if (packet.is_ok()) {
            ++next_id_;
        }
        return packet;
    }

    [[nodiscard]] ::fixy::Result<LinearFountainPacket<SourceSymbols, SymbolBytes>, FountainError>
    next_packet_owned() noexcept {
        auto packet = next_packet();
        if (packet.is_err()) {
            return ::fixy::err(packet.err().expect("the encoder refused the packet"));
        }
        return ::fixy::mint_linear<packet_type>(std::move(packet).expect("the encoder gave the packet"));
    }

    [[nodiscard]] ::fixy::Result<packet_type, FountainError>
    encode_packet(::fixy::View<std::byte const> input, FountainSeed seed, std::uint32_t encoding_id) const noexcept {
        auto admitted = admit_source_bytes(input.size());
        if (admitted.is_err()) {
            return ::fixy::err(admitted.err().expect("the admission refused the count"));
        }
        return encode_packet(input, seed, std::move(admitted).expect("the admission took the count"), encoding_id);
    }

    template <ByteContiguousBuffer Input>
    [[nodiscard]] ::fixy::Result<LinearFountainPacket<SourceSymbols, SymbolBytes>, FountainError>
    encode_owned(LinearFountainBuffer<Input>&& input, FountainSeed seed, std::uint32_t encoding_id) const noexcept {
        auto raw = std::move(input).consume();
        auto packet = encode_packet(detail::bytes(raw), seed, encoding_id);
        if (packet.is_err()) {
            return ::fixy::err(packet.err().expect("the encoder refused the packet"));
        }
        return ::fixy::mint_linear<packet_type>(std::move(packet).expect("the encoder gave the packet"));
    }

private:
    constexpr FountainEncoder() noexcept = default;

    template <std::size_t Symbols, std::size_t Bytes, typename Algo, ::foundation::effects::IsExecCtx FriendCtx>
        requires CtxFitsFountainMint<FriendCtx, Symbols, Bytes, Symbols, Algo>
    friend constexpr FountainEncoder<Symbols, Bytes, Algo> mint_fountain_encoder(FriendCtx const&) noexcept;

    [[nodiscard]] CRUCIBLE_HOT ::fixy::Result<packet_type, FountainError>
    encode_packet(::fixy::View<std::byte const> input, FountainSeed seed, source_byte_count source_bytes,
                  std::uint32_t encoding_id) const noexcept {
        auto const mask = fountain_detail::packet_mask<SourceSymbols>(encoding_id, seed);
        if ((mask == 0) || ((mask & ~fountain_detail::valid_mask<SourceSymbols>()) != 0)) {
            return ::fixy::err(FountainError::PacketShapeMismatch);
        }

        packet_type packet{};
        packet.encoding_id = encoding_id;
        packet.source_bytes = source_bytes;
        packet.mask = mask;

        FountainSymbol<SymbolBytes> symbol{};
        auto remaining = mask;
        while (remaining != 0) {
            const auto source = static_cast<std::size_t>(::fixy::countr_zero(remaining));
            fountain_detail::copy_symbol(input, source, symbol);
            detail::mul_xor(packet.payload.view(), symbol.view(), 1);
            remaining &= remaining - 1U;
        }

        return packet;
    }

    ::fixy::View<std::byte const> input_{};
    FountainSeed seed_ = ::fixy::mint_band<FountainSeed>(0);
    source_byte_count source_bytes_ = ::fixy::mint_refined<packet_type::source_bytes_range>(std::size_t{1});
    std::uint32_t next_id_ = 0;
    bool active_ = false;
};

template <std::size_t SourceSymbols, std::size_t SymbolBytes,
          std::size_t MaxEquations = SourceSymbols + (SourceSymbols / 2U) + 8U, typename Algorithm = LtFountain>
    requires FountainShape<SourceSymbols, SymbolBytes, MaxEquations> && FountainAlgorithm<Algorithm>
class FountainDecoder {
public:
    using packet_type = FountainPacket<SourceSymbols, SymbolBytes>;
    using source_byte_count = typename packet_type::source_byte_count;

    static constexpr std::size_t source_symbols = SourceSymbols;
    static constexpr std::size_t symbol_bytes = SymbolBytes;
    static constexpr std::size_t max_equations = MaxEquations;
    static constexpr std::size_t max_source_bytes = packet_type::max_source_bytes;

    [[nodiscard]] ::fixy::Result<FountainDecodeState, FountainError> add_packet(packet_type const& packet) noexcept {
        CRUCIBLE_TRY(_, validate_packet(packet));
        remember_source_bytes(packet.source_bytes);

        Equation equation{
            .mask = packet.mask,
            .payload = packet.payload,
        };
        reduce_known(equation);
        if (equation.mask == 0) {
            if (!fountain_detail::zero_symbol(equation.payload)) {
                return ::fixy::err(FountainError::InconsistentEquation);
            }
            return state();
        }

        if (::fixy::popcount(equation.mask) == 1) {
            CRUCIBLE_TRY(_, admit_symbol(equation.mask, equation.payload));
            CRUCIBLE_TRY(_, peel());
            return state();
        }

        CRUCIBLE_TRY(_, store_equation(equation));
        return state();
    }

    [[nodiscard]] ::fixy::Result<FountainDecodeState, FountainError>
    add_packet_owned(LinearFountainPacket<SourceSymbols, SymbolBytes>&& packet) noexcept {
        auto raw = std::move(packet).consume();
        return add_packet(raw);
    }

    [[nodiscard]] ::fixy::Result<::fixy::View<std::byte const>, FountainError> extract_decoded() const noexcept {
        if (!complete()) {
            return ::fixy::err(FountainError::DecodeIncomplete);
        }
        return decoded_.view().window(0, source_bytes_.value()).expect("the source bytes fit in the decoded symbols");
    }

    [[nodiscard]] ::fixy::Result<::fixy::Unit, FountainError>
    extract_decoded(::fixy::View<std::byte> output) const noexcept {
        if (!complete()) {
            return ::fixy::err(FountainError::DecodeIncomplete);
        }
        if (output.size() != source_bytes_.value()) {
            return ::fixy::err(FountainError::InvalidOutputSize);
        }
        ::fixy::copy(output, decoded_.view().window(0, output.size()).expect("the output fits in the decoded symbols"))
            .expect("the two Views have the count of the source bytes");
        return ::fixy::Unit{};
    }

    [[nodiscard]] constexpr bool complete() const noexcept {
        return decoded_mask_ == fountain_detail::valid_mask<SourceSymbols>();
    }

    [[nodiscard]] constexpr std::size_t decoded_count() const noexcept {
        return static_cast<std::size_t>(::fixy::popcount(decoded_mask_));
    }

private:
    constexpr FountainDecoder() noexcept = default;

    template <std::size_t Symbols, std::size_t Bytes, std::size_t Equations, typename Algo,
              ::foundation::effects::IsExecCtx FriendCtx>
        requires CtxFitsFountainMint<FriendCtx, Symbols, Bytes, Equations, Algo>
    friend constexpr FountainDecoder<Symbols, Bytes, Equations, Algo> mint_fountain_decoder(FriendCtx const&) noexcept;

    struct Equation {
        std::uint64_t mask = 0;
        FountainSymbol<SymbolBytes> payload{};
    };

    // The source symbol of the lowest bit of a mask.  validate_packet
    // admits only the bits of valid_mask, and each equation mask is a part
    // of a packet mask, so the bit is below SourceSymbols.
    [[nodiscard]] static constexpr ::fixy::Index<SourceSymbols> symbol_of(std::uint64_t mask) noexcept {
        return ::fixy::Index<SourceSymbols>::check(static_cast<std::size_t>(::fixy::countr_zero(mask)))
            .expect("a bit of an admitted mask is a source symbol");
    }

    // The bytes of a source symbol in decoded_.
    [[nodiscard]] constexpr ::fixy::View<std::byte const, SymbolBytes>
    decoded_symbol(::fixy::Index<SourceSymbols> symbol) const noexcept {
        return decoded_.view()
            .template window<SymbolBytes>(symbol.raw() * SymbolBytes)
            .expect("each source symbol lies inside the decoded bytes");
    }

    [[nodiscard]] ::fixy::Result<::fixy::Unit, FountainError>
    validate_packet(packet_type const& packet) const noexcept {
        if (packet.symbol_count != SourceSymbols || packet.bytes_per_symbol != SymbolBytes || packet.mask == 0
            || ((packet.mask & ~fountain_detail::valid_mask<SourceSymbols>()) != 0)) {
            return ::fixy::err(FountainError::PacketShapeMismatch);
        }
        // A wire decoder can build source_bytes through mint_refined_trusted,
        // which skips the bound, so the bound is not proved on arrival.  It
        // is checked again here because extract_decoded() hands out
        // source_bytes_ bytes of decoded_, which is max_source_bytes long.
        const auto claimed_source_bytes = packet.source_bytes.value();
        if (claimed_source_bytes == 0 || claimed_source_bytes > max_source_bytes) {
            return ::fixy::err(FountainError::PacketShapeMismatch);
        }
        if (have_source_bytes_ && claimed_source_bytes != source_bytes_.value()) {
            return ::fixy::err(FountainError::PacketShapeMismatch);
        }
        return ::fixy::Unit{};
    }

    constexpr void remember_source_bytes(source_byte_count source_bytes) noexcept {
        if (!have_source_bytes_) {
            source_bytes_ = source_bytes;
            have_source_bytes_ = true;
        }
    }

    void reduce_known(Equation& equation) const noexcept {
        auto overlap = equation.mask & decoded_mask_;
        while (overlap != 0) {
            auto const symbol = symbol_of(overlap);
            detail::mul_xor(equation.payload.view(), decoded_symbol(symbol), 1);
            equation.mask &= ~(std::uint64_t{1} << symbol.raw());
            overlap &= overlap - 1U;
        }
    }

    [[nodiscard]] ::fixy::Result<::fixy::Unit, FountainError>
    admit_symbol(std::uint64_t singleton_mask, FountainSymbol<SymbolBytes> const& payload) noexcept {
        auto const symbol = symbol_of(singleton_mask);

        if ((decoded_mask_ & singleton_mask) != 0) {
            if (!::fixy::equal(payload.view(), decoded_symbol(symbol))) {
                return ::fixy::err(FountainError::InconsistentEquation);
            }
            return ::fixy::Unit{};
        }

        ::fixy::copy(decoded_.view()
                         .template window<SymbolBytes>(symbol.raw() * SymbolBytes)
                         .expect("each source symbol lies inside the decoded bytes"),
                     payload.view());
        decoded_mask_ |= singleton_mask;
        return ::fixy::Unit{};
    }

    [[nodiscard]] ::fixy::Result<::fixy::Unit, FountainError> peel() noexcept {
        bool progressed = true;
        while (progressed) {
            progressed = false;
            for (Equation& equation : equations_) {
                if (equation.mask == 0) {
                    continue;
                }

                reduce_known(equation);
                if (equation.mask == 0) {
                    if (!fountain_detail::zero_symbol(equation.payload)) {
                        return ::fixy::err(FountainError::InconsistentEquation);
                    }
                    continue;
                }
                if (::fixy::popcount(equation.mask) != 1) {
                    continue;
                }

                CRUCIBLE_TRY(_, admit_symbol(equation.mask, equation.payload));
                equation.mask = 0;
                progressed = true;
            }
        }
        return ::fixy::Unit{};
    }

    [[nodiscard]] ::fixy::Result<::fixy::Unit, FountainError> store_equation(Equation const& equation) noexcept {
        for (Equation& existing : equations_) {
            if (existing.mask == equation.mask) {
                if (!(equation.payload == existing.payload)) {
                    return ::fixy::err(FountainError::InconsistentEquation);
                }
                return ::fixy::Unit{};
            }
        }

        for (Equation& existing : equations_) {
            if (existing.mask == 0) {
                existing = equation;
                return ::fixy::Unit{};
            }
        }
        return ::fixy::err(FountainError::EquationCapacityExceeded);
    }

    [[nodiscard]] constexpr FountainDecodeState state() const noexcept {
        return complete() ? FountainDecodeState::Complete : FountainDecodeState::NeedsMore;
    }

    ::foundation::core::FixedArray<Equation, MaxEquations> equations_{};
    ::foundation::core::FixedArray<std::byte, SourceSymbols * SymbolBytes> decoded_{};
    source_byte_count source_bytes_ = ::fixy::mint_refined<packet_type::source_bytes_range>(std::size_t{1});
    std::uint64_t decoded_mask_ = 0;
    bool have_source_bytes_ = false;
};

template <std::size_t SourceSymbols, std::size_t SymbolBytes, typename Algorithm, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsFountainMint<Ctx, SourceSymbols, SymbolBytes, SourceSymbols, Algorithm>
[[nodiscard]] constexpr FountainEncoder<SourceSymbols, SymbolBytes, Algorithm>
mint_fountain_encoder(Ctx const&) noexcept {
    return FountainEncoder<SourceSymbols, SymbolBytes, Algorithm>{};
}

template <std::size_t SourceSymbols, std::size_t SymbolBytes, std::size_t MaxEquations, typename Algorithm,
          ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsFountainMint<Ctx, SourceSymbols, SymbolBytes, MaxEquations, Algorithm>
[[nodiscard]] constexpr FountainDecoder<SourceSymbols, SymbolBytes, MaxEquations, Algorithm>
mint_fountain_decoder(Ctx const&) noexcept {
    return FountainDecoder<SourceSymbols, SymbolBytes, MaxEquations, Algorithm>{};
}

}  // namespace crucible::cntp
