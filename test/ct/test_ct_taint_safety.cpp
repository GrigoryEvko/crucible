// The ctgrind check of the old-tree constant-time surface, which the
// production session wiring still calls: every crucible::safety::ct
// primitive at every unsigned width, the byte comparison at several
// lengths, and the constant-time session carrier.  The checks are the
// ones the new-tree test runs, through the shared header.

#include "checks.h"

#include <crucible/safety/_ConstantTime.h>
#include <crucible/sessions/SessionCT.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace ct_taint_safety {

struct AuthTag {
    std::array<std::uint8_t, 16> bytes{};
};

}  // namespace ct_taint_safety

template <>
struct crucible::safety::ct::requires_ct<ct_taint_safety::AuthTag> : std::true_type {};

namespace {

using ct_taint::make_public;
using ct_taint::make_secret;
using ct_taint::opaque;
using ct_taint::Tally;

// The old tree's primitives, spelled for the shared checks.
struct SafetyCt {
    template <typename T>
    static T mask_from_bit(T bit01) noexcept {
        return ::crucible::safety::ct::mask_from_bit(bit01);
    }
    template <typename T>
    static T select(T bit01, T a, T b) noexcept {
        return ::crucible::safety::ct::select(bit01, a, b);
    }
    template <typename T>
    static T less(T a, T b) noexcept {
        return ::crucible::safety::ct::less(a, b);
    }
    template <typename T>
    static T is_zero(T x) noexcept {
        return ::crucible::safety::ct::is_zero(x);
    }
    template <typename T>
    static void cswap(T cond01, T& a, T& b) noexcept {
        ::crucible::safety::ct::cswap(cond01, a, b);
    }
    static bool eq(std::span<const std::byte> a, std::span<const std::byte> b) noexcept {
        return ::crucible::safety::ct::eq(a, b);
    }
};

void check_session_payload(Tally& tally) noexcept {
    using ct_taint_safety::AuthTag;
    for (int flip = -1; flip < 16; ++flip) {
        AuthTag left{};
        for (std::size_t i = 0; i < left.bytes.size(); ++i) left.bytes[i] = opaque(static_cast<std::uint8_t>(i * 7));
        AuthTag right = left;
        if (flip >= 0) right.bytes[static_cast<std::size_t>(flip)] ^= 0x01;
        make_secret(left);
        make_secret(right);
        ::crucible::safety::ct::CTPayload<AuthTag> const lhs{left};
        ::crucible::safety::ct::CTPayload<AuthTag> const rhs{right};
        tally.expect(make_public(::crucible::safety::ct::eq(lhs, rhs)) == (flip < 0), "session CTPayload eq");
    }
}

}  // namespace

int main(int argc, char** argv) {
    return ct_taint::run(argc, argv, "safety", [](Tally& tally) noexcept {
        ct_taint::check_scalars<SafetyCt>(tally);
        ct_taint::check_eq<SafetyCt>(tally);
        check_session_payload(tally);
    });
}
