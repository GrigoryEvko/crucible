// ── example_fixy_stance_versioned (FIXY-G13 worked example) ───────────
//
// Demonstrates temporal grade stability for production stances:
//
//   1. Encode a binding with MimicEmit v1 (initial ship).
//   2. Decode under a binary that supports MimicEmit v1..v2 with
//      explicit migration path declared — succeeds with migration.
//   3. Decode under a binary that supports v3 only and lacks a v1→v3
//      migration path — fails with structured diagnostic.
//   4. Federation across 3 peers running different versions, with
//      overlapping accept_versions windows — successful negotiation.
//
// CRUCIBLE.md §16: Cipher cold-tier artifacts persist across binary
// upgrades.  Without G13 the binding's grade vector silently retags
// to the current binary's semantics; with G13 the binary refuses an
// unmigrated load.

#include <crucible/fixy/Fixy.h>

#include <array>
#include <cstdio>

namespace cf = crucible::fixy;
namespace cs = crucible::fixy::stance;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fx = crucible::effects;

namespace {

// Production stance tag parameters.
struct NvVendor {};
struct BitexactStrict {};

using EmitBinding = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cg::with<fx::Effect::Bg, fx::Effect::Alloc, fx::Effect::IO>,
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
    cg::reentrant,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>>;

// MimicEmit v1 tag (the shipped one).
using MimicEmitV1 = cs::MimicEmitTag<NvVendor, BitexactStrict>;

}  // namespace

int main() {
    // Step 1 — encode v1 artifact.
    constexpr std::size_t kSize = cs::wire_grade_v2_size_v<EmitBinding>;
    std::array<std::uint8_t, kSize> artifact{};
    const std::size_t written =
        cs::wire_encode_v2<EmitBinding, MimicEmitV1>(artifact);
    if (written != kSize) {
        std::fputs("encode v1 size mismatch\n", stderr);
        return 1;
    }
    std::fprintf(stdout,
        "example_fixy_stance_versioned: encoded %zu bytes "
        "(stance_id=%u, stance_version=%u)\n",
        kSize,
        static_cast<unsigned>(artifact[0] | (artifact[1] << 8)),
        static_cast<unsigned>(artifact[2] | (artifact[3] << 8)));

    // Step 2 — decode under a binary that accepts v1..v2 (binary
    // shipping today's MimicEmit AND has migration paths).
    using AcceptV1to2 = cs::accept_versions<MimicEmitV1, 1, 2>;
    auto r = cs::wire_decode_v2<EmitBinding, MimicEmitV1, AcceptV1to2>(artifact);
    if (!r) {
        std::fprintf(stderr, "decode v1 in [1,2] failed: %s\n",
            cf::wire_grade_error_name(r.error()).data());
        return 1;
    }
    std::fputs("example_fixy_stance_versioned: decode v1 in [1,2] OK\n", stdout);

    // Step 3 — decode under a binary that only accepts v3..v4
    // (binary has dropped legacy support).
    using AcceptV3to4 = cs::accept_versions<MimicEmitV1, 3, 4>;
    auto r2 = cs::wire_decode_v2<EmitBinding, MimicEmitV1, AcceptV3to4>(artifact);
    if (r2) {
        std::fputs("decode v1 in [3,4] unexpectedly succeeded\n", stderr);
        return 1;
    }
    if (r2.error() != cf::WireGradeError::StanceVersionUnsupported) {
        std::fputs("wrong error for out-of-window load\n", stderr);
        return 1;
    }
    std::fprintf(stdout,
        "example_fixy_stance_versioned: decode v1 in [3,4] correctly "
        "rejected with %s\n",
        cf::wire_grade_error_name(r2.error()).data());

    // Step 4 — federation across 3 peers with overlapping windows.
    auto channel = cf::mint_federation_channel_versioned<
        cs::accept_versions<MimicEmitV1, 1, 3>,
        cs::accept_versions<MimicEmitV1, 2, 4>,
        cs::accept_versions<MimicEmitV1, 2, 5>>();
    std::fprintf(stdout,
        "example_fixy_stance_versioned: federation shared window "
        "[%u, %u]\n",
        static_cast<unsigned>(decltype(channel)::shared_min_v),
        static_cast<unsigned>(decltype(channel)::shared_max_v));

    return 0;
}
