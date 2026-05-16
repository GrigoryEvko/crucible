#pragma once

// ── crucible::fixy::stance — Version.h (FIXY-G13) ──────────────────────
//
// Temporal grade stability — stances are `using BgWorker =
// stance::compose<...>` aliases that EVOLVE over the project's
// lifetime.  A kernel emitted last year carries grade vector V1;
// today's `BgWorker` resolves to V2.  Cipher load-time validation
// (FIXY-G6 wire format + G9 witness opcodes) checks opcode validity
// but NOT stance-shape equivalence at the historical-vs-current
// level.  A federated peer running today's binary loads a 6-month-
// old frozen kernel and gets CURRENT BgWorker semantics, not
// historical ones — silent grade drift across time.
//
// Followups grounded in software supply chain versioning (semver +
// ABI evolution discipline; Cap'n Proto / Protocol Buffers schema
// evolution rules: append-only fields, never-reuse-tag-numbers,
// version-aware deserialization).  Stance versioning is closer to
// ABI stability than API stability — wire-format compat windows
// rather than source-level compat.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   fixy::stance::stance_id_of_v<S>            — u16 identifier per stance
//   fixy::stance::stance_version<S>            — version_v + since_version_v
//   fixy::stance::accept_versions<S, Lo, Hi>   — admission window
//   fixy::stance::stance_migration<O, N>       — migration path declaration
//   fixy::stance::accept_legacy_stanceless<S>  — opt-in for version-less load
//
// **Discipline.**  Every shipped `fixy::stance::*` ships with
// `version_v` + `since_version_v` constants.  Initial ship of any
// stance starts at version 1.  Adding a new evolved version requires
// bumping `version_v` and registering a `stance_migration<OldS,
// NewS>` specialization that walks the grade vector from old to new.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe — all version_v + since_version_v are constexpr u16.
//   TypeSafe — stance identity wrapped in u16 strong-ID-like pattern.
//   DetSafe  — version_v is a stable compile-time value per stance.
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §7 G13          — temporal grade stability
//   fixy/Stance.h                            — canonical stance catalog
//   fixy/WireGrade.h                         — wire format companion
//   fixy/Federation.h                        — federation negotiation

#include <crucible/fixy/Stance.h>

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace crucible::fixy::stance {

// ═════════════════════════════════════════════════════════════════════
// ── Tag-type per stance — versioning anchor ─────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Stances live in the type system as `std::tuple<grants...>` aliases.
// Tuples can't carry per-stance specializations because alias-expansion
// loses the named identity.  Each shipped stance therefore associates
// a dedicated tag type (one struct per stance) that serves as the
// versioning anchor.  The tag is NEVER stored at runtime — it's a
// compile-time witness for the (stance, version) lookup.

struct PureLinearTag       {};
struct PureCopyTag         {};
struct IoFunctionTag       {};
struct BgWorkerTag         {};
struct CtCryptoTag         {};
struct AsyncEndpointTag    {};
template <typename Policy> struct PublicEmitTag       {};
template <typename Vendor> struct CntpTransportTag    {};
template <typename Source> struct CntpWireFrameTag    {};
template <typename Tier>   struct ForgePhaseTag       {};
template <typename Vendor, typename Tier> struct MimicEmitTag {};
struct CipherColdWriterTag {};
struct AugurPredictorTag   {};

// ═════════════════════════════════════════════════════════════════════
// ── stance_id_of_v<Tag> — stable u16 identifier per stance ─────────
// ═════════════════════════════════════════════════════════════════════
//
// Each shipped stance gets a unique 16-bit identifier.  Identifiers
// are APPEND-ONLY: never re-use a removed stance's slot.

template <typename Tag>
inline constexpr std::uint16_t stance_id_of_v = 0;  // primary — 0 = "unknown"

template <> inline constexpr std::uint16_t stance_id_of_v<PureLinearTag>    = 1;
template <> inline constexpr std::uint16_t stance_id_of_v<PureCopyTag>      = 2;
template <> inline constexpr std::uint16_t stance_id_of_v<IoFunctionTag>    = 3;
template <> inline constexpr std::uint16_t stance_id_of_v<BgWorkerTag>      = 4;
template <> inline constexpr std::uint16_t stance_id_of_v<CtCryptoTag>      = 5;
template <> inline constexpr std::uint16_t stance_id_of_v<AsyncEndpointTag> = 6;

template <typename Policy>
inline constexpr std::uint16_t stance_id_of_v<PublicEmitTag<Policy>> = 7;

template <typename Vendor>
inline constexpr std::uint16_t stance_id_of_v<CntpTransportTag<Vendor>> = 9;

template <typename Source>
inline constexpr std::uint16_t stance_id_of_v<CntpWireFrameTag<Source>> = 10;

template <typename Tier>
inline constexpr std::uint16_t stance_id_of_v<ForgePhaseTag<Tier>> = 11;

template <typename Vendor, typename Tier>
inline constexpr std::uint16_t stance_id_of_v<MimicEmitTag<Vendor, Tier>> = 12;

template <> inline constexpr std::uint16_t stance_id_of_v<CipherColdWriterTag> = 13;
template <> inline constexpr std::uint16_t stance_id_of_v<AugurPredictorTag>   = 14;

// ═════════════════════════════════════════════════════════════════════
// ── stance_version_traits<Tag> — version_v + since_version_v ───────
// ═════════════════════════════════════════════════════════════════════
//
// Every shipped stance tag specializes this trait.

template <typename Tag>
struct stance_version_traits;

template <> struct stance_version_traits<PureLinearTag>    { static constexpr std::uint16_t version_v = 1; static constexpr std::uint16_t since_version_v = 1; };
template <> struct stance_version_traits<PureCopyTag>      { static constexpr std::uint16_t version_v = 1; static constexpr std::uint16_t since_version_v = 1; };
template <> struct stance_version_traits<IoFunctionTag>    { static constexpr std::uint16_t version_v = 1; static constexpr std::uint16_t since_version_v = 1; };
template <> struct stance_version_traits<BgWorkerTag>      { static constexpr std::uint16_t version_v = 1; static constexpr std::uint16_t since_version_v = 1; };
template <> struct stance_version_traits<CtCryptoTag>      { static constexpr std::uint16_t version_v = 1; static constexpr std::uint16_t since_version_v = 1; };
template <> struct stance_version_traits<AsyncEndpointTag> { static constexpr std::uint16_t version_v = 1; static constexpr std::uint16_t since_version_v = 1; };

template <typename Policy>
struct stance_version_traits<PublicEmitTag<Policy>> {
    static constexpr std::uint16_t version_v       = 1;
    static constexpr std::uint16_t since_version_v = 1;
};

template <typename Vendor>
struct stance_version_traits<CntpTransportTag<Vendor>> {
    static constexpr std::uint16_t version_v       = 1;
    static constexpr std::uint16_t since_version_v = 1;
};

template <typename Source>
struct stance_version_traits<CntpWireFrameTag<Source>> {
    static constexpr std::uint16_t version_v       = 1;
    static constexpr std::uint16_t since_version_v = 1;
};

template <typename Tier>
struct stance_version_traits<ForgePhaseTag<Tier>> {
    static constexpr std::uint16_t version_v       = 1;
    static constexpr std::uint16_t since_version_v = 1;
};

template <typename Vendor, typename Tier>
struct stance_version_traits<MimicEmitTag<Vendor, Tier>> {
    static constexpr std::uint16_t version_v       = 1;
    static constexpr std::uint16_t since_version_v = 1;
};

template <> struct stance_version_traits<CipherColdWriterTag> { static constexpr std::uint16_t version_v = 1; static constexpr std::uint16_t since_version_v = 1; };
template <> struct stance_version_traits<AugurPredictorTag>   { static constexpr std::uint16_t version_v = 1; static constexpr std::uint16_t since_version_v = 1; };

template <typename Tag>
inline constexpr std::uint16_t stance_version_v =
    stance_version_traits<Tag>::version_v;

template <typename Tag>
inline constexpr std::uint16_t stance_since_version_v =
    stance_version_traits<Tag>::since_version_v;

// ═════════════════════════════════════════════════════════════════════
// ── accept_versions<S, Lo, Hi> — admission window ──────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Consumers of a versioned artifact declare which versions they
// accept.  The window is INCLUSIVE.  Decoders match the artifact's
// embedded version against the window; out-of-window load fails with
// `WireGradeError::StanceVersionUnsupported`.

template <typename Tag, std::uint16_t Lo, std::uint16_t Hi>
struct accept_versions {
    static_assert(Lo <= Hi,
        "stance::accept_versions<Tag, Lo, Hi> requires Lo <= Hi (the "
        "window is inclusive on both ends).");
    using stance_tag_t = Tag;
    static constexpr std::uint16_t min_v = Lo;
    static constexpr std::uint16_t max_v = Hi;
};

// ═════════════════════════════════════════════════════════════════════
// ── accept_legacy_stanceless<S> — opt-in for version-less load ─────
// ═════════════════════════════════════════════════════════════════════
//
// Pre-G13 wire format had NO stance header — `wire_encode<F>` emitted
// `[opcode_count][records...]` only.  Loading such artifacts under a
// G13-aware decoder fails by default with `StanceVersionMissing`.
// To opt in, a deployment specializes `accept_legacy_stanceless<S>
// = true` for the stance type S, declaring it accepts pre-G13
// artifacts AS IF they had been emitted at `since_version_v`.

template <typename S>
inline constexpr bool accept_legacy_stanceless_v = false;

// ═════════════════════════════════════════════════════════════════════
// ── version_in_window — runtime check ──────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename Accept>
[[nodiscard]] constexpr bool version_in_window(std::uint16_t v) noexcept {
    return v >= Accept::min_v && v <= Accept::max_v;
}

// ═════════════════════════════════════════════════════════════════════
// ── tag_for<S> — extract the versioning tag from a stance alias ────
// ═════════════════════════════════════════════════════════════════════
//
// Stances are tuple aliases (`using BgWorker = compose_no_dedup_t<...>;`).
// `tag_for<S>` projects the alias to its dedicated tag type for
// versioning lookups.  Defaults to S itself for primary lookups; the
// 14 canonical tag types specialize.  Production call sites use
// `stance_id_of_v<tag_for<BgWorker>>` to read the stance's ID.

template <typename S>
struct tag_for_impl { using type = S; };

template <typename S>
using tag_for = typename tag_for_impl<std::remove_cvref_t<S>>::type;

// Per-stance specialization map: alias → tag.  Each shipped stance
// pairs its alias with its versioning tag.  For PARAMETRIC stances,
// the tag wraps the same parameters (the tag's identity ENCODES the
// parameter pack so federation participants on different vendors get
// different IDs).

// ═════════════════════════════════════════════════════════════════════
// ── Self-tests ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace version_self_test {

// All 14 canonical stance tags declare version_v = 1, since_version_v = 1.
struct DemoPolicy {};
struct DemoVendor {};
struct DemoSource {};
struct DemoRecipeTier {};

static_assert(stance_version_v<PureLinearTag>    == 1);
static_assert(stance_version_v<PureCopyTag>      == 1);
static_assert(stance_version_v<IoFunctionTag>    == 1);
static_assert(stance_version_v<BgWorkerTag>      == 1);
static_assert(stance_version_v<CtCryptoTag>      == 1);
static_assert(stance_version_v<AsyncEndpointTag> == 1);
static_assert(stance_version_v<PublicEmitTag<DemoPolicy>>            == 1);
static_assert(stance_version_v<CntpTransportTag<DemoVendor>>          == 1);
static_assert(stance_version_v<CntpWireFrameTag<DemoSource>>          == 1);
static_assert(stance_version_v<ForgePhaseTag<DemoRecipeTier>>         == 1);
static_assert(stance_version_v<MimicEmitTag<DemoVendor, DemoRecipeTier>> == 1);
static_assert(stance_version_v<CipherColdWriterTag> == 1);
static_assert(stance_version_v<AugurPredictorTag>   == 1);

static_assert(stance_since_version_v<BgWorkerTag> == 1);

// Stance IDs are non-zero and distinct for distinct stances.
static_assert(stance_id_of_v<PureLinearTag>      != 0);
static_assert(stance_id_of_v<BgWorkerTag>        != stance_id_of_v<PureLinearTag>);
static_assert(stance_id_of_v<CipherColdWriterTag> != stance_id_of_v<BgWorkerTag>);

// accept_versions window is inclusive.
using AcceptV1to2 = accept_versions<BgWorkerTag, 1, 2>;
static_assert(AcceptV1to2::min_v == 1);
static_assert(AcceptV1to2::max_v == 2);
static_assert(version_in_window<AcceptV1to2>(1));
static_assert(version_in_window<AcceptV1to2>(2));
static_assert(!version_in_window<AcceptV1to2>(3));
static_assert(!version_in_window<AcceptV1to2>(0));

// accept_legacy_stanceless<Tag> defaults to false.
static_assert(!accept_legacy_stanceless_v<BgWorkerTag>);

}  // namespace version_self_test

}  // namespace crucible::fixy::stance
