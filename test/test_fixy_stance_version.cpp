// ── test_fixy_stance_version (FIXY-G13 positive test) ─────────────────
//
// Pin stance versioning machinery:
//   * all 14 canonical stance tags declare version_v=1, since_version_v=1
//   * accept_versions window semantics
//   * stance_migration identity + hypothetical v1→v2 evolution
//   * wire_encode_v2 / wire_decode_v2 round-trip
//   * federation negotiation with overlapping vs disjoint version windows

#include <crucible/fixy/Fixy.h>

#include <array>
#include <cstdio>

namespace cf = crucible::fixy;
namespace cs = crucible::fixy::stance;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fx = crucible::effects;
namespace sw = crucible::safety::witness;

namespace {

// ── 1. All 14 stance tags declare version 1 ───────────────────────
struct DemoPolicy {};
struct DemoVendor {};
struct DemoSource {};
struct DemoRecipeTier {};

static_assert(cs::stance_version_v<cs::PureLinearTag>     == 1);
static_assert(cs::stance_version_v<cs::PureCopyTag>       == 1);
static_assert(cs::stance_version_v<cs::IoFunctionTag>     == 1);
static_assert(cs::stance_version_v<cs::BgWorkerTag>       == 1);
static_assert(cs::stance_version_v<cs::CtCryptoTag>       == 1);
static_assert(cs::stance_version_v<cs::AsyncEndpointTag>  == 1);
static_assert(cs::stance_version_v<cs::PublicEmitTag<DemoPolicy>> == 1);
static_assert(cs::stance_version_v<cs::CntpTransportTag<DemoVendor>> == 1);
static_assert(cs::stance_version_v<cs::CntpWireFrameTag<DemoSource>> == 1);
static_assert(cs::stance_version_v<cs::ForgePhaseTag<DemoRecipeTier>> == 1);
static_assert(cs::stance_version_v<cs::MimicEmitTag<DemoVendor, DemoRecipeTier>> == 1);
static_assert(cs::stance_version_v<cs::CipherColdWriterTag> == 1);
static_assert(cs::stance_version_v<cs::AugurPredictorTag>   == 1);

static_assert(cs::stance_since_version_v<cs::BgWorkerTag> == 1);

// All 14 tags carry distinct IDs (modulo SecretConsumer=PureLinear by design).
static_assert(cs::stance_id_of_v<cs::PureLinearTag>      == 1);
static_assert(cs::stance_id_of_v<cs::PureCopyTag>        == 2);
static_assert(cs::stance_id_of_v<cs::IoFunctionTag>      == 3);
static_assert(cs::stance_id_of_v<cs::BgWorkerTag>        == 4);
static_assert(cs::stance_id_of_v<cs::CtCryptoTag>        == 5);
static_assert(cs::stance_id_of_v<cs::AsyncEndpointTag>   == 6);
static_assert(cs::stance_id_of_v<cs::PublicEmitTag<DemoPolicy>>      == 7);
static_assert(cs::stance_id_of_v<cs::CntpTransportTag<DemoVendor>>   == 9);
static_assert(cs::stance_id_of_v<cs::CntpWireFrameTag<DemoSource>>   == 10);
static_assert(cs::stance_id_of_v<cs::ForgePhaseTag<DemoRecipeTier>>  == 11);
static_assert(cs::stance_id_of_v<cs::MimicEmitTag<DemoVendor, DemoRecipeTier>> == 12);
static_assert(cs::stance_id_of_v<cs::CipherColdWriterTag> == 13);
static_assert(cs::stance_id_of_v<cs::AugurPredictorTag>   == 14);

// ── 2. Hypothetical evolution — BgWorker v1 → v2 ──────────────────
//
// A locally-defined "v2 tag" that re-uses the BgWorker stance ID but
// reports version_v = 2.  Real evolution would name a separate
// `BgWorkerV2Tag`; here we declare a registered migration path.

struct BgWorkerV2Tag {};

}  // namespace

namespace crucible::fixy::stance {

template <> inline constexpr std::uint16_t stance_id_of_v<::BgWorkerV2Tag> = 4;
template <> struct stance_version_traits<::BgWorkerV2Tag> {
    static constexpr std::uint16_t version_v       = 2;
    static constexpr std::uint16_t since_version_v = 1;
};

// Migration path v1 → v2 — payload-preserving identity (the only
// thing changing is the version stamp, not the grade vector).
template <>
struct stance_migration<BgWorkerTag, ::BgWorkerV2Tag> {
    static constexpr bool migrate_v = true;
    template <typename G>
    [[nodiscard]] static constexpr G migrate(G v) noexcept { return v; }
};

}  // namespace crucible::fixy::stance

namespace {

static_assert(cs::stance_version_v<BgWorkerV2Tag> == 2);
static_assert(cs::stance_since_version_v<BgWorkerV2Tag> == 1);
static_assert(cs::stance_id_of_v<BgWorkerV2Tag> == 4);
static_assert(cs::can_migrate_v<cs::BgWorkerTag, BgWorkerV2Tag>);
static_assert(cs::migrate<cs::BgWorkerTag, BgWorkerV2Tag>(42) == 42);

// Reverse migration not declared → fails the can-migrate predicate.
static_assert(!cs::can_migrate_v<BgWorkerV2Tag, cs::BgWorkerTag>);

// ── 3. accept_versions window semantics ───────────────────────────
using AcceptV1   = cs::accept_versions<cs::BgWorkerTag, 1, 1>;
using AcceptV1to2 = cs::accept_versions<cs::BgWorkerTag, 1, 2>;
using AcceptV2to3 = cs::accept_versions<cs::BgWorkerTag, 2, 3>;
using AcceptV3to5 = cs::accept_versions<cs::BgWorkerTag, 3, 5>;

static_assert(cs::version_in_window<AcceptV1>(1));
static_assert(!cs::version_in_window<AcceptV1>(2));
static_assert(cs::version_in_window<AcceptV1to2>(1));
static_assert(cs::version_in_window<AcceptV1to2>(2));
static_assert(!cs::version_in_window<AcceptV1to2>(3));
static_assert(cs::version_in_window<AcceptV2to3>(2));
static_assert(!cs::version_in_window<AcceptV2to3>(1));

// ── 4. Federation negotiation — overlapping vs disjoint windows ───
//
// Three peers; all admit version 2 → channel mints OK.
static_assert(
    cf::federation_version_windows_compatible_v<
        cs::accept_versions<cs::MimicEmitTag<DemoVendor, DemoRecipeTier>, 1, 3>,
        cs::accept_versions<cs::MimicEmitTag<DemoVendor, DemoRecipeTier>, 2, 4>,
        cs::accept_versions<cs::MimicEmitTag<DemoVendor, DemoRecipeTier>, 2, 5>>);

static_assert(cf::federation_version_meet_lo_v<
    cs::accept_versions<cs::MimicEmitTag<DemoVendor, DemoRecipeTier>, 1, 3>,
    cs::accept_versions<cs::MimicEmitTag<DemoVendor, DemoRecipeTier>, 2, 4>,
    cs::accept_versions<cs::MimicEmitTag<DemoVendor, DemoRecipeTier>, 2, 5>> == 2);

static_assert(cf::federation_version_meet_hi_v<
    cs::accept_versions<cs::MimicEmitTag<DemoVendor, DemoRecipeTier>, 1, 3>,
    cs::accept_versions<cs::MimicEmitTag<DemoVendor, DemoRecipeTier>, 2, 4>,
    cs::accept_versions<cs::MimicEmitTag<DemoVendor, DemoRecipeTier>, 2, 5>> == 3);

// Disjoint: peer A wants [1,2], peer B wants [4,5] → no overlap.
static_assert(
    !cf::federation_version_windows_compatible_v<
        cs::accept_versions<cs::BgWorkerTag, 1, 2>,
        cs::accept_versions<cs::BgWorkerTag, 4, 5>>);

// ── 5. Wire round-trip — encode v1, decode v1, decode v2-window ────

// A minimal-grant binding with a stance discipline pinned to BgWorker.
// Using fn<int, ...> for the test fixture (matches fixy/Fixy.h conventions).
using BgWorkerBinding = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cg::with<fx::Effect::Bg, fx::Effect::Alloc, fx::Effect::Block>,
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
    cf::accept_default_strict_for<cd::Staleness>>;

// Encoded size includes the 4-byte stance header.
static_assert(cs::wire_grade_v2_size_v<BgWorkerBinding> ==
              cs::kStanceHeaderSize + cf::wire_grade_size_v<BgWorkerBinding>);

}  // namespace

int main() {
    constexpr std::size_t kSize = cs::wire_grade_v2_size_v<BgWorkerBinding>;
    std::array<std::uint8_t, kSize> buf{};

    // Encode with BgWorker v1.
    const std::size_t written =
        cs::wire_encode_v2<BgWorkerBinding, cs::BgWorkerTag>(buf);
    if (written != kSize) {
        std::fputs("test_fixy_stance_version: encode size mismatch\n", stderr);
        return 1;
    }
    // First 4 bytes: stance_id=4, stance_version=1.
    if (buf[0] != 4 || buf[1] != 0 || buf[2] != 1 || buf[3] != 0) {
        std::fputs("test_fixy_stance_version: header bytes wrong\n", stderr);
        return 1;
    }

    // Decode with accept_versions<BgWorker, 1, 2> — succeeds.
    auto ok = cs::wire_decode_v2<BgWorkerBinding, cs::BgWorkerTag,
        cs::accept_versions<cs::BgWorkerTag, 1, 2>>(buf);
    if (!ok) {
        std::fprintf(stderr,
            "test_fixy_stance_version: decode v1 in [1,2] failed: %s\n",
            cf::wire_grade_error_name(ok.error()).data());
        return 1;
    }

    // Decode with accept_versions<BgWorker, 2, 3> — out of window.
    auto err = cs::wire_decode_v2<BgWorkerBinding, cs::BgWorkerTag,
        cs::accept_versions<cs::BgWorkerTag, 2, 3>>(buf);
    if (err) {
        std::fputs("test_fixy_stance_version: decode v1 in [2,3] unexpectedly succeeded\n", stderr);
        return 1;
    }
    if (err.error() != cf::WireGradeError::StanceVersionUnsupported) {
        std::fputs("test_fixy_stance_version: wrong error for out-of-window\n", stderr);
        return 1;
    }

    // Decode with mismatched tag (PureLinear ID 1 vs encoded ID 4).
    auto idmiss = cs::wire_decode_v2<BgWorkerBinding, cs::PureLinearTag,
        cs::accept_versions<cs::PureLinearTag, 1, 2>>(buf);
    if (idmiss) {
        std::fputs("test_fixy_stance_version: stance_id mismatch unexpectedly succeeded\n", stderr);
        return 1;
    }
    if (idmiss.error() != cf::WireGradeError::StanceIdMismatch) {
        std::fputs("test_fixy_stance_version: wrong error for stance_id mismatch\n", stderr);
        return 1;
    }

    // Federation versioned mint: 3 peers, overlapping windows.
    auto channel = cf::mint_federation_channel_versioned<
        cs::accept_versions<cs::MimicEmitTag<DemoVendor, DemoRecipeTier>, 1, 3>,
        cs::accept_versions<cs::MimicEmitTag<DemoVendor, DemoRecipeTier>, 2, 4>,
        cs::accept_versions<cs::MimicEmitTag<DemoVendor, DemoRecipeTier>, 2, 5>>();
    if (decltype(channel)::shared_min_v != 2 ||
        decltype(channel)::shared_max_v != 3) {
        std::fputs("test_fixy_stance_version: versioned federation shared window wrong\n", stderr);
        return 1;
    }

    std::fputs("test_fixy_stance_version: OK\n", stdout);
    return 0;
}
