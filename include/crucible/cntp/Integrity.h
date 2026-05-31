#pragma once

// End-to-end CNT-P payload integrity.
//
// Link CRCs are per-hop.  This header owns the payload-layer check:
// sender computes xxHash64 over the payload bytes, receiver recomputes
// the same platform-independent value, and successful unwrap returns a
// source::IntegrityVerified payload.

#include <crucible/Platform.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Simd.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <bit>
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

namespace crucible::cntp {

enum class IntegrityError : std::uint8_t {
    HashMismatch,
    LengthOverflow,
    ZeroHash,
};

enum class IntegrityMode : std::uint8_t {
    AlwaysOn,
    Sampled,
    OptInPerFlow,
};

using IntegrityHash = safety::Refined<safety::non_zero, std::uint64_t>;
using IntegritySampleRate = safety::Refined<safety::positive, std::uint16_t>;

struct IntegrityPolicy {
    IntegrityMode mode = IntegrityMode::AlwaysOn;
    IntegritySampleRate sample_rate{1};
    bool flow_opt_in = true;

    [[nodiscard]] constexpr bool enabled(std::uint64_t sequence) const noexcept {
        switch (mode) {
            case IntegrityMode::AlwaysOn:
                return true;
            case IntegrityMode::OptInPerFlow:
                return flow_opt_in;
            case IntegrityMode::Sampled: {
                const auto rate = static_cast<std::uint64_t>(sample_rate.value());
                return rate == 1 || (sequence % rate) == 0;
            }
            default:
                return true;
        }
    }
};

struct IntegrityViolationEvent {
    IntegrityHash expected;
    IntegrityHash actual;
    std::uint64_t payload_bytes = 0;
};

struct IntegrityStats {
    std::uint64_t checked = 0;
    std::uint64_t violations = 0;

    constexpr void record_ok() noexcept {
        ++checked;
    }

    constexpr void record_violation() noexcept {
        ++checked;
        ++violations;
    }
};

template <typename Payload>
struct IntegrityWrappedMessage {
    Payload payload;
    IntegrityHash hash;
};

template <typename Payload>
using IntegrityOwnedPayload = safety::Linear<Payload>;

template <typename Payload>
using IntegrityVerifiedPayload =
    safety::Tagged<IntegrityOwnedPayload<Payload>,
                   safety::source::IntegrityVerified>;

template <typename T>
concept BytePayloadElement =
    std::same_as<std::remove_cv_t<T>, std::byte> ||
    (sizeof(T) == 1 && std::is_trivially_copyable_v<T>);

template <typename Payload>
concept HasContiguousPayloadData = requires(Payload const& payload) {
    std::data(payload);
    std::size(payload);
};

template <typename Payload>
using payload_data_pointer_t =
    decltype(std::data(std::declval<Payload const&>()));

template <typename Payload>
concept ByteContiguousPayload =
    HasContiguousPayloadData<Payload> &&
    std::is_pointer_v<payload_data_pointer_t<Payload>> &&
    BytePayloadElement<
        std::remove_pointer_t<payload_data_pointer_t<Payload>>>;

namespace detail {

inline constexpr std::uint64_t xxh_prime64_1 =
    11'400'714'785'074'694'791ULL;
inline constexpr std::uint64_t xxh_prime64_2 =
    14'029'467'366'897'019'727ULL;
inline constexpr std::uint64_t xxh_prime64_3 =
    1'609'587'929'392'839'161ULL;
inline constexpr std::uint64_t xxh_prime64_4 =
    9'650'029'242'287'828'579ULL;
inline constexpr std::uint64_t xxh_prime64_5 =
    2'870'177'450'012'600'261ULL;
inline constexpr std::size_t xxh_stripe_bytes = 32;

[[nodiscard, gnu::pure]] inline std::uint64_t
load_le64(std::byte const* ptr) noexcept {
    std::uint64_t out = 0;
    std::memcpy(&out, ptr, sizeof(out));
    if constexpr (std::endian::native == std::endian::big) {
        out = std::byteswap(out);
    }
    return out;
}

[[nodiscard, gnu::pure]] inline std::uint32_t
load_le32(std::byte const* ptr) noexcept {
    std::uint32_t out = 0;
    std::memcpy(&out, ptr, sizeof(out));
    if constexpr (std::endian::native == std::endian::big) {
        out = std::byteswap(out);
    }
    return out;
}

[[nodiscard, gnu::const]] CRUCIBLE_HOT std::uint64_t
round64(std::uint64_t acc, std::uint64_t lane) noexcept {
    acc += lane * xxh_prime64_2;
    acc = std::rotl(acc, 31);
    acc *= xxh_prime64_1;
    return acc;
}

[[nodiscard]] CRUCIBLE_HOT ::crucible::simd::u64x4
load_stripe_le64(std::byte const* ptr) noexcept {
    alignas(32) std::array<std::uint64_t, 4> lanes{};
    std::memcpy(lanes.data(), ptr, xxh_stripe_bytes);
    if constexpr (std::endian::native == std::endian::big) {
        for (auto& lane : lanes) {
            lane = std::byteswap(lane);
        }
    }
    return ::crucible::simd::load_aligned<::crucible::simd::u64x4>(
        lanes.data());
}

[[nodiscard, gnu::const]] CRUCIBLE_HOT ::crucible::simd::u64x4
rotl64(::crucible::simd::u64x4 value, int bits) noexcept {
    return (value << bits) | (value >> (64 - bits));
}

[[nodiscard, gnu::const]] CRUCIBLE_HOT std::uint64_t
merge_round64(std::uint64_t acc, std::uint64_t lane) noexcept {
    acc ^= round64(0, lane);
    acc *= xxh_prime64_1;
    acc += xxh_prime64_4;
    return acc;
}

[[nodiscard, gnu::const]] CRUCIBLE_HOT std::uint64_t
avalanche64(std::uint64_t hash) noexcept {
    hash ^= hash >> 33;
    hash *= xxh_prime64_2;
    hash ^= hash >> 29;
    hash *= xxh_prime64_3;
    hash ^= hash >> 32;
    return hash;
}

CRUCIBLE_HOT void
process_stripe(std::byte const* ptr,
               std::uint64_t& v1,
               std::uint64_t& v2,
               std::uint64_t& v3,
               std::uint64_t& v4) noexcept {
    using ::crucible::simd::u64x4;

    alignas(32) std::array<std::uint64_t, 4> acc_values{v1, v2, v3, v4};
    auto acc = ::crucible::simd::load_aligned<u64x4>(acc_values.data());

    // The crucible::simd facade exposes only binary operators (no
    // compound-assignment forms); `acc = acc + ...` is bit-identical to
    // the prior `acc += ...` per lane.
    acc = acc + load_stripe_le64(ptr) * u64x4(xxh_prime64_2);
    acc = rotl64(acc, 31);
    acc = acc * u64x4(xxh_prime64_1);

    ::crucible::simd::store_aligned(acc, acc_values.data());
    v1 = acc_values[0];
    v2 = acc_values[1];
    v3 = acc_values[2];
    v4 = acc_values[3];
}

[[nodiscard]] CRUCIBLE_HOT std::uint64_t
finalize_tail(std::uint64_t hash,
              std::byte const* ptr,
              std::size_t len) noexcept {
    while (len >= 8) {
        hash ^= round64(0, load_le64(ptr));
        hash = std::rotl(hash, 27) * xxh_prime64_1 + xxh_prime64_4;
        ptr += 8;
        len -= 8;
    }
    if (len >= 4) {
        hash ^= static_cast<std::uint64_t>(load_le32(ptr)) * xxh_prime64_1;
        hash = std::rotl(hash, 23) * xxh_prime64_2 + xxh_prime64_3;
        ptr += 4;
        len -= 4;
    }
    while (len != 0) {
        hash ^= static_cast<std::uint64_t>(*ptr) * xxh_prime64_5;
        hash = std::rotl(hash, 11) * xxh_prime64_1;
        ++ptr;
        --len;
    }
    return avalanche64(hash);
}

template <ByteContiguousPayload Payload>
[[nodiscard]] inline std::span<const std::byte>
payload_bytes(Payload const& payload) noexcept {
    auto const* ptr = std::data(payload);
    auto const count = static_cast<std::size_t>(std::size(payload));
    // FIXY-U-082 / fixy-A5-028: std::as_bytes is the C++20 idiom for
    // typed-span → byte-span — strict-aliasing-safe, zero-cost, no cast
    // required.  Drops the reinterpret_cast entirely.
    return std::as_bytes(std::span{ptr, count});
}

}  // namespace detail

[[nodiscard]] inline std::expected<IntegrityHash, IntegrityError>
admit_integrity_hash(std::uint64_t hash) noexcept {
    if (hash == 0) {
        return std::unexpected(IntegrityError::ZeroHash);
    }
    return IntegrityHash{hash, IntegrityHash::Trusted{}};
}

[[nodiscard]] CRUCIBLE_HOT std::uint64_t
xxhash64_raw(std::span<const std::byte> data,
             std::uint64_t seed = 0) noexcept {
    std::byte const* ptr = data.data();
    std::size_t len = data.size();

    std::uint64_t hash = 0;
    if (len >= detail::xxh_stripe_bytes) {
        std::uint64_t v1 = seed + detail::xxh_prime64_1 +
                           detail::xxh_prime64_2;
        std::uint64_t v2 = seed + detail::xxh_prime64_2;
        std::uint64_t v3 = seed;
        std::uint64_t v4 = seed - detail::xxh_prime64_1;

        do {
            detail::process_stripe(ptr, v1, v2, v3, v4);
            ptr += detail::xxh_stripe_bytes;
            len -= detail::xxh_stripe_bytes;
        } while (len >= detail::xxh_stripe_bytes);

        hash = std::rotl(v1, 1) + std::rotl(v2, 7) +
               std::rotl(v3, 12) + std::rotl(v4, 18);
        hash = detail::merge_round64(hash, v1);
        hash = detail::merge_round64(hash, v2);
        hash = detail::merge_round64(hash, v3);
        hash = detail::merge_round64(hash, v4);
    } else {
        hash = seed + detail::xxh_prime64_5;
    }

    hash += data.size();
    return detail::finalize_tail(hash, ptr, len);
}

[[nodiscard]] inline std::expected<IntegrityHash, IntegrityError>
xxhash64(std::span<const std::byte> data,
         std::uint64_t seed = 0) noexcept {
    return admit_integrity_hash(xxhash64_raw(data, seed));
}

class XxHash64State {
public:
    explicit constexpr XxHash64State(std::uint64_t seed = 0) noexcept
        : seed_{seed},
          v1_{seed + detail::xxh_prime64_1 + detail::xxh_prime64_2},
          v2_{seed + detail::xxh_prime64_2},
          v3_{seed},
          v4_{seed - detail::xxh_prime64_1} {}

    [[nodiscard]] CRUCIBLE_HOT std::expected<void, IntegrityError>
    update(std::span<const std::byte> data) noexcept {
        if (data.empty()) {
            return {};
        }
        if (data.size() >
            std::numeric_limits<std::uint64_t>::max() - total_len_) {
            return std::unexpected(IntegrityError::LengthOverflow);
        }

        total_len_ += data.size();
        std::byte const* ptr = data.data();
        std::size_t len = data.size();

        if (memory_size_ + len < detail::xxh_stripe_bytes) {
            std::memcpy(memory_.data() + memory_size_, ptr, len);
            memory_size_ += len;
            return {};
        }

        if (memory_size_ != 0) {
            const std::size_t fill = detail::xxh_stripe_bytes - memory_size_;
            std::memcpy(memory_.data() + memory_size_, ptr, fill);
            detail::process_stripe(memory_.data(), v1_, v2_, v3_, v4_);
            ptr += fill;
            len -= fill;
            memory_size_ = 0;
        }

        while (len >= detail::xxh_stripe_bytes) {
            detail::process_stripe(ptr, v1_, v2_, v3_, v4_);
            ptr += detail::xxh_stripe_bytes;
            len -= detail::xxh_stripe_bytes;
        }

        if (len != 0) {
            std::memcpy(memory_.data(), ptr, len);
            memory_size_ = len;
        }
        return {};
    }

    [[nodiscard]] inline std::expected<IntegrityHash, IntegrityError>
    digest() const noexcept {
        std::uint64_t hash = 0;
        if (total_len_ >= detail::xxh_stripe_bytes) {
            hash = std::rotl(v1_, 1) + std::rotl(v2_, 7) +
                   std::rotl(v3_, 12) + std::rotl(v4_, 18);
            hash = detail::merge_round64(hash, v1_);
            hash = detail::merge_round64(hash, v2_);
            hash = detail::merge_round64(hash, v3_);
            hash = detail::merge_round64(hash, v4_);
        } else {
            hash = seed_ + detail::xxh_prime64_5;
        }

        hash += total_len_;
        return admit_integrity_hash(
            detail::finalize_tail(hash, memory_.data(), memory_size_));
    }

private:
    std::uint64_t seed_ = 0;
    std::uint64_t total_len_ = 0;
    std::uint64_t v1_ = 0;
    std::uint64_t v2_ = 0;
    std::uint64_t v3_ = 0;
    std::uint64_t v4_ = 0;
    std::array<std::byte, detail::xxh_stripe_bytes> memory_{};
    std::size_t memory_size_ = 0;
};

[[nodiscard]] constexpr XxHash64State
xxhash64_streaming(std::uint64_t seed = 0) noexcept {
    return XxHash64State{seed};
}

template <ByteContiguousPayload Payload>
[[nodiscard]] inline std::expected<
    IntegrityWrappedMessage<IntegrityOwnedPayload<Payload>>,
    IntegrityError>
wrap(Payload payload) noexcept(std::is_nothrow_move_constructible_v<Payload>) {
    auto hash = xxhash64(detail::payload_bytes(payload));
    if (!hash) {
        return std::unexpected(hash.error());
    }
    return IntegrityWrappedMessage<IntegrityOwnedPayload<Payload>>{
        .payload = IntegrityOwnedPayload<Payload>{std::move(payload)},
        .hash = *hash,
    };
}

template <ByteContiguousPayload Payload>
[[nodiscard]] inline std::expected<
    IntegrityVerifiedPayload<Payload>,
    IntegrityError>
unwrap(IntegrityWrappedMessage<IntegrityOwnedPayload<Payload>> message) noexcept(
    std::is_nothrow_move_constructible_v<Payload>) {
    auto actual = xxhash64(detail::payload_bytes(message.payload.peek()));
    if (!actual) {
        return std::unexpected(actual.error());
    }
    if (actual->value() != message.hash.value()) {
        return std::unexpected(IntegrityError::HashMismatch);
    }
    return IntegrityVerifiedPayload<Payload>{std::move(message.payload)};
}

static_assert(sizeof(IntegrityHash) == sizeof(std::uint64_t));
static_assert(sizeof(IntegrityOwnedPayload<std::span<const std::byte>>) ==
              sizeof(std::span<const std::byte>));
static_assert(sizeof(IntegrityWrappedMessage<std::span<const std::byte>>) ==
              sizeof(std::span<const std::byte>) + sizeof(std::uint64_t));

}  // namespace crucible::cntp
