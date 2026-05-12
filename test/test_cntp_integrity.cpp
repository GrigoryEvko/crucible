#include <crucible/cntp/Integrity.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

int main() {
    namespace ci = crucible::cntp;

    static_assert(sizeof(ci::IntegrityHash) == sizeof(std::uint64_t));
    static_assert(sizeof(ci::IntegrityOwnedPayload<std::array<std::byte, 5>>) ==
                  sizeof(std::array<std::byte, 5>));
    static_assert(!std::copy_constructible<
                  ci::IntegrityOwnedPayload<std::array<std::byte, 5>>>);
    static_assert(ci::ByteContiguousPayload<std::array<std::byte, 5>>);
    static_assert(ci::ByteContiguousPayload<std::span<const std::byte>>);
    static_assert(!ci::ByteContiguousPayload<std::uint64_t>);

    std::array<std::byte, 0> empty{};
    assert(ci::xxhash64_raw(std::span<const std::byte>{empty}) ==
           0xEF46'DB37'51D8'E999ULL);

    std::array<std::byte, 5> hello{
        std::byte{0x68},
        std::byte{0x65},
        std::byte{0x6C},
        std::byte{0x6C},
        std::byte{0x6F},
    };
    auto hello_hash = ci::xxhash64(std::span<const std::byte>{hello});
    assert(hello_hash.has_value());
    assert(hello_hash->value() == 0x26C7'827D'889F'6DA3ULL);

    std::array<std::byte, 96> payload{};
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::byte>((i * 17U + 3U) & 0xFFU);
    }

    auto one_shot = ci::xxhash64(std::span<const std::byte>{payload});
    assert(one_shot.has_value());

    auto stream = ci::xxhash64_streaming();
    assert(stream.update(std::span<const std::byte>{payload.data(), 7})
               .has_value());
    assert(stream.update(std::span<const std::byte>{payload.data() + 7, 25})
               .has_value());
    assert(stream.update(std::span<const std::byte>{payload.data() + 32, 64})
               .has_value());
    auto streamed = stream.digest();
    assert(streamed.has_value());
    assert(streamed->value() == one_shot->value());

    auto wrapped = ci::wrap(payload);
    assert(wrapped.has_value());
    assert(wrapped->hash.value() == one_shot->value());

    auto verified = ci::unwrap(std::move(wrapped).value());
    assert(verified.has_value());
    static_assert(std::same_as<
                  std::remove_cvref_t<decltype(verified.value())>,
                  ci::IntegrityVerifiedPayload<std::array<std::byte, 96>>>);
    assert(verified->value().peek()[0] == std::byte{3});

    auto owned_payload = std::move(*verified).into();
    auto restored_payload = std::move(owned_payload).consume();
    assert(restored_payload[95] == payload[95]);

    auto tampered_wrapped = ci::wrap(payload);
    assert(tampered_wrapped.has_value());
    auto tampered = std::move(tampered_wrapped).value();
    tampered.payload.peek_mut()[5] = std::byte{0xEE};
    auto rejected = ci::unwrap(std::move(tampered));
    assert(!rejected.has_value());
    assert(rejected.error() == ci::IntegrityError::HashMismatch);

    auto zero = ci::admit_integrity_hash(0);
    assert(!zero.has_value());
    assert(zero.error() == ci::IntegrityError::ZeroHash);

    ci::IntegrityPolicy sampled{
        .mode = ci::IntegrityMode::Sampled,
        .sample_rate = ci::IntegritySampleRate{4},
        .flow_opt_in = true,
    };
    assert(sampled.enabled(0));
    assert(!sampled.enabled(1));
    assert(sampled.enabled(4));

    ci::IntegrityPolicy opt_in{
        .mode = ci::IntegrityMode::OptInPerFlow,
        .sample_rate = ci::IntegritySampleRate{1},
        .flow_opt_in = false,
    };
    assert(!opt_in.enabled(0));

    ci::IntegrityStats stats{};
    stats.record_ok();
    stats.record_violation();
    assert(stats.checked == 2);
    assert(stats.violations == 1);

    return 0;
}
