// The private key of mTLS leaves no byte of the key in its storage after its
// end, and the source of a move holds zero bytes.  The key clears its bytes
// with the volatile clear of foundation/core/Zeroize.h, so the optimizer keeps
// the clear of an object whose life ends.  The old key cleared its bytes with
// a plain loop, and at -O3 GCC removed the stores of the destructor (lifetime
// dead-store elimination), so the bytes of the key stayed in its storage.
//
// The probe builds a key in a buffer of bytes that outlives it, fills the key,
// ends it, and reads the buffer.  The probe first reads the key in the buffer,
// so a buffer that no key filled does not pass.  The test runs in each build of
// the gate, and the Release builds are the ones where the old key failed.

#include <crucible/cntp/MtlsTransport.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <new>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

namespace cntp = ::crucible::cntp;

// The bytes of the pem of the key: a pattern with each byte above zero.
using PemBytes = ::fixy::FixedArray<std::byte, cntp::MtlsPrivateKeyBytes::max_bytes>;

[[nodiscard]] PemBytes pattern() noexcept {
    return ::fixy::build_fixed_array<std::byte, PemBytes::size()>([](auto run) noexcept {
        std::size_t position = 0;
        for (std::byte& byte : run) {
            byte = static_cast<std::byte>(0x41U + (position % 26U));
            ++position;
        }
    });
}

// The bytes of a key with the pattern and an algorithm other than the zero
// enumerator.
[[nodiscard]] cntp::MtlsPrivateKeyBytes filled_key_bytes() noexcept {
    cntp::MtlsPrivateKeyBytes key{};
    key.bytes = pattern();
    key.nbytes = static_cast<std::uint16_t>(cntp::MtlsPrivateKeyBytes::max_bytes);
    key.algorithm = cntp::MtlsKeyAlgorithm::EcdsaP256;
    return key;
}

// The key of the admission of the pattern, in its Secret.
[[nodiscard]] cntp::MtlsPrivateKey admitted_key() noexcept {
    PemBytes const pem = pattern();
    return cntp::admit_private_key_pem<cntp::MtlsKeyAlgorithm::EcdsaP256>(pem.view())
        .expect("the pattern is a key of at most max_bytes");
}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the private key of mTLS leaves no byte of the key in its buffer after its end")
namespace {

// The storage of a key after its end: 1 when each byte is zero, 0 when a byte
// of the key stays, and 2 when the buffer did not hold the key before the end.
// The reads are volatile, so the optimizer reads each byte from memory.
template <class Key, class Make>
[[nodiscard]] int storage_after_end(Make make) noexcept {
    alignas(Key) unsigned char storage[sizeof(Key)] = {};
    Key* const key = ::new(static_cast<void*>(storage)) Key(make());
    volatile unsigned char const* const bytes = storage;
    bool is_filled = false;
    for (std::size_t index = 0; index < sizeof(Key); ++index) {
        is_filled = is_filled || bytes[index] != 0;
    }
    key->~Key();
    if (!is_filled) return 2;
    for (std::size_t index = 0; index < sizeof(Key); ++index) {
        if (bytes[index] != 0) return 0;
    }
    return 1;
}

// The storage of the source of a move of the key bytes, after the move and
// before the end of the source: 1 when each byte is zero, 0 when a byte stays.
[[nodiscard]] int source_after_move() noexcept {
    using Key = cntp::MtlsPrivateKeyBytes;
    alignas(Key) unsigned char storage[sizeof(Key)] = {};
    Key* const source = ::new(static_cast<void*>(storage)) Key(filled_key_bytes());
    Key const target{std::move(*source)};
    volatile unsigned char const* const bytes = storage;
    int result = 1;
    for (std::size_t index = 0; index < sizeof(Key); ++index) {
        if (bytes[index] != 0) result = 0;
    }
    source->~Key();
    return target.size_bytes() == Key::max_bytes ? result : 0;
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    int const bytes_end = storage_after_end<cntp::MtlsPrivateKeyBytes>([] noexcept { return filled_key_bytes(); });
    CRUCIBLE_TEST_ASSERT(kTest, bytes_end != 2 && "the buffer holds the key bytes before their end");
    CRUCIBLE_TEST_ASSERT(kTest, bytes_end == 1 && "the key bytes leave no byte of the key after their end");
    int const secret_end = storage_after_end<cntp::MtlsPrivateKey>([] noexcept { return admitted_key(); });
    CRUCIBLE_TEST_ASSERT(kTest, secret_end != 2 && "the buffer holds the key before its end");
    CRUCIBLE_TEST_ASSERT(kTest, secret_end == 1 && "the Secret of the key leaves no byte of the key after its end");
    CRUCIBLE_TEST_ASSERT(kTest, source_after_move() == 1 && "the source of a move of the key holds zero bytes");
    ::fixy::check::pass(kTest, "test_cntp_mtls_key_zeroize: all PASSED\n");
    return 0;
}
