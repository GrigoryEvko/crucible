// ── test_fixy_wire_grade — FIXY-G6 positive test ──────────────────────
//
// Exercise wire_encode + wire_decode on the 4 worked-example bindings:
//   * Round-trip encode-then-decode succeeds for matching F.
//   * Cross-binding decode (encode from F1, decode as F2) returns
//     GradeMismatch when grade shapes differ.
//   * Encoded size matches wire_grade_size_v<F>.

#include <crucible/fixy/Fixy.h>

#include <array>
#include <cstdio>
#include <vector>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fx = crucible::effects;

namespace {

// ── Binding shape 1: all-strict ──
using AllStrictFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// ── Binding shape 2: Mimic NV BITEXACT — relaxes Usage, Effect, Vendor, Precision, Mutation ──
using MimicHookFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cg::with<fx::Effect::Bg, fx::Effect::Alloc>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cg::vendor_nv,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cg::precision_f32,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cg::mutable_in_place,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// ── Binding shape 3: differs from MimicHookFn only in vendor (AM vs NV) ──
using MimicHookAmFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cg::with<fx::Effect::Bg, fx::Effect::Alloc>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cg::vendor_am,                                  // CHANGED
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cg::precision_f32,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cg::mutable_in_place,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// Compile-time size checks.
static_assert(cf::wire_grade_size_v<AllStrictFn> > 0);
static_assert(cf::wire_grade_size_v<MimicHookFn> > cf::wire_grade_size_v<AllStrictFn>,
    "MimicHook has Effect_With (4-byte payload), Vendor (1-byte), "
    "and a few more payload bytes than the all-strict encoding.");

}  // namespace

int main() {
    // Round-trip all-strict.
    {
        std::vector<std::uint8_t> buf(cf::wire_grade_size_v<AllStrictFn>);
        std::size_t written = cf::wire_encode<AllStrictFn>(buf);
        if (written != buf.size()) {
            std::fprintf(stderr, "wire_encode<AllStrictFn>: wrote %zu, expected %zu\n",
                         written, buf.size());
            return 1;
        }
        auto r = cf::wire_decode<AllStrictFn>(buf);
        if (!r) {
            std::fprintf(stderr, "wire_decode<AllStrictFn>: %s\n",
                         std::string(cf::wire_grade_error_name(r.error())).c_str());
            return 2;
        }
    }

    // Round-trip MimicHookFn.
    {
        std::vector<std::uint8_t> buf(cf::wire_grade_size_v<MimicHookFn>);
        std::size_t written = cf::wire_encode<MimicHookFn>(buf);
        if (written != buf.size()) {
            std::fprintf(stderr, "wire_encode<MimicHookFn>: wrote %zu, expected %zu\n",
                         written, buf.size());
            return 3;
        }
        auto r = cf::wire_decode<MimicHookFn>(buf);
        if (!r) {
            std::fprintf(stderr, "wire_decode<MimicHookFn>: %s\n",
                         std::string(cf::wire_grade_error_name(r.error())).c_str());
            return 4;
        }
    }

    // Cross-binding decode — encode AllStrictFn, decode MimicHookFn.
    // Buffer sizes differ → BufferTooSmall OR BadOpcodeCount depending
    // on which side is bigger.  Either is the right shape of failure.
    {
        std::vector<std::uint8_t> buf(cf::wire_grade_size_v<MimicHookFn>, 0);
        // Encode the all-strict shape into a buffer sized for MimicHook
        // (large enough to read).  The decode-as-MimicHookFn will detect
        // mismatched opcodes / payloads.
        std::vector<std::uint8_t> as_buf(cf::wire_grade_size_v<AllStrictFn>);
        [[maybe_unused]] auto wrote_as = cf::wire_encode<AllStrictFn>(as_buf);
        for (std::size_t i = 0; i < as_buf.size() && i < buf.size(); ++i) {
            buf[i] = as_buf[i];
        }
        auto r = cf::wire_decode<MimicHookFn>(buf);
        if (r) {
            std::fputs("cross-binding decode unexpectedly succeeded\n", stderr);
            return 5;
        }
    }

    // Cross-binding decode of MimicHookAm via MimicHookNv — same size,
    // SAME EVERYTHING except vendor byte → GradeMismatch.
    {
        std::vector<std::uint8_t> buf_nv(cf::wire_grade_size_v<MimicHookFn>);
        [[maybe_unused]] auto wrote_nv = cf::wire_encode<MimicHookFn>(buf_nv);
        auto r = cf::wire_decode<MimicHookAmFn>(buf_nv);
        if (r) {
            std::fputs("vendor-mismatched decode unexpectedly succeeded\n", stderr);
            return 6;
        }
        if (r.error() != cf::WireGradeError::GradeMismatch) {
            std::fprintf(stderr, "vendor mismatch error wrong shape: %s\n",
                         std::string(cf::wire_grade_error_name(r.error())).c_str());
            return 7;
        }
    }

    // BufferTooSmall path — decode with truncated buffer.
    {
        std::vector<std::uint8_t> buf(cf::wire_grade_size_v<AllStrictFn>);
        [[maybe_unused]] auto wrote_as_all = cf::wire_encode<AllStrictFn>(buf);
        std::span<const std::uint8_t> truncated{buf.data(), buf.size() / 2};
        auto r = cf::wire_decode<AllStrictFn>(truncated);
        if (r) {
            std::fputs("truncated decode unexpectedly succeeded\n", stderr);
            return 8;
        }
        if (r.error() != cf::WireGradeError::BufferTooSmall) {
            std::fprintf(stderr, "truncated error wrong shape: %s\n",
                         std::string(cf::wire_grade_error_name(r.error())).c_str());
            return 9;
        }
    }

    std::fputs("test_fixy_wire_grade: OK\n", stdout);
    return 0;
}
