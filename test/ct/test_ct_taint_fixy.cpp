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
#include <fixy/Core.h>
#include <fixy/Secret.h>
#include <fixy/Tags.h>
#include <fixy/check/Check.h>
#include <fixy/session/Classified.h>
#include <foundation/Quarantine.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using ::fixy::check::Tally;
using ct_taint::make_public;
using ct_taint::make_secret;
using ct_taint::opaque;

// The fixy::ct primitives, spelled for the shared checks.
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
    static bool eq(::fixy::View<std::byte const> a, ::fixy::View<std::byte const> b) noexcept {
        return ::fixy::ct::eq(a, b);
    }
    template <std::size_t N>
    static bool eq_static(::fixy::View<std::byte const, N> a, ::fixy::View<std::byte const, N> b) noexcept {
        return ::fixy::ct::eq(a, b);
    }
};

struct[[= ::fixy::session::constant_time_value{}]] AuthTag {
    ::fixy::FixedArray<std::uint8_t, 16> bytes{};
};

// The index of the byte whose low bit differs between the two tags of one
// compare, or no index for two equal tags.
using FlippedByte = ::fixy::Option<::fixy::Index<16>>;

void check_one_payload(Tally& tally, FlippedByte const flip) noexcept {
    using enum ::fixy::check::CheckOutcome;
    AuthTag left{};
    for (::fixy::Index<16> const index : left.bytes.indices())
        left.bytes.at(index) = opaque(static_cast<std::uint8_t>(index.raw() * 7));
    AuthTag right = left;
    flip.match([&right](::fixy::Index<16> const at) noexcept { right.bytes.at(at) ^= 0x01; }, [] noexcept {});
    make_secret(left);
    make_secret(right);
    ::fixy::session::CTPayload<AuthTag> const lhs{left};
    ::fixy::session::CTPayload<AuthTag> const rhs{right};
    tally.count(make_public(::fixy::session::eq(lhs, rhs)) == flip.is_none() ? held : failed, "session CTPayload eq");
}

// The equal tags first, then a flip of each byte in order.
void check_session_payload(Tally& tally) noexcept {
    check_one_payload(tally, ::fixy::none);
    for (::fixy::Index<16> const flip : ::fixy::IndexRange<16>{})
        check_one_payload(tally, ::fixy::some(flip));
}

void check_secret_derivation(Tally& tally) noexcept {
    using enum ::fixy::check::CheckOutcome;
    constexpr ::fixy::FixedArray<std::uint64_t, 2> bit_values{std::uint64_t{0}, std::uint64_t{1}};
    for (std::uint64_t const bit_value : bit_values) {
        std::uint64_t bit = opaque(bit_value);
        make_secret(bit);
        auto classified = ::fixy::mint_secret<std::uint64_t>(bit);
        auto chosen = std::move(classified).transform([](std::uint64_t const& b) noexcept {
            return ::fixy::ct::select(b, std::uint64_t{0xAA}, std::uint64_t{0x55});
        });
        std::uint64_t const out = std::move(chosen).declassify<::fixy::tags::secret_policy::HashForCompare>();
        tally.count(make_public(out) == (bit_value == 1 ? 0xAAu : 0x55u) ? held : failed,
                    "Secret transform through select");
    }
}

// The mTLS private key is the one key the production tree holds.  Its
// admission copies the secret bytes into the key buffer, the key moves
// into the Secret, and the destructor zeroizes each copy.  The length of
// the key is public, so only the bytes are secret.  The array holds its
// bytes and no other byte, so make_secret marks exactly them.
void check_mtls_private_key(Tally& tally) noexcept {
    using enum ::fixy::check::CheckOutcome;
    using ::crucible::cntp::MtlsKeyAlgorithm;
    ::fixy::FixedArray<std::byte, 64> pem{};
    static_assert(sizeof(pem) == 64, "make_secret of the key marks the bytes of the key and no other byte");
    for (::fixy::Index<64> const index : pem.indices())
        pem.at(index) = static_cast<std::byte>(opaque(static_cast<unsigned>(index.raw() * 13u + 5u)));
    make_secret(pem);
    {
        auto admitted = ::crucible::cntp::admit_private_key_pem<MtlsKeyAlgorithm::Ed25519>(::fixy::bytes(pem));
        tally.count(admitted.is_ok() ? held : failed, "mTLS private key admission");
        auto key = std::move(admitted).expect("the key admission succeeds");
        auto moved = std::move(key);
        tally.count(moved.declassify<::fixy::tags::secret_policy::LengthOnly>() == pem.size() ? held : failed,
                    "mTLS private key length");
    }
}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: main takes the arguments of the C runtime as a count and an array of texts")
int main(int argc, char** argv) {
    return ct_taint::run(::fixy::os::args(argc, argv), [](Tally& tally) noexcept {
        ct_taint::check_scalars<FixyCt>(tally);
        ct_taint::check_eq<FixyCt>(tally);
        check_session_payload(tally);
        check_secret_derivation(tally);
        check_mtls_private_key(tally);
    });
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
