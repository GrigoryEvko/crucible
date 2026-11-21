#include <crucible/cntp/Integrity.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

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

using HelloBytes = ::foundation::core::FixedArray<std::byte, 5>;
using PayloadBytes = ::foundation::core::FixedArray<std::byte, 96>;

// The error of an unwrap.  A verified payload is linear, so the payload of an
// unwrap that succeeds ends here, and the error then holds no value.
[[nodiscard]] ::fixy::Option<ci::IntegrityError>
unwrap_error(ci::IntegrityWrappedMessage<ci::IntegrityOwnedPayload<PayloadBytes>> message) {
    return ci::unwrap(std::move(message))
        .match(
            [](ci::IntegrityVerifiedPayload<PayloadBytes> verified) noexcept {
                (void)std::move(verified).into().consume();
                return ::fixy::Option<ci::IntegrityError>{::fixy::none};
            },
            [](ci::IntegrityError error) noexcept { return ::fixy::some(error); });
}

}  // namespace

int main() {
    static_assert(sizeof(ci::IntegrityHash) == sizeof(std::uint64_t));
    static_assert(::fixy::qtt_consume_tracked || sizeof(ci::IntegrityOwnedPayload<HelloBytes>) == sizeof(HelloBytes));
    static_assert(!std::copy_constructible<ci::IntegrityOwnedPayload<HelloBytes>>);
    static_assert(ci::ByteContiguousPayload<HelloBytes>);
    static_assert(ci::ByteContiguousPayload<::fixy::View<std::byte const>>);
    static_assert(!ci::ByteContiguousPayload<std::uint64_t>);

    CRUCIBLE_TEST_ASSERT(kTest, ci::xxhash64_raw(::fixy::View<std::byte const>{}) == 0xEF46DB3751D8E999ULL);

    HelloBytes const hello{
        std::byte{0x68}, std::byte{0x65}, std::byte{0x6C}, std::byte{0x6C}, std::byte{0x6F},
    };
    CRUCIBLE_TEST_ASSERT(kTest, ci::xxhash64(hello.view()).expect("the hash of hello is not zero").value()
                                    == 0x26C7827D889F6DA3ULL);

    PayloadBytes payload{};
    for (auto index : payload.indices()) {
        auto const value = static_cast<std::byte>((index.raw() * 17U + 3U) & 0xFFU);
        payload.with_element(index, [value](std::byte& element) noexcept { element = value; });
    }

    std::uint64_t const one_shot = ci::xxhash64(payload.view()).expect("the hash of the payload is not zero").value();

    auto stream = ci::xxhash64_streaming();
    auto const bytes = payload.view();
    CRUCIBLE_TEST_ASSERT(kTest, stream.update(bytes.window(0, 7).expect("seven bytes fit")).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, stream.update(bytes.window(7, 25).expect("25 bytes fit")).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, stream.update(bytes.window(32, 64).expect("64 bytes fit")).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, stream.digest().expect("the digest is not zero").value() == one_shot);

    auto wrapped = ci::wrap(payload).expect("the hash of the payload is not zero");
    CRUCIBLE_TEST_ASSERT(kTest, wrapped.hash.value() == one_shot);

    auto verified = ci::unwrap(std::move(wrapped)).expect("the hash of the payload matches");
    static_assert(std::same_as<decltype(verified), ci::IntegrityVerifiedPayload<PayloadBytes>>);
    CRUCIBLE_TEST_ASSERT(kTest, verified.value().peek().at(::fixy::index<0>()) == std::byte{3});

    auto owned_payload = std::move(verified).into();
    auto restored_payload = std::move(owned_payload).consume();
    CRUCIBLE_TEST_ASSERT(kTest, restored_payload.at(::fixy::index<95>()) == payload.at(::fixy::index<95>()));

    auto tampered = ci::wrap(payload).expect("the hash of the payload is not zero");
    tampered.payload.peek_mut().with_element(::fixy::index<5>(),
                                             [](std::byte& element) noexcept { element = std::byte{0xEE}; });
    CRUCIBLE_TEST_ASSERT(kTest, unwrap_error(std::move(tampered)).expect("the unwrap refuses a changed payload")
                                    == ci::IntegrityError::HashMismatch);

    CRUCIBLE_TEST_ASSERT(kTest, ci::admit_integrity_hash(0).err().expect("a zero hash is refused")
                                    == ci::IntegrityError::ZeroHash);

    ci::IntegrityPolicy sampled{
        .mode = ci::IntegrityMode::Sampled,
        .sample_rate = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{4}),
        .flow_opt_in = true,
    };
    CRUCIBLE_TEST_ASSERT(kTest, sampled.enabled(0));
    CRUCIBLE_TEST_ASSERT(kTest, !sampled.enabled(1));
    CRUCIBLE_TEST_ASSERT(kTest, sampled.enabled(4));

    ci::IntegrityPolicy opt_in{
        .mode = ci::IntegrityMode::OptInPerFlow,
        .sample_rate = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{1}),
        .flow_opt_in = false,
    };
    CRUCIBLE_TEST_ASSERT(kTest, !opt_in.enabled(0));

    ci::IntegrityStats stats{};
    stats.record_ok();
    stats.record_violation();
    CRUCIBLE_TEST_ASSERT(kTest, stats.checked == 2);
    CRUCIBLE_TEST_ASSERT(kTest, stats.violations == 1);

    return 0;
}
