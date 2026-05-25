#pragma once

// Deterministic LT fountain coding for CNT-P multicast / gossip payloads.
//
// This is the bounded, static-storage LT primitive.  It sends the first
// SourceSymbols packets systematically, then emits Philox-determined repair
// packets.  Decoding uses the standard peeling algorithm over XOR equations.
// Raptor10 pre-coding is intentionally not claimed here; it belongs behind a
// separate algorithm tag once the RFC-5053 matrix machinery lands.

#include <crucible/Philox.h>
#include <crucible/cntp/Fec.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Refined.h>

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>

namespace crucible::cntp {

struct LtFountain {};

template <typename Algorithm>
concept FountainAlgorithm = std::same_as<Algorithm, LtFountain>;

template <std::size_t SourceSymbols,
          std::size_t SymbolBytes,
          std::size_t MaxEquations>
concept FountainShape =
    SourceSymbols > 0 &&
    SourceSymbols <= 64 &&
    SymbolBytes > 0 &&
    SymbolBytes <= std::numeric_limits<std::uint16_t>::max() &&
    MaxEquations >= SourceSymbols;

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
using LinearFountainBuffer = safety::Linear<Buffer>;

template <std::size_t SourceSymbols, std::size_t SymbolBytes>
    requires FountainShape<SourceSymbols, SymbolBytes, SourceSymbols>
struct FountainPacket {
    static constexpr std::size_t source_symbols = SourceSymbols;
    static constexpr std::size_t symbol_bytes = SymbolBytes;
    static constexpr std::size_t max_source_bytes =
        SourceSymbols * SymbolBytes;

    using source_byte_count =
        safety::Refined<safety::in_range<1, max_source_bytes>, std::size_t>;

    std::uint32_t encoding_id = 0;
    std::uint16_t symbol_count = static_cast<std::uint16_t>(SourceSymbols);
    std::uint16_t bytes_per_symbol = static_cast<std::uint16_t>(SymbolBytes);
    source_byte_count source_bytes{1, typename source_byte_count::Trusted{}};
    std::uint64_t mask = 0;
    std::array<std::byte, SymbolBytes> payload{};
};

template <std::size_t SourceSymbols, std::size_t SymbolBytes>
using LinearFountainPacket =
    safety::Linear<FountainPacket<SourceSymbols, SymbolBytes>>;

namespace fountain_detail {

template <std::size_t SourceSymbols>
[[nodiscard]] consteval std::uint64_t valid_mask() noexcept {
    if constexpr (SourceSymbols == 64) {
        return std::numeric_limits<std::uint64_t>::max();
    } else {
        return (std::uint64_t{1} << SourceSymbols) - 1U;
    }
}

[[nodiscard, gnu::const]] constexpr std::uint64_t
repair_offset(std::uint32_t encoding_id, std::uint32_t lane) noexcept {
    return (static_cast<std::uint64_t>(encoding_id) << 32U) |
           static_cast<std::uint64_t>(lane);
}

template <std::size_t SourceSymbols>
    requires (SourceSymbols > 0 && SourceSymbols <= 64)
[[nodiscard]] constexpr std::uint8_t
sample_ideal_soliton_degree(std::uint32_t uniform) noexcept {
    if constexpr (SourceSymbols == 1) {
        return 1;
    } else {
        constexpr auto range = std::uint64_t{1} << 32U;
        for (std::size_t d = 1; d <= SourceSymbols; ++d) {
            std::uint64_t numerator = 1;
            std::uint64_t denominator = SourceSymbols;
            if (d > 1) {
                numerator =
                    (d * SourceSymbols) + d - SourceSymbols;
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
    requires (SourceSymbols > 0 && SourceSymbols <= 64)
[[nodiscard]] CRUCIBLE_HOT std::uint64_t
repair_mask(std::uint32_t encoding_id, FountainSeed seed) noexcept {
    const auto degree_rng =
        Philox::generate_det(repair_offset(encoding_id, 0), seed).peek();
    const auto degree =
        sample_ideal_soliton_degree<SourceSymbols>(degree_rng[0]);

    std::array<std::uint8_t, SourceSymbols> permutation{};
    for (std::size_t i = 0; i < SourceSymbols; ++i) {
        permutation[i] = static_cast<std::uint8_t>(i);
    }

    std::uint64_t mask = 0;
    for (std::size_t i = 0; i < degree; ++i) {
        const auto rng =
            Philox::generate_det(repair_offset(encoding_id,
                                               static_cast<std::uint32_t>(i + 1)),
                                 seed).peek();
        const auto span = SourceSymbols - i;
        // fixy-A5-034: Lemire's nearly-divisionless range — replaces the
        // biased `% span` on a single 32-bit Philox word.  Concatenates
        // two Philox words into a 64-bit uniform, multiplies by span,
        // returns the upper 64 bits.  Worst-case bias is span/2^64 ≈
        // 2^-58 (span ≤ 64), below double-precision epsilon.  DetSafe
        // preserved: __uint128_t arithmetic is bit-identical on every
        // supported platform (x86_64 native, AArch64 native).
        const auto uniform64 =
            (static_cast<std::uint64_t>(rng[1]) << 32U) |
             static_cast<std::uint64_t>(rng[0]);
        const auto product =
            static_cast<__uint128_t>(uniform64) *
            static_cast<__uint128_t>(span);
        const auto j = i + static_cast<std::size_t>(product >> 64U);
        std::swap(permutation[i], permutation[j]);
        mask |= std::uint64_t{1} << permutation[i];
    }
    return mask;
}

template <std::size_t SourceSymbols>
    requires (SourceSymbols > 0 && SourceSymbols <= 64)
[[nodiscard]] CRUCIBLE_HOT std::uint64_t
packet_mask(std::uint32_t encoding_id, FountainSeed seed) noexcept {
    if (encoding_id < SourceSymbols) {
        return std::uint64_t{1} << encoding_id;
    }
    return repair_mask<SourceSymbols>(encoding_id, seed);
}

template <std::size_t SymbolBytes>
CRUCIBLE_HOT void copy_symbol(std::span<const std::byte> input,
                              std::size_t symbol,
                              std::array<std::byte, SymbolBytes>& out) noexcept {
    out.fill(std::byte{0});

    const auto start = symbol * SymbolBytes;
    if (start >= input.size()) {
        return;
    }

    const auto live = std::min(SymbolBytes, input.size() - start);
    std::memcpy(out.data(), input.data() + start, live);
}

template <std::size_t SymbolBytes>
[[nodiscard]] CRUCIBLE_HOT bool equal_symbol(
    std::array<std::byte, SymbolBytes> const& lhs,
    std::byte const* rhs) noexcept {
    return std::memcmp(lhs.data(), rhs, SymbolBytes) == 0;
}

template <std::size_t SymbolBytes>
[[nodiscard]] CRUCIBLE_HOT bool zero_symbol(
    std::array<std::byte, SymbolBytes> const& symbol) noexcept {
    std::array<std::byte, SymbolBytes> zero{};
    return symbol == zero;
}

}  // namespace fountain_detail

template <std::size_t SourceSymbols,
          std::size_t SymbolBytes,
          typename Algorithm = LtFountain>
    requires FountainShape<SourceSymbols, SymbolBytes, SourceSymbols> &&
             FountainAlgorithm<Algorithm>
class FountainEncoder {
public:
    using packet_type = FountainPacket<SourceSymbols, SymbolBytes>;
    using source_byte_count = typename packet_type::source_byte_count;
    using concurrent_budget =
        effects::ConcurrentRow<effects::SmBudget<1>>;

    static constexpr std::size_t source_symbols = SourceSymbols;
    static constexpr std::size_t symbol_bytes = SymbolBytes;
    static constexpr std::size_t max_source_bytes =
        packet_type::max_source_bytes;

    [[nodiscard]] static constexpr std::expected<source_byte_count, FountainError>
    admit_source_bytes(std::size_t bytes) noexcept {
        if (bytes == 0 || bytes > max_source_bytes) {
            return std::unexpected(FountainError::InvalidInputSize);
        }
        return source_byte_count{bytes, typename source_byte_count::Trusted{}};
    }

    [[nodiscard]] std::expected<void, FountainError>
    start_encoding(std::span<const std::byte> input,
                   FountainSeed seed) noexcept {
        auto admitted = admit_source_bytes(input.size());
        if (!admitted) {
            return std::unexpected(admitted.error());
        }

        input_ = input;
        seed_ = seed;
        source_bytes_ = *admitted;
        next_id_ = 0;
        active_ = true;
        return {};
    }

    [[nodiscard]] std::expected<packet_type, FountainError>
    next_packet() noexcept {
        if (!active_) {
            return std::unexpected(FountainError::EncoderNotStarted);
        }

        auto packet = encode_packet(input_, seed_, source_bytes_, next_id_);
        if (packet) {
            ++next_id_;
        }
        return packet;
    }

    [[nodiscard]] std::expected<LinearFountainPacket<SourceSymbols, SymbolBytes>,
                                FountainError>
    next_packet_owned() noexcept {
        auto packet = next_packet();
        if (!packet) {
            return std::unexpected(packet.error());
        }
        return LinearFountainPacket<SourceSymbols, SymbolBytes>{*packet};
    }

    [[nodiscard]] std::expected<packet_type, FountainError>
    encode_packet(std::span<const std::byte> input,
                  FountainSeed seed,
                  std::uint32_t encoding_id) const noexcept {
        auto admitted = admit_source_bytes(input.size());
        if (!admitted) {
            return std::unexpected(admitted.error());
        }
        return encode_packet(input, seed, *admitted, encoding_id);
    }

    template <ByteContiguousBuffer Input>
    [[nodiscard]] std::expected<LinearFountainPacket<SourceSymbols, SymbolBytes>,
                                FountainError>
    encode_owned(LinearFountainBuffer<Input>&& input,
                 FountainSeed seed,
                 std::uint32_t encoding_id) const noexcept {
        auto raw = std::move(input).consume();
        auto packet = encode_packet(detail::bytes(raw),
                                    seed,
                                    encoding_id);
        if (!packet) {
            return std::unexpected(packet.error());
        }
        return LinearFountainPacket<SourceSymbols, SymbolBytes>{*packet};
    }

private:
    [[nodiscard]] CRUCIBLE_HOT std::expected<packet_type, FountainError>
    encode_packet(std::span<const std::byte> input,
                  FountainSeed seed,
                  source_byte_count source_bytes,
                  std::uint32_t encoding_id) const noexcept {
        auto const mask =
            fountain_detail::packet_mask<SourceSymbols>(encoding_id, seed);
        if ((mask == 0) ||
            ((mask & ~fountain_detail::valid_mask<SourceSymbols>()) != 0)) {
            return std::unexpected(FountainError::PacketShapeMismatch);
        }

        packet_type packet{};
        packet.encoding_id = encoding_id;
        packet.source_bytes = source_bytes;
        packet.mask = mask;
        packet.payload.fill(std::byte{0});

        std::array<std::byte, SymbolBytes> symbol{};
        auto remaining = mask;
        while (remaining != 0) {
            const auto bit = std::countr_zero(remaining);
            const auto source = static_cast<std::size_t>(bit);
            fountain_detail::copy_symbol(input, source, symbol);
            detail::mul_xor(packet.payload.data(), symbol.data(), 1, SymbolBytes);
            remaining &= remaining - 1U;
        }

        return packet;
    }

    std::span<const std::byte> input_{};
    FountainSeed seed_{};
    source_byte_count source_bytes_{1, typename source_byte_count::Trusted{}};
    std::uint32_t next_id_ = 0;
    bool active_ = false;
};

template <std::size_t SourceSymbols,
          std::size_t SymbolBytes,
          std::size_t MaxEquations =
              SourceSymbols + (SourceSymbols / 2U) + 8U,
          typename Algorithm = LtFountain>
    requires FountainShape<SourceSymbols, SymbolBytes, MaxEquations> &&
             FountainAlgorithm<Algorithm>
class FountainDecoder {
public:
    using packet_type = FountainPacket<SourceSymbols, SymbolBytes>;
    using source_byte_count = typename packet_type::source_byte_count;

    static constexpr std::size_t source_symbols = SourceSymbols;
    static constexpr std::size_t symbol_bytes = SymbolBytes;
    static constexpr std::size_t max_equations = MaxEquations;
    static constexpr std::size_t max_source_bytes =
        packet_type::max_source_bytes;

    [[nodiscard]] std::expected<FountainDecodeState, FountainError>
    add_packet(packet_type const& packet) noexcept {
        auto checked = validate_packet(packet);
        if (!checked) {
            return std::unexpected(checked.error());
        }
        remember_source_bytes(packet.source_bytes);

        Equation equation{
            .mask = packet.mask,
            .payload = packet.payload,
        };
        reduce_known(equation);
        if (equation.mask == 0) {
            if (!fountain_detail::zero_symbol(equation.payload)) {
                return std::unexpected(FountainError::InconsistentEquation);
            }
            return state();
        }

        if (std::popcount(equation.mask) == 1) {
            auto resolved = admit_symbol(equation.mask, equation.payload);
            if (!resolved) {
                return std::unexpected(resolved.error());
            }
            auto peeled = peel();
            if (!peeled) {
                return std::unexpected(peeled.error());
            }
            return state();
        }

        auto stored = store_equation(equation);
        if (!stored) {
            return std::unexpected(stored.error());
        }
        return state();
    }

    [[nodiscard]] std::expected<FountainDecodeState, FountainError>
    add_packet_owned(LinearFountainPacket<SourceSymbols, SymbolBytes>&& packet)
        noexcept {
        auto raw = std::move(packet).consume();
        return add_packet(raw);
    }

    [[nodiscard]] std::expected<std::span<const std::byte>, FountainError>
    extract_decoded() const noexcept {
        if (!complete()) {
            return std::unexpected(FountainError::DecodeIncomplete);
        }
        return std::span<const std::byte>{
            decoded_.data(),
            source_bytes_.value()};
    }

    [[nodiscard]] std::expected<void, FountainError>
    extract_decoded(std::span<std::byte> output) const noexcept {
        if (!complete()) {
            return std::unexpected(FountainError::DecodeIncomplete);
        }
        if (output.size() != source_bytes_.value()) {
            return std::unexpected(FountainError::InvalidOutputSize);
        }
        std::memcpy(output.data(), decoded_.data(), output.size());
        return {};
    }

    [[nodiscard]] constexpr bool complete() const noexcept {
        return decoded_mask_ == fountain_detail::valid_mask<SourceSymbols>();
    }

    [[nodiscard]] constexpr std::size_t decoded_count() const noexcept {
        return static_cast<std::size_t>(std::popcount(decoded_mask_));
    }

private:
    struct Equation {
        std::uint64_t mask = 0;
        std::array<std::byte, SymbolBytes> payload{};
    };

    [[nodiscard]] std::expected<void, FountainError>
    validate_packet(packet_type const& packet) const noexcept {
        if (packet.symbol_count != SourceSymbols ||
            packet.bytes_per_symbol != SymbolBytes ||
            packet.mask == 0 ||
            ((packet.mask & ~fountain_detail::valid_mask<SourceSymbols>()) != 0)) {
            return std::unexpected(FountainError::PacketShapeMismatch);
        }
        // source_bytes is a Refined<in_range<1, max_source_bytes>> field, but
        // wire-deserialized packets construct it through the Trusted{} escape
        // hatch (the value comes from attacker-controlled bytes), so the
        // refinement invariant is NOT guaranteed here.  Re-check the bound at
        // this trust boundary: extract_decoded() returns/copies
        // source_bytes_.value() bytes out of the decoded_ array, which holds
        // exactly max_source_bytes bytes — an out-of-range source_bytes would
        // otherwise drive an out-of-bounds read.
        const auto claimed_source_bytes = packet.source_bytes.value();
        if (claimed_source_bytes == 0 ||
            claimed_source_bytes > max_source_bytes) {
            return std::unexpected(FountainError::PacketShapeMismatch);
        }
        if (have_source_bytes_ &&
            claimed_source_bytes != source_bytes_.value()) {
            return std::unexpected(FountainError::PacketShapeMismatch);
        }
        return {};
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
            const auto bit = std::countr_zero(overlap);
            auto const* symbol =
                decoded_.data() + (static_cast<std::size_t>(bit) * SymbolBytes);
            detail::mul_xor(equation.payload.data(), symbol, 1, SymbolBytes);
            equation.mask &= ~(std::uint64_t{1} << bit);
            overlap &= overlap - 1U;
        }
    }

    [[nodiscard]] std::expected<void, FountainError>
    admit_symbol(std::uint64_t singleton_mask,
                 std::array<std::byte, SymbolBytes> const& payload) noexcept {
        const auto bit = std::countr_zero(singleton_mask);
        const auto index = static_cast<std::size_t>(bit);
        auto* dst = decoded_.data() + index * SymbolBytes;

        if ((decoded_mask_ & singleton_mask) != 0) {
            if (!fountain_detail::equal_symbol(payload, dst)) {
                return std::unexpected(FountainError::InconsistentEquation);
            }
            return {};
        }

        std::memcpy(dst, payload.data(), SymbolBytes);
        decoded_mask_ |= singleton_mask;
        return {};
    }

    [[nodiscard]] std::expected<void, FountainError>
    peel() noexcept {
        bool progressed = true;
        while (progressed) {
            progressed = false;
            for (auto& equation : equations_) {
                if (equation.mask == 0) {
                    continue;
                }

                reduce_known(equation);
                if (equation.mask == 0) {
                    if (!fountain_detail::zero_symbol(equation.payload)) {
                        return std::unexpected(
                            FountainError::InconsistentEquation);
                    }
                    continue;
                }
                if (std::popcount(equation.mask) != 1) {
                    continue;
                }

                auto resolved = admit_symbol(equation.mask, equation.payload);
                if (!resolved) {
                    return std::unexpected(resolved.error());
                }
                equation.mask = 0;
                progressed = true;
            }
        }
        return {};
    }

    [[nodiscard]] std::expected<void, FountainError>
    store_equation(Equation equation) noexcept {
        for (auto& existing : equations_) {
            if (existing.mask == equation.mask) {
                if (!fountain_detail::equal_symbol(equation.payload,
                                                   existing.payload.data())) {
                    return std::unexpected(FountainError::InconsistentEquation);
                }
                return {};
            }
        }

        for (auto& existing : equations_) {
            if (existing.mask == 0) {
                existing = equation;
                return {};
            }
        }
        return std::unexpected(FountainError::EquationCapacityExceeded);
    }

    [[nodiscard]] constexpr FountainDecodeState state() const noexcept {
        return complete() ? FountainDecodeState::Complete
                          : FountainDecodeState::NeedsMore;
    }

    std::array<Equation, MaxEquations> equations_{};
    std::array<std::byte, SourceSymbols * SymbolBytes> decoded_{};
    source_byte_count source_bytes_{1, typename source_byte_count::Trusted{}};
    std::uint64_t decoded_mask_ = 0;
    bool have_source_bytes_ = false;
};

template <std::size_t SourceSymbols,
          std::size_t SymbolBytes,
          typename Algorithm = LtFountain>
    requires FountainShape<SourceSymbols, SymbolBytes, SourceSymbols> &&
             FountainAlgorithm<Algorithm>
[[nodiscard]] constexpr FountainEncoder<SourceSymbols, SymbolBytes, Algorithm>
mint_fountain_encoder(effects::Init) noexcept {
    return FountainEncoder<SourceSymbols, SymbolBytes, Algorithm>{};
}

template <std::size_t SourceSymbols,
          std::size_t SymbolBytes,
          std::size_t MaxEquations =
              SourceSymbols + (SourceSymbols / 2U) + 8U,
          typename Algorithm = LtFountain>
    requires FountainShape<SourceSymbols, SymbolBytes, MaxEquations> &&
             FountainAlgorithm<Algorithm>
[[nodiscard]] constexpr FountainDecoder<SourceSymbols,
                                        SymbolBytes,
                                        MaxEquations,
                                        Algorithm>
mint_fountain_decoder(effects::Init) noexcept {
    return FountainDecoder<SourceSymbols,
                           SymbolBytes,
                           MaxEquations,
                           Algorithm>{};
}

static_assert(FountainShape<8, 1024, 16>);
static_assert(!FountainShape<0, 1024, 16>);
static_assert(!FountainShape<8, 0, 16>);
static_assert(!FountainShape<65, 1024, 80>);
static_assert(sizeof(LinearFountainBuffer<std::span<std::byte>>) ==
              sizeof(std::span<std::byte>));

}  // namespace crucible::cntp
