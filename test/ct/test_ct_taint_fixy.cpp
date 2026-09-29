// The ctgrind check of the constant-time surface: every fixy::ct primitive
// at every unsigned width, the byte comparison at several lengths and in
// its static-extent form, the constant-time session carrier, a Secret
// derived through the primitives, and the admission of the mTLS private
// key.  Each secret operand is undefined to memcheck, so a branch or an
// address that depends on it is a memcheck error, and --error-exitcode
// fails the test.

#include "checks.h"

#include <crucible/cntp/MtlsTransport.h>
#include <fixy/ConstantTime.h>
#include <fixy/Secret.h>
#include <fixy/Tags.h>
#include <fixy/session/Classified.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace {

using ct_taint::make_public;
using ct_taint::make_secret;
using ct_taint::opaque;
using ct_taint::Tally;

// The new tree's primitives, spelled for the shared checks.
struct FixyCt {
    template <typename T>
    static T mask_from_bit(T bit01) noexcept {
        return ::fixy::ct::mask_from_bit(bit01);
    }
    template <typename T>
    static T select(T bit01, T a, T b) noexcept {
        return ::fixy::ct::select(bit01, a, b);
    }
    template <typename T>
    static T less(T a, T b) noexcept {
        return ::fixy::ct::less(a, b);
    }
    template <typename T>
    static T is_zero(T x) noexcept {
        return ::fixy::ct::is_zero(x);
    }
    template <typename T>
    static void cswap(T cond01, T& a, T& b) noexcept {
        ::fixy::ct::cswap(cond01, a, b);
    }
    static bool eq(std::span<const std::byte> a, std::span<const std::byte> b) noexcept { return ::fixy::ct::eq(a, b); }
    template <std::size_t N>
    static bool eq_static(std::array<std::byte, N> const& a, std::array<std::byte, N> const& b) noexcept {
        return ::fixy::ct::eq(std::span<const std::byte, N>{a}, std::span<const std::byte, N>{b});
    }
};

struct[[= ::fixy::session::constant_time_value{}]] AuthTag {
    std::array<std::uint8_t, 16> bytes{};
};

void check_session_payload(Tally& tally) noexcept {
    for (int flip = -1; flip < 16; ++flip) {
        AuthTag left{};
        for (std::size_t i = 0; i < left.bytes.size(); ++i)
            left.bytes[i] = opaque(static_cast<std::uint8_t>(i * 7));
        AuthTag right = left;
        if (flip >= 0) right.bytes[static_cast<std::size_t>(flip)] ^= 0x01;
        make_secret(left);
        make_secret(right);
        ::fixy::session::CTPayload<AuthTag> const lhs{left};
        ::fixy::session::CTPayload<AuthTag> const rhs{right};
        tally.expect(make_public(::fixy::session::eq(lhs, rhs)) == (flip < 0), "session CTPayload eq");
    }
}

void check_secret_derivation(Tally& tally) noexcept {
    for (std::uint64_t const bit_value : {std::uint64_t{0}, std::uint64_t{1}}) {
        std::uint64_t bit = opaque(bit_value);
        make_secret(bit);
        auto classified = ::fixy::mint_secret<std::uint64_t>(bit);
        auto chosen = std::move(classified).transform([](std::uint64_t b) noexcept {
            return ::fixy::ct::select(b, std::uint64_t{0xAA}, std::uint64_t{0x55});
        });
        std::uint64_t const out = std::move(chosen).declassify<::fixy::tags::secret_policy::HashForCompare>();
        tally.expect(make_public(out) == (bit_value == 1 ? 0xAAu : 0x55u), "Secret transform through select");
    }
}

// The mTLS private key is the one key the production tree holds.  Its
// admission copies the secret bytes into the key buffer, the key moves
// into the Secret, and the destructor zeroizes each copy.  The length of
// the key is public, so only the bytes are secret.
void check_mtls_private_key(Tally& tally) noexcept {
    using ::crucible::cntp::MtlsKeyAlgorithm;
    std::array<std::byte, 64> pem{};
    for (std::size_t i = 0; i < pem.size(); ++i)
        pem[i] = static_cast<std::byte>(opaque(static_cast<unsigned>(i * 13u + 5u)));
    ct_taint::make_secret_bytes(pem.data(), pem.size());
    {
        auto admitted =
            ::crucible::cntp::admit_private_key_pem<MtlsKeyAlgorithm::Ed25519>(std::span<const std::byte>{pem});
        tally.expect(admitted.has_value(), "mTLS private key admission");
        auto key = std::move(admitted).value();
        auto moved = std::move(key);
        tally.expect(moved.size() == pem.size(), "mTLS private key length");
    }
}

}  // namespace

int main(int argc, char** argv) {
    return ct_taint::run(argc, argv, [](Tally& tally) noexcept {
        ct_taint::check_scalars<FixyCt>(tally);
        ct_taint::check_eq<FixyCt>(tally);
        check_session_payload(tally);
        check_secret_derivation(tally);
        check_mtls_private_key(tally);
    });
}
