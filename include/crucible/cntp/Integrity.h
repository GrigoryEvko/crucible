#pragma once

// A link CRC covers one hop.  This check covers the payload end to end, and
// the hash is platform-independent so a receiver on other hardware recomputes
// the same value.

#include <fixy/Core.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <foundation/Platform.h>
#include <foundation/Simd.h>
#include <foundation/contracts/Pre.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
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

using IntegrityHash = ::fixy::Refined<::fixy::non_zero, std::uint64_t>;
using IntegritySampleRate = ::fixy::Refined<::fixy::positive, std::uint16_t>;

struct IntegrityPolicy {
    IntegrityMode mode = IntegrityMode::AlwaysOn;
    IntegritySampleRate sample_rate = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{1});
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

    constexpr void record_ok() noexcept { ++checked; }

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
using IntegrityOwnedPayload = ::fixy::Linear<Payload>;

// IntegrityVerified is an earned tag.  unwrap reaches it by the retag from
// External, the edge whose discharge is the receiver recomputing the hash
// and matching the one on the wire.
template <typename Payload>
using IntegrityVerifiedPayload =
    ::fixy::Tagged<IntegrityOwnedPayload<Payload>, ::fixy::tags::source::IntegrityVerified>;

// A payload whose bytes are one contiguous run: a View of bytes, or an owner
// whose view() gives one, such as a FixedArray or a HeapArray of bytes.
template <typename Payload>
concept ByteContiguousPayload =
    std::convertible_to<Payload const&, ::fixy::View<std::byte const>> || requires(Payload const& payload) {
        { payload.view() } -> std::convertible_to<::fixy::View<std::byte const>>;
    };

namespace detail {

inline constexpr std::uint64_t xxh_prime64_1 = 11400714785074694791ULL;
inline constexpr std::uint64_t xxh_prime64_2 = 14029467366897019727ULL;
inline constexpr std::uint64_t xxh_prime64_3 = 1609587929392839161ULL;
inline constexpr std::uint64_t xxh_prime64_4 = 9650029242287828579ULL;
inline constexpr std::uint64_t xxh_prime64_5 = 2870177450012600261ULL;
inline constexpr std::size_t xxh_stripe_bytes = 32;

// The word whose little-endian image is the bytes of word.  The platform is
// little-endian (foundation/Platform.h), so the copy of the bytes is the word.
[[nodiscard, gnu::pure]] inline std::uint64_t load_le64(::fixy::View<std::byte const, 8> word) noexcept {
    ::foundation::core::FixedArray<std::byte, 8> image{};
    ::fixy::copy(image.view(), word);
    return ::fixy::bit_cast<std::uint64_t>(image);
}

[[nodiscard, gnu::pure]] inline std::uint32_t load_le32(::fixy::View<std::byte const, 4> word) noexcept {
    ::foundation::core::FixedArray<std::byte, 4> image{};
    ::fixy::copy(image.view(), word);
    return ::fixy::bit_cast<std::uint32_t>(image);
}

[[nodiscard, gnu::const]] CRUCIBLE_HOT std::uint64_t round64(std::uint64_t acc, std::uint64_t lane) noexcept {
    acc += lane * xxh_prime64_2;
    acc = ::fixy::rotl(acc, 31);
    acc *= xxh_prime64_1;
    return acc;
}

[[nodiscard, gnu::const]] CRUCIBLE_HOT ::foundation::simd::u64x4 rotl64(::foundation::simd::u64x4 value,
                                                                        int bits) noexcept {
    return (value << bits) | (value >> (64 - bits));
}

[[nodiscard, gnu::const]] CRUCIBLE_HOT std::uint64_t merge_round64(std::uint64_t acc, std::uint64_t lane) noexcept {
    acc ^= round64(0, lane);
    acc *= xxh_prime64_1;
    acc += xxh_prime64_4;
    return acc;
}

[[nodiscard, gnu::const]] CRUCIBLE_HOT std::uint64_t avalanche64(std::uint64_t hash) noexcept {
    hash ^= hash >> 33;
    hash *= xxh_prime64_2;
    hash ^= hash >> 29;
    hash *= xxh_prime64_3;
    hash ^= hash >> 32;
    return hash;
}

// One round of the four accumulators over one stripe.  The lanes of the
// stripe are its four little-endian words, in order.
CRUCIBLE_HOT void process_stripe(::fixy::View<std::byte const, xxh_stripe_bytes> stripe, std::uint64_t& v1,
                                 std::uint64_t& v2, std::uint64_t& v3, std::uint64_t& v4) noexcept {
    using ::foundation::simd::u64x4;

    u64x4 acc{u64x4::raw_type{v1, v2, v3, v4}};

    // The simd facade offers binary operators only, so these are written out
    // rather than as compound assignments.
    acc = acc + ::foundation::simd::load<u64x4>(stripe) * u64x4(xxh_prime64_2);
    acc = rotl64(acc, 31);
    acc = acc * u64x4(xxh_prime64_1);

    v1 = acc[0];
    v2 = acc[1];
    v3 = acc[2];
    v4 = acc[3];
}

// The step of the word at the constant position Word of a tail, when the
// tail holds that word.
template <std::size_t Word>
[[nodiscard]] CRUCIBLE_HOT std::uint64_t tail_word(std::uint64_t hash, ::fixy::View<std::byte const> tail) noexcept {
    if (tail.size() < (Word + 1) * 8) {
        return hash;
    }
    hash ^= round64(0, load_le64(tail.window<8>(Word * 8).expect("the word lies inside the tail")));
    return ::fixy::rotl(hash, 27) * xxh_prime64_1 + xxh_prime64_4;
}

// The step of the byte at the constant position Byte of the last bytes of a
// tail, when they hold that byte.
template <std::size_t Byte>
[[nodiscard]] CRUCIBLE_HOT std::uint64_t tail_byte(std::uint64_t hash, ::fixy::View<std::byte const> bytes) noexcept {
    if (bytes.size() <= Byte) {
        return hash;
    }
    std::byte const byte = bytes.window<1>(Byte).expect("the byte lies inside the rest").at(::fixy::index<0>());
    hash ^= static_cast<std::uint64_t>(byte) * xxh_prime64_5;
    return ::fixy::rotl(hash, 11) * xxh_prime64_1;
}

// The tail of fewer than one stripe: each word of eight bytes, then a half
// of four bytes when one is left, then each byte.  A tail holds at most three
// words and three bytes, so each step is one test of a count with a constant
// position, and the code has no loop.
[[nodiscard]] CRUCIBLE_HOT std::uint64_t finalize_tail(std::uint64_t hash,
                                                       ::fixy::View<std::byte const> tail) noexcept {
    CRUCIBLE_PRE(tail.size() < xxh_stripe_bytes);

    hash = tail_word<0>(hash, tail);
    hash = tail_word<1>(hash, tail);
    hash = tail_word<2>(hash, tail);

    ::fixy::View<std::byte const> const rest = tail.chunks<8>().rest();
    if (rest.size() >= 4) {
        hash ^=
            static_cast<std::uint64_t>(load_le32(rest.window<4>(0).expect("the half starts the rest"))) * xxh_prime64_1;
        hash = ::fixy::rotl(hash, 23) * xxh_prime64_2 + xxh_prime64_3;
    }

    ::fixy::View<std::byte const> const bytes = rest.chunks<4>().rest();
    hash = tail_byte<0>(hash, bytes);
    hash = tail_byte<1>(hash, bytes);
    hash = tail_byte<2>(hash, bytes);
    return avalanche64(hash);
}

template <ByteContiguousPayload Payload>
[[nodiscard]] constexpr ::fixy::View<std::byte const> payload_bytes(Payload const& payload) noexcept {
    if constexpr (requires { payload.view(); }) {
        return payload.view();
    } else {
        return payload;
    }
}

}  // namespace detail

[[nodiscard]] constexpr ::fixy::Result<IntegrityHash, IntegrityError>
admit_integrity_hash(std::uint64_t hash) noexcept {
    return ::fixy::admit_refined<::fixy::non_zero>(hash, IntegrityError::ZeroHash);
}

[[nodiscard]] CRUCIBLE_HOT std::uint64_t xxhash64_raw(::fixy::View<std::byte const> data,
                                                      std::uint64_t seed = 0) noexcept {
    auto const stripes = data.chunks<detail::xxh_stripe_bytes>();

    std::uint64_t hash = 0;
    if (data.size() >= detail::xxh_stripe_bytes) {
        std::uint64_t v1 = seed + detail::xxh_prime64_1 + detail::xxh_prime64_2;
        std::uint64_t v2 = seed + detail::xxh_prime64_2;
        std::uint64_t v3 = seed;
        std::uint64_t v4 = seed - detail::xxh_prime64_1;

        for (::fixy::View<std::byte const, detail::xxh_stripe_bytes> stripe : stripes) {
            detail::process_stripe(stripe, v1, v2, v3, v4);
        }

        hash = ::fixy::rotl(v1, 1) + ::fixy::rotl(v2, 7) + ::fixy::rotl(v3, 12) + ::fixy::rotl(v4, 18);
        hash = detail::merge_round64(hash, v1);
        hash = detail::merge_round64(hash, v2);
        hash = detail::merge_round64(hash, v3);
        hash = detail::merge_round64(hash, v4);
    } else {
        hash = seed + detail::xxh_prime64_5;
    }

    hash += data.size();
    return detail::finalize_tail(hash, stripes.rest());
}

[[nodiscard]] inline ::fixy::Result<IntegrityHash, IntegrityError> xxhash64(::fixy::View<std::byte const> data,
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

    [[nodiscard]] CRUCIBLE_HOT ::fixy::Result<::fixy::Unit, IntegrityError>
    update(::fixy::View<std::byte const> data) noexcept {
        if (data.size() == 0) {
            return ::fixy::Unit{};
        }
        if (data.size() > ::fixy::max_of<std::uint64_t> - total_len_) {
            return ::fixy::err(IntegrityError::LengthOverflow);
        }
        total_len_ += data.size();

        // The room is the count of the bytes that complete the held stripe.
        std::size_t const held = held_bytes();
        std::size_t const room = detail::xxh_stripe_bytes - held;
        if (data.size() < room) {
            ::fixy::copy(memory_.view().window(held, data.size()).expect("the bytes fit in the room"), data)
                .expect("the window has the count of the bytes");
            memory_size_ = held + data.size();
            return ::fixy::Unit{};
        }

        ::fixy::View<std::byte const> rest = data;
        if (held != 0) {
            ::fixy::copy(memory_.view().window(held, room).expect("the room ends at the end of the stripe"),
                         data.window(0, room).expect("the bytes fill the room"))
                .expect("the two windows have the count of the room");
            detail::process_stripe(memory_.view(), v1_, v2_, v3_, v4_);
            rest = data.window(room, data.size() - room).expect("the bytes after the room");
            memory_size_ = 0;
        }

        auto const stripes = rest.chunks<detail::xxh_stripe_bytes>();
        for (::fixy::View<std::byte const, detail::xxh_stripe_bytes> stripe : stripes) {
            detail::process_stripe(stripe, v1_, v2_, v3_, v4_);
        }

        auto const tail = stripes.rest();
        if (tail.size() != 0) {
            ::fixy::copy(memory_.view().window(0, tail.size()).expect("the tail is shorter than one stripe"), tail)
                .expect("the window has the count of the tail");
            memory_size_ = tail.size();
        }
        return ::fixy::Unit{};
    }

    [[nodiscard]] inline ::fixy::Result<IntegrityHash, IntegrityError> digest() const noexcept {
        std::uint64_t hash = 0;
        if (total_len_ >= detail::xxh_stripe_bytes) {
            hash = ::fixy::rotl(v1_, 1) + ::fixy::rotl(v2_, 7) + ::fixy::rotl(v3_, 12) + ::fixy::rotl(v4_, 18);
            hash = detail::merge_round64(hash, v1_);
            hash = detail::merge_round64(hash, v2_);
            hash = detail::merge_round64(hash, v3_);
            hash = detail::merge_round64(hash, v4_);
        } else {
            hash = seed_ + detail::xxh_prime64_5;
        }

        hash += total_len_;
        return admit_integrity_hash(detail::finalize_tail(
            hash, memory_.view().window(0, held_bytes()).expect("the held bytes are part of the memory")));
    }

private:
    // The count of the bytes in memory_.  It is below one stripe in each
    // state, because update() processes each full stripe.
    [[nodiscard]] constexpr std::size_t held_bytes() const noexcept {
        std::size_t const held = memory_size_;
        [[assume(held < detail::xxh_stripe_bytes)]];
        return held;
    }

    std::uint64_t seed_ = 0;
    std::uint64_t total_len_ = 0;
    std::uint64_t v1_ = 0;
    std::uint64_t v2_ = 0;
    std::uint64_t v3_ = 0;
    std::uint64_t v4_ = 0;
    ::foundation::core::FixedArray<std::byte, detail::xxh_stripe_bytes> memory_{};
    std::size_t memory_size_ = 0;
};

[[nodiscard]] constexpr XxHash64State xxhash64_streaming(std::uint64_t seed = 0) noexcept {
    return XxHash64State{seed};
}

template <ByteContiguousPayload Payload>
[[nodiscard]] inline ::fixy::Result<IntegrityWrappedMessage<IntegrityOwnedPayload<Payload>>, IntegrityError>
wrap(Payload payload) noexcept(std::is_nothrow_move_constructible_v<Payload>) {
    auto hash = xxhash64(detail::payload_bytes(payload));
    if (hash.is_err()) {
        return ::fixy::err(hash.err().expect("the hash holds its refusal"));
    }
    return IntegrityWrappedMessage<IntegrityOwnedPayload<Payload>>{
        .payload = ::fixy::mint_linear<Payload>(std::move(payload)),
        .hash = std::move(hash).expect("the hash is admitted"),
    };
}

// The hash of the payload must be the hash of the message.  A payload whose
// hash is zero has no admitted hash, so it gives ZeroHash.
template <ByteContiguousPayload Payload>
[[nodiscard]] inline ::fixy::Result<IntegrityVerifiedPayload<Payload>, IntegrityError>
unwrap(IntegrityWrappedMessage<IntegrityOwnedPayload<Payload>> message) noexcept(
    std::is_nothrow_move_constructible_v<Payload>) {
    std::uint64_t const actual = xxhash64_raw(detail::payload_bytes(message.payload.peek()));
    if (actual == 0) {
        return ::fixy::err(IntegrityError::ZeroHash);
    }
    if (actual != message.hash.value()) {
        return ::fixy::err(IntegrityError::HashMismatch);
    }
    return ::fixy::mint_tagged<::fixy::tags::source::External>(std::move(message.payload))
        .template retag<::fixy::tags::source::IntegrityVerified>();
}

}  // namespace crucible::cntp
