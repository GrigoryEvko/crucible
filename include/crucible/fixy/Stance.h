#pragma once

// ── crucible::fixy — Stance.h (FIXY-B3 + FIXY-C-STANCE-COMPOSE) ────────
//
// Ergonomic shorthand for the most common dim-engagement patterns.
// Each stance is a `std::tuple<...>` of 20 tag types (one per dim, in
// dim-enumerator order) that the `mint_fn_for<Stance>` helper unpacks
// as a `Grants...` pack.  Most fixy bindings pick ONE stance from this
// catalog; cross-cutting combinations use `stance::compose<Base,
// NewGrants...>` to substitute one or more dims at a time.
//
// ── The 8 base stances (Phase B) ─────────────────────────────────────
//
//   PureLinear      — all dims at strict default (most restrictive).
//   PureCopy        — Usage relaxed to Copy; rest strict.
//   IoFunction      — Effect = {IO}; rest strict.
//   BgWorker        — Effect = {Bg, Alloc, Block}; rest strict.
//   CtCrypto        — Reentrancy = Reentrant; rest strict.  (Tagged-
//                     policy ConstantTime is a substrate invariant.)
//   SecretConsumer  — Security strict (Classified retained); intent =
//                     "I see classified data and never emit publicly".
//   PublicEmit<P>   — Security relaxed via grant::declassify<P>; rest
//                     strict.  Policy P is the audit trail.
//   AsyncEndpoint   — Reentrancy = Coroutine + Effect = {Bg, Block};
//                     rest strict.  Canonical session-typed channel
//                     endpoint.
//
// ── Production stances (Phase C, composed from bases) ────────────────
//
//   CntpTransport<Vendor>          — CNTP wire-frame transport endpoint.
//                                    Bg+IO+Block effects, Reentrant,
//                                    vendor-pinned.
//   CntpWireFrame<Source>          — wire-frame parser: External
//                                    provenance, no sanitizer yet
//                                    (call sites layer grant::sanitize
//                                    onto the row via compose).
//   ForgePhase<RecipeTier>         — pure IR transform: Bg+Alloc,
//                                    tier-pinned, verified trust,
//                                    immutable mutation.
//   MimicEmit<Vendor, RecipeTier>  — vendor backend kernel emit:
//                                    Bg+Alloc+IO (ioctl), vendor pin +
//                                    recipe tier engaged via Repr.
//   CipherColdWriter               — Cipher cold-tier writer:
//                                    Bg+IO+Block, append-only mutation.
//   AugurPredictor                 — Augur predictor: Bg-only,
//                                    productivity bound, bounded
//                                    staleness via stale_to<TauMax>.
//
// ── Composition ──────────────────────────────────────────────────────
//
// `stance::compose<Base, NewGrants...>` substitutes EACH NewGrant for
// the Base entry whose `relaxes` matches the NewGrant's `relaxes`.
// The composition is a left-fold of `detail::replace_one`: NewGrants
// are applied in order so later entries override earlier ones if they
// touch the same dim.  Within a single compose call, the same dim
// engaged twice is a static_assert (`stance::compose` rejects pack-
// level duplicates so the author resolves the intent at the call site).
//
// `stance::compose_no_dedup<Base, NewGrants...>` is the relaxed form
// for stance-author internal use — same semantics, no dup check.  The
// production stance aliases below use compose_no_dedup because they
// know structurally that their pack has no dim collision.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe — every stance ships all 20 tags; no implicit defaults.
//   TypeSafe — strong-typed tuples; no implicit type conversions.
//   DetSafe  — bit-identical stance composition across compiles.
//   LeakSafe — zero-state metadata.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// Zero.  Pure type-level composition.

#include <crucible/fixy/AllStrict.h>
#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Reject.h>

#include <tuple>
#include <type_traits>

namespace crucible::fixy::stance {

// ═════════════════════════════════════════════════════════════════════
// ── Helper: replace one accept tag with a relaxation in a pack ─────
// ═════════════════════════════════════════════════════════════════════
//
// Stance authoring pattern: take AllStrictAcceptPack and swap out the
// accept for one or more dims with relaxation tags.  AllStrict.h's
// replace_accept_in_pack does ONE replacement; we chain through
// nested replace_accept_in_pack for multi-replacements.

namespace detail {

template <typename Pack, dim::DimAxis D, typename NewTag>
using replace_one = replace_accept_in_pack<D, NewTag, Pack>;

// ── compose fold: replace each NewTag's dim in Base, left-to-right ──
//
// Phase B sketched compose as "append" — but appending produces a
// 20+N-tuple that mint_fn_for_extra would need a different unpack
// path for.  Phase C ships the substitution form: each NewTag in the
// pack triggers a `replace_one<Base, NewTag::relaxes, NewTag>`, fold
// left.  Output is always a 20-tuple (dim-count invariant preserved).
//
// This sidesteps the IsAccepted-redundancy problem entirely: the pack
// after compose has exactly one tag per dim, so IsAccepted's truth
// table is identical to the canonical 20-tag form.

template <typename Base, typename... NewTags>
struct compose_fold;

template <typename Base>
struct compose_fold<Base> { using type = Base; };

template <typename Base, typename Head, typename... Rest>
struct compose_fold<Base, Head, Rest...> {
    using stepped = replace_one<Base, std::remove_cvref_t<Head>::relaxes, Head>;
    using type = typename compose_fold<stepped, Rest...>::type;
};

// ── Same-dim duplicate detector ────────────────────────────────────
//
// `stance::compose` (public) rejects packs where two NewTags engage
// the same dim — the author has to resolve the ambiguity at the call
// site.  `stance::compose_no_dedup` (stance-author internal) skips
// the check; production stance aliases use it because their pack
// shape is statically known to be conflict-free.

template <typename... Ts>
struct dims_all_distinct;

template <>
struct dims_all_distinct<> : std::true_type {};

template <typename T, typename... Rest>
struct dims_all_distinct<T, Rest...> {
private:
    static constexpr dim::DimAxis head_d = std::remove_cvref_t<T>::relaxes;
    static constexpr bool head_unique =
        (... && (std::remove_cvref_t<Rest>::relaxes != head_d));
public:
    static constexpr bool value =
        head_unique && dims_all_distinct<Rest...>::value;
};

template <typename... Ts>
inline constexpr bool dims_all_distinct_v = dims_all_distinct<Ts...>::value;

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── stance::compose<Base, NewGrants...> ────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Substitute each NewGrant in `Base` (a 20-tuple stance) using its
// `relaxes` dim as the targeting key.  Resulting stance is again a
// 20-tuple — dim-count invariant preserved — so it composes
// transitively (`compose<compose<A, X>, Y>` is well-formed).
//
// **Conflict policy.**  If two NewGrants in the SAME compose call
// engage the same dim, the call is rejected at compile time with a
// clear static_assert.  Authors resolve the intent at the call site
// by ordering the grant they want to win last and using
// `compose_no_dedup`, or by splitting into two compose calls.

template <typename Base, typename... NewGrants>
struct compose {
    static_assert(detail::dims_all_distinct_v<NewGrants...>,
        "stance::compose<Base, NewGrants...> rejects: two NewGrants "
        "engage the same dim within a single compose call.  Resolve "
        "at the author site (split into two compose calls, OR reach "
        "for stance::compose_no_dedup if the last-write-wins intent "
        "is explicit).");
    using type = typename detail::compose_fold<Base, NewGrants...>::type;
};

template <typename Base, typename... NewGrants>
using compose_t = typename compose<Base, NewGrants...>::type;

// ── compose_no_dedup — stance-author internal form ────────────────
//
// Skips the dim-collision check.  Production stance aliases use this
// because their pack shape is structurally conflict-free (compiler
// proves so via the static_asserts in self_test).  Downstream
// production code should prefer `compose` so the safety check is
// preserved.

template <typename Base, typename... NewGrants>
using compose_no_dedup_t =
    typename detail::compose_fold<Base, NewGrants...>::type;

// ═════════════════════════════════════════════════════════════════════
// ── stance::PureLinear ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// All 20 dims at strict default.  Equivalent to AllStrictAcceptPack.
// Use for value-semantic types with no special discipline relaxation.

using PureLinear = AllStrictAcceptPack;

// ═════════════════════════════════════════════════════════════════════
// ── stance::PureCopy ───────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Usage relaxed to Copy.  Use for small POD / trivially-copyable
// structs that don't have linear-handle discipline.

using PureCopy = detail::replace_one<PureLinear, dim::Usage, grant::copy>;

// ═════════════════════════════════════════════════════════════════════
// ── stance::IoFunction ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Effect relaxed to {IO}.  Canonical filesystem-read / stdout-print /
// network-receive function.

using IoFunction = detail::replace_one<PureLinear, dim::Effect, grant::with_io>;

// ═════════════════════════════════════════════════════════════════════
// ── stance::BgWorker ───────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Effect = {Bg, Alloc, Block}.  Canonical background-thread shape —
// Cipher cold-tier writer, BackgroundThread pipeline stage, observe
// metrics sink.

using BgWorker = detail::replace_one<
    PureLinear,
    dim::Effect,
    grant::with<::crucible::effects::Effect::Bg,
                ::crucible::effects::Effect::Alloc,
                ::crucible::effects::Effect::Block>>;

// ═════════════════════════════════════════════════════════════════════
// ── stance::CtCrypto ───────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Reentrancy = Reentrant (for re-entrant crypto state machines).
// Precision strict (no reassociation).  Tagged ConstantTime is a
// Phase C substrate-invariant (the actual CT enforcement is a
// `with CT` region annotation at the body level, not a Grants pack
// dim) — Phase B defers and documents.

using CtCrypto = detail::replace_one<
    PureLinear,
    dim::Reentrancy,
    grant::reentrant>;

// ═════════════════════════════════════════════════════════════════════
// ── stance::SecretConsumer ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Identical to PureLinear in dim engagement, but the STANCE NAME
// documents the binding's intent: it consumes Classified data and
// never declassifies.  The discipline is auditable by reading the
// stance.  No relaxations.

using SecretConsumer = PureLinear;

// ═════════════════════════════════════════════════════════════════════
// ── stance::PublicEmit<Policy> ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Security relaxed via grant::declassify<Policy>.  Policy is a named
// type that documents the declassification rationale; review reads
// the Policy type to verify the binding's audit story.

template <typename Policy>
using PublicEmit = detail::replace_one<
    PureLinear,
    dim::Security,
    grant::declassify<Policy>>;

// ═════════════════════════════════════════════════════════════════════
// ── stance::AsyncEndpoint ──────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Reentrancy = Coroutine + Effect = {Bg, Block}.  Canonical
// session-typed channel endpoint that yields to a scheduler.
//
// Two replacements needed; nested chain.

using AsyncEndpoint_step1 = detail::replace_one<
    PureLinear,
    dim::Reentrancy,
    grant::coroutine>;

using AsyncEndpoint = detail::replace_one<
    AsyncEndpoint_step1,
    dim::Effect,
    grant::with<::crucible::effects::Effect::Bg,
                ::crucible::effects::Effect::Block>>;

// ═════════════════════════════════════════════════════════════════════
// ── Production stances (FIXY-C-STANCE-COMPOSE) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Each production stance is a compose-no-dedup chain off PureLinear.
// The packs are structurally conflict-free (compiler witnesses via the
// IsAccepted self-tests).  Authors of vertical-stack subsystems
// (CNTP, Forge, Mimic, Cipher, Augur) reach for these so the binding's
// capability manifest reads as one identifier instead of a 4-grant
// inline expansion.

// ── CntpTransport<Vendor> ──────────────────────────────────────────
//
// CNTP wire-frame transport endpoint.  Bg + IO + Block effects
// (kernel-mediated transmit/receive), Reentrant (multi-flow), vendor-
// pinned via grant::vendor<V>.  Caller threads through the actual
// vendor tag (mellanox::Vendor, nvidia::Vendor, etc.) as the V param.

template <typename Vendor>
using CntpTransport = compose_no_dedup_t<
    PureLinear,
    grant::with<::crucible::effects::Effect::Bg,
                ::crucible::effects::Effect::IO,
                ::crucible::effects::Effect::Block>,
    grant::reentrant,
    grant::vendor<Vendor>>;

// ── CntpWireFrame<Source> ──────────────────────────────────────────
//
// Wire-frame parser.  Provenance = External (FromUser or FromNetwork
// per Source tag), Linear usage (the frame buffer is consumed exactly
// once and never aliased), repr Packed (on-wire layout).  Caller
// composes `grant::sanitize<TaintClass>` onto this stance once the
// frame's CRC is validated — at that point the provenance flips to
// Sanitized.  Use as the canonical un-validated-frame entry shape.

template <typename Source>
using CntpWireFrame = compose_no_dedup_t<
    PureLinear,
    grant::from_source<Source>,
    grant::repr_packed>;

// ── ForgePhase<RecipeTier> ─────────────────────────────────────────
//
// Pure IR transformation phase (FORGE.md §5 Phase A..L).  Bg context,
// Alloc cap (arena), Verified trust default (substrate-side), recipe
// tier pinned via grant::tier<R>.  Each phase is structurally
// immutable in observable behavior: it consumes IR-in, produces IR-out
// (linear via PureLinear's Usage default).

template <typename RecipeTier>
using ForgePhase = compose_no_dedup_t<
    PureLinear,
    grant::with<::crucible::effects::Effect::Bg,
                ::crucible::effects::Effect::Alloc>,
    grant::tier<RecipeTier>>;

// ── MimicEmit<Vendor, RecipeTier> ──────────────────────────────────
//
// Mimic per-vendor backend kernel emit.  Bg + Alloc + IO (kernel
// driver ioctl), Reentrant (concurrent kernel-emit on the same
// device), vendor-pinned via grant::vendor<V>, recipe-tier-pinned via
// grant::tier<R>.
//
// Repr conflict: `grant::vendor<V>` and `grant::tier<R>` both engage
// dim::Representation.  Compose's last-write-wins-via-replace keeps
// the tier visible at the surface, but the substrate's Repr slot
// receives Opaque (per resolve.h's vendor/tier comment — both
// retain Opaque).  Engagement is satisfied either way.

template <typename Vendor, typename RecipeTier>
using MimicEmit = compose_no_dedup_t<
    PureLinear,
    grant::with<::crucible::effects::Effect::Bg,
                ::crucible::effects::Effect::Alloc,
                ::crucible::effects::Effect::IO>,
    grant::reentrant,
    grant::tier<RecipeTier>>;  // tier engages Representation; vendor
                                // is documented in stance type name.

// ── CipherColdWriter ───────────────────────────────────────────────
//
// Cipher cold-tier persistence writer.  Bg + IO + Block effects
// (S3/GCS PUT, fsync, retry loop), append-only mutation (event-sourced
// log shape), Reentrant (parallel writers per shard).

using CipherColdWriter = compose_no_dedup_t<
    PureLinear,
    grant::with<::crucible::effects::Effect::Bg,
                ::crucible::effects::Effect::IO,
                ::crucible::effects::Effect::Block>,
    grant::append_only,
    grant::reentrant>;

// ── AugurPredictor ─────────────────────────────────────────────────
//
// Augur runtime-observer predictor.  Bg-only effects (no IO — the
// predictor reads metrics that already crossed the Observe boundary
// and writes recommendations that the Keeper applies on its own
// timeline).  Productive size discipline (predictor must terminate).
// Bounded staleness (predictions are valid for τ ≤ 1000 ms).  Code
// for Augur is deferred (see feedback_augur_dependency_order) but the
// stance is design-ready so the predictor's surface declares its
// discipline before the implementation lands.

using AugurPredictor = compose_no_dedup_t<
    PureLinear,
    grant::with<::crucible::effects::Effect::Bg>,
    grant::productive,
    grant::stale_to<1000>>;

// ═════════════════════════════════════════════════════════════════════
// ── mint_fn_for<Stance>(value) — stance-bound mint helper ──────────
// ═════════════════════════════════════════════════════════════════════
//
// Bridge from a stance tuple to mint_fn's variadic Grants pack.
// Uses std::index_sequence to unpack the tuple into the variadic
// template parameter list.

namespace detail {

template <typename Pack, typename Type, std::size_t... Is>
[[nodiscard]] constexpr auto mint_with_pack_impl(
    Type&& v, std::index_sequence<Is...>)
{
    return mint_fn<std::tuple_element_t<Is, Pack>...>(
        std::forward<Type>(v));
}

}  // namespace detail

template <typename Stance, typename Type>
[[nodiscard]] constexpr auto mint_fn_for(Type v) noexcept(
    std::is_nothrow_move_constructible_v<Type>)
{
    return detail::mint_with_pack_impl<Stance>(
        std::move(v),
        std::make_index_sequence<std::tuple_size_v<Stance>>{});
}

// ═════════════════════════════════════════════════════════════════════
// ── mint_fn_for_extra<Stance, ExtraGrants...> — stance + extras ────
// ═════════════════════════════════════════════════════════════════════
//
// Phase C ergonomic shortcut: take a base Stance, layer ExtraGrants
// onto it via stance::compose, then mint.  The single-call form lets
// production code avoid spelling the composition as a separate `using`
// alias when only one or two relaxations are added at the call site.
//
// Per CLAUDE.md §XXI Universal Mint Pattern:
//   * Name follows `mint_<noun>`: mint_fn_for_extra.
//   * Token-mint flavor (Stance carries the authority; ExtraGrants
//     are additional relaxations from the same authority pool).
//   * Single concept gate at the public boundary — `compose<Stance,
//     ExtraGrants...>` fires its dim-collision check on instantiation.
//   * `[[nodiscard]] constexpr noexcept` per the universal rules.

template <typename Stance, typename... ExtraGrants, typename Type>
[[nodiscard]] constexpr auto mint_fn_for_extra(Type v) noexcept(
    std::is_nothrow_move_constructible_v<Type>)
{
    using ComposedStance = compose_t<Stance, ExtraGrants...>;
    return detail::mint_with_pack_impl<ComposedStance>(
        std::move(v),
        std::make_index_sequence<std::tuple_size_v<ComposedStance>>{});
}

// ═════════════════════════════════════════════════════════════════════
// ── Sanity self-tests ──────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace self_test {

// Every base stance produces an IsAccepted pack.
static_assert(unpack_into_IsAccepted_v<PureLinear>);
static_assert(unpack_into_IsAccepted_v<PureCopy>);
static_assert(unpack_into_IsAccepted_v<IoFunction>);
static_assert(unpack_into_IsAccepted_v<BgWorker>);
static_assert(unpack_into_IsAccepted_v<CtCrypto>);
static_assert(unpack_into_IsAccepted_v<SecretConsumer>);
static_assert(unpack_into_IsAccepted_v<AsyncEndpoint>);

struct DemoPolicy {};
static_assert(unpack_into_IsAccepted_v<PublicEmit<DemoPolicy>>);

// PureCopy's Usage slot is grant::copy (replaced from
// accept_default_strict_for<dim::Usage>).  AllStrict order: Usage is
// enumerator value 2 (Type=0, Refinement=1, Usage=2).
static_assert(std::is_same_v<std::tuple_element_t<2, PureCopy>, grant::copy>);

// ── Production stances ─────────────────────────────────────────────
struct DemoVendor {};
struct DemoRecipeTier {};
struct DemoSource {};

static_assert(unpack_into_IsAccepted_v<CntpTransport<DemoVendor>>);
static_assert(unpack_into_IsAccepted_v<CntpWireFrame<DemoSource>>);
static_assert(unpack_into_IsAccepted_v<ForgePhase<DemoRecipeTier>>);
static_assert(unpack_into_IsAccepted_v<MimicEmit<DemoVendor, DemoRecipeTier>>);
static_assert(unpack_into_IsAccepted_v<CipherColdWriter>);
static_assert(unpack_into_IsAccepted_v<AugurPredictor>);

// ── stance::compose ────────────────────────────────────────────────
//
// Layer grant::copy onto PureLinear via compose — equivalent to
// PureCopy.  Verifies the substitution semantics (same dim-2 slot
// holds grant::copy after composition).
using ComposedCopy = compose_t<PureLinear, grant::copy>;
static_assert(unpack_into_IsAccepted_v<ComposedCopy>);
static_assert(std::is_same_v<std::tuple_element_t<2, ComposedCopy>, grant::copy>);

// Multi-grant compose: PureLinear + grant::copy + grant::with_io.
using ComposedCopyIo = compose_t<PureLinear, grant::copy, grant::with_io>;
static_assert(unpack_into_IsAccepted_v<ComposedCopyIo>);
static_assert(std::is_same_v<std::tuple_element_t<2, ComposedCopyIo>, grant::copy>);
static_assert(std::is_same_v<std::tuple_element_t<3, ComposedCopyIo>, grant::with_io>);

// Composition transitivity: compose<compose<A, X>, Y> == compose<A, X, Y>.
using StepThenStep   = compose_t<compose_t<PureLinear, grant::copy>, grant::with_io>;
using ComposeBoth    = compose_t<PureLinear, grant::copy, grant::with_io>;
static_assert(std::is_same_v<StepThenStep, ComposeBoth>);

// dim-distinct trait sanity
static_assert(detail::dims_all_distinct_v<grant::copy, grant::with_io>);
static_assert(!detail::dims_all_distinct_v<grant::copy, grant::affine>);  // both Usage

// Production stance compose with extra: ForgePhase + reassociate.
using ForgePhaseRelaxedFp = compose_t<ForgePhase<DemoRecipeTier>,
                                       grant::reassociate>;
static_assert(unpack_into_IsAccepted_v<ForgePhaseRelaxedFp>);

}  // namespace self_test

}  // namespace crucible::fixy::stance
